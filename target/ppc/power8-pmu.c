/*
 * PMU emulation helpers for TCG IBM POWER chips
 *
 *  Copyright IBM Corp. 2021
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "power8-pmu.h"
#include "cpu.h"
#include "helper_regs.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/ppc/ppc.h"

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)

#define PMC_COUNTER_NEGATIVE_VAL 0x80000000UL

static bool pmc_is_inactive(CPUPPCState *env, int sprn)
{
    if (env->spr[SPR_POWER_MMCR0] & MMCR0_FC) {
        return true;
    }

    if (sprn < SPR_POWER_PMC5) {
        return env->spr[SPR_POWER_MMCR0] & MMCR0_FC14;
    }

    return env->spr[SPR_POWER_MMCR0] & MMCR0_FC56;
}

static bool pmc_has_overflow_enabled(CPUPPCState *env, int sprn)
{
    if (sprn == SPR_POWER_PMC1) {
        return env->spr[SPR_POWER_MMCR0] & MMCR0_PMC1CE;
    }

    return env->spr[SPR_POWER_MMCR0] & MMCR0_PMCjCE;
}

/*
 * For PMCs 1-4, IBM POWER chips has support for an implementation
 * dependent event, 0x1E, that enables cycle counting. The Linux kernel
 * makes extensive use of 0x1E, so let's also support it.
 *
 * Likewise, event 0x2 is an implementation-dependent event that IBM
 * POWER chips implement (at least since POWER8) that is equivalent to
 * PM_INST_CMPL. Let's support this event on PMCs 1-4 as well.
 */
static PMUEventType pmc_get_event(CPUPPCState *env, int sprn)
{
    uint8_t mmcr1_evt_extr[] = { MMCR1_PMC1EVT_EXTR, MMCR1_PMC2EVT_EXTR,
                                 MMCR1_PMC3EVT_EXTR, MMCR1_PMC4EVT_EXTR };
    PMUEventType evt_type = PMU_EVENT_INVALID;
    uint8_t pmcsel;
    int i;

    if (pmc_is_inactive(env, sprn)) {
        return PMU_EVENT_INACTIVE;
    }

    if (sprn == SPR_POWER_PMC5) {
        return PMU_EVENT_INSTRUCTIONS;
    }

    if (sprn == SPR_POWER_PMC6) {
        return PMU_EVENT_CYCLES;
    }

    i = sprn - SPR_POWER_PMC1;
    pmcsel = extract64(env->spr[SPR_POWER_MMCR1], mmcr1_evt_extr[i],
                       MMCR1_EVT_SIZE);

    switch (pmcsel) {
    case 0x2:
        evt_type = PMU_EVENT_INSTRUCTIONS;
        break;
    case 0x1E:
        evt_type = PMU_EVENT_CYCLES;
        break;
    case 0xF0:
        /*
         * PMC1SEL = 0xF0 is the architected PowerISA v3.1
         * event that counts cycles using PMC1.
         */
        if (sprn == SPR_POWER_PMC1) {
            evt_type = PMU_EVENT_CYCLES;
        }
        break;
    case 0xFA:
        /*
         * PMC4SEL = 0xFA is the "instructions completed
         * with run latch set" event.
         */
        if (sprn == SPR_POWER_PMC4) {
            evt_type = PMU_EVENT_INSN_RUN_LATCH;
        }
        break;
    case 0xFE:
        /*
         * PMC1SEL = 0xFE is the architected PowerISA v3.1
         * event to sample instructions using PMC1.
         */
        if (sprn == SPR_POWER_PMC1) {
            evt_type = PMU_EVENT_INSTRUCTIONS;
        }
        break;
    default:
        break;
    }

    return evt_type;
}

bool pmu_insn_cnt_enabled(CPUPPCState *env)
{
    int sprn;

    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC5; sprn++) {
        if (pmc_get_event(env, sprn) == PMU_EVENT_INSTRUCTIONS ||
            pmc_get_event(env, sprn) == PMU_EVENT_INSN_RUN_LATCH) {
            return true;
        }
    }

    return false;
}

static void pmu_update_cycles(CPUPPCState *env)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t time_delta = now - env->pmu_base_time;
    int sprn;

    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC6; sprn++) {
        if (pmc_get_event(env, sprn) != PMU_EVENT_CYCLES) {
            continue;
        }

        /*
         * The pseries and powernv clock runs at 1Ghz, meaning
         * that 1 nanosec equals 1 cycle.
         */
        env->spr[sprn] += time_delta;
    }

    /* Update base_time for future calculations */
    env->pmu_base_time = now;
}

/*
 * Helper function to retrieve the cycle overflow timer of the
 * 'sprn' counter.
 */
static QEMUTimer *get_cyc_overflow_timer(CPUPPCState *env, int sprn)
{
    return env->pmu_cyc_overflow_timers[sprn - SPR_POWER_PMC1];
}

static void pmc_update_overflow_timer(CPUPPCState *env, int sprn)
{
    QEMUTimer *pmc_overflow_timer = get_cyc_overflow_timer(env, sprn);
    int64_t timeout;

    /*
     * PMC5 does not have an overflow timer and this pointer
     * will be NULL.
     */
    if (!pmc_overflow_timer) {
        return;
    }

    if (pmc_get_event(env, sprn) != PMU_EVENT_CYCLES ||
        !pmc_has_overflow_enabled(env, sprn)) {
        /* Overflow timer is not needed for this counter */
        timer_del(pmc_overflow_timer);
        return;
    }

    if (env->spr[sprn] >= PMC_COUNTER_NEGATIVE_VAL) {
        timeout =  0;
    } else {
        timeout = PMC_COUNTER_NEGATIVE_VAL - env->spr[sprn];
    }

    /*
     * Use timer_mod_anticipate() because an overflow timer might
     * be already running for this PMC.
     */
    timer_mod_anticipate(pmc_overflow_timer, env->pmu_base_time + timeout);
}

static void pmu_update_overflow_timers(CPUPPCState *env)
{
    int sprn;

    /*
     * Scroll through all PMCs and start counter overflow timers for
     * PM_CYC events, if needed.
     */
    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC6; sprn++) {
        pmc_update_overflow_timer(env, sprn);
    }
}

void helper_store_mmcr0(CPUPPCState *env, target_ulong value)
{
    pmu_update_cycles(env);

    env->spr[SPR_POWER_MMCR0] = value;

    /* MMCR0 writes can change HFLAGS_PMCCCLEAR and HFLAGS_INSN_CNT */
    hreg_compute_hflags(env);

    /* Update cycle overflow timers with the current MMCR0 state */
    pmu_update_overflow_timers(env);
}

void helper_store_mmcr1(CPUPPCState *env, uint64_t value)
{
    pmu_update_cycles(env);

    env->spr[SPR_POWER_MMCR1] = value;

    /* MMCR1 writes can change HFLAGS_INSN_CNT */
    hreg_compute_hflags(env);
}

target_ulong helper_read_pmc(CPUPPCState *env, uint32_t sprn)
{
    pmu_update_cycles(env);

    return env->spr[sprn];
}

void helper_store_pmc(CPUPPCState *env, uint32_t sprn, uint64_t value)
{
    pmu_update_cycles(env);

    env->spr[sprn] = value;

    pmc_update_overflow_timer(env, sprn);
}

static void fire_PMC_interrupt(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    if (!(env->spr[SPR_POWER_MMCR0] & MMCR0_EBE)) {
        return;
    }

    /* PMC interrupt not implemented yet */
    return;
}

/* Helper to fire a PMC interrupt from TCG code */
void helper_pmu_overflow(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);

    fire_PMC_interrupt(cpu);
}

static void cpu_ppc_pmu_timer_cb(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    fire_PMC_interrupt(cpu);
}

void cpu_ppc_pmu_init(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);
    int i, sprn;

    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC6; sprn++) {
        if (sprn == SPR_POWER_PMC5) {
            continue;
        }

        i = sprn - SPR_POWER_PMC1;

        env->pmu_cyc_overflow_timers[i] = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                                       &cpu_ppc_pmu_timer_cb,
                                                       cpu);
    }
}
#endif /* defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY) */
