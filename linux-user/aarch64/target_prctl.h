/*
 * AArch64 specific prctl functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef AARCH64_TARGET_PRCTL_H
#define AARCH64_TARGET_PRCTL_H

static abi_long do_prctl_get_vl(CPUArchState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    if (cpu_isar_feature(aa64_sve, cpu)) {
        return (EX_TBFLAG_A64(env->hflags, SVE_LEN) + 1) * 16;
    }
    return -TARGET_EINVAL;
}
#define do_prctl_get_vl do_prctl_get_vl

static abi_long do_prctl_set_vl(CPUArchState *env, abi_long arg2)
{
    /*
     * We cannot support either PR_SVE_SET_VL_ONEXEC or PR_SVE_VL_INHERIT.
     * Note the kernel definition of sve_vl_valid allows for VQ=512,
     * i.e. VL=8192, even though the current architectural maximum is VQ=16.
     */
    if (cpu_isar_feature(aa64_sve, env_archcpu(env))
        && arg2 >= 0 && arg2 <= 512 * 16 && !(arg2 & 15)) {
        uint32_t vq, old_vq;

        old_vq = EX_TBFLAG_A64(env->hflags, SVE_LEN) + 1;

        /*
         * Bound the value of vq, so that we know that it fits into
         * the 4-bit field in ZCR_EL1.  Rely on the hflags rebuild
         * to sort out the length supported by the cpu.
         */
        vq = MAX(arg2 / 16, 1);
        vq = MIN(vq, 16);
        env->vfp.zcr_el[1] = vq - 1;
        arm_rebuild_hflags(env);

        vq = EX_TBFLAG_A64(env->hflags, SVE_LEN) + 1;

        if (vq < old_vq) {
            aarch64_sve_narrow_vq(env, vq);
        }
        return vq * 16;
    }
    return -TARGET_EINVAL;
}
#define do_prctl_set_vl do_prctl_set_vl

static abi_long do_prctl_reset_keys(CPUArchState *env, abi_long arg2)
{
    ARMCPU *cpu = env_archcpu(env);

    if (cpu_isar_feature(aa64_pauth, cpu)) {
        int all = (PR_PAC_APIAKEY | PR_PAC_APIBKEY |
                   PR_PAC_APDAKEY | PR_PAC_APDBKEY | PR_PAC_APGAKEY);
        int ret = 0;
        Error *err = NULL;

        if (arg2 == 0) {
            arg2 = all;
        } else if (arg2 & ~all) {
            return -TARGET_EINVAL;
        }
        if (arg2 & PR_PAC_APIAKEY) {
            ret |= qemu_guest_getrandom(&env->keys.apia,
                                        sizeof(ARMPACKey), &err);
        }
        if (arg2 & PR_PAC_APIBKEY) {
            ret |= qemu_guest_getrandom(&env->keys.apib,
                                        sizeof(ARMPACKey), &err);
        }
        if (arg2 & PR_PAC_APDAKEY) {
            ret |= qemu_guest_getrandom(&env->keys.apda,
                                        sizeof(ARMPACKey), &err);
        }
        if (arg2 & PR_PAC_APDBKEY) {
            ret |= qemu_guest_getrandom(&env->keys.apdb,
                                        sizeof(ARMPACKey), &err);
        }
        if (arg2 & PR_PAC_APGAKEY) {
            ret |= qemu_guest_getrandom(&env->keys.apga,
                                        sizeof(ARMPACKey), &err);
        }
        if (ret != 0) {
            /*
             * Some unknown failure in the crypto.  The best
             * we can do is log it and fail the syscall.
             * The real syscall cannot fail this way.
             */
            qemu_log_mask(LOG_UNIMP, "PR_PAC_RESET_KEYS: Crypto failure: %s",
                          error_get_pretty(err));
            error_free(err);
            return -TARGET_EIO;
        }
        return 0;
    }
    return -TARGET_EINVAL;
}
#define do_prctl_reset_keys do_prctl_reset_keys

static abi_long do_prctl_set_tagged_addr_ctrl(CPUArchState *env, abi_long arg2)
{
    abi_ulong valid_mask = PR_TAGGED_ADDR_ENABLE;
    ARMCPU *cpu = env_archcpu(env);

    if (cpu_isar_feature(aa64_mte, cpu)) {
        valid_mask |= PR_MTE_TCF_MASK;
        valid_mask |= PR_MTE_TAG_MASK;
    }

    if (arg2 & ~valid_mask) {
        return -TARGET_EINVAL;
    }
    env->tagged_addr_enable = arg2 & PR_TAGGED_ADDR_ENABLE;

    if (cpu_isar_feature(aa64_mte, cpu)) {
        switch (arg2 & PR_MTE_TCF_MASK) {
        case PR_MTE_TCF_NONE:
        case PR_MTE_TCF_SYNC:
        case PR_MTE_TCF_ASYNC:
            break;
        default:
            return -EINVAL;
        }

        /*
         * Write PR_MTE_TCF to SCTLR_EL1[TCF0].
         * Note that the syscall values are consistent with hw.
         */
        env->cp15.sctlr_el[1] =
            deposit64(env->cp15.sctlr_el[1], 38, 2, arg2 >> PR_MTE_TCF_SHIFT);

        /*
         * Write PR_MTE_TAG to GCR_EL1[Exclude].
         * Note that the syscall uses an include mask,
         * and hardware uses an exclude mask -- invert.
         */
        env->cp15.gcr_el1 =
            deposit64(env->cp15.gcr_el1, 0, 16, ~arg2 >> PR_MTE_TAG_SHIFT);
        arm_rebuild_hflags(env);
    }
    return 0;
}
#define do_prctl_set_tagged_addr_ctrl do_prctl_set_tagged_addr_ctrl

static abi_long do_prctl_get_tagged_addr_ctrl(CPUArchState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    abi_long ret = 0;

    if (env->tagged_addr_enable) {
        ret |= PR_TAGGED_ADDR_ENABLE;
    }
    if (cpu_isar_feature(aa64_mte, cpu)) {
        /* See do_prctl_set_tagged_addr_ctrl. */
        ret |= extract64(env->cp15.sctlr_el[1], 38, 2) << PR_MTE_TCF_SHIFT;
        ret = deposit64(ret, PR_MTE_TAG_SHIFT, 16, ~env->cp15.gcr_el1);
    }
    return ret;
}
#define do_prctl_get_tagged_addr_ctrl do_prctl_get_tagged_addr_ctrl

#endif /* AARCH64_TARGET_PRCTL_H */
