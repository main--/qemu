/*
 * LoongArch translate functions
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

/* bit0(signaling/quiet) bit1(lt) bit2(eq) bit3(un) bit4(neq) */
static uint32_t get_fcmp_flags(int cond)
{
    uint32_t flags = 0;

    if (cond & 0x1) {
        flags |= FCMP_LT;
    }
    if (cond & 0x2) {
        flags |= FCMP_EQ;
    }
    if (cond & 0x4) {
        flags |= FCMP_UN;
    }
    if (cond & 0x8) {
        flags |= FCMP_GT | FCMP_LT;
    }
    return flags;
}

static bool trans_fcmp_cond_s(DisasContext *ctx, arg_fcmp_cond_s *a)
{
    TCGv var = tcg_temp_new();
    uint32_t flags;
    void (*fn)(TCGv, TCGv_env, TCGv, TCGv, TCGv_i32);

    fn = (a->fcond & 1 ? gen_helper_fcmp_s_s : gen_helper_fcmp_c_s);
    flags = get_fcmp_flags(a->fcond >> 1);

    fn(var, cpu_env, cpu_fpr[a->fj], cpu_fpr[a->fk], tcg_constant_i32(flags));

    tcg_gen_st8_tl(var, cpu_env, offsetof(CPULoongArchState, cf[a->cd & 0x7]));
    tcg_temp_free(var);
    return true;
}

static bool trans_fcmp_cond_d(DisasContext *ctx, arg_fcmp_cond_d *a)
{
    TCGv var = tcg_temp_new();
    uint32_t flags;
    void (*fn)(TCGv, TCGv_env, TCGv, TCGv, TCGv_i32);
    fn = (a->fcond & 1 ? gen_helper_fcmp_s_d : gen_helper_fcmp_c_d);
    flags = get_fcmp_flags(a->fcond >> 1);

    fn(var, cpu_env, cpu_fpr[a->fj], cpu_fpr[a->fk], tcg_constant_i32(flags));

    tcg_gen_st8_tl(var, cpu_env, offsetof(CPULoongArchState, cf[a->cd & 0x7]));

    tcg_temp_free(var);
    return true;
}
