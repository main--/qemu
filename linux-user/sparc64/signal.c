/*
 *  Emulation of Linux signals
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "../sparc/signal.c"

#define SPARC_MC_TSTATE 0
#define SPARC_MC_PC 1
#define SPARC_MC_NPC 2
#define SPARC_MC_Y 3
#define SPARC_MC_G1 4
#define SPARC_MC_G2 5
#define SPARC_MC_G3 6
#define SPARC_MC_G4 7
#define SPARC_MC_G5 8
#define SPARC_MC_G6 9
#define SPARC_MC_G7 10
#define SPARC_MC_O0 11
#define SPARC_MC_O1 12
#define SPARC_MC_O2 13
#define SPARC_MC_O3 14
#define SPARC_MC_O4 15
#define SPARC_MC_O5 16
#define SPARC_MC_O6 17
#define SPARC_MC_O7 18
#define SPARC_MC_NGREG 19

typedef abi_ulong target_mc_greg_t;
typedef target_mc_greg_t target_mc_gregset_t[SPARC_MC_NGREG];

struct target_mc_fq {
    abi_ulong mcfq_addr;
    uint32_t mcfq_insn;
};

/*
 * Note the manual 16-alignment; the kernel gets this because it
 * includes a "long double qregs[16]" in the mcpu_fregs union,
 * which we can't do.
 */
struct target_mc_fpu {
    union {
        uint32_t sregs[32];
        uint64_t dregs[32];
    } mcfpu_fregs;
    abi_ulong mcfpu_fsr;
    abi_ulong mcfpu_fprs;
    abi_ulong mcfpu_gsr;
    abi_ulong mcfpu_fq;
    unsigned char mcfpu_qcnt;
    unsigned char mcfpu_qentsz;
    unsigned char mcfpu_enab;
} __attribute__((aligned(16)));
typedef struct target_mc_fpu target_mc_fpu_t;

typedef struct {
    target_mc_gregset_t mc_gregs;
    target_mc_greg_t mc_fp;
    target_mc_greg_t mc_i7;
    target_mc_fpu_t mc_fpregs;
} target_mcontext_t;

struct target_ucontext {
    abi_ulong tuc_link;
    abi_ulong tuc_flags;
    target_sigset_t tuc_sigmask;
    target_mcontext_t tuc_mcontext;
};

/* A V9 register window */
struct target_reg_window {
    abi_ulong locals[8];
    abi_ulong ins[8];
};

/* {set, get}context() needed for 64-bit SparcLinux userland. */
void sparc64_set_context(CPUSPARCState *env)
{
    abi_ulong ucp_addr;
    struct target_ucontext *ucp;
    target_mc_gregset_t *grp;
    target_mc_fpu_t *fpup;
    abi_ulong pc, npc, tstate;
    unsigned int i;
    unsigned char fenab;

    ucp_addr = env->regwptr[WREG_O0];
    if (!lock_user_struct(VERIFY_READ, ucp, ucp_addr, 1)) {
        goto do_sigsegv;
    }
    grp  = &ucp->tuc_mcontext.mc_gregs;
    __get_user(pc, &((*grp)[SPARC_MC_PC]));
    __get_user(npc, &((*grp)[SPARC_MC_NPC]));
    if ((pc | npc) & 3) {
        goto do_sigsegv;
    }
    if (env->regwptr[WREG_O1]) {
        target_sigset_t target_set;
        sigset_t set;

        if (TARGET_NSIG_WORDS == 1) {
            __get_user(target_set.sig[0], &ucp->tuc_sigmask.sig[0]);
        } else {
            abi_ulong *src, *dst;
            src = ucp->tuc_sigmask.sig;
            dst = target_set.sig;
            for (i = 0; i < TARGET_NSIG_WORDS; i++, dst++, src++) {
                __get_user(*dst, src);
            }
        }
        target_to_host_sigset_internal(&set, &target_set);
        set_sigmask(&set);
    }
    env->pc = pc;
    env->npc = npc;
    __get_user(env->y, &((*grp)[SPARC_MC_Y]));
    __get_user(tstate, &((*grp)[SPARC_MC_TSTATE]));
    /* Honour TSTATE_ASI, TSTATE_ICC and TSTATE_XCC only */
    env->asi = (tstate >> 24) & 0xff;
    cpu_put_ccr(env, (tstate >> 32) & 0xff);
    __get_user(env->gregs[1], (&(*grp)[SPARC_MC_G1]));
    __get_user(env->gregs[2], (&(*grp)[SPARC_MC_G2]));
    __get_user(env->gregs[3], (&(*grp)[SPARC_MC_G3]));
    __get_user(env->gregs[4], (&(*grp)[SPARC_MC_G4]));
    __get_user(env->gregs[5], (&(*grp)[SPARC_MC_G5]));
    __get_user(env->gregs[6], (&(*grp)[SPARC_MC_G6]));
    /* Skip g7 as that's the thread register in userspace */

    /*
     * Note that unlike the kernel, we didn't need to mess with the
     * guest register window state to save it into a pt_regs to run
     * the kernel. So for us the guest's O regs are still in WREG_O*
     * (unlike the kernel which has put them in UREG_I* in a pt_regs)
     * and the fp and i7 are still in WREG_I6 and WREG_I7 and don't
     * need to be written back to userspace memory.
     */
    __get_user(env->regwptr[WREG_O0], (&(*grp)[SPARC_MC_O0]));
    __get_user(env->regwptr[WREG_O1], (&(*grp)[SPARC_MC_O1]));
    __get_user(env->regwptr[WREG_O2], (&(*grp)[SPARC_MC_O2]));
    __get_user(env->regwptr[WREG_O3], (&(*grp)[SPARC_MC_O3]));
    __get_user(env->regwptr[WREG_O4], (&(*grp)[SPARC_MC_O4]));
    __get_user(env->regwptr[WREG_O5], (&(*grp)[SPARC_MC_O5]));
    __get_user(env->regwptr[WREG_O6], (&(*grp)[SPARC_MC_O6]));
    __get_user(env->regwptr[WREG_O7], (&(*grp)[SPARC_MC_O7]));

    __get_user(env->regwptr[WREG_FP], &(ucp->tuc_mcontext.mc_fp));
    __get_user(env->regwptr[WREG_I7], &(ucp->tuc_mcontext.mc_i7));

    fpup = &ucp->tuc_mcontext.mc_fpregs;

    __get_user(fenab, &(fpup->mcfpu_enab));
    if (fenab) {
        abi_ulong fprs;

        /*
         * We use the FPRS from the guest only in deciding whether
         * to restore the upper, lower, or both banks of the FPU regs.
         * The kernel here writes the FPU register data into the
         * process's current_thread_info state and unconditionally
         * clears FPRS and TSTATE_PEF: this disables the FPU so that the
         * next FPU-disabled trap will copy the data out of
         * current_thread_info and into the real FPU registers.
         * QEMU doesn't need to handle lazy-FPU-state-restoring like that,
         * so we always load the data directly into the FPU registers
         * and leave FPRS and TSTATE_PEF alone (so the FPU stays enabled).
         * Note that because we (and the kernel) always write zeroes for
         * the fenab and fprs in sparc64_get_context() none of this code
         * will execute unless the guest manually constructed or changed
         * the context structure.
         */
        __get_user(fprs, &(fpup->mcfpu_fprs));
        if (fprs & FPRS_DL) {
            for (i = 0; i < 16; i++) {
                __get_user(env->fpr[i].ll, &(fpup->mcfpu_fregs.dregs[i]));
            }
        }
        if (fprs & FPRS_DU) {
            for (i = 16; i < 32; i++) {
                __get_user(env->fpr[i].ll, &(fpup->mcfpu_fregs.dregs[i]));
            }
        }
        __get_user(env->fsr, &(fpup->mcfpu_fsr));
        __get_user(env->gsr, &(fpup->mcfpu_gsr));
    }
    unlock_user_struct(ucp, ucp_addr, 0);
    return;

 do_sigsegv:
    unlock_user_struct(ucp, ucp_addr, 0);
    force_sig(TARGET_SIGSEGV);
}

void sparc64_get_context(CPUSPARCState *env)
{
    abi_ulong ucp_addr;
    struct target_ucontext *ucp;
    target_mc_gregset_t *grp;
    target_mcontext_t *mcp;
    int err;
    unsigned int i;
    target_sigset_t target_set;
    sigset_t set;

    ucp_addr = env->regwptr[WREG_O0];
    if (!lock_user_struct(VERIFY_WRITE, ucp, ucp_addr, 0)) {
        goto do_sigsegv;
    }

    memset(ucp, 0, sizeof(*ucp));

    mcp = &ucp->tuc_mcontext;
    grp = &mcp->mc_gregs;

    /* Skip over the trap instruction, first. */
    env->pc = env->npc;
    env->npc += 4;

    /*
     * If we're only reading the signal mask then do_sigprocmask()
     * is guaranteed not to fail, which is important because we don't
     * have any way to signal a failure or restart this operation since
     * this is not a normal syscall.
     */
    err = do_sigprocmask(0, NULL, &set);
    assert(err == 0);
    host_to_target_sigset_internal(&target_set, &set);
    if (TARGET_NSIG_WORDS == 1) {
        __put_user(target_set.sig[0], (abi_ulong *)&ucp->tuc_sigmask);
    } else {
        abi_ulong *src, *dst;
        src = target_set.sig;
        dst = ucp->tuc_sigmask.sig;
        for (i = 0; i < TARGET_NSIG_WORDS; i++, dst++, src++) {
            __put_user(*src, dst);
        }
    }

    __put_user(sparc64_tstate(env), &((*grp)[SPARC_MC_TSTATE]));
    __put_user(env->pc, &((*grp)[SPARC_MC_PC]));
    __put_user(env->npc, &((*grp)[SPARC_MC_NPC]));
    __put_user(env->y, &((*grp)[SPARC_MC_Y]));
    __put_user(env->gregs[1], &((*grp)[SPARC_MC_G1]));
    __put_user(env->gregs[2], &((*grp)[SPARC_MC_G2]));
    __put_user(env->gregs[3], &((*grp)[SPARC_MC_G3]));
    __put_user(env->gregs[4], &((*grp)[SPARC_MC_G4]));
    __put_user(env->gregs[5], &((*grp)[SPARC_MC_G5]));
    __put_user(env->gregs[6], &((*grp)[SPARC_MC_G6]));
    __put_user(env->gregs[7], &((*grp)[SPARC_MC_G7]));

    /*
     * Note that unlike the kernel, we didn't need to mess with the
     * guest register window state to save it into a pt_regs to run
     * the kernel. So for us the guest's O regs are still in WREG_O*
     * (unlike the kernel which has put them in UREG_I* in a pt_regs)
     * and the fp and i7 are still in WREG_I6 and WREG_I7 and don't
     * need to be fished out of userspace memory.
     */
    __put_user(env->regwptr[WREG_O0], &((*grp)[SPARC_MC_O0]));
    __put_user(env->regwptr[WREG_O1], &((*grp)[SPARC_MC_O1]));
    __put_user(env->regwptr[WREG_O2], &((*grp)[SPARC_MC_O2]));
    __put_user(env->regwptr[WREG_O3], &((*grp)[SPARC_MC_O3]));
    __put_user(env->regwptr[WREG_O4], &((*grp)[SPARC_MC_O4]));
    __put_user(env->regwptr[WREG_O5], &((*grp)[SPARC_MC_O5]));
    __put_user(env->regwptr[WREG_O6], &((*grp)[SPARC_MC_O6]));
    __put_user(env->regwptr[WREG_O7], &((*grp)[SPARC_MC_O7]));

    __put_user(env->regwptr[WREG_FP], &(mcp->mc_fp));
    __put_user(env->regwptr[WREG_I7], &(mcp->mc_i7));

    /*
     * We don't write out the FPU state. This matches the kernel's
     * implementation (which has the code for doing this but
     * hidden behind an "if (fenab)" where fenab is always 0).
     */

    unlock_user_struct(ucp, ucp_addr, 1);
    return;

 do_sigsegv:
    unlock_user_struct(ucp, ucp_addr, 1);
    force_sig(TARGET_SIGSEGV);
}
