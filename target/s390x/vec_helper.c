/*
 * QEMU TCG support -- s390x vector support instructions and utilitites
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "internal.h"
#include "vec.h"
#include "tcg/tcg.h"
#include "tcg/tcg-gvec-desc.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"

/*
 * Each vector is stored as two 64bit host values. So when talking about
 * byte/halfword/word numbers, we have to take care of proper translation
 * between element numbers.
 *
 * Big Endian (target/possible host)
 * B:  [ 0][ 1][ 2][ 3][ 4][ 5][ 6][ 7] - [ 8][ 9][10][11][12][13][14][15]
 * HW: [     0][     1][     2][     3] - [     4][     5][     6][     7]
 * W:  [             0][             1] - [             2][             3]
 * DW: [                             0] - [                             1]
 *
 * Little Endian (possible host)
 * B:  [ 7][ 6][ 5][ 4][ 3][ 2][ 1][ 0] - [15][14][13][12][11][10][ 9][ 8]
 * HW: [     3][     2][     1][     0] - [     7][     6][     5][     4]
 * W:  [             1][             0] - [             3][             2]
 * DW: [                             0] - [                             1]
 */
#ifndef HOST_WORDS_BIGENDIAN
#define H1(x)  ((x) ^ 7)
#define H2(x)  ((x) ^ 3)
#define H4(x)  ((x) ^ 1)
#else
#define H1(x)  (x)
#define H2(x)  (x)
#define H4(x)  (x)
#endif

uint8_t s390_vec_read_element8(const S390Vector *v, uint8_t enr)
{
    g_assert(enr < 16);
    return v->byte[H1(enr)];
}

uint16_t s390_vec_read_element16(const S390Vector *v, uint8_t enr)
{
    g_assert(enr < 8);
    return v->halfword[H2(enr)];
}

uint32_t s390_vec_read_element32(const S390Vector *v, uint8_t enr)
{
    g_assert(enr < 4);
    return v->word[H4(enr)];
}

uint64_t s390_vec_read_element64(const S390Vector *v, uint8_t enr)
{
    g_assert(enr < 2);
    return v->doubleword[enr];
}

void s390_vec_write_element8(S390Vector *v, uint8_t enr, uint8_t data)
{
    g_assert(enr < 16);
    v->byte[H1(enr)] = data;
}

void s390_vec_write_element16(S390Vector *v, uint8_t enr, uint16_t data)
{
    g_assert(enr < 8);
    v->halfword[H2(enr)] = data;
}

void s390_vec_write_element32(S390Vector *v, uint8_t enr, uint32_t data)
{
    g_assert(enr < 4);
    v->word[H4(enr)] = data;
}

void s390_vec_write_element64(S390Vector *v, uint8_t enr, uint64_t data)
{
    g_assert(enr < 2);
    v->doubleword[enr] = data;
}

void HELPER(vll)(CPUS390XState *env, void *v1, uint64_t addr, uint64_t bytes)
{
    if (likely(bytes >= 16)) {
        uint64_t t0, t1;

        t0 = cpu_ldq_data_ra(env, addr, GETPC());
        addr = wrap_address(env, addr + 8);
        t1 = cpu_ldq_data_ra(env, addr, GETPC());
        s390_vec_write_element64(v1, 0, t0);
        s390_vec_write_element64(v1, 1, t1);
    } else {
        S390Vector tmp = {};
        int i;

        for (i = 0; i < bytes; i++) {
            uint8_t byte = cpu_ldub_data_ra(env, addr, GETPC());

            s390_vec_write_element8(&tmp, i, byte);
            addr = wrap_address(env, addr + 1);
        }
        *(S390Vector *)v1 = tmp;
    }
}

#define DEF_VPK_HFN(BITS, TBITS)                                               \
typedef uint##TBITS##_t (*vpk##BITS##_fn)(uint##BITS##_t, int *);              \
static int vpk##BITS##_hfn(S390Vector *v1, const S390Vector *v2,               \
                           const S390Vector *v3, vpk##BITS##_fn fn)            \
{                                                                              \
    int i, saturated = 0;                                                      \
    S390Vector tmp;                                                            \
                                                                               \
    for (i = 0; i < (128 / TBITS); i++) {                                      \
        uint##BITS##_t src;                                                    \
                                                                               \
        if (i < (128 / BITS)) {                                                \
            src = s390_vec_read_element##BITS(v2, i);                          \
        } else {                                                               \
            src = s390_vec_read_element##BITS(v3, i - (128 / BITS));           \
        }                                                                      \
        s390_vec_write_element##TBITS(&tmp, i, fn(src, &saturated));           \
    }                                                                          \
    *v1 = tmp;                                                                 \
    return saturated;                                                          \
}
DEF_VPK_HFN(64, 32)
DEF_VPK_HFN(32, 16)
DEF_VPK_HFN(16, 8)

#define DEF_VPK(BITS, TBITS)                                                   \
static uint##TBITS##_t vpk##BITS##e(uint##BITS##_t src, int *saturated)        \
{                                                                              \
    return src;                                                                \
}                                                                              \
void HELPER(gvec_vpk##BITS)(void *v1, const void *v2, const void *v3,          \
                            uint32_t desc)                                     \
{                                                                              \
    vpk##BITS##_hfn(v1, v2, v3, vpk##BITS##e);                                 \
}
DEF_VPK(64, 32)
DEF_VPK(32, 16)
DEF_VPK(16, 8)

#define DEF_VPKS(BITS, TBITS)                                                  \
static uint##TBITS##_t vpks##BITS##e(uint##BITS##_t src, int *saturated)       \
{                                                                              \
    if ((int##BITS##_t)src > INT##TBITS##_MAX) {                               \
        (*saturated)++;                                                        \
        return INT##TBITS##_MAX;                                               \
    } else if ((int##BITS##_t)src < INT##TBITS##_MIN) {                        \
        (*saturated)++;                                                        \
        return INT##TBITS##_MIN;                                               \
    }                                                                          \
    return src;                                                                \
}                                                                              \
void HELPER(gvec_vpks##BITS)(void *v1, const void *v2, const void *v3,         \
                             uint32_t desc)                                    \
{                                                                              \
    vpk##BITS##_hfn(v1, v2, v3, vpks##BITS##e);                                \
}                                                                              \
void HELPER(gvec_vpks_cc##BITS)(void *v1, const void *v2, const void *v3,      \
                                CPUS390XState *env, uint32_t desc)             \
{                                                                              \
    int saturated = vpk##BITS##_hfn(v1, v2, v3, vpks##BITS##e);                \
                                                                               \
    if (saturated == (128 / TBITS)) {                                          \
        env->cc_op = 3;                                                        \
    } else if (saturated) {                                                    \
        env->cc_op = 1;                                                        \
    } else {                                                                   \
        env->cc_op = 0;                                                        \
    }                                                                          \
}
DEF_VPKS(64, 32)
DEF_VPKS(32, 16)
DEF_VPKS(16, 8)

#define DEF_VPKLS(BITS, TBITS)                                                 \
static uint##TBITS##_t vpkls##BITS##e(uint##BITS##_t src, int *saturated)      \
{                                                                              \
    if (src > UINT##TBITS##_MAX) {                                             \
        (*saturated)++;                                                        \
        return UINT##TBITS##_MAX;                                              \
    }                                                                          \
    return src;                                                                \
}                                                                              \
void HELPER(gvec_vpkls##BITS)(void *v1, const void *v2, const void *v3,        \
                              uint32_t desc)                                   \
{                                                                              \
    vpk##BITS##_hfn(v1, v2, v3, vpkls##BITS##e);                               \
}                                                                              \
void HELPER(gvec_vpkls_cc##BITS)(void *v1, const void *v2, const void *v3,     \
                                 CPUS390XState *env, uint32_t desc)            \
{                                                                              \
    int saturated = vpk##BITS##_hfn(v1, v2, v3, vpkls##BITS##e);               \
                                                                               \
    if (saturated == (128 / TBITS)) {                                          \
        env->cc_op = 3;                                                        \
    } else if (saturated) {                                                    \
        env->cc_op = 1;                                                        \
    } else {                                                                   \
        env->cc_op = 0;                                                        \
    }                                                                          \
}
DEF_VPKLS(64, 32)
DEF_VPKLS(32, 16)
DEF_VPKLS(16, 8)
