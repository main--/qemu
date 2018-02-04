/*
 * Software floating point for a given type.
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

/* Before including this file, define:
 *   FLOATXX   the floating-point type
 *   FS        the type letter (e.g. S, D, Q)
 *   WC        the word count required
 * and include all of the relevant files.
 */

#define _FP_ISNAN(fs, wc, X) \
    (X##_e == _FP_EXPMAX_##fs && !_FP_FRAC_ZEROP_##wc (X))
#define FP_ISNAN(fs, wc, X) \
    _FP_ISNAN(fs, wc, X)
#define FP_CHOOSENAN(fs, wc, R, A, B, OP) \
    _FP_CHOOSENAN(fs, wc, R, A, B, OP)
#define FP_SETQNAN(fs, wc, X) \
    _FP_SETQNAN(fs, wc, X)
#define FP_ADD_INTERNAL(fs, wc, R, A, B, OP) \
    _FP_ADD_INTERNAL(fs, wc, R, A, B, '-')

static FLOATXX addsub_internal(FLOATXX a, FLOATXX b, float_status *status,
                               bool subtract)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(B);
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_UNPACK_SEMIRAW_, FS)(A, a);
    glue(FP_UNPACK_SEMIRAW_, FS)(B, b);

    B_s ^= (subtract & !FP_ISNAN(FS, WC, B));
    FP_ADD_INTERNAL(FS, WC, R, A, B, '+');

    glue(FP_PACK_SEMIRAW_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;

    return r;
}

FLOATXX glue(FLOATXX,_add)(FLOATXX a, FLOATXX b, float_status *status)
{
    return addsub_internal(a, b, status, false);
}

FLOATXX glue(FLOATXX,_sub)(FLOATXX a, FLOATXX b, float_status *status)
{
    return addsub_internal(a, b, status, true);
}

FLOATXX glue(FLOATXX,_mul)(FLOATXX a, FLOATXX b, float_status *status)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(B);
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_UNPACK_, FS)(A, a);
    glue(FP_UNPACK_, FS)(B, b);
    glue(FP_MUL_, FS)(R, A, B);
    glue(FP_PACK_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;

    return r;
}

FLOATXX glue(FLOATXX,_div)(FLOATXX a, FLOATXX b, float_status *status)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(B);
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_UNPACK_, FS)(A, a);
    glue(FP_UNPACK_, FS)(B, b);
    glue(FP_DIV_, FS)(R, A, B);
    glue(FP_PACK_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;

    return r;
}

#define DO_FLOAT_TO_INT(NAME, SZ, FP_TO_INT_WHICH)   \
int##SZ##_t NAME(FLOATXX a, float_status *status) \
{                                                 \
    FP_DECL_EX;                                   \
    glue(FP_DECL_, FS)(A);                        \
    uint##SZ##_t r;                               \
    FP_INIT_ROUNDMODE;                            \
    glue(FP_UNPACK_RAW_, FS)(A, a);               \
    glue(FP_TO_INT_WHICH, FS)(r, A, SZ, 1);       \
    FP_HANDLE_EXCEPTIONS;                         \
    return r;                                     \
}

#define DO_FLOAT_TO_UINT(NAME, SZ, FP_TO_INT_WHICH)   \
uint##SZ##_t NAME(FLOATXX a, float_status *status) \
{                                                  \
    FP_DECL_EX;                                    \
    glue(FP_DECL_, FS)(A);                         \
    uint##SZ##_t r;                                \
    FP_INIT_ROUNDMODE;                             \
    glue(FP_UNPACK_RAW_, FS)(A, a);                \
    glue(FP_TO_INT_WHICH, FS)(r, A, SZ, 0);        \
    FP_HANDLE_EXCEPTIONS;                          \
    return r;                                      \
}

DO_FLOAT_TO_INT(glue(FLOATXX,_to_int16), 16, FP_TO_INT_ROUND_)
DO_FLOAT_TO_INT(glue(FLOATXX,_to_int32), 32, FP_TO_INT_ROUND_)
DO_FLOAT_TO_INT(glue(FLOATXX,_to_int64), 64, FP_TO_INT_ROUND_)

DO_FLOAT_TO_INT(glue(FLOATXX,_to_int16_round_to_zero), 16, FP_TO_INT_)
DO_FLOAT_TO_INT(glue(FLOATXX,_to_int32_round_to_zero), 32, FP_TO_INT_)
DO_FLOAT_TO_INT(glue(FLOATXX,_to_int64_round_to_zero), 64, FP_TO_INT_)

DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint16), 16, FP_TO_INT_ROUND_)
DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint32), 32, FP_TO_INT_ROUND_)
DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint64), 64, FP_TO_INT_ROUND_)

DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint16_round_to_zero), 16, FP_TO_INT_)
DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint32_round_to_zero), 32, FP_TO_INT_)
DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint64_round_to_zero), 64, FP_TO_INT_)

#undef DO_FLOAT_TO_INT
#undef DO_FLOAT_TO_UINT

FLOATXX glue(int64_to_,FLOATXX)(int64_t a, float_status *status)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_FROM_INT_, FS)(R, a, 64, uint64_t);
    glue(FP_PACK_RAW_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;
    return r;
}

FLOATXX glue(int16_to_,FLOATXX)(int16_t a, float_status *status)
{
    return glue(int64_to_,FLOATXX)(a, status);
}

FLOATXX glue(int32_to_,FLOATXX)(int32_t a, float_status *status)
{
    return glue(int64_to_,FLOATXX)(a, status);
}

/* The code within _FP_FROM_INT always tests A against 0.  For the
   unsigned conversion, this may result in a compiler warning.
   For -Werror, we need to suppress this.  */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"

FLOATXX glue(uint64_to_,FLOATXX)(uint64_t a, float_status *status)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_FROM_INT_, FS)(R, a, 64, uint64_t);
    glue(FP_PACK_RAW_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;
    return r;
}

#pragma GCC diagnostic pop

FLOATXX glue(uint16_to_,FLOATXX)(uint16_t a, float_status *status)
{
    return glue(uint64_to_,FLOATXX)(a, status);
}

FLOATXX glue(uint32_to_,FLOATXX)(uint32_t a, float_status *status)
{
    return glue(uint64_to_,FLOATXX)(a, status);
}

static int compare_internal(FLOATXX a, FLOATXX b,
                            float_status *status, bool quiet)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(B);
    int r;

    FP_INIT_EXCEPTIONS;
    glue(FP_UNPACK_RAW_, FS)(A, a);
    glue(FP_UNPACK_RAW_, FS)(B, b);
    glue(FP_CMP_, FS)(r, A, B, float_relation_unordered, (quiet ? 1 : 2));
    FP_HANDLE_EXCEPTIONS;

    return r;
}

int glue(FLOATXX,_compare)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, false);
}

int glue(FLOATXX,_compare_quiet)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, true);
}

int glue(FLOATXX,_eq)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, false) == 0;
}

int glue(FLOATXX,_le)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, false) <= 0;
}

int glue(FLOATXX,_lt)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, false) < 0;
}

int glue(FLOATXX,_unordered)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, false) == float_relation_unordered;
}

int glue(FLOATXX,_eq_quiet)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, true) == 0;
}

int glue(FLOATXX,_le_quiet)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, true) <= 0;
}

int glue(FLOATXX,_lt_quiet)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, true) < 0;
}

int glue(FLOATXX,_unordered_quiet)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, true) == float_relation_unordered;
}

#define MINMAX_MAX  0
#define MINMAX_MIN  1
#define MINMAX_IEEE 2
#define MINMAX_MAG  4

static FLOATXX minmax_internal(FLOATXX a, FLOATXX b,
                               float_status *status, int flags)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(B);
    bool save_A_s;
    int cmp;

    FP_INIT_EXCEPTIONS;
    glue(FP_UNPACK_RAW_, FS)(A, a);
    glue(FP_UNPACK_RAW_, FS)(B, b);

    /* When comparing magnitudes, squish the signs.  */
    save_A_s = A_s;
    if (flags & MINMAX_MAG) {
        A_s = B_s = 0;
    }

    glue(FP_CMP_, FS)(cmp, A, B, float_relation_unordered, 1);
    FP_HANDLE_EXCEPTIONS;

    if (unlikely(cmp == float_relation_unordered)) {
        glue(FP_DECL_, FS)(R);
        FLOATXX r;

        if (flags & MINMAX_IEEE) {
            if (glue(FP_ISSIGNAN_, FS)(A) || glue(FP_ISSIGNAN_, FS)(B)) {
                /* fall through to FP_CHOOSENAN */
            } else if (!FP_ISNAN(FS, WC, A)) {
                return a;
            } else if (!FP_ISNAN(FS, WC, B)) {
                return b;
            }
        }

        FP_CHOOSENAN(FS, WC, R, A, B, 'm');
        FP_SETQNAN(FS, WC, R);
        glue(FP_PACK_RAW_, FS)(r, R);
        return r;
    }

    /* Specially handle min(+0.0, -0.0) = -0.0, which compare as equal. */
    cmp = (cmp == 0 ? save_A_s : cmp < 0);
    cmp ^= flags & MINMAX_MIN;
    return cmp ? b : a;
}

FLOATXX glue(FLOATXX,_max)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status, MINMAX_MAX);
}

FLOATXX glue(FLOATXX,_min)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status, MINMAX_MIN);
}

FLOATXX glue(FLOATXX,_maxnum)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status, MINMAX_MAX | MINMAX_IEEE);
}

FLOATXX glue(FLOATXX,_minnum)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status, MINMAX_MIN | MINMAX_IEEE);
}

FLOATXX glue(FLOATXX,_maxnummag)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status,
                           MINMAX_MAX | MINMAX_IEEE | MINMAX_MAG);
}

FLOATXX glue(FLOATXX,_minnummag)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status,
                           MINMAX_MIN | MINMAX_IEEE | MINMAX_MAG);
}
