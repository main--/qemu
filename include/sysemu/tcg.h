/*
 * QEMU TCG support
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSEMU_TCG_H
#define SYSEMU_TCG_H

#ifndef CONFIG_TCG
#define tcg_enabled() 0
#define cpu_has_work(cpu) false
#else

void tcg_exec_init(unsigned long tb_size, int splitwx);

extern bool tcg_allowed;
#define tcg_enabled() (tcg_allowed)

/**
 * qemu_tcg_mttcg_enabled:
 * Check whether we are running MultiThread TCG or not.
 *
 * Returns: %true if we are in MTTCG mode %false otherwise.
 */
extern bool mttcg_enabled;
#define qemu_tcg_mttcg_enabled() (mttcg_enabled)

/**
 * cpu_has_work:
 * @cpu: The vCPU to check.
 *
 * Checks whether the CPU has work to do.
 *
 * Returns: %true if the CPU has work, %false otherwise.
 */
bool cpu_has_work(CPUState *cpu);

#endif /* CONFIG_TCG */

#endif
