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

#include "power8_pmu.h"
#include "cpu.h"
#include "helper_regs.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/ppc/ppc.h"

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)

static void update_PMC_PM_CYC(CPUPPCState *env, int sprn,
                              uint64_t time_delta)
{
    /*
     * The pseries and pvn clock runs at 1Ghz, meaning that
     * 1 nanosec equals 1 cycle.
     */
    env->spr[sprn] += time_delta;
}

static uint8_t get_PMC_event(CPUPPCState *env, int sprn)
{
    int event = 0x0;

    switch (sprn) {
    case SPR_POWER_PMC1:
        event = MMCR1_PMC1SEL & env->spr[SPR_POWER_MMCR1];
        event = event >> MMCR1_PMC1SEL_SHIFT;
        break;
    case SPR_POWER_PMC2:
        event = MMCR1_PMC2SEL & env->spr[SPR_POWER_MMCR1];
        event = event >> MMCR1_PMC2SEL_SHIFT;
        break;
    case SPR_POWER_PMC3:
        event = MMCR1_PMC3SEL & env->spr[SPR_POWER_MMCR1];
        event = event >> MMCR1_PMC3SEL_SHIFT;
        break;
    case SPR_POWER_PMC4:
        event = MMCR1_PMC4SEL & env->spr[SPR_POWER_MMCR1];
        break;
    default:
        break;
    }

    return event;
}

static bool pmc_is_running(CPUPPCState *env, int sprn)
{
    if (sprn < SPR_POWER_PMC5) {
        return !(env->spr[SPR_POWER_MMCR0] & MMCR0_FC14);
    }

    return !(env->spr[SPR_POWER_MMCR0] & MMCR0_FC56);
}

static void update_programmable_PMC_reg(CPUPPCState *env, int sprn,
                                        uint64_t time_delta)
{
    uint8_t event = get_PMC_event(env, sprn);

    /*
     * MMCR0_PMC1SEL = 0xF0 is the architected PowerISA v3.1 event
     * that counts cycles using PMC1.
     *
     * IBM POWER chips also has support for an implementation dependent
     * event, 0x1E, that enables cycle counting on PMCs 1-4. The
     * Linux kernel makes extensive use of 0x1E, so let's also support
     * it.
     */
    switch (event) {
    case 0xF0:
        if (sprn == SPR_POWER_PMC1) {
            update_PMC_PM_CYC(env, sprn, time_delta);
        }
        break;
    case 0x1E:
        update_PMC_PM_CYC(env, sprn, time_delta);
        break;
    default:
        return;
    }
}

static void update_cycles_PMCs(CPUPPCState *env)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t time_delta = now - env->pmu_base_time;
    bool PMC14_running = !(env->spr[SPR_POWER_MMCR0] & MMCR0_FC14);
    bool PMC6_running = !(env->spr[SPR_POWER_MMCR0] & MMCR0_FC56);
    int sprn;

    if (PMC14_running) {
        for (sprn = SPR_POWER_PMC1; sprn < SPR_POWER_PMC5; sprn++) {
            update_programmable_PMC_reg(env, sprn, time_delta);
        }
    }

    if (PMC6_running) {
        update_PMC_PM_CYC(env, SPR_POWER_PMC6, time_delta);
    }
}

static void cpu_ppc_pmu_timer_cb(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;
    uint64_t mmcr0;

    mmcr0 = env->spr[SPR_POWER_MMCR0];
    if (env->spr[SPR_POWER_MMCR0] & MMCR0_EBE) {
        /* freeeze counters if needed */
        if (mmcr0 & MMCR0_FCECE) {
            mmcr0 &= ~MMCR0_FCECE;
            mmcr0 |= MMCR0_FC;
        }

        /* Clear PMAE and set PMAO */
        if (mmcr0 & MMCR0_PMAE) {
            mmcr0 &= ~MMCR0_PMAE;
            mmcr0 |= MMCR0_PMAO;
        }
        env->spr[SPR_POWER_MMCR0] = mmcr0;

        /* Fire the PMC hardware exception */
        ppc_set_irq(cpu, PPC_INTERRUPT_PMC, 1);
    }
}

void cpu_ppc_pmu_timer_init(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);
    QEMUTimer *timer;
    int i;

    for (i = 0; i < PMU_TIMERS_LEN; i++) {
        timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &cpu_ppc_pmu_timer_cb,
                             cpu);
        env->pmu_intr_timers[i] = timer;
    }
}

void helper_store_mmcr0(CPUPPCState *env, target_ulong value)
{
    target_ulong curr_value = env->spr[SPR_POWER_MMCR0];
    bool curr_FC = curr_value & MMCR0_FC;
    bool new_FC = value & MMCR0_FC;

    env->spr[SPR_POWER_MMCR0] = value;

    /* MMCR0 writes can change HFLAGS_PMCCCLEAR */
    if ((curr_value & MMCR0_PMCC) != (value & MMCR0_PMCC)) {
        hreg_compute_hflags(env);
    }

    /*
     * In an frozen count (FC) bit change:
     *
     * - if PMCs were running (curr_FC = false) and we're freezing
     * them (new_FC = true), save the PMCs values in the registers.
     *
     * - if PMCs were frozen (curr_FC = true) and we're activating
     * them (new_FC = false), set the new base_time for future cycle
     * calculations.
     */
    if (curr_FC != new_FC) {
        if (!curr_FC) {
            update_cycles_PMCs(env);
        } else {
            env->pmu_base_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }
    }
}

static bool pmc_counting_insns(CPUPPCState *env, int sprn,
                               uint8_t event)
{
    bool ret = false;

    if (!pmc_is_running(env, sprn)) {
        return false;
    }

    if (sprn == SPR_POWER_PMC5) {
        return true;
    }

    event = get_PMC_event(env, sprn);

    /*
     * Event 0x2 is an implementation-dependent event that IBM
     * POWER chips implement (at least since POWER8) that is
     * equivalent to PM_INST_CMPL. Let's support this event on
     * all programmable PMCs.
     *
     * Event 0xFE is the PowerISA v3.1 architected event to
     * sample PM_INST_CMPL using PMC1.
     */
    switch (sprn) {
    case SPR_POWER_PMC1:
        return event == 0x2 || event == 0xFE;
    case SPR_POWER_PMC2:
    case SPR_POWER_PMC3:
        return event == 0x2;
    case SPR_POWER_PMC4:
        /*
         * Event 0xFA is the "instructions completed with run latch
         * set" event. Consider it as instruction counting event.
         * The caller is responsible for handling it separately
         * from PM_INST_CMPL.
         */
        return event == 0x2 || event == 0xFA;
    default:
        break;
    }

    return ret;
}

/* This helper assumes that the PMC is running. */
void helper_insns_inc(CPUPPCState *env, uint32_t num_insns)
{
    int sprn;

    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC5; sprn++) {
        uint8_t event = get_PMC_event(env, sprn);

        if (pmc_counting_insns(env, sprn, event)) {
            if (sprn == SPR_POWER_PMC4 && event == 0xFA) {
                if (env->spr[SPR_CTRL] & CTRL_RUN) {
                    env->spr[SPR_POWER_PMC4] += num_insns;
                }
            } else {
                env->spr[sprn] += num_insns;
            }
        }
    }
}

#endif /* defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY) */
