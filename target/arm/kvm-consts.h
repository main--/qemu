/*
 * KVM ARM ABI constant definitions
 *
 * Copyright (c) 2013 Linaro Limited
 *
 * Provide versions of KVM constant defines that can be used even
 * when CONFIG_KVM is not set and we don't have access to the
 * KVM headers. If CONFIG_KVM is set, we do a compile-time check
 * that we haven't got out of sync somehow.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef ARM_KVM_CONSTS_H
#define ARM_KVM_CONSTS_H

#ifdef CONFIG_KVM
#include <linux/kvm.h>
#include <linux/psci.h>

#define MISMATCH_CHECK(X, Y) QEMU_BUILD_BUG_ON(X != Y)

#else

#define MISMATCH_CHECK(X, Y) QEMU_BUILD_BUG_ON(0)

#endif

#define CP_REG_SIZE_SHIFT 52
#define CP_REG_SIZE_MASK       0x00f0000000000000ULL
#define CP_REG_SIZE_U32        0x0020000000000000ULL
#define CP_REG_SIZE_U64        0x0030000000000000ULL
#define CP_REG_ARM             0x4000000000000000ULL
#define CP_REG_ARCH_MASK       0xff00000000000000ULL

MISMATCH_CHECK(CP_REG_SIZE_SHIFT, KVM_REG_SIZE_SHIFT);
MISMATCH_CHECK(CP_REG_SIZE_MASK, KVM_REG_SIZE_MASK);
MISMATCH_CHECK(CP_REG_SIZE_U32, KVM_REG_SIZE_U32);
MISMATCH_CHECK(CP_REG_SIZE_U64, KVM_REG_SIZE_U64);
MISMATCH_CHECK(CP_REG_ARM, KVM_REG_ARM);
MISMATCH_CHECK(CP_REG_ARCH_MASK, KVM_REG_ARCH_MASK);

#define QEMU_PSCI_0_1_FN_BASE 0x95c1ba5e
#define QEMU_PSCI_0_1_FN(n) (QEMU_PSCI_0_1_FN_BASE + (n))
#define QEMU_PSCI_0_1_FN_CPU_SUSPEND QEMU_PSCI_0_1_FN(0)
#define QEMU_PSCI_0_1_FN_CPU_OFF QEMU_PSCI_0_1_FN(1)
#define QEMU_PSCI_0_1_FN_CPU_ON QEMU_PSCI_0_1_FN(2)
#define QEMU_PSCI_0_1_FN_MIGRATE QEMU_PSCI_0_1_FN(3)

MISMATCH_CHECK(QEMU_PSCI_0_1_FN_CPU_SUSPEND, KVM_PSCI_FN_CPU_SUSPEND);
MISMATCH_CHECK(QEMU_PSCI_0_1_FN_CPU_OFF, KVM_PSCI_FN_CPU_OFF);
MISMATCH_CHECK(QEMU_PSCI_0_1_FN_CPU_ON, KVM_PSCI_FN_CPU_ON);
MISMATCH_CHECK(QEMU_PSCI_0_1_FN_MIGRATE, KVM_PSCI_FN_MIGRATE);

#define QEMU_PSCI_0_2_FN_BASE 0x84000000
#define QEMU_PSCI_0_2_FN(n) (QEMU_PSCI_0_2_FN_BASE + (n))

#define QEMU_PSCI_0_2_64BIT 0x40000000
#define QEMU_PSCI_0_2_FN64_BASE \
        (QEMU_PSCI_0_2_FN_BASE + QEMU_PSCI_0_2_64BIT)
#define QEMU_PSCI_0_2_FN64(n) (QEMU_PSCI_0_2_FN64_BASE + (n))

#define QEMU_PSCI_0_2_FN_PSCI_VERSION QEMU_PSCI_0_2_FN(0)
#define QEMU_PSCI_0_2_FN_CPU_SUSPEND QEMU_PSCI_0_2_FN(1)
#define QEMU_PSCI_0_2_FN_CPU_OFF QEMU_PSCI_0_2_FN(2)
#define QEMU_PSCI_0_2_FN_CPU_ON QEMU_PSCI_0_2_FN(3)
#define QEMU_PSCI_0_2_FN_AFFINITY_INFO QEMU_PSCI_0_2_FN(4)
#define QEMU_PSCI_0_2_FN_MIGRATE QEMU_PSCI_0_2_FN(5)
#define QEMU_PSCI_0_2_FN_MIGRATE_INFO_TYPE QEMU_PSCI_0_2_FN(6)
#define QEMU_PSCI_0_2_FN_MIGRATE_INFO_UP_CPU QEMU_PSCI_0_2_FN(7)
#define QEMU_PSCI_0_2_FN_SYSTEM_OFF QEMU_PSCI_0_2_FN(8)
#define QEMU_PSCI_0_2_FN_SYSTEM_RESET QEMU_PSCI_0_2_FN(9)

#define QEMU_PSCI_0_2_FN64_CPU_SUSPEND QEMU_PSCI_0_2_FN64(1)
#define QEMU_PSCI_0_2_FN64_CPU_OFF QEMU_PSCI_0_2_FN64(2)
#define QEMU_PSCI_0_2_FN64_CPU_ON QEMU_PSCI_0_2_FN64(3)
#define QEMU_PSCI_0_2_FN64_AFFINITY_INFO QEMU_PSCI_0_2_FN64(4)
#define QEMU_PSCI_0_2_FN64_MIGRATE QEMU_PSCI_0_2_FN64(5)

#define QEMU_PSCI_1_0_FN_PSCI_FEATURES QEMU_PSCI_0_2_FN(10)

MISMATCH_CHECK(QEMU_PSCI_0_2_FN_CPU_SUSPEND, PSCI_0_2_FN_CPU_SUSPEND);
MISMATCH_CHECK(QEMU_PSCI_0_2_FN_CPU_OFF, PSCI_0_2_FN_CPU_OFF);
MISMATCH_CHECK(QEMU_PSCI_0_2_FN_CPU_ON, PSCI_0_2_FN_CPU_ON);
MISMATCH_CHECK(QEMU_PSCI_0_2_FN_MIGRATE, PSCI_0_2_FN_MIGRATE);
MISMATCH_CHECK(QEMU_PSCI_0_2_FN64_CPU_SUSPEND, PSCI_0_2_FN64_CPU_SUSPEND);
MISMATCH_CHECK(QEMU_PSCI_0_2_FN64_CPU_ON, PSCI_0_2_FN64_CPU_ON);
MISMATCH_CHECK(QEMU_PSCI_0_2_FN64_MIGRATE, PSCI_0_2_FN64_MIGRATE);
MISMATCH_CHECK(QEMU_PSCI_1_0_FN_PSCI_FEATURES, PSCI_1_0_FN_PSCI_FEATURES);

/* PSCI v0.2 return values used by TCG emulation of PSCI */

/* No Trusted OS migration to worry about when offlining CPUs */
#define QEMU_PSCI_0_2_RET_TOS_MIGRATION_NOT_REQUIRED        2

#define QEMU_PSCI_VERSION_0_1                     0x00001
#define QEMU_PSCI_VERSION_0_2                     0x00002
#define QEMU_PSCI_VERSION_1_0                     0x10000
#define QEMU_PSCI_VERSION_1_1                     0x10001

MISMATCH_CHECK(QEMU_PSCI_0_2_RET_TOS_MIGRATION_NOT_REQUIRED, PSCI_0_2_TOS_MP);
/* We don't bother to check every possible version value */
MISMATCH_CHECK(QEMU_PSCI_VERSION_0_2, PSCI_VERSION(0, 2));
MISMATCH_CHECK(QEMU_PSCI_VERSION_1_1, PSCI_VERSION(1, 1));

/* PSCI return values (inclusive of all PSCI versions) */
#define QEMU_PSCI_RET_SUCCESS                     0
#define QEMU_PSCI_RET_NOT_SUPPORTED               -1
#define QEMU_PSCI_RET_INVALID_PARAMS              -2
#define QEMU_PSCI_RET_DENIED                      -3
#define QEMU_PSCI_RET_ALREADY_ON                  -4
#define QEMU_PSCI_RET_ON_PENDING                  -5
#define QEMU_PSCI_RET_INTERNAL_FAILURE            -6
#define QEMU_PSCI_RET_NOT_PRESENT                 -7
#define QEMU_PSCI_RET_DISABLED                    -8

MISMATCH_CHECK(QEMU_PSCI_RET_SUCCESS, PSCI_RET_SUCCESS);
MISMATCH_CHECK(QEMU_PSCI_RET_NOT_SUPPORTED, PSCI_RET_NOT_SUPPORTED);
MISMATCH_CHECK(QEMU_PSCI_RET_INVALID_PARAMS, PSCI_RET_INVALID_PARAMS);
MISMATCH_CHECK(QEMU_PSCI_RET_DENIED, PSCI_RET_DENIED);
MISMATCH_CHECK(QEMU_PSCI_RET_ALREADY_ON, PSCI_RET_ALREADY_ON);
MISMATCH_CHECK(QEMU_PSCI_RET_ON_PENDING, PSCI_RET_ON_PENDING);
MISMATCH_CHECK(QEMU_PSCI_RET_INTERNAL_FAILURE, PSCI_RET_INTERNAL_FAILURE);
MISMATCH_CHECK(QEMU_PSCI_RET_NOT_PRESENT, PSCI_RET_NOT_PRESENT);
MISMATCH_CHECK(QEMU_PSCI_RET_DISABLED, PSCI_RET_DISABLED);

/* Note that KVM uses overlapping values for AArch32 and AArch64
 * target CPU numbers. AArch32 targets:
 */
#define QEMU_KVM_ARM_TARGET_CORTEX_A15 0
#define QEMU_KVM_ARM_TARGET_CORTEX_A7 1

/* AArch64 targets: */
#define QEMU_KVM_ARM_TARGET_AEM_V8 0
#define QEMU_KVM_ARM_TARGET_FOUNDATION_V8 1
#define QEMU_KVM_ARM_TARGET_CORTEX_A57 2
#define QEMU_KVM_ARM_TARGET_XGENE_POTENZA 3
#define QEMU_KVM_ARM_TARGET_CORTEX_A53 4

/* There's no kernel define for this: sentinel value which
 * matches no KVM target value for either 64 or 32 bit
 */
#define QEMU_KVM_ARM_TARGET_NONE UINT_MAX

MISMATCH_CHECK(QEMU_KVM_ARM_TARGET_AEM_V8, KVM_ARM_TARGET_AEM_V8);
MISMATCH_CHECK(QEMU_KVM_ARM_TARGET_FOUNDATION_V8, KVM_ARM_TARGET_FOUNDATION_V8);
MISMATCH_CHECK(QEMU_KVM_ARM_TARGET_CORTEX_A57, KVM_ARM_TARGET_CORTEX_A57);
MISMATCH_CHECK(QEMU_KVM_ARM_TARGET_XGENE_POTENZA, KVM_ARM_TARGET_XGENE_POTENZA);
MISMATCH_CHECK(QEMU_KVM_ARM_TARGET_CORTEX_A53, KVM_ARM_TARGET_CORTEX_A53);

#define CP_REG_ARM64                   0x6000000000000000ULL
#define CP_REG_ARM_COPROC_MASK         0x000000000FFF0000
#define CP_REG_ARM_COPROC_SHIFT        16
#define CP_REG_ARM64_SYSREG            (0x0013 << CP_REG_ARM_COPROC_SHIFT)
#define CP_REG_ARM64_SYSREG_OP0_MASK   0x000000000000c000
#define CP_REG_ARM64_SYSREG_OP0_SHIFT  14
#define CP_REG_ARM64_SYSREG_OP1_MASK   0x0000000000003800
#define CP_REG_ARM64_SYSREG_OP1_SHIFT  11
#define CP_REG_ARM64_SYSREG_CRN_MASK   0x0000000000000780
#define CP_REG_ARM64_SYSREG_CRN_SHIFT  7
#define CP_REG_ARM64_SYSREG_CRM_MASK   0x0000000000000078
#define CP_REG_ARM64_SYSREG_CRM_SHIFT  3
#define CP_REG_ARM64_SYSREG_OP2_MASK   0x0000000000000007
#define CP_REG_ARM64_SYSREG_OP2_SHIFT  0

/* No kernel define but it's useful to QEMU */
#define CP_REG_ARM64_SYSREG_CP (CP_REG_ARM64_SYSREG >> CP_REG_ARM_COPROC_SHIFT)

MISMATCH_CHECK(CP_REG_ARM64, KVM_REG_ARM64);
MISMATCH_CHECK(CP_REG_ARM_COPROC_MASK, KVM_REG_ARM_COPROC_MASK);
MISMATCH_CHECK(CP_REG_ARM_COPROC_SHIFT, KVM_REG_ARM_COPROC_SHIFT);
MISMATCH_CHECK(CP_REG_ARM64_SYSREG, KVM_REG_ARM64_SYSREG);
MISMATCH_CHECK(CP_REG_ARM64_SYSREG_OP0_MASK, KVM_REG_ARM64_SYSREG_OP0_MASK);
MISMATCH_CHECK(CP_REG_ARM64_SYSREG_OP0_SHIFT, KVM_REG_ARM64_SYSREG_OP0_SHIFT);
MISMATCH_CHECK(CP_REG_ARM64_SYSREG_OP1_MASK, KVM_REG_ARM64_SYSREG_OP1_MASK);
MISMATCH_CHECK(CP_REG_ARM64_SYSREG_OP1_SHIFT, KVM_REG_ARM64_SYSREG_OP1_SHIFT);
MISMATCH_CHECK(CP_REG_ARM64_SYSREG_CRN_MASK, KVM_REG_ARM64_SYSREG_CRN_MASK);
MISMATCH_CHECK(CP_REG_ARM64_SYSREG_CRN_SHIFT, KVM_REG_ARM64_SYSREG_CRN_SHIFT);
MISMATCH_CHECK(CP_REG_ARM64_SYSREG_CRM_MASK, KVM_REG_ARM64_SYSREG_CRM_MASK);
MISMATCH_CHECK(CP_REG_ARM64_SYSREG_CRM_SHIFT, KVM_REG_ARM64_SYSREG_CRM_SHIFT);
MISMATCH_CHECK(CP_REG_ARM64_SYSREG_OP2_MASK, KVM_REG_ARM64_SYSREG_OP2_MASK);
MISMATCH_CHECK(CP_REG_ARM64_SYSREG_OP2_SHIFT, KVM_REG_ARM64_SYSREG_OP2_SHIFT);

#undef MISMATCH_CHECK

#endif
