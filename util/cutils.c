/*
 * Simple C functions to supplement the C library
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/host-utils.h"
#include <math.h>

#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "net/net.h"
#include "qemu/cutils.h"

void strpadcpy(char *buf, int buf_size, const char *str, char pad)
{
    int len = qemu_strnlen(str, buf_size);
    memcpy(buf, str, len);
    memset(buf + len, pad, buf_size - len);
}

void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

/* strcat and truncate. */
char *pstrcat(char *buf, int buf_size, const char *s)
{
    int len;
    len = strlen(buf);
    if (len < buf_size)
        pstrcpy(buf + len, buf_size - len, s);
    return buf;
}

int strstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (*p != *q)
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

int stristart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (qemu_toupper(*p) != qemu_toupper(*q))
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

/* XXX: use host strnlen if available ? */
int qemu_strnlen(const char *s, int max_len)
{
    int i;

    for(i = 0; i < max_len; i++) {
        if (s[i] == '\0') {
            break;
        }
    }
    return i;
}

char *qemu_strsep(char **input, const char *delim)
{
    char *result = *input;
    if (result != NULL) {
        char *p;

        for (p = result; *p != '\0'; p++) {
            if (strchr(delim, *p)) {
                break;
            }
        }
        if (*p == '\0') {
            *input = NULL;
        } else {
            *p = '\0';
            *input = p + 1;
        }
    }
    return result;
}

time_t mktimegm(struct tm *tm)
{
    time_t t;
    int y = tm->tm_year + 1900, m = tm->tm_mon + 1, d = tm->tm_mday;
    if (m < 3) {
        m += 12;
        y--;
    }
    t = 86400ULL * (d + (153 * m - 457) / 5 + 365 * y + y / 4 - y / 100 + 
                 y / 400 - 719469);
    t += 3600 * tm->tm_hour + 60 * tm->tm_min + tm->tm_sec;
    return t;
}

/*
 * Make sure data goes on disk, but if possible do not bother to
 * write out the inode just for timestamp updates.
 *
 * Unfortunately even in 2009 many operating systems do not support
 * fdatasync and have to fall back to fsync.
 */
int qemu_fdatasync(int fd)
{
#ifdef CONFIG_FDATASYNC
    return fdatasync(fd);
#else
    return fsync(fd);
#endif
}

/* vector definitions */

extern void link_error(void);

#define ACCEL_BUFFER_ZERO(NAME, SIZE, VECTYPE, ZERO)            \
static bool __attribute__((noinline))                           \
NAME(const void *buf, size_t len)                               \
{                                                               \
    const void *end = buf + len;                                \
    do {                                                        \
        const VECTYPE *p = buf;                                 \
        VECTYPE t;                                              \
        __builtin_prefetch(buf + SIZE);                         \
        barrier();                                              \
        if (SIZE == sizeof(VECTYPE) * 4) {                      \
            t = (p[0] | p[1]) | (p[2] | p[3]);                  \
        } else if (SIZE == sizeof(VECTYPE) * 8) {               \
            t  = p[0] | p[1];                                   \
            t |= p[2] | p[3];                                   \
            t |= p[4] | p[5];                                   \
            t |= p[6] | p[7];                                   \
        } else {                                                \
            link_error();                                       \
        }                                                       \
        if (unlikely(!ZERO(t))) {                               \
            return false;                                       \
        }                                                       \
        buf += SIZE;                                            \
    } while (buf < end);                                        \
    return true;                                                \
}

typedef bool (*accel_zero_fn)(const void *, size_t);

static bool __attribute__((noinline))
buffer_zero_base(const void *buf, size_t len)
{
    size_t i;

    /* Check bytes until the buffer is aligned.  */
    for (i = 0; i < len && ((uintptr_t)buf + i) % sizeof(long); ++i) {
        const char *p = buf + i;
        if (*p) {
            return false;
        }
    }

    /* Check longs until we run out.  */
    for (; i + sizeof(long) <= len; i += sizeof(long)) {
        const long *p = buf + i;
        if (*p) {
            return false;
        }
    }

    /* Check the last few bytes of the tail.  */
    for (; i < len; ++i) {
        const char *p = buf + i;
        if (*p) {
            return false;
        }
    }

    return true;
}

#define IDENT_ZERO(X)  (X)
ACCEL_BUFFER_ZERO(buffer_zero_int, 4*sizeof(long), long, IDENT_ZERO)

static bool select_accel_int(const void *buf, size_t len)
{
    uintptr_t ibuf = (uintptr_t)buf;
    /* Note that this condition used to be the input constraint for
       buffer_is_zero, therefore it is highly likely to be true.  */
    if (likely(len % (4 * sizeof(long)) == 0)
        && likely(ibuf % sizeof(long) == 0)) {
        return buffer_zero_int(buf, len);
    }
    return buffer_zero_base(buf, len);
}

#ifdef __ALTIVEC__
#include <altivec.h>
/* The altivec.h header says we're allowed to undef these for
 * C++ compatibility.  Here we don't care about C++, but we
 * undef them anyway to avoid namespace pollution.
 * altivec.h may redefine the bool macro as vector type.
 * Reset it to POSIX semantics.
 */
#undef vector
#undef pixel
#undef bool
#define bool _Bool
#define DO_ZERO(X)  vec_all_eq(X, (__vector unsigned char){ 0 })
ACCEL_BUFFER_ZERO(buffer_zero_ppc, 128, __vector unsigned char, DO_ZERO)

static bool select_accel_fn(const void *buf, size_t len)
{
    uintptr_t ibuf = (uintptr_t)buf;
    if (len % 128 == 0 && ibuf % sizeof(__vector unsigned char) == 0) {
        return buffer_zero_ppc(buf, len);
    }
    return select_accel_int(buf, len);
}

#elif defined(CONFIG_AVX2_OPT) || defined(__SSE2__)
#include <cpuid.h>
#include <x86intrin.h>

#ifdef CONFIG_AVX2_OPT
#pragma GCC push_options
#pragma GCC target("avx2")

static bool __attribute__((noinline))
buffer_zero_avx2(const void *buf, size_t len)
{
    const __m256i *p = buf;
    const __m256i *end = buf + len;
    __m256i t;

    do {
        p += 4;
        __builtin_prefetch(p);
        /* Note that most AVX insns handle unaligned operands by
           default; we only need take care for the initial load.  */
        __asm volatile("vmovdqu -0x80(%1),%0\n\t"
                       "vpor -0x60(%1),%0,%0\n\t"
                       "vpor -0x40(%1),%0,%0\n\t"
                       "vpor -0x20(%1),%0,%0"
                       : "=x"(t) : "r"(p));
        if (unlikely(!_mm256_testz_si256(t, t))) {
            return false;
        }
    } while (p < end);
    return true;
}

#pragma GCC pop_options
#pragma GCC push_options
#pragma GCC target("avx")

static bool __attribute__((noinline))
buffer_zero_avx(const void *buf, size_t len)
{
    const __m128i *p = buf;
    const __m128i *end = buf + len;
    __m128i t;

    do {
        p += 4;
        __builtin_prefetch(p);
        /* Note that most AVX insns handle unaligned operands by
           default; we only need take care for the initial load.  */
        __asm volatile("vmovdqu -0x40(%1),%0\n\t"
                       "vpor -0x20(%1),%0,%0\n\t"
                       "vpor -0x20(%1),%0,%0\n\t"
                       "vpor -0x10(%1),%0,%0"
                       : "=x"(t) : "r"(p));
        if (unlikely(!_mm_testz_si128(t, t))) {
            return false;
        }
    } while (p < end);
    return true;
}

#pragma GCC pop_options
#pragma GCC push_options
#pragma GCC target("sse4")

static bool __attribute__((noinline))
buffer_zero_sse4(const void *buf, size_t len)
{
    const __m128i *p = buf;
    const __m128i *end = buf + len;
    __m128i t0, t1, t2, t3;

    do {
        p += 4;
        __builtin_prefetch(p);
        __asm volatile("movdqu -0x40(%4),%0\n\t"
                       "movdqu -0x20(%4),%1\n\t"
                       "movdqu -0x20(%4),%2\n\t"
                       "movdqu -0x10(%4),%3\n\t"
                       "por %1,%0\n\t"
                       "por %3,%2\n\t"
                       "por %2,%0"
                       : "=x"(t0), "=x"(t1), "=x"(t2), "=x"(t3) : "r"(p));
        if (unlikely(!_mm_testz_si128(t0, t0))) {
            return false;
        }
    } while (p < end);
    return true;
}

#pragma GCC pop_options
#pragma GCC push_options
#pragma GCC target("sse2")
#endif /* CONFIG_AVX2_OPT */

static bool __attribute__((noinline))
buffer_zero_sse2(const void *buf, size_t len)
{
    const __m128i *p = buf;
    const __m128i *end = buf + len;
    __m128i zero = _mm_setzero_si128();
    __m128i t0, t1, t2, t3;

    do {
        p += 4;
        __builtin_prefetch(p);
        __asm volatile("movdqu -0x40(%4),%0\n\t"
                       "movdqu -0x20(%4),%1\n\t"
                       "movdqu -0x20(%4),%2\n\t"
                       "movdqu -0x10(%4),%3\n\t"
                       "por %1,%0\n\t"
                       "por %3,%2\n\t"
                       "por %2,%0"
                       : "=x"(t0), "=x"(t1), "=x"(t2), "=x"(t3) : "r"(p));
        if (unlikely(_mm_movemask_epi8(_mm_cmpeq_epi8(t0, zero)) == 0xFFFF)) {
            return false;
        }
    } while (p < end);
    return true;
}

#ifdef CONFIG_AVX2_OPT
#pragma GCC pop_options

#define CACHE_SSE2    1
#define CACHE_SSE4    2
#define CACHE_AVX1    4
#define CACHE_AVX2    8

static int cpuid_cache;

static void __attribute__((constructor)) init_cpuid_cache(void)
{
    int max = __get_cpuid_max(0, NULL);
    int a, b, c, d;
    int cache = 0;

    if (max >= 1) {
        __cpuid(1, a, b, c, d);
        if (d & bit_SSE2) {
            cache |= CACHE_SSE2;
        }
        if (c & bit_SSE4_1) {
            cache |= CACHE_SSE4;
        }

        /* We must check that AVX is not just available, but usable.  */
        if ((c & bit_OSXSAVE) && (c & bit_AVX)) {
            __asm("xgetbv" : "=a"(a), "=d"(d) : "c"(0));
            if ((a & 6) == 6) {
                cache |= CACHE_AVX1;
                if (max >= 7) {
                    __cpuid_count(7, 0, a, b, c, d);
                    if (b & bit_AVX2) {
                        cache |= CACHE_AVX2;
                    }
                }
            }
        }
    }
    cpuid_cache = cache;
}
#endif /* CONFIG_AVX2_OPT */

static bool select_accel_fn(const void *buf, size_t len)
{
#ifdef CONFIG_AVX2_OPT
    int cache = cpuid_cache;

    /* Force bits that the compiler tells us must be there.
       This allows the compiler to optimize subsequent tests.  */
#ifdef __AVX2__
    cache |= CACHE_AVX2;
#endif
#ifdef __AVX__
    cache |= CACHE_AVX1;
#endif
#ifdef __SSE4_1__
    cache |= CACHE_SSE4;
#endif
#ifdef __SSE2__
    cache |= CACHE_SSE2;
#endif

    if (len % 128 == 0 && (cache & CACHE_AVX2)) {
        return buffer_zero_avx2(buf, len);
    }
    if (len % 64 == 0) {
        if (cache & CACHE_AVX1) {
            return buffer_zero_avx(buf, len);
        }
        if (cache & CACHE_SSE4) {
            return buffer_zero_sse4(buf, len);
        }
        if (cache & CACHE_SSE2) {
            return buffer_zero_sse2(buf, len);
        }
    }
#else
    if (len % 64 == 0) {
        return buffer_zero_sse2(buf, len);
    }
#endif
    return select_accel_int(buf, len);
}

#elif defined(__aarch64__)
#include "arm_neon.h"

#define DO_ZERO(X)  (vgetq_lane_u64((X), 0) | vgetq_lane_u64((X), 1))
ACCEL_BUFFER_ZERO(buffer_zero_neon, 128, uint64x2_t, DO_ZERO)

static bool select_accel_fn(const void *buf, size_t len)
{
    uintptr_t ibuf = (uintptr_t)buf;
    if (len % 128 == 0 && ibuf % sizeof(uint64x2_t) == 0) {
        return buffer_zero_neon(buf, len);
    }
    return select_accel_int(buf, len);
}

#else
#define select_accel_fn  select_accel_int
#endif

/*
 * Checks if a buffer is all zeroes
 */
bool buffer_is_zero(const void *buf, size_t len)
{
    if (unlikely(len == 0)) {
        return true;
    }

    /* Fetch the beginning of the buffer while we select the accelerator.  */
    __builtin_prefetch(buf);

    /* Use an optimized zero check if possible.  Note that this also
       includes a check for an unrolled loop over longs, as well as
       the unsized, unaligned fallback to buffer_zero_base.  */
    return select_accel_fn(buf, len);
}

#ifndef _WIN32
/* Sets a specific flag */
int fcntl_setfl(int fd, int flag)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        return -errno;

    if (fcntl(fd, F_SETFL, flags | flag) == -1)
        return -errno;

    return 0;
}
#endif

static int64_t suffix_mul(char suffix, int64_t unit)
{
    switch (qemu_toupper(suffix)) {
    case QEMU_STRTOSZ_DEFSUFFIX_B:
        return 1;
    case QEMU_STRTOSZ_DEFSUFFIX_KB:
        return unit;
    case QEMU_STRTOSZ_DEFSUFFIX_MB:
        return unit * unit;
    case QEMU_STRTOSZ_DEFSUFFIX_GB:
        return unit * unit * unit;
    case QEMU_STRTOSZ_DEFSUFFIX_TB:
        return unit * unit * unit * unit;
    case QEMU_STRTOSZ_DEFSUFFIX_PB:
        return unit * unit * unit * unit * unit;
    case QEMU_STRTOSZ_DEFSUFFIX_EB:
        return unit * unit * unit * unit * unit * unit;
    }
    return -1;
}

/*
 * Convert string to bytes, allowing either B/b for bytes, K/k for KB,
 * M/m for MB, G/g for GB or T/t for TB. End pointer will be returned
 * in *end, if not NULL. Return -ERANGE on overflow, Return -EINVAL on
 * other error.
 */
int64_t qemu_strtosz_suffix_unit(const char *nptr, char **end,
                            const char default_suffix, int64_t unit)
{
    int64_t retval = -EINVAL;
    char *endptr;
    unsigned char c;
    int mul_required = 0;
    double val, mul, integral, fraction;

    errno = 0;
    val = strtod(nptr, &endptr);
    if (isnan(val) || endptr == nptr || errno != 0) {
        goto fail;
    }
    fraction = modf(val, &integral);
    if (fraction != 0) {
        mul_required = 1;
    }
    c = *endptr;
    mul = suffix_mul(c, unit);
    if (mul >= 0) {
        endptr++;
    } else {
        mul = suffix_mul(default_suffix, unit);
        assert(mul >= 0);
    }
    if (mul == 1 && mul_required) {
        goto fail;
    }
    if ((val * mul >= INT64_MAX) || val < 0) {
        retval = -ERANGE;
        goto fail;
    }
    retval = val * mul;

fail:
    if (end) {
        *end = endptr;
    }

    return retval;
}

int64_t qemu_strtosz_suffix(const char *nptr, char **end,
                            const char default_suffix)
{
    return qemu_strtosz_suffix_unit(nptr, end, default_suffix, 1024);
}

int64_t qemu_strtosz(const char *nptr, char **end)
{
    return qemu_strtosz_suffix(nptr, end, QEMU_STRTOSZ_DEFSUFFIX_MB);
}

/**
 * Helper function for qemu_strto*l() functions.
 */
static int check_strtox_error(const char *p, char *endptr, const char **next,
                              int err)
{
    /* If no conversion was performed, prefer BSD behavior over glibc
     * behavior.
     */
    if (err == 0 && endptr == p) {
        err = EINVAL;
    }
    if (!next && *endptr) {
        return -EINVAL;
    }
    if (next) {
        *next = endptr;
    }
    return -err;
}

/**
 * QEMU wrappers for strtol(), strtoll(), strtoul(), strotull() C functions.
 *
 * Convert ASCII string @nptr to a long integer value
 * from the given @base. Parameters @nptr, @endptr, @base
 * follows same semantics as strtol() C function.
 *
 * Unlike from strtol() function, if @endptr is not NULL, this
 * function will return -EINVAL whenever it cannot fully convert
 * the string in @nptr with given @base to a long. This function returns
 * the result of the conversion only through the @result parameter.
 *
 * If NULL is passed in @endptr, then the whole string in @ntpr
 * is a number otherwise it returns -EINVAL.
 *
 * RETURN VALUE
 * Unlike from strtol() function, this wrapper returns either
 * -EINVAL or the errno set by strtol() function (e.g -ERANGE).
 * If the conversion overflows, -ERANGE is returned, and @result
 * is set to the max value of the desired type
 * (e.g. LONG_MAX, LLONG_MAX, ULONG_MAX, ULLONG_MAX). If the case
 * of underflow, -ERANGE is returned, and @result is set to the min
 * value of the desired type. For strtol(), strtoll(), @result is set to
 * LONG_MIN, LLONG_MIN, respectively, and for strtoul(), strtoull() it
 * is set to 0.
 */
int qemu_strtol(const char *nptr, const char **endptr, int base,
                long *result)
{
    char *p;
    int err = 0;
    if (!nptr) {
        if (endptr) {
            *endptr = nptr;
        }
        err = -EINVAL;
    } else {
        errno = 0;
        *result = strtol(nptr, &p, base);
        err = check_strtox_error(nptr, p, endptr, errno);
    }
    return err;
}

/**
 * Converts ASCII string to an unsigned long integer.
 *
 * If string contains a negative number, value will be converted to
 * the unsigned representation of the signed value, unless the original
 * (nonnegated) value would overflow, in this case, it will set @result
 * to ULONG_MAX, and return ERANGE.
 *
 * The same behavior holds, for qemu_strtoull() but sets @result to
 * ULLONG_MAX instead of ULONG_MAX.
 *
 * See qemu_strtol() documentation for more info.
 */
int qemu_strtoul(const char *nptr, const char **endptr, int base,
                 unsigned long *result)
{
    char *p;
    int err = 0;
    if (!nptr) {
        if (endptr) {
            *endptr = nptr;
        }
        err = -EINVAL;
    } else {
        errno = 0;
        *result = strtoul(nptr, &p, base);
        /* Windows returns 1 for negative out-of-range values.  */
        if (errno == ERANGE) {
            *result = -1;
        }
        err = check_strtox_error(nptr, p, endptr, errno);
    }
    return err;
}

/**
 * Converts ASCII string to a long long integer.
 *
 * See qemu_strtol() documentation for more info.
 */
int qemu_strtoll(const char *nptr, const char **endptr, int base,
                 int64_t *result)
{
    char *p;
    int err = 0;
    if (!nptr) {
        if (endptr) {
            *endptr = nptr;
        }
        err = -EINVAL;
    } else {
        errno = 0;
        *result = strtoll(nptr, &p, base);
        err = check_strtox_error(nptr, p, endptr, errno);
    }
    return err;
}

/**
 * Converts ASCII string to an unsigned long long integer.
 *
 * See qemu_strtol() documentation for more info.
 */
int qemu_strtoull(const char *nptr, const char **endptr, int base,
                  uint64_t *result)
{
    char *p;
    int err = 0;
    if (!nptr) {
        if (endptr) {
            *endptr = nptr;
        }
        err = -EINVAL;
    } else {
        errno = 0;
        *result = strtoull(nptr, &p, base);
        /* Windows returns 1 for negative out-of-range values.  */
        if (errno == ERANGE) {
            *result = -1;
        }
        err = check_strtox_error(nptr, p, endptr, errno);
    }
    return err;
}

/**
 * parse_uint:
 *
 * @s: String to parse
 * @value: Destination for parsed integer value
 * @endptr: Destination for pointer to first character not consumed
 * @base: integer base, between 2 and 36 inclusive, or 0
 *
 * Parse unsigned integer
 *
 * Parsed syntax is like strtoull()'s: arbitrary whitespace, a single optional
 * '+' or '-', an optional "0x" if @base is 0 or 16, one or more digits.
 *
 * If @s is null, or @base is invalid, or @s doesn't start with an
 * integer in the syntax above, set *@value to 0, *@endptr to @s, and
 * return -EINVAL.
 *
 * Set *@endptr to point right beyond the parsed integer (even if the integer
 * overflows or is negative, all digits will be parsed and *@endptr will
 * point right beyond them).
 *
 * If the integer is negative, set *@value to 0, and return -ERANGE.
 *
 * If the integer overflows unsigned long long, set *@value to
 * ULLONG_MAX, and return -ERANGE.
 *
 * Else, set *@value to the parsed integer, and return 0.
 */
int parse_uint(const char *s, unsigned long long *value, char **endptr,
               int base)
{
    int r = 0;
    char *endp = (char *)s;
    unsigned long long val = 0;

    if (!s) {
        r = -EINVAL;
        goto out;
    }

    errno = 0;
    val = strtoull(s, &endp, base);
    if (errno) {
        r = -errno;
        goto out;
    }

    if (endp == s) {
        r = -EINVAL;
        goto out;
    }

    /* make sure we reject negative numbers: */
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '-') {
        val = 0;
        r = -ERANGE;
        goto out;
    }

out:
    *value = val;
    *endptr = endp;
    return r;
}

/**
 * parse_uint_full:
 *
 * @s: String to parse
 * @value: Destination for parsed integer value
 * @base: integer base, between 2 and 36 inclusive, or 0
 *
 * Parse unsigned integer from entire string
 *
 * Have the same behavior of parse_uint(), but with an additional check
 * for additional data after the parsed number. If extra characters are present
 * after the parsed number, the function will return -EINVAL, and *@v will
 * be set to 0.
 */
int parse_uint_full(const char *s, unsigned long long *value, int base)
{
    char *endp;
    int r;

    r = parse_uint(s, value, &endp, base);
    if (r < 0) {
        return r;
    }
    if (*endp) {
        *value = 0;
        return -EINVAL;
    }

    return 0;
}

int qemu_parse_fd(const char *param)
{
    long fd;
    char *endptr;

    errno = 0;
    fd = strtol(param, &endptr, 10);
    if (param == endptr /* no conversion performed */                    ||
        errno != 0      /* not representable as long; possibly others */ ||
        *endptr != '\0' /* final string not empty */                     ||
        fd < 0          /* invalid as file descriptor */                 ||
        fd > INT_MAX    /* not representable as int */) {
        return -1;
    }
    return fd;
}

/*
 * Implementation of  ULEB128 (http://en.wikipedia.org/wiki/LEB128)
 * Input is limited to 14-bit numbers
 */
int uleb128_encode_small(uint8_t *out, uint32_t n)
{
    g_assert(n <= 0x3fff);
    if (n < 0x80) {
        *out++ = n;
        return 1;
    } else {
        *out++ = (n & 0x7f) | 0x80;
        *out++ = n >> 7;
        return 2;
    }
}

int uleb128_decode_small(const uint8_t *in, uint32_t *n)
{
    if (!(*in & 0x80)) {
        *n = *in++;
        return 1;
    } else {
        *n = *in++ & 0x7f;
        /* we exceed 14 bit number */
        if (*in & 0x80) {
            return -1;
        }
        *n |= *in++ << 7;
        return 2;
    }
}

/*
 * helper to parse debug environment variables
 */
int parse_debug_env(const char *name, int max, int initial)
{
    char *debug_env = getenv(name);
    char *inv = NULL;
    long debug;

    if (!debug_env) {
        return initial;
    }
    errno = 0;
    debug = strtol(debug_env, &inv, 10);
    if (inv == debug_env) {
        return initial;
    }
    if (debug < 0 || debug > max || errno != 0) {
        fprintf(stderr, "warning: %s not in [0, %d]", name, max);
        return initial;
    }
    return debug;
}

/*
 * Helper to print ethernet mac address
 */
const char *qemu_ether_ntoa(const MACAddr *mac)
{
    static char ret[18];

    snprintf(ret, sizeof(ret), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac->a[0], mac->a[1], mac->a[2], mac->a[3], mac->a[4], mac->a[5]);

    return ret;
}
