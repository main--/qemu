/*
 * The per-CPU TranslationBlock jump cache.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_TCG_TB_JMP_CACHE_H
#define ACCEL_TCG_TB_JMP_CACHE_H

#define TB_JMP_CACHE_BITS 12
#define TB_JMP_CACHE_SIZE (1 << TB_JMP_CACHE_BITS)

/*
 * Accessed in parallel; all accesses to 'tb' must be atomic.
 * For TARGET_TB_PCREL, accesses to 'pc' must be protected by
 * a load_acquire/store_release to 'tb'.
 */
struct CPUJumpCache {
    struct {
        TranslationBlock *tb;
#if TARGET_TB_PCREL
        target_ulong pc;
#endif
    } array[TB_JMP_CACHE_SIZE];
};

#endif /* ACCEL_TCG_TB_JMP_CACHE_H */
