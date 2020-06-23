/*
 *  x86 FPU, MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/SSE4/PNI helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <math.h>
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "fpu/softfloat.h"

#ifdef CONFIG_SOFTMMU
#include "hw/irq.h"
#endif

#define FPU_RC_MASK         0xc00
#define FPU_RC_NEAR         0x000
#define FPU_RC_DOWN         0x400
#define FPU_RC_UP           0x800
#define FPU_RC_CHOP         0xc00

#define MAXTAN 9223372036854775808.0

/* the following deal with x86 long double-precision numbers */
#define MAXEXPD 0x7fff
#define EXPBIAS 16383
#define EXPD(fp)        (fp.l.upper & 0x7fff)
#define SIGND(fp)       ((fp.l.upper) & 0x8000)
#define MANTD(fp)       (fp.l.lower)
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7fff)) | EXPBIAS

#define FPUS_IE (1 << 0)
#define FPUS_DE (1 << 1)
#define FPUS_ZE (1 << 2)
#define FPUS_OE (1 << 3)
#define FPUS_UE (1 << 4)
#define FPUS_PE (1 << 5)
#define FPUS_SF (1 << 6)
#define FPUS_SE (1 << 7)
#define FPUS_B  (1 << 15)

#define FPUC_EM 0x3f

#define floatx80_lg2 make_floatx80(0x3ffd, 0x9a209a84fbcff799LL)
#define floatx80_lg2_d make_floatx80(0x3ffd, 0x9a209a84fbcff798LL)
#define floatx80_l2e make_floatx80(0x3fff, 0xb8aa3b295c17f0bcLL)
#define floatx80_l2e_d make_floatx80(0x3fff, 0xb8aa3b295c17f0bbLL)
#define floatx80_l2t make_floatx80(0x4000, 0xd49a784bcd1b8afeLL)
#define floatx80_l2t_u make_floatx80(0x4000, 0xd49a784bcd1b8affLL)
#define floatx80_ln2_d make_floatx80(0x3ffe, 0xb17217f7d1cf79abLL)
#define floatx80_pi_d make_floatx80(0x4000, 0xc90fdaa22168c234LL)

#if !defined(CONFIG_USER_ONLY)
static qemu_irq ferr_irq;

void x86_register_ferr_irq(qemu_irq irq)
{
    ferr_irq = irq;
}

static void cpu_clear_ignne(void)
{
    CPUX86State *env = &X86_CPU(first_cpu)->env;
    env->hflags2 &= ~HF2_IGNNE_MASK;
}

void cpu_set_ignne(void)
{
    CPUX86State *env = &X86_CPU(first_cpu)->env;
    env->hflags2 |= HF2_IGNNE_MASK;
    /*
     * We get here in response to a write to port F0h.  The chipset should
     * deassert FP_IRQ and FERR# instead should stay signaled until FPSW_SE is
     * cleared, because FERR# and FP_IRQ are two separate pins on real
     * hardware.  However, we don't model FERR# as a qemu_irq, so we just
     * do directly what the chipset would do, i.e. deassert FP_IRQ.
     */
    qemu_irq_lower(ferr_irq);
}
#endif


static inline void fpush(CPUX86State *env)
{
    env->fpstt = (env->fpstt - 1) & 7;
    env->fptags[env->fpstt] = 0; /* validate stack entry */
}

static inline void fpop(CPUX86State *env)
{
    env->fptags[env->fpstt] = 1; /* invalidate stack entry */
    env->fpstt = (env->fpstt + 1) & 7;
}

static inline floatx80 helper_fldt(CPUX86State *env, target_ulong ptr,
                                   uintptr_t retaddr)
{
    CPU_LDoubleU temp;

    temp.l.lower = cpu_ldq_data_ra(env, ptr, retaddr);
    temp.l.upper = cpu_lduw_data_ra(env, ptr + 8, retaddr);
    return temp.d;
}

static inline void helper_fstt(CPUX86State *env, floatx80 f, target_ulong ptr,
                               uintptr_t retaddr)
{
    CPU_LDoubleU temp;

    temp.d = f;
    cpu_stq_data_ra(env, ptr, temp.l.lower, retaddr);
    cpu_stw_data_ra(env, ptr + 8, temp.l.upper, retaddr);
}

/* x87 FPU helpers */

static inline double floatx80_to_double(CPUX86State *env, floatx80 a)
{
    union {
        float64 f64;
        double d;
    } u;

    u.f64 = floatx80_to_float64(a, &env->fp_status);
    return u.d;
}

static inline floatx80 double_to_floatx80(CPUX86State *env, double a)
{
    union {
        float64 f64;
        double d;
    } u;

    u.d = a;
    return float64_to_floatx80(u.f64, &env->fp_status);
}

static void fpu_set_exception(CPUX86State *env, int mask)
{
    env->fpus |= mask;
    if (env->fpus & (~env->fpuc & FPUC_EM)) {
        env->fpus |= FPUS_SE | FPUS_B;
    }
}

static inline uint8_t save_exception_flags(CPUX86State *env)
{
    uint8_t old_flags = get_float_exception_flags(&env->fp_status);
    set_float_exception_flags(0, &env->fp_status);
    return old_flags;
}

static void merge_exception_flags(CPUX86State *env, uint8_t old_flags)
{
    uint8_t new_flags = get_float_exception_flags(&env->fp_status);
    float_raise(old_flags, &env->fp_status);
    fpu_set_exception(env,
                      ((new_flags & float_flag_invalid ? FPUS_IE : 0) |
                       (new_flags & float_flag_divbyzero ? FPUS_ZE : 0) |
                       (new_flags & float_flag_overflow ? FPUS_OE : 0) |
                       (new_flags & float_flag_underflow ? FPUS_UE : 0) |
                       (new_flags & float_flag_inexact ? FPUS_PE : 0) |
                       (new_flags & float_flag_input_denormal ? FPUS_DE : 0)));
}

static inline floatx80 helper_fdiv(CPUX86State *env, floatx80 a, floatx80 b)
{
    uint8_t old_flags = save_exception_flags(env);
    floatx80 ret = floatx80_div(a, b, &env->fp_status);
    merge_exception_flags(env, old_flags);
    return ret;
}

static void fpu_raise_exception(CPUX86State *env, uintptr_t retaddr)
{
    if (env->cr[0] & CR0_NE_MASK) {
        raise_exception_ra(env, EXCP10_COPR, retaddr);
    }
#if !defined(CONFIG_USER_ONLY)
    else if (ferr_irq && !(env->hflags2 & HF2_IGNNE_MASK)) {
        qemu_irq_raise(ferr_irq);
    }
#endif
}

void helper_flds_FT0(CPUX86State *env, uint32_t val)
{
    uint8_t old_flags = save_exception_flags(env);
    union {
        float32 f;
        uint32_t i;
    } u;

    u.i = val;
    FT0 = float32_to_floatx80(u.f, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fldl_FT0(CPUX86State *env, uint64_t val)
{
    uint8_t old_flags = save_exception_flags(env);
    union {
        float64 f;
        uint64_t i;
    } u;

    u.i = val;
    FT0 = float64_to_floatx80(u.f, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fildl_FT0(CPUX86State *env, int32_t val)
{
    FT0 = int32_to_floatx80(val, &env->fp_status);
}

void helper_flds_ST0(CPUX86State *env, uint32_t val)
{
    uint8_t old_flags = save_exception_flags(env);
    int new_fpstt;
    union {
        float32 f;
        uint32_t i;
    } u;

    new_fpstt = (env->fpstt - 1) & 7;
    u.i = val;
    env->fpregs[new_fpstt].d = float32_to_floatx80(u.f, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
    merge_exception_flags(env, old_flags);
}

void helper_fldl_ST0(CPUX86State *env, uint64_t val)
{
    uint8_t old_flags = save_exception_flags(env);
    int new_fpstt;
    union {
        float64 f;
        uint64_t i;
    } u;

    new_fpstt = (env->fpstt - 1) & 7;
    u.i = val;
    env->fpregs[new_fpstt].d = float64_to_floatx80(u.f, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
    merge_exception_flags(env, old_flags);
}

void helper_fildl_ST0(CPUX86State *env, int32_t val)
{
    int new_fpstt;

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = int32_to_floatx80(val, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void helper_fildll_ST0(CPUX86State *env, int64_t val)
{
    int new_fpstt;

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = int64_to_floatx80(val, &env->fp_status);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

uint32_t helper_fsts_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    union {
        float32 f;
        uint32_t i;
    } u;

    u.f = floatx80_to_float32(ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
    return u.i;
}

uint64_t helper_fstl_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    union {
        float64 f;
        uint64_t i;
    } u;

    u.f = floatx80_to_float64(ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
    return u.i;
}

int32_t helper_fist_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32(ST0, &env->fp_status);
    if (val != (int16_t)val) {
        set_float_exception_flags(float_flag_invalid, &env->fp_status);
        val = -32768;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int32_t helper_fistl_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x80000000;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int64_t helper_fistll_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int64_t val;

    val = floatx80_to_int64(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x8000000000000000ULL;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int32_t helper_fistt_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32_round_to_zero(ST0, &env->fp_status);
    if (val != (int16_t)val) {
        set_float_exception_flags(float_flag_invalid, &env->fp_status);
        val = -32768;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int32_t helper_fisttl_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int32_t val;

    val = floatx80_to_int32_round_to_zero(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x80000000;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

int64_t helper_fisttll_ST0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int64_t val;

    val = floatx80_to_int64_round_to_zero(ST0, &env->fp_status);
    if (get_float_exception_flags(&env->fp_status) & float_flag_invalid) {
        val = 0x8000000000000000ULL;
    }
    merge_exception_flags(env, old_flags);
    return val;
}

void helper_fldt_ST0(CPUX86State *env, target_ulong ptr)
{
    int new_fpstt;

    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = helper_fldt(env, ptr, GETPC());
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void helper_fstt_ST0(CPUX86State *env, target_ulong ptr)
{
    helper_fstt(env, ST0, ptr, GETPC());
}

void helper_fpush(CPUX86State *env)
{
    fpush(env);
}

void helper_fpop(CPUX86State *env)
{
    fpop(env);
}

void helper_fdecstp(CPUX86State *env)
{
    env->fpstt = (env->fpstt - 1) & 7;
    env->fpus &= ~0x4700;
}

void helper_fincstp(CPUX86State *env)
{
    env->fpstt = (env->fpstt + 1) & 7;
    env->fpus &= ~0x4700;
}

/* FPU move */

void helper_ffree_STN(CPUX86State *env, int st_index)
{
    env->fptags[(env->fpstt + st_index) & 7] = 1;
}

void helper_fmov_ST0_FT0(CPUX86State *env)
{
    ST0 = FT0;
}

void helper_fmov_FT0_STN(CPUX86State *env, int st_index)
{
    FT0 = ST(st_index);
}

void helper_fmov_ST0_STN(CPUX86State *env, int st_index)
{
    ST0 = ST(st_index);
}

void helper_fmov_STN_ST0(CPUX86State *env, int st_index)
{
    ST(st_index) = ST0;
}

void helper_fxchg_ST0_STN(CPUX86State *env, int st_index)
{
    floatx80 tmp;

    tmp = ST(st_index);
    ST(st_index) = ST0;
    ST0 = tmp;
}

/* FPU operations */

static const int fcom_ccval[4] = {0x0100, 0x4000, 0x0000, 0x4500};

void helper_fcom_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    FloatRelation ret;

    ret = floatx80_compare(ST0, FT0, &env->fp_status);
    env->fpus = (env->fpus & ~0x4500) | fcom_ccval[ret + 1];
    merge_exception_flags(env, old_flags);
}

void helper_fucom_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    FloatRelation ret;

    ret = floatx80_compare_quiet(ST0, FT0, &env->fp_status);
    env->fpus = (env->fpus & ~0x4500) | fcom_ccval[ret + 1];
    merge_exception_flags(env, old_flags);
}

static const int fcomi_ccval[4] = {CC_C, CC_Z, 0, CC_Z | CC_P | CC_C};

void helper_fcomi_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int eflags;
    FloatRelation ret;

    ret = floatx80_compare(ST0, FT0, &env->fp_status);
    eflags = cpu_cc_compute_all(env, CC_OP);
    eflags = (eflags & ~(CC_Z | CC_P | CC_C)) | fcomi_ccval[ret + 1];
    CC_SRC = eflags;
    merge_exception_flags(env, old_flags);
}

void helper_fucomi_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    int eflags;
    FloatRelation ret;

    ret = floatx80_compare_quiet(ST0, FT0, &env->fp_status);
    eflags = cpu_cc_compute_all(env, CC_OP);
    eflags = (eflags & ~(CC_Z | CC_P | CC_C)) | fcomi_ccval[ret + 1];
    CC_SRC = eflags;
    merge_exception_flags(env, old_flags);
}

void helper_fadd_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    ST0 = floatx80_add(ST0, FT0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fmul_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    ST0 = floatx80_mul(ST0, FT0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsub_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    ST0 = floatx80_sub(ST0, FT0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsubr_ST0_FT0(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    ST0 = floatx80_sub(FT0, ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fdiv_ST0_FT0(CPUX86State *env)
{
    ST0 = helper_fdiv(env, ST0, FT0);
}

void helper_fdivr_ST0_FT0(CPUX86State *env)
{
    ST0 = helper_fdiv(env, FT0, ST0);
}

/* fp operations between STN and ST0 */

void helper_fadd_STN_ST0(CPUX86State *env, int st_index)
{
    uint8_t old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_add(ST(st_index), ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fmul_STN_ST0(CPUX86State *env, int st_index)
{
    uint8_t old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_mul(ST(st_index), ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsub_STN_ST0(CPUX86State *env, int st_index)
{
    uint8_t old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_sub(ST(st_index), ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsubr_STN_ST0(CPUX86State *env, int st_index)
{
    uint8_t old_flags = save_exception_flags(env);
    ST(st_index) = floatx80_sub(ST0, ST(st_index), &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fdiv_STN_ST0(CPUX86State *env, int st_index)
{
    floatx80 *p;

    p = &ST(st_index);
    *p = helper_fdiv(env, *p, ST0);
}

void helper_fdivr_STN_ST0(CPUX86State *env, int st_index)
{
    floatx80 *p;

    p = &ST(st_index);
    *p = helper_fdiv(env, ST0, *p);
}

/* misc FPU operations */
void helper_fchs_ST0(CPUX86State *env)
{
    ST0 = floatx80_chs(ST0);
}

void helper_fabs_ST0(CPUX86State *env)
{
    ST0 = floatx80_abs(ST0);
}

void helper_fld1_ST0(CPUX86State *env)
{
    ST0 = floatx80_one;
}

void helper_fldl2t_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_UP:
        ST0 = floatx80_l2t_u;
        break;
    default:
        ST0 = floatx80_l2t;
        break;
    }
}

void helper_fldl2e_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_l2e_d;
        break;
    default:
        ST0 = floatx80_l2e;
        break;
    }
}

void helper_fldpi_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_pi_d;
        break;
    default:
        ST0 = floatx80_pi;
        break;
    }
}

void helper_fldlg2_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_lg2_d;
        break;
    default:
        ST0 = floatx80_lg2;
        break;
    }
}

void helper_fldln2_ST0(CPUX86State *env)
{
    switch (env->fpuc & FPU_RC_MASK) {
    case FPU_RC_DOWN:
    case FPU_RC_CHOP:
        ST0 = floatx80_ln2_d;
        break;
    default:
        ST0 = floatx80_ln2;
        break;
    }
}

void helper_fldz_ST0(CPUX86State *env)
{
    ST0 = floatx80_zero;
}

void helper_fldz_FT0(CPUX86State *env)
{
    FT0 = floatx80_zero;
}

uint32_t helper_fnstsw(CPUX86State *env)
{
    return (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
}

uint32_t helper_fnstcw(CPUX86State *env)
{
    return env->fpuc;
}

void update_fp_status(CPUX86State *env)
{
    int rnd_type;

    /* set rounding mode */
    switch (env->fpuc & FPU_RC_MASK) {
    default:
    case FPU_RC_NEAR:
        rnd_type = float_round_nearest_even;
        break;
    case FPU_RC_DOWN:
        rnd_type = float_round_down;
        break;
    case FPU_RC_UP:
        rnd_type = float_round_up;
        break;
    case FPU_RC_CHOP:
        rnd_type = float_round_to_zero;
        break;
    }
    set_float_rounding_mode(rnd_type, &env->fp_status);
    switch ((env->fpuc >> 8) & 3) {
    case 0:
        rnd_type = 32;
        break;
    case 2:
        rnd_type = 64;
        break;
    case 3:
    default:
        rnd_type = 80;
        break;
    }
    set_floatx80_rounding_precision(rnd_type, &env->fp_status);
}

void helper_fldcw(CPUX86State *env, uint32_t val)
{
    cpu_set_fpuc(env, val);
}

void helper_fclex(CPUX86State *env)
{
    env->fpus &= 0x7f00;
}

void helper_fwait(CPUX86State *env)
{
    if (env->fpus & FPUS_SE) {
        fpu_raise_exception(env, GETPC());
    }
}

void helper_fninit(CPUX86State *env)
{
    env->fpus = 0;
    env->fpstt = 0;
    cpu_set_fpuc(env, 0x37f);
    env->fptags[0] = 1;
    env->fptags[1] = 1;
    env->fptags[2] = 1;
    env->fptags[3] = 1;
    env->fptags[4] = 1;
    env->fptags[5] = 1;
    env->fptags[6] = 1;
    env->fptags[7] = 1;
}

/* BCD ops */

void helper_fbld_ST0(CPUX86State *env, target_ulong ptr)
{
    floatx80 tmp;
    uint64_t val;
    unsigned int v;
    int i;

    val = 0;
    for (i = 8; i >= 0; i--) {
        v = cpu_ldub_data_ra(env, ptr + i, GETPC());
        val = (val * 100) + ((v >> 4) * 10) + (v & 0xf);
    }
    tmp = int64_to_floatx80(val, &env->fp_status);
    if (cpu_ldub_data_ra(env, ptr + 9, GETPC()) & 0x80) {
        tmp = floatx80_chs(tmp);
    }
    fpush(env);
    ST0 = tmp;
}

void helper_fbst_ST0(CPUX86State *env, target_ulong ptr)
{
    uint8_t old_flags = save_exception_flags(env);
    int v;
    target_ulong mem_ref, mem_end;
    int64_t val;
    CPU_LDoubleU temp;

    temp.d = ST0;

    val = floatx80_to_int64(ST0, &env->fp_status);
    mem_ref = ptr;
    if (val >= 1000000000000000000LL || val <= -1000000000000000000LL) {
        set_float_exception_flags(float_flag_invalid, &env->fp_status);
        while (mem_ref < ptr + 7) {
            cpu_stb_data_ra(env, mem_ref++, 0, GETPC());
        }
        cpu_stb_data_ra(env, mem_ref++, 0xc0, GETPC());
        cpu_stb_data_ra(env, mem_ref++, 0xff, GETPC());
        cpu_stb_data_ra(env, mem_ref++, 0xff, GETPC());
        merge_exception_flags(env, old_flags);
        return;
    }
    mem_end = mem_ref + 9;
    if (SIGND(temp)) {
        cpu_stb_data_ra(env, mem_end, 0x80, GETPC());
        val = -val;
    } else {
        cpu_stb_data_ra(env, mem_end, 0x00, GETPC());
    }
    while (mem_ref < mem_end) {
        if (val == 0) {
            break;
        }
        v = val % 100;
        val = val / 100;
        v = ((v / 10) << 4) | (v % 10);
        cpu_stb_data_ra(env, mem_ref++, v, GETPC());
    }
    while (mem_ref < mem_end) {
        cpu_stb_data_ra(env, mem_ref++, 0, GETPC());
    }
    merge_exception_flags(env, old_flags);
}

void helper_f2xm1(CPUX86State *env)
{
    double val = floatx80_to_double(env, ST0);

    val = pow(2.0, val) - 1.0;
    ST0 = double_to_floatx80(env, val);
}

void helper_fyl2x(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if (fptemp > 0.0) {
        fptemp = log(fptemp) / log(2.0); /* log2(ST) */
        fptemp *= floatx80_to_double(env, ST1);
        ST1 = double_to_floatx80(env, fptemp);
        fpop(env);
    } else {
        env->fpus &= ~0x4700;
        env->fpus |= 0x400;
    }
}

void helper_fptan(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        fptemp = tan(fptemp);
        ST0 = double_to_floatx80(env, fptemp);
        fpush(env);
        ST0 = floatx80_one;
        env->fpus &= ~0x400; /* C2 <-- 0 */
        /* the above code is for |arg| < 2**52 only */
    }
}

/* Values of pi/4, pi/2, 3pi/4 and pi, with 128-bit precision.  */
#define pi_4_exp 0x3ffe
#define pi_4_sig_high 0xc90fdaa22168c234ULL
#define pi_4_sig_low 0xc4c6628b80dc1cd1ULL
#define pi_2_exp 0x3fff
#define pi_2_sig_high 0xc90fdaa22168c234ULL
#define pi_2_sig_low 0xc4c6628b80dc1cd1ULL
#define pi_34_exp 0x4000
#define pi_34_sig_high 0x96cbe3f9990e91a7ULL
#define pi_34_sig_low 0x9394c9e8a0a5159dULL
#define pi_exp 0x4000
#define pi_sig_high 0xc90fdaa22168c234ULL
#define pi_sig_low 0xc4c6628b80dc1cd1ULL

/*
 * Polynomial coefficients for an approximation to atan(x), with only
 * odd powers of x used, for x in the interval [-1/16, 1/16].  (Unlike
 * for some other approximations, no low part is needed for the first
 * coefficient here to achieve a sufficiently accurate result, because
 * the coefficient in this minimax approximation is very close to
 * exactly 1.)
 */
#define fpatan_coeff_0 make_floatx80(0x3fff, 0x8000000000000000ULL)
#define fpatan_coeff_1 make_floatx80(0xbffd, 0xaaaaaaaaaaaaaa43ULL)
#define fpatan_coeff_2 make_floatx80(0x3ffc, 0xccccccccccbfe4f8ULL)
#define fpatan_coeff_3 make_floatx80(0xbffc, 0x92492491fbab2e66ULL)
#define fpatan_coeff_4 make_floatx80(0x3ffb, 0xe38e372881ea1e0bULL)
#define fpatan_coeff_5 make_floatx80(0xbffb, 0xba2c0104bbdd0615ULL)
#define fpatan_coeff_6 make_floatx80(0x3ffb, 0x9baf7ebf898b42efULL)

struct fpatan_data {
    /* High and low parts of atan(x).  */
    floatx80 atan_high, atan_low;
};

static const struct fpatan_data fpatan_table[9] = {
    { floatx80_zero,
      floatx80_zero },
    { make_floatx80(0x3ffb, 0xfeadd4d5617b6e33ULL),
      make_floatx80(0xbfb9, 0xdda19d8305ddc420ULL) },
    { make_floatx80(0x3ffc, 0xfadbafc96406eb15ULL),
      make_floatx80(0x3fbb, 0xdb8f3debef442fccULL) },
    { make_floatx80(0x3ffd, 0xb7b0ca0f26f78474ULL),
      make_floatx80(0xbfbc, 0xeab9bdba460376faULL) },
    { make_floatx80(0x3ffd, 0xed63382b0dda7b45ULL),
      make_floatx80(0x3fbc, 0xdfc88bd978751a06ULL) },
    { make_floatx80(0x3ffe, 0x8f005d5ef7f59f9bULL),
      make_floatx80(0x3fbd, 0xb906bc2ccb886e90ULL) },
    { make_floatx80(0x3ffe, 0xa4bc7d1934f70924ULL),
      make_floatx80(0x3fbb, 0xcd43f9522bed64f8ULL) },
    { make_floatx80(0x3ffe, 0xb8053e2bc2319e74ULL),
      make_floatx80(0xbfbc, 0xd3496ab7bd6eef0cULL) },
    { make_floatx80(0x3ffe, 0xc90fdaa22168c235ULL),
      make_floatx80(0xbfbc, 0xece675d1fc8f8cbcULL) },
};

void helper_fpatan(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    uint64_t arg0_sig = extractFloatx80Frac(ST0);
    int32_t arg0_exp = extractFloatx80Exp(ST0);
    bool arg0_sign = extractFloatx80Sign(ST0);
    uint64_t arg1_sig = extractFloatx80Frac(ST1);
    int32_t arg1_exp = extractFloatx80Exp(ST1);
    bool arg1_sign = extractFloatx80Sign(ST1);

    if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST0, &env->fp_status);
    } else if (floatx80_is_signaling_nan(ST1, &env->fp_status)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_silence_nan(ST1, &env->fp_status);
    } else if (floatx80_invalid_encoding(ST0) ||
               floatx80_invalid_encoding(ST1)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST1 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_any_nan(ST0)) {
        ST1 = ST0;
    } else if (floatx80_is_any_nan(ST1)) {
        /* Pass this NaN through.  */
    } else if (floatx80_is_zero(ST1) && !arg0_sign) {
        /* Pass this zero through.  */
    } else if (((floatx80_is_infinity(ST0) && !floatx80_is_infinity(ST1)) ||
                 arg0_exp - arg1_exp >= 80) &&
               !arg0_sign) {
        /*
         * Dividing ST1 by ST0 gives the correct result up to
         * rounding, and avoids spurious underflow exceptions that
         * might result from passing some small values through the
         * polynomial approximation, but if a finite nonzero result of
         * division is exact, the result of fpatan is still inexact
         * (and underflowing where appropriate).
         */
        signed char save_prec = env->fp_status.floatx80_rounding_precision;
        env->fp_status.floatx80_rounding_precision = 80;
        ST1 = floatx80_div(ST1, ST0, &env->fp_status);
        env->fp_status.floatx80_rounding_precision = save_prec;
        if (!floatx80_is_zero(ST1) &&
            !(get_float_exception_flags(&env->fp_status) &
              float_flag_inexact)) {
            /*
             * The mathematical result is very slightly closer to zero
             * than this exact result.  Round a value with the
             * significand adjusted accordingly to get the correct
             * exceptions, and possibly an adjusted result depending
             * on the rounding mode.
             */
            uint64_t sig = extractFloatx80Frac(ST1);
            int32_t exp = extractFloatx80Exp(ST1);
            bool sign = extractFloatx80Sign(ST1);
            if (exp == 0) {
                normalizeFloatx80Subnormal(sig, &exp, &sig);
            }
            ST1 = normalizeRoundAndPackFloatx80(80, sign, exp, sig - 1,
                                                -1, &env->fp_status);
        }
    } else {
        /* The result is inexact.  */
        bool rsign = arg1_sign;
        int32_t rexp;
        uint64_t rsig0, rsig1;
        if (floatx80_is_zero(ST1)) {
            /*
             * ST0 is negative.  The result is pi with the sign of
             * ST1.
             */
            rexp = pi_exp;
            rsig0 = pi_sig_high;
            rsig1 = pi_sig_low;
        } else if (floatx80_is_infinity(ST1)) {
            if (floatx80_is_infinity(ST0)) {
                if (arg0_sign) {
                    rexp = pi_34_exp;
                    rsig0 = pi_34_sig_high;
                    rsig1 = pi_34_sig_low;
                } else {
                    rexp = pi_4_exp;
                    rsig0 = pi_4_sig_high;
                    rsig1 = pi_4_sig_low;
                }
            } else {
                rexp = pi_2_exp;
                rsig0 = pi_2_sig_high;
                rsig1 = pi_2_sig_low;
            }
        } else if (floatx80_is_zero(ST0) || arg1_exp - arg0_exp >= 80) {
            rexp = pi_2_exp;
            rsig0 = pi_2_sig_high;
            rsig1 = pi_2_sig_low;
        } else if (floatx80_is_infinity(ST0) || arg0_exp - arg1_exp >= 80) {
            /* ST0 is negative.  */
            rexp = pi_exp;
            rsig0 = pi_sig_high;
            rsig1 = pi_sig_low;
        } else {
            /*
             * ST0 and ST1 are finite, nonzero and with exponents not
             * too far apart.
             */
            int32_t adj_exp, num_exp, den_exp, xexp, yexp, n, texp, zexp, aexp;
            int32_t azexp, axexp;
            bool adj_sub, ysign, zsign;
            uint64_t adj_sig0, adj_sig1, num_sig, den_sig, xsig0, xsig1;
            uint64_t msig0, msig1, msig2, remsig0, remsig1, remsig2;
            uint64_t ysig0, ysig1, tsig, zsig0, zsig1, asig0, asig1;
            uint64_t azsig0, azsig1;
            uint64_t azsig2, azsig3, axsig0, axsig1;
            floatx80 x8;
            FloatRoundMode save_mode = env->fp_status.float_rounding_mode;
            signed char save_prec = env->fp_status.floatx80_rounding_precision;
            env->fp_status.float_rounding_mode = float_round_nearest_even;
            env->fp_status.floatx80_rounding_precision = 80;

            if (arg0_exp == 0) {
                normalizeFloatx80Subnormal(arg0_sig, &arg0_exp, &arg0_sig);
            }
            if (arg1_exp == 0) {
                normalizeFloatx80Subnormal(arg1_sig, &arg1_exp, &arg1_sig);
            }
            if (arg0_exp > arg1_exp ||
                (arg0_exp == arg1_exp && arg0_sig >= arg1_sig)) {
                /* Work with abs(ST1) / abs(ST0).  */
                num_exp = arg1_exp;
                num_sig = arg1_sig;
                den_exp = arg0_exp;
                den_sig = arg0_sig;
                if (arg0_sign) {
                    /* The result is subtracted from pi.  */
                    adj_exp = pi_exp;
                    adj_sig0 = pi_sig_high;
                    adj_sig1 = pi_sig_low;
                    adj_sub = true;
                } else {
                    /* The result is used as-is.  */
                    adj_exp = 0;
                    adj_sig0 = 0;
                    adj_sig1 = 0;
                    adj_sub = false;
                }
            } else {
                /* Work with abs(ST0) / abs(ST1).  */
                num_exp = arg0_exp;
                num_sig = arg0_sig;
                den_exp = arg1_exp;
                den_sig = arg1_sig;
                /* The result is added to or subtracted from pi/2.  */
                adj_exp = pi_2_exp;
                adj_sig0 = pi_2_sig_high;
                adj_sig1 = pi_2_sig_low;
                adj_sub = !arg0_sign;
            }

            /*
             * Compute x = num/den, where 0 < x <= 1 and x is not too
             * small.
             */
            xexp = num_exp - den_exp + 0x3ffe;
            remsig0 = num_sig;
            remsig1 = 0;
            if (den_sig <= remsig0) {
                shift128Right(remsig0, remsig1, 1, &remsig0, &remsig1);
                ++xexp;
            }
            xsig0 = estimateDiv128To64(remsig0, remsig1, den_sig);
            mul64To128(den_sig, xsig0, &msig0, &msig1);
            sub128(remsig0, remsig1, msig0, msig1, &remsig0, &remsig1);
            while ((int64_t) remsig0 < 0) {
                --xsig0;
                add128(remsig0, remsig1, 0, den_sig, &remsig0, &remsig1);
            }
            xsig1 = estimateDiv128To64(remsig1, 0, den_sig);
            /*
             * No need to correct any estimation error in xsig1; even
             * with such error, it is accurate enough.
             */

            /*
             * Split x as x = t + y, where t = n/8 is the nearest
             * multiple of 1/8 to x.
             */
            x8 = normalizeRoundAndPackFloatx80(80, false, xexp + 3, xsig0,
                                               xsig1, &env->fp_status);
            n = floatx80_to_int32(x8, &env->fp_status);
            if (n == 0) {
                ysign = false;
                yexp = xexp;
                ysig0 = xsig0;
                ysig1 = xsig1;
                texp = 0;
                tsig = 0;
            } else {
                int shift = clz32(n) + 32;
                texp = 0x403b - shift;
                tsig = n;
                tsig <<= shift;
                if (texp == xexp) {
                    sub128(xsig0, xsig1, tsig, 0, &ysig0, &ysig1);
                    if ((int64_t) ysig0 >= 0) {
                        ysign = false;
                        if (ysig0 == 0) {
                            if (ysig1 == 0) {
                                yexp = 0;
                            } else {
                                shift = clz64(ysig1) + 64;
                                yexp = xexp - shift;
                                shift128Left(ysig0, ysig1, shift,
                                             &ysig0, &ysig1);
                            }
                        } else {
                            shift = clz64(ysig0);
                            yexp = xexp - shift;
                            shift128Left(ysig0, ysig1, shift, &ysig0, &ysig1);
                        }
                    } else {
                        ysign = true;
                        sub128(0, 0, ysig0, ysig1, &ysig0, &ysig1);
                        if (ysig0 == 0) {
                            shift = clz64(ysig1) + 64;
                        } else {
                            shift = clz64(ysig0);
                        }
                        yexp = xexp - shift;
                        shift128Left(ysig0, ysig1, shift, &ysig0, &ysig1);
                    }
                } else {
                    /*
                     * t's exponent must be greater than x's because t
                     * is positive and the nearest multiple of 1/8 to
                     * x, and if x has a greater exponent, the power
                     * of 2 with that exponent is also a multiple of
                     * 1/8.
                     */
                    uint64_t usig0, usig1;
                    shift128RightJamming(xsig0, xsig1, texp - xexp,
                                         &usig0, &usig1);
                    ysign = true;
                    sub128(tsig, 0, usig0, usig1, &ysig0, &ysig1);
                    if (ysig0 == 0) {
                        shift = clz64(ysig1) + 64;
                    } else {
                        shift = clz64(ysig0);
                    }
                    yexp = texp - shift;
                    shift128Left(ysig0, ysig1, shift, &ysig0, &ysig1);
                }
            }

            /*
             * Compute z = y/(1+tx), so arctan(x) = arctan(t) +
             * arctan(z).
             */
            zsign = ysign;
            if (texp == 0 || yexp == 0) {
                zexp = yexp;
                zsig0 = ysig0;
                zsig1 = ysig1;
            } else {
                /*
                 * t <= 1, x <= 1 and if both are 1 then y is 0, so tx < 1.
                 */
                int32_t dexp = texp + xexp - 0x3ffe;
                uint64_t dsig0, dsig1, dsig2;
                mul128By64To192(xsig0, xsig1, tsig, &dsig0, &dsig1, &dsig2);
                /*
                 * dexp <= 0x3fff (and if equal, dsig0 has a leading 0
                 * bit).  Add 1 to produce the denominator 1+tx.
                 */
                shift128RightJamming(dsig0, dsig1, 0x3fff - dexp,
                                     &dsig0, &dsig1);
                dsig0 |= 0x8000000000000000ULL;
                zexp = yexp - 1;
                remsig0 = ysig0;
                remsig1 = ysig1;
                remsig2 = 0;
                if (dsig0 <= remsig0) {
                    shift128Right(remsig0, remsig1, 1, &remsig0, &remsig1);
                    ++zexp;
                }
                zsig0 = estimateDiv128To64(remsig0, remsig1, dsig0);
                mul128By64To192(dsig0, dsig1, zsig0, &msig0, &msig1, &msig2);
                sub192(remsig0, remsig1, remsig2, msig0, msig1, msig2,
                       &remsig0, &remsig1, &remsig2);
                while ((int64_t) remsig0 < 0) {
                    --zsig0;
                    add192(remsig0, remsig1, remsig2, 0, dsig0, dsig1,
                           &remsig0, &remsig1, &remsig2);
                }
                zsig1 = estimateDiv128To64(remsig1, remsig2, dsig0);
                /* No need to correct any estimation error in zsig1.  */
            }

            if (zexp == 0) {
                azexp = 0;
                azsig0 = 0;
                azsig1 = 0;
            } else {
                floatx80 z2, accum;
                uint64_t z2sig0, z2sig1, z2sig2, z2sig3;
                /* Compute z^2.  */
                mul128To256(zsig0, zsig1, zsig0, zsig1,
                            &z2sig0, &z2sig1, &z2sig2, &z2sig3);
                z2 = normalizeRoundAndPackFloatx80(80, false,
                                                   zexp + zexp - 0x3ffe,
                                                   z2sig0, z2sig1,
                                                   &env->fp_status);

                /* Compute the lower parts of the polynomial expansion.  */
                accum = floatx80_mul(fpatan_coeff_6, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_5, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_4, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_3, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_2, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);
                accum = floatx80_add(fpatan_coeff_1, accum, &env->fp_status);
                accum = floatx80_mul(accum, z2, &env->fp_status);

                /*
                 * The full polynomial expansion is z*(fpatan_coeff_0 + accum).
                 * fpatan_coeff_0 is 1, and accum is negative and much smaller.
                 */
                aexp = extractFloatx80Exp(fpatan_coeff_0);
                shift128RightJamming(extractFloatx80Frac(accum), 0,
                                     aexp - extractFloatx80Exp(accum),
                                     &asig0, &asig1);
                sub128(extractFloatx80Frac(fpatan_coeff_0), 0, asig0, asig1,
                       &asig0, &asig1);
                /* Multiply by z to compute arctan(z).  */
                azexp = aexp + zexp - 0x3ffe;
                mul128To256(asig0, asig1, zsig0, zsig1, &azsig0, &azsig1,
                            &azsig2, &azsig3);
            }

            /* Add arctan(t) (positive or zero) and arctan(z) (sign zsign).  */
            if (texp == 0) {
                /* z is positive.  */
                axexp = azexp;
                axsig0 = azsig0;
                axsig1 = azsig1;
            } else {
                bool low_sign = extractFloatx80Sign(fpatan_table[n].atan_low);
                int32_t low_exp = extractFloatx80Exp(fpatan_table[n].atan_low);
                uint64_t low_sig0 =
                    extractFloatx80Frac(fpatan_table[n].atan_low);
                uint64_t low_sig1 = 0;
                axexp = extractFloatx80Exp(fpatan_table[n].atan_high);
                axsig0 = extractFloatx80Frac(fpatan_table[n].atan_high);
                axsig1 = 0;
                shift128RightJamming(low_sig0, low_sig1, axexp - low_exp,
                                     &low_sig0, &low_sig1);
                if (low_sign) {
                    sub128(axsig0, axsig1, low_sig0, low_sig1,
                           &axsig0, &axsig1);
                } else {
                    add128(axsig0, axsig1, low_sig0, low_sig1,
                           &axsig0, &axsig1);
                }
                if (azexp >= axexp) {
                    shift128RightJamming(axsig0, axsig1, azexp - axexp + 1,
                                         &axsig0, &axsig1);
                    axexp = azexp + 1;
                    shift128RightJamming(azsig0, azsig1, 1,
                                         &azsig0, &azsig1);
                } else {
                    shift128RightJamming(axsig0, axsig1, 1,
                                         &axsig0, &axsig1);
                    shift128RightJamming(azsig0, azsig1, axexp - azexp + 1,
                                         &azsig0, &azsig1);
                    ++axexp;
                }
                if (zsign) {
                    sub128(axsig0, axsig1, azsig0, azsig1,
                           &axsig0, &axsig1);
                } else {
                    add128(axsig0, axsig1, azsig0, azsig1,
                           &axsig0, &axsig1);
                }
            }

            if (adj_exp == 0) {
                rexp = axexp;
                rsig0 = axsig0;
                rsig1 = axsig1;
            } else {
                /*
                 * Add or subtract arctan(x) (exponent axexp,
                 * significand axsig0 and axsig1, positive, not
                 * necessarily normalized) to the number given by
                 * adj_exp, adj_sig0 and adj_sig1, according to
                 * adj_sub.
                 */
                if (adj_exp >= axexp) {
                    shift128RightJamming(axsig0, axsig1, adj_exp - axexp + 1,
                                         &axsig0, &axsig1);
                    rexp = adj_exp + 1;
                    shift128RightJamming(adj_sig0, adj_sig1, 1,
                                         &adj_sig0, &adj_sig1);
                } else {
                    shift128RightJamming(axsig0, axsig1, 1,
                                         &axsig0, &axsig1);
                    shift128RightJamming(adj_sig0, adj_sig1,
                                         axexp - adj_exp + 1,
                                         &adj_sig0, &adj_sig1);
                    rexp = axexp + 1;
                }
                if (adj_sub) {
                    sub128(adj_sig0, adj_sig1, axsig0, axsig1,
                           &rsig0, &rsig1);
                } else {
                    add128(adj_sig0, adj_sig1, axsig0, axsig1,
                           &rsig0, &rsig1);
                }
            }

            env->fp_status.float_rounding_mode = save_mode;
            env->fp_status.floatx80_rounding_precision = save_prec;
        }
        /* This result is inexact.  */
        rsig1 |= 1;
        ST1 = normalizeRoundAndPackFloatx80(80, rsign, rexp,
                                            rsig0, rsig1, &env->fp_status);
    }

    fpop(env);
    merge_exception_flags(env, old_flags);
}

void helper_fxtract(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    CPU_LDoubleU temp;

    temp.d = ST0;

    if (floatx80_is_zero(ST0)) {
        /* Easy way to generate -inf and raising division by 0 exception */
        ST0 = floatx80_div(floatx80_chs(floatx80_one), floatx80_zero,
                           &env->fp_status);
        fpush(env);
        ST0 = temp.d;
    } else if (floatx80_invalid_encoding(ST0)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST0 = floatx80_default_nan(&env->fp_status);
        fpush(env);
        ST0 = ST1;
    } else if (floatx80_is_any_nan(ST0)) {
        if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST0 = floatx80_silence_nan(ST0, &env->fp_status);
        }
        fpush(env);
        ST0 = ST1;
    } else if (floatx80_is_infinity(ST0)) {
        fpush(env);
        ST0 = ST1;
        ST1 = floatx80_infinity;
    } else {
        int expdif;

        if (EXPD(temp) == 0) {
            int shift = clz64(temp.l.lower);
            temp.l.lower <<= shift;
            expdif = 1 - EXPBIAS - shift;
            float_raise(float_flag_input_denormal, &env->fp_status);
        } else {
            expdif = EXPD(temp) - EXPBIAS;
        }
        /* DP exponent bias */
        ST0 = int32_to_floatx80(expdif, &env->fp_status);
        fpush(env);
        BIASEXPONENT(temp);
        ST0 = temp.d;
    }
    merge_exception_flags(env, old_flags);
}

void helper_fprem1(CPUX86State *env)
{
    double st0, st1, dblq, fpsrcop, fptemp;
    CPU_LDoubleU fpsrcop1, fptemp1;
    int expdif;
    signed long long int q;

    st0 = floatx80_to_double(env, ST0);
    st1 = floatx80_to_double(env, ST1);

    if (isinf(st0) || isnan(st0) || isnan(st1) || (st1 == 0.0)) {
        ST0 = double_to_floatx80(env, 0.0 / 0.0); /* NaN */
        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        return;
    }

    fpsrcop = st0;
    fptemp = st1;
    fpsrcop1.d = ST0;
    fptemp1.d = ST1;
    expdif = EXPD(fpsrcop1) - EXPD(fptemp1);

    if (expdif < 0) {
        /* optimisation? taken from the AMD docs */
        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        /* ST0 is unchanged */
        return;
    }

    if (expdif < 53) {
        dblq = fpsrcop / fptemp;
        /* round dblq towards nearest integer */
        dblq = rint(dblq);
        st0 = fpsrcop - fptemp * dblq;

        /* convert dblq to q by truncating towards zero */
        if (dblq < 0.0) {
            q = (signed long long int)(-dblq);
        } else {
            q = (signed long long int)dblq;
        }

        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        /* (C0,C3,C1) <-- (q2,q1,q0) */
        env->fpus |= (q & 0x4) << (8 - 2);  /* (C0) <-- q2 */
        env->fpus |= (q & 0x2) << (14 - 1); /* (C3) <-- q1 */
        env->fpus |= (q & 0x1) << (9 - 0);  /* (C1) <-- q0 */
    } else {
        env->fpus |= 0x400;  /* C2 <-- 1 */
        fptemp = pow(2.0, expdif - 50);
        fpsrcop = (st0 / st1) / fptemp;
        /* fpsrcop = integer obtained by chopping */
        fpsrcop = (fpsrcop < 0.0) ?
                  -(floor(fabs(fpsrcop))) : floor(fpsrcop);
        st0 -= (st1 * fpsrcop * fptemp);
    }
    ST0 = double_to_floatx80(env, st0);
}

void helper_fprem(CPUX86State *env)
{
    double st0, st1, dblq, fpsrcop, fptemp;
    CPU_LDoubleU fpsrcop1, fptemp1;
    int expdif;
    signed long long int q;

    st0 = floatx80_to_double(env, ST0);
    st1 = floatx80_to_double(env, ST1);

    if (isinf(st0) || isnan(st0) || isnan(st1) || (st1 == 0.0)) {
        ST0 = double_to_floatx80(env, 0.0 / 0.0); /* NaN */
        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        return;
    }

    fpsrcop = st0;
    fptemp = st1;
    fpsrcop1.d = ST0;
    fptemp1.d = ST1;
    expdif = EXPD(fpsrcop1) - EXPD(fptemp1);

    if (expdif < 0) {
        /* optimisation? taken from the AMD docs */
        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        /* ST0 is unchanged */
        return;
    }

    if (expdif < 53) {
        dblq = fpsrcop / fptemp; /* ST0 / ST1 */
        /* round dblq towards zero */
        dblq = (dblq < 0.0) ? ceil(dblq) : floor(dblq);
        st0 = fpsrcop - fptemp * dblq; /* fpsrcop is ST0 */

        /* convert dblq to q by truncating towards zero */
        if (dblq < 0.0) {
            q = (signed long long int)(-dblq);
        } else {
            q = (signed long long int)dblq;
        }

        env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
        /* (C0,C3,C1) <-- (q2,q1,q0) */
        env->fpus |= (q & 0x4) << (8 - 2);  /* (C0) <-- q2 */
        env->fpus |= (q & 0x2) << (14 - 1); /* (C3) <-- q1 */
        env->fpus |= (q & 0x1) << (9 - 0);  /* (C1) <-- q0 */
    } else {
        int N = 32 + (expdif % 32); /* as per AMD docs */

        env->fpus |= 0x400;  /* C2 <-- 1 */
        fptemp = pow(2.0, (double)(expdif - N));
        fpsrcop = (st0 / st1) / fptemp;
        /* fpsrcop = integer obtained by chopping */
        fpsrcop = (fpsrcop < 0.0) ?
                  -(floor(fabs(fpsrcop))) : floor(fpsrcop);
        st0 -= (st1 * fpsrcop * fptemp);
    }
    ST0 = double_to_floatx80(env, st0);
}

void helper_fyl2xp1(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp + 1.0) > 0.0) {
        fptemp = log(fptemp + 1.0) / log(2.0); /* log2(ST + 1.0) */
        fptemp *= floatx80_to_double(env, ST1);
        ST1 = double_to_floatx80(env, fptemp);
        fpop(env);
    } else {
        env->fpus &= ~0x4700;
        env->fpus |= 0x400;
    }
}

void helper_fsqrt(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    if (floatx80_is_neg(ST0)) {
        env->fpus &= ~0x4700;  /* (C3,C2,C1,C0) <-- 0000 */
        env->fpus |= 0x400;
    }
    ST0 = floatx80_sqrt(ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fsincos(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, sin(fptemp));
        fpush(env);
        ST0 = double_to_floatx80(env, cos(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**63 only */
    }
}

void helper_frndint(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    ST0 = floatx80_round_to_int(ST0, &env->fp_status);
    merge_exception_flags(env, old_flags);
}

void helper_fscale(CPUX86State *env)
{
    uint8_t old_flags = save_exception_flags(env);
    if (floatx80_invalid_encoding(ST1) || floatx80_invalid_encoding(ST0)) {
        float_raise(float_flag_invalid, &env->fp_status);
        ST0 = floatx80_default_nan(&env->fp_status);
    } else if (floatx80_is_any_nan(ST1)) {
        if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
            float_raise(float_flag_invalid, &env->fp_status);
        }
        ST0 = ST1;
        if (floatx80_is_signaling_nan(ST0, &env->fp_status)) {
            float_raise(float_flag_invalid, &env->fp_status);
            ST0 = floatx80_silence_nan(ST0, &env->fp_status);
        }
    } else if (floatx80_is_infinity(ST1) &&
               !floatx80_invalid_encoding(ST0) &&
               !floatx80_is_any_nan(ST0)) {
        if (floatx80_is_neg(ST1)) {
            if (floatx80_is_infinity(ST0)) {
                float_raise(float_flag_invalid, &env->fp_status);
                ST0 = floatx80_default_nan(&env->fp_status);
            } else {
                ST0 = (floatx80_is_neg(ST0) ?
                       floatx80_chs(floatx80_zero) :
                       floatx80_zero);
            }
        } else {
            if (floatx80_is_zero(ST0)) {
                float_raise(float_flag_invalid, &env->fp_status);
                ST0 = floatx80_default_nan(&env->fp_status);
            } else {
                ST0 = (floatx80_is_neg(ST0) ?
                       floatx80_chs(floatx80_infinity) :
                       floatx80_infinity);
            }
        }
    } else {
        int n;
        signed char save = env->fp_status.floatx80_rounding_precision;
        uint8_t save_flags = get_float_exception_flags(&env->fp_status);
        set_float_exception_flags(0, &env->fp_status);
        n = floatx80_to_int32_round_to_zero(ST1, &env->fp_status);
        set_float_exception_flags(save_flags, &env->fp_status);
        env->fp_status.floatx80_rounding_precision = 80;
        ST0 = floatx80_scalbn(ST0, n, &env->fp_status);
        env->fp_status.floatx80_rounding_precision = save;
    }
    merge_exception_flags(env, old_flags);
}

void helper_fsin(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, sin(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**53 only */
    }
}

void helper_fcos(CPUX86State *env)
{
    double fptemp = floatx80_to_double(env, ST0);

    if ((fptemp > MAXTAN) || (fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = double_to_floatx80(env, cos(fptemp));
        env->fpus &= ~0x400;  /* C2 <-- 0 */
        /* the above code is for |arg| < 2**63 only */
    }
}

void helper_fxam_ST0(CPUX86State *env)
{
    CPU_LDoubleU temp;
    int expdif;

    temp.d = ST0;

    env->fpus &= ~0x4700; /* (C3,C2,C1,C0) <-- 0000 */
    if (SIGND(temp)) {
        env->fpus |= 0x200; /* C1 <-- 1 */
    }

    if (env->fptags[env->fpstt]) {
        env->fpus |= 0x4100; /* Empty */
        return;
    }

    expdif = EXPD(temp);
    if (expdif == MAXEXPD) {
        if (MANTD(temp) == 0x8000000000000000ULL) {
            env->fpus |= 0x500; /* Infinity */
        } else if (MANTD(temp) & 0x8000000000000000ULL) {
            env->fpus |= 0x100; /* NaN */
        }
    } else if (expdif == 0) {
        if (MANTD(temp) == 0) {
            env->fpus |=  0x4000; /* Zero */
        } else {
            env->fpus |= 0x4400; /* Denormal */
        }
    } else if (MANTD(temp) & 0x8000000000000000ULL) {
        env->fpus |= 0x400;
    }
}

static void do_fstenv(CPUX86State *env, target_ulong ptr, int data32,
                      uintptr_t retaddr)
{
    int fpus, fptag, exp, i;
    uint64_t mant;
    CPU_LDoubleU tmp;

    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for (i = 7; i >= 0; i--) {
        fptag <<= 2;
        if (env->fptags[i]) {
            fptag |= 3;
        } else {
            tmp.d = env->fpregs[i].d;
            exp = EXPD(tmp);
            mant = MANTD(tmp);
            if (exp == 0 && mant == 0) {
                /* zero */
                fptag |= 1;
            } else if (exp == 0 || exp == MAXEXPD
                       || (mant & (1LL << 63)) == 0) {
                /* NaNs, infinity, denormal */
                fptag |= 2;
            }
        }
    }
    if (data32) {
        /* 32 bit */
        cpu_stl_data_ra(env, ptr, env->fpuc, retaddr);
        cpu_stl_data_ra(env, ptr + 4, fpus, retaddr);
        cpu_stl_data_ra(env, ptr + 8, fptag, retaddr);
        cpu_stl_data_ra(env, ptr + 12, 0, retaddr); /* fpip */
        cpu_stl_data_ra(env, ptr + 16, 0, retaddr); /* fpcs */
        cpu_stl_data_ra(env, ptr + 20, 0, retaddr); /* fpoo */
        cpu_stl_data_ra(env, ptr + 24, 0, retaddr); /* fpos */
    } else {
        /* 16 bit */
        cpu_stw_data_ra(env, ptr, env->fpuc, retaddr);
        cpu_stw_data_ra(env, ptr + 2, fpus, retaddr);
        cpu_stw_data_ra(env, ptr + 4, fptag, retaddr);
        cpu_stw_data_ra(env, ptr + 6, 0, retaddr);
        cpu_stw_data_ra(env, ptr + 8, 0, retaddr);
        cpu_stw_data_ra(env, ptr + 10, 0, retaddr);
        cpu_stw_data_ra(env, ptr + 12, 0, retaddr);
    }
}

void helper_fstenv(CPUX86State *env, target_ulong ptr, int data32)
{
    do_fstenv(env, ptr, data32, GETPC());
}

static void cpu_set_fpus(CPUX86State *env, uint16_t fpus)
{
    env->fpstt = (fpus >> 11) & 7;
    env->fpus = fpus & ~0x3800 & ~FPUS_B;
    env->fpus |= env->fpus & FPUS_SE ? FPUS_B : 0;
#if !defined(CONFIG_USER_ONLY)
    if (!(env->fpus & FPUS_SE)) {
        /*
         * Here the processor deasserts FERR#; in response, the chipset deasserts
         * IGNNE#.
         */
        cpu_clear_ignne();
    }
#endif
}

static void do_fldenv(CPUX86State *env, target_ulong ptr, int data32,
                      uintptr_t retaddr)
{
    int i, fpus, fptag;

    if (data32) {
        cpu_set_fpuc(env, cpu_lduw_data_ra(env, ptr, retaddr));
        fpus = cpu_lduw_data_ra(env, ptr + 4, retaddr);
        fptag = cpu_lduw_data_ra(env, ptr + 8, retaddr);
    } else {
        cpu_set_fpuc(env, cpu_lduw_data_ra(env, ptr, retaddr));
        fpus = cpu_lduw_data_ra(env, ptr + 2, retaddr);
        fptag = cpu_lduw_data_ra(env, ptr + 4, retaddr);
    }
    cpu_set_fpus(env, fpus);
    for (i = 0; i < 8; i++) {
        env->fptags[i] = ((fptag & 3) == 3);
        fptag >>= 2;
    }
}

void helper_fldenv(CPUX86State *env, target_ulong ptr, int data32)
{
    do_fldenv(env, ptr, data32, GETPC());
}

void helper_fsave(CPUX86State *env, target_ulong ptr, int data32)
{
    floatx80 tmp;
    int i;

    do_fstenv(env, ptr, data32, GETPC());

    ptr += (14 << data32);
    for (i = 0; i < 8; i++) {
        tmp = ST(i);
        helper_fstt(env, tmp, ptr, GETPC());
        ptr += 10;
    }

    /* fninit */
    env->fpus = 0;
    env->fpstt = 0;
    cpu_set_fpuc(env, 0x37f);
    env->fptags[0] = 1;
    env->fptags[1] = 1;
    env->fptags[2] = 1;
    env->fptags[3] = 1;
    env->fptags[4] = 1;
    env->fptags[5] = 1;
    env->fptags[6] = 1;
    env->fptags[7] = 1;
}

void helper_frstor(CPUX86State *env, target_ulong ptr, int data32)
{
    floatx80 tmp;
    int i;

    do_fldenv(env, ptr, data32, GETPC());
    ptr += (14 << data32);

    for (i = 0; i < 8; i++) {
        tmp = helper_fldt(env, ptr, GETPC());
        ST(i) = tmp;
        ptr += 10;
    }
}

#if defined(CONFIG_USER_ONLY)
void cpu_x86_fsave(CPUX86State *env, target_ulong ptr, int data32)
{
    helper_fsave(env, ptr, data32);
}

void cpu_x86_frstor(CPUX86State *env, target_ulong ptr, int data32)
{
    helper_frstor(env, ptr, data32);
}
#endif

#define XO(X)  offsetof(X86XSaveArea, X)

static void do_xsave_fpu(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int fpus, fptag, i;
    target_ulong addr;

    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for (i = 0; i < 8; i++) {
        fptag |= (env->fptags[i] << i);
    }

    cpu_stw_data_ra(env, ptr + XO(legacy.fcw), env->fpuc, ra);
    cpu_stw_data_ra(env, ptr + XO(legacy.fsw), fpus, ra);
    cpu_stw_data_ra(env, ptr + XO(legacy.ftw), fptag ^ 0xff, ra);

    /* In 32-bit mode this is eip, sel, dp, sel.
       In 64-bit mode this is rip, rdp.
       But in either case we don't write actual data, just zeros.  */
    cpu_stq_data_ra(env, ptr + XO(legacy.fpip), 0, ra); /* eip+sel; rip */
    cpu_stq_data_ra(env, ptr + XO(legacy.fpdp), 0, ra); /* edp+sel; rdp */

    addr = ptr + XO(legacy.fpregs);
    for (i = 0; i < 8; i++) {
        floatx80 tmp = ST(i);
        helper_fstt(env, tmp, addr, ra);
        addr += 16;
    }
}

static void do_xsave_mxcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_stl_data_ra(env, ptr + XO(legacy.mxcsr), env->mxcsr, ra);
    cpu_stl_data_ra(env, ptr + XO(legacy.mxcsr_mask), 0x0000ffff, ra);
}

static void do_xsave_sse(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, nb_xmm_regs;
    target_ulong addr;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    addr = ptr + XO(legacy.xmm_regs);
    for (i = 0; i < nb_xmm_regs; i++) {
        cpu_stq_data_ra(env, addr, env->xmm_regs[i].ZMM_Q(0), ra);
        cpu_stq_data_ra(env, addr + 8, env->xmm_regs[i].ZMM_Q(1), ra);
        addr += 16;
    }
}

static void do_xsave_bndregs(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    target_ulong addr = ptr + offsetof(XSaveBNDREG, bnd_regs);
    int i;

    for (i = 0; i < 4; i++, addr += 16) {
        cpu_stq_data_ra(env, addr, env->bnd_regs[i].lb, ra);
        cpu_stq_data_ra(env, addr + 8, env->bnd_regs[i].ub, ra);
    }
}

static void do_xsave_bndcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_stq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.cfgu),
                    env->bndcs_regs.cfgu, ra);
    cpu_stq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.sts),
                    env->bndcs_regs.sts, ra);
}

static void do_xsave_pkru(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_stq_data_ra(env, ptr, env->pkru, ra);
}

void helper_fxsave(CPUX86State *env, target_ulong ptr)
{
    uintptr_t ra = GETPC();

    /* The operand must be 16 byte aligned */
    if (ptr & 0xf) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    do_xsave_fpu(env, ptr, ra);

    if (env->cr[4] & CR4_OSFXSR_MASK) {
        do_xsave_mxcsr(env, ptr, ra);
        /* Fast FXSAVE leaves out the XMM registers */
        if (!(env->efer & MSR_EFER_FFXSR)
            || (env->hflags & HF_CPL_MASK)
            || !(env->hflags & HF_LMA_MASK)) {
            do_xsave_sse(env, ptr, ra);
        }
    }
}

static uint64_t get_xinuse(CPUX86State *env)
{
    uint64_t inuse = -1;

    /* For the most part, we don't track XINUSE.  We could calculate it
       here for all components, but it's probably less work to simply
       indicate in use.  That said, the state of BNDREGS is important
       enough to track in HFLAGS, so we might as well use that here.  */
    if ((env->hflags & HF_MPX_IU_MASK) == 0) {
       inuse &= ~XSTATE_BNDREGS_MASK;
    }
    return inuse;
}

static void do_xsave(CPUX86State *env, target_ulong ptr, uint64_t rfbm,
                     uint64_t inuse, uint64_t opt, uintptr_t ra)
{
    uint64_t old_bv, new_bv;

    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }

    /* The operand must be 64 byte aligned.  */
    if (ptr & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    /* Never save anything not enabled by XCR0.  */
    rfbm &= env->xcr0;
    opt &= rfbm;

    if (opt & XSTATE_FP_MASK) {
        do_xsave_fpu(env, ptr, ra);
    }
    if (rfbm & XSTATE_SSE_MASK) {
        /* Note that saving MXCSR is not suppressed by XSAVEOPT.  */
        do_xsave_mxcsr(env, ptr, ra);
    }
    if (opt & XSTATE_SSE_MASK) {
        do_xsave_sse(env, ptr, ra);
    }
    if (opt & XSTATE_BNDREGS_MASK) {
        do_xsave_bndregs(env, ptr + XO(bndreg_state), ra);
    }
    if (opt & XSTATE_BNDCSR_MASK) {
        do_xsave_bndcsr(env, ptr + XO(bndcsr_state), ra);
    }
    if (opt & XSTATE_PKRU_MASK) {
        do_xsave_pkru(env, ptr + XO(pkru_state), ra);
    }

    /* Update the XSTATE_BV field.  */
    old_bv = cpu_ldq_data_ra(env, ptr + XO(header.xstate_bv), ra);
    new_bv = (old_bv & ~rfbm) | (inuse & rfbm);
    cpu_stq_data_ra(env, ptr + XO(header.xstate_bv), new_bv, ra);
}

void helper_xsave(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    do_xsave(env, ptr, rfbm, get_xinuse(env), -1, GETPC());
}

void helper_xsaveopt(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    uint64_t inuse = get_xinuse(env);
    do_xsave(env, ptr, rfbm, inuse, inuse, GETPC());
}

static void do_xrstor_fpu(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, fpuc, fpus, fptag;
    target_ulong addr;

    fpuc = cpu_lduw_data_ra(env, ptr + XO(legacy.fcw), ra);
    fpus = cpu_lduw_data_ra(env, ptr + XO(legacy.fsw), ra);
    fptag = cpu_lduw_data_ra(env, ptr + XO(legacy.ftw), ra);
    cpu_set_fpuc(env, fpuc);
    cpu_set_fpus(env, fpus);
    fptag ^= 0xff;
    for (i = 0; i < 8; i++) {
        env->fptags[i] = ((fptag >> i) & 1);
    }

    addr = ptr + XO(legacy.fpregs);
    for (i = 0; i < 8; i++) {
        floatx80 tmp = helper_fldt(env, addr, ra);
        ST(i) = tmp;
        addr += 16;
    }
}

static void do_xrstor_mxcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    cpu_set_mxcsr(env, cpu_ldl_data_ra(env, ptr + XO(legacy.mxcsr), ra));
}

static void do_xrstor_sse(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    int i, nb_xmm_regs;
    target_ulong addr;

    if (env->hflags & HF_CS64_MASK) {
        nb_xmm_regs = 16;
    } else {
        nb_xmm_regs = 8;
    }

    addr = ptr + XO(legacy.xmm_regs);
    for (i = 0; i < nb_xmm_regs; i++) {
        env->xmm_regs[i].ZMM_Q(0) = cpu_ldq_data_ra(env, addr, ra);
        env->xmm_regs[i].ZMM_Q(1) = cpu_ldq_data_ra(env, addr + 8, ra);
        addr += 16;
    }
}

static void do_xrstor_bndregs(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    target_ulong addr = ptr + offsetof(XSaveBNDREG, bnd_regs);
    int i;

    for (i = 0; i < 4; i++, addr += 16) {
        env->bnd_regs[i].lb = cpu_ldq_data_ra(env, addr, ra);
        env->bnd_regs[i].ub = cpu_ldq_data_ra(env, addr + 8, ra);
    }
}

static void do_xrstor_bndcsr(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    /* FIXME: Extend highest implemented bit of linear address.  */
    env->bndcs_regs.cfgu
        = cpu_ldq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.cfgu), ra);
    env->bndcs_regs.sts
        = cpu_ldq_data_ra(env, ptr + offsetof(XSaveBNDCSR, bndcsr.sts), ra);
}

static void do_xrstor_pkru(CPUX86State *env, target_ulong ptr, uintptr_t ra)
{
    env->pkru = cpu_ldq_data_ra(env, ptr, ra);
}

void helper_fxrstor(CPUX86State *env, target_ulong ptr)
{
    uintptr_t ra = GETPC();

    /* The operand must be 16 byte aligned */
    if (ptr & 0xf) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    do_xrstor_fpu(env, ptr, ra);

    if (env->cr[4] & CR4_OSFXSR_MASK) {
        do_xrstor_mxcsr(env, ptr, ra);
        /* Fast FXRSTOR leaves out the XMM registers */
        if (!(env->efer & MSR_EFER_FFXSR)
            || (env->hflags & HF_CPL_MASK)
            || !(env->hflags & HF_LMA_MASK)) {
            do_xrstor_sse(env, ptr, ra);
        }
    }
}

#if defined(CONFIG_USER_ONLY)
void cpu_x86_fxsave(CPUX86State *env, target_ulong ptr)
{
    helper_fxsave(env, ptr);
}

void cpu_x86_fxrstor(CPUX86State *env, target_ulong ptr)
{
    helper_fxrstor(env, ptr);
}
#endif

void helper_xrstor(CPUX86State *env, target_ulong ptr, uint64_t rfbm)
{
    uintptr_t ra = GETPC();
    uint64_t xstate_bv, xcomp_bv, reserve0;

    rfbm &= env->xcr0;

    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, ra);
    }

    /* The operand must be 64 byte aligned.  */
    if (ptr & 63) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    xstate_bv = cpu_ldq_data_ra(env, ptr + XO(header.xstate_bv), ra);

    if ((int64_t)xstate_bv < 0) {
        /* FIXME: Compact form.  */
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    /* Standard form.  */

    /* The XSTATE_BV field must not set bits not present in XCR0.  */
    if (xstate_bv & ~env->xcr0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    /* The XCOMP_BV field must be zero.  Note that, as of the April 2016
       revision, the description of the XSAVE Header (Vol 1, Sec 13.4.2)
       describes only XCOMP_BV, but the description of the standard form
       of XRSTOR (Vol 1, Sec 13.8.1) checks bytes 23:8 for zero, which
       includes the next 64-bit field.  */
    xcomp_bv = cpu_ldq_data_ra(env, ptr + XO(header.xcomp_bv), ra);
    reserve0 = cpu_ldq_data_ra(env, ptr + XO(header.reserve0), ra);
    if (xcomp_bv || reserve0) {
        raise_exception_ra(env, EXCP0D_GPF, ra);
    }

    if (rfbm & XSTATE_FP_MASK) {
        if (xstate_bv & XSTATE_FP_MASK) {
            do_xrstor_fpu(env, ptr, ra);
        } else {
            helper_fninit(env);
            memset(env->fpregs, 0, sizeof(env->fpregs));
        }
    }
    if (rfbm & XSTATE_SSE_MASK) {
        /* Note that the standard form of XRSTOR loads MXCSR from memory
           whether or not the XSTATE_BV bit is set.  */
        do_xrstor_mxcsr(env, ptr, ra);
        if (xstate_bv & XSTATE_SSE_MASK) {
            do_xrstor_sse(env, ptr, ra);
        } else {
            /* ??? When AVX is implemented, we may have to be more
               selective in the clearing.  */
            memset(env->xmm_regs, 0, sizeof(env->xmm_regs));
        }
    }
    if (rfbm & XSTATE_BNDREGS_MASK) {
        if (xstate_bv & XSTATE_BNDREGS_MASK) {
            do_xrstor_bndregs(env, ptr + XO(bndreg_state), ra);
            env->hflags |= HF_MPX_IU_MASK;
        } else {
            memset(env->bnd_regs, 0, sizeof(env->bnd_regs));
            env->hflags &= ~HF_MPX_IU_MASK;
        }
    }
    if (rfbm & XSTATE_BNDCSR_MASK) {
        if (xstate_bv & XSTATE_BNDCSR_MASK) {
            do_xrstor_bndcsr(env, ptr + XO(bndcsr_state), ra);
        } else {
            memset(&env->bndcs_regs, 0, sizeof(env->bndcs_regs));
        }
        cpu_sync_bndcs_hflags(env);
    }
    if (rfbm & XSTATE_PKRU_MASK) {
        uint64_t old_pkru = env->pkru;
        if (xstate_bv & XSTATE_PKRU_MASK) {
            do_xrstor_pkru(env, ptr + XO(pkru_state), ra);
        } else {
            env->pkru = 0;
        }
        if (env->pkru != old_pkru) {
            CPUState *cs = env_cpu(env);
            tlb_flush(cs);
        }
    }
}

#undef XO

uint64_t helper_xgetbv(CPUX86State *env, uint32_t ecx)
{
    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }

    switch (ecx) {
    case 0:
        return env->xcr0;
    case 1:
        if (env->features[FEAT_XSAVE] & CPUID_XSAVE_XGETBV1) {
            return env->xcr0 & get_xinuse(env);
        }
        break;
    }
    raise_exception_ra(env, EXCP0D_GPF, GETPC());
}

void helper_xsetbv(CPUX86State *env, uint32_t ecx, uint64_t mask)
{
    uint32_t dummy, ena_lo, ena_hi;
    uint64_t ena;

    /* The OS must have enabled XSAVE.  */
    if (!(env->cr[4] & CR4_OSXSAVE_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }

    /* Only XCR0 is defined at present; the FPU may not be disabled.  */
    if (ecx != 0 || (mask & XSTATE_FP_MASK) == 0) {
        goto do_gpf;
    }

    /* Disallow enabling unimplemented features.  */
    cpu_x86_cpuid(env, 0x0d, 0, &ena_lo, &dummy, &dummy, &ena_hi);
    ena = ((uint64_t)ena_hi << 32) | ena_lo;
    if (mask & ~ena) {
        goto do_gpf;
    }

    /* Disallow enabling only half of MPX.  */
    if ((mask ^ (mask * (XSTATE_BNDCSR_MASK / XSTATE_BNDREGS_MASK)))
        & XSTATE_BNDCSR_MASK) {
        goto do_gpf;
    }

    env->xcr0 = mask;
    cpu_sync_bndcs_hflags(env);
    return;

 do_gpf:
    raise_exception_ra(env, EXCP0D_GPF, GETPC());
}

/* MMX/SSE */
/* XXX: optimize by storing fptt and fptags in the static cpu state */

#define SSE_DAZ             0x0040
#define SSE_RC_MASK         0x6000
#define SSE_RC_NEAR         0x0000
#define SSE_RC_DOWN         0x2000
#define SSE_RC_UP           0x4000
#define SSE_RC_CHOP         0x6000
#define SSE_FZ              0x8000

void update_mxcsr_status(CPUX86State *env)
{
    uint32_t mxcsr = env->mxcsr;
    int rnd_type;

    /* set rounding mode */
    switch (mxcsr & SSE_RC_MASK) {
    default:
    case SSE_RC_NEAR:
        rnd_type = float_round_nearest_even;
        break;
    case SSE_RC_DOWN:
        rnd_type = float_round_down;
        break;
    case SSE_RC_UP:
        rnd_type = float_round_up;
        break;
    case SSE_RC_CHOP:
        rnd_type = float_round_to_zero;
        break;
    }
    set_float_rounding_mode(rnd_type, &env->sse_status);

    /* set denormals are zero */
    set_flush_inputs_to_zero((mxcsr & SSE_DAZ) ? 1 : 0, &env->sse_status);

    /* set flush to zero */
    set_flush_to_zero((mxcsr & SSE_FZ) ? 1 : 0, &env->fp_status);
}

void helper_ldmxcsr(CPUX86State *env, uint32_t val)
{
    cpu_set_mxcsr(env, val);
}

void helper_enter_mmx(CPUX86State *env)
{
    env->fpstt = 0;
    *(uint32_t *)(env->fptags) = 0;
    *(uint32_t *)(env->fptags + 4) = 0;
}

void helper_emms(CPUX86State *env)
{
    /* set to empty state */
    *(uint32_t *)(env->fptags) = 0x01010101;
    *(uint32_t *)(env->fptags + 4) = 0x01010101;
}

/* XXX: suppress */
void helper_movq(CPUX86State *env, void *d, void *s)
{
    *(uint64_t *)d = *(uint64_t *)s;
}

#define SHIFT 0
#include "ops_sse.h"

#define SHIFT 1
#include "ops_sse.h"
