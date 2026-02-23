//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_bigint.c
// Purpose: Implements arbitrary-precision integer arithmetic for the Viper
//          runtime. Uses a base-2^32 little-endian digit array with a separate
//          sign flag. Covers grade-school add/sub/mul, Knuth Algorithm D for
//          division, binary GCD, and conversion to/from int64 and strings.
//
// Key invariants:
//   - Digits are stored in little-endian order (index 0 = least significant).
//   - Zero is always represented as non-negative with zero digits (len == 0).
//   - The sign flag is 0 for non-negative and 1 for negative; -0 is normalised
//     to +0 after every operation.
//   - Digit arrays are heap-allocated via calloc and tracked separately from
//     the GC-managed outer object; the finalizer frees them explicitly.
//   - All arithmetic functions are pure with respect to their output objects;
//     no shared mutable state — safe for concurrent use on distinct objects.
//
// Ownership/Lifetime:
//   - BigInt objects are allocated via rt_obj_new_i64 (GC-managed); the
//     finalizer (bigint_finalizer) frees the digit array via free().
//   - Intermediate bigint_t values used during computation are owned by the
//     function and freed before return or on error paths.
//
// Links: src/runtime/core/rt_bigint.h (public API),
//        src/runtime/core/rt_object.c (rt_obj_new_i64, rt_obj_set_finalizer),
//        src/runtime/core/rt_string.c (string output for to-string conversion)
//
//===----------------------------------------------------------------------===//

#include "rt_bigint.h"

#include "rt_bytes.h"
#include "rt_object.h"
#include "rt_string.h"

// External trap function
extern void rt_trap(const char *msg);

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Representation
//=============================================================================

#define BIGINT_BASE ((uint64_t)1 << 32)
#define BIGINT_CLASS_ID 0x424967496E74 // "BigInt"

typedef struct
{
    uint32_t *digits; // Little-endian digits (least significant first)
    int64_t len;      // Number of digits
    int64_t cap;      // Capacity
    int sign;         // 0 = non-negative, 1 = negative
} bigint_t;

//=============================================================================
// Memory Management
//=============================================================================

static void bigint_finalizer(void *obj);

static bigint_t *bigint_alloc(int64_t capacity)
{
    void *obj = rt_obj_new_i64(BIGINT_CLASS_ID, sizeof(bigint_t));
    if (!obj)
        return NULL;

    bigint_t *bi = (bigint_t *)obj;
    bi->cap = capacity > 0 ? capacity : 4;
    bi->digits = calloc((size_t)bi->cap, sizeof(uint32_t));
    bi->len = 0;
    bi->sign = 0;

    rt_obj_set_finalizer(obj, bigint_finalizer);
    return bi;
}

static void bigint_finalizer(void *obj)
{
    bigint_t *bi = (bigint_t *)obj;
    if (bi->digits)
    {
        free(bi->digits);
        bi->digits = NULL;
    }
}

static void bigint_ensure_capacity(bigint_t *bi, int64_t cap)
{
    if (cap <= bi->cap)
        return;

    int64_t new_cap = bi->cap * 2;
    if (new_cap < cap)
        new_cap = cap;

    uint32_t *new_digits = realloc(bi->digits, (size_t)new_cap * sizeof(uint32_t));
    if (!new_digits)
        return;
    memset(new_digits + bi->cap, 0, (size_t)(new_cap - bi->cap) * sizeof(uint32_t));
    bi->digits = new_digits;
    bi->cap = new_cap;
}

static void bigint_normalize(bigint_t *bi)
{
    while (bi->len > 0 && bi->digits[bi->len - 1] == 0)
        bi->len--;

    // Zero is always non-negative
    if (bi->len == 0)
        bi->sign = 0;
}

static bigint_t *bigint_clone(bigint_t *a)
{
    bigint_t *result = bigint_alloc(a->len);
    if (!result)
        return NULL;

    result->len = a->len;
    result->sign = a->sign;
    memcpy(result->digits, a->digits, (size_t)a->len * sizeof(uint32_t));
    return result;
}

//=============================================================================
// BigInt Creation
//=============================================================================

void *rt_bigint_from_i64(int64_t val)
{
    bigint_t *bi = bigint_alloc(2);
    if (!bi)
        return NULL;

    if (val == 0)
    {
        bi->len = 0;
        bi->sign = 0;
    }
    else if (val < 0)
    {
        bi->sign = 1;
        uint64_t uval;
        if (val == INT64_MIN)
        {
            uval = (uint64_t)INT64_MAX + 1;
        }
        else
        {
            uval = (uint64_t)(-val);
        }

        bi->digits[0] = (uint32_t)(uval & 0xFFFFFFFF);
        bi->digits[1] = (uint32_t)(uval >> 32);
        bi->len = (bi->digits[1] != 0) ? 2 : 1;
    }
    else
    {
        bi->sign = 0;
        bi->digits[0] = (uint32_t)(val & 0xFFFFFFFF);
        bi->digits[1] = (uint32_t)((uint64_t)val >> 32);
        bi->len = (bi->digits[1] != 0) ? 2 : 1;
    }

    return bi;
}

void *rt_bigint_from_str(rt_string str)
{
    if (!str)
        return NULL;

    const char *s = rt_string_cstr(str);
    if (!s)
        return NULL;

    int64_t slen = rt_str_len(str);
    if (slen == 0)
        return NULL;

    int sign = 0;
    int64_t i = 0;

    // Skip leading whitespace
    while (i < slen && (s[i] == ' ' || s[i] == '\t'))
        i++;

    // Check sign
    if (i < slen && s[i] == '-')
    {
        sign = 1;
        i++;
    }
    else if (i < slen && s[i] == '+')
    {
        i++;
    }

    // Check base
    int base = 10;
    if (i + 1 < slen && s[i] == '0')
    {
        if (s[i + 1] == 'x' || s[i + 1] == 'X')
        {
            base = 16;
            i += 2;
        }
        else if (s[i + 1] == 'b' || s[i + 1] == 'B')
        {
            base = 2;
            i += 2;
        }
        else if (s[i + 1] == 'o' || s[i + 1] == 'O')
        {
            base = 8;
            i += 2;
        }
    }

    bigint_t *result = bigint_alloc(4);
    if (!result)
        return NULL;

    result->len = 0;
    result->sign = 0;

    while (i < slen)
    {
        char c = s[i];
        int digit;

        if (c >= '0' && c <= '9')
        {
            digit = c - '0';
        }
        else if (c >= 'a' && c <= 'z')
        {
            digit = c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'Z')
        {
            digit = c - 'A' + 10;
        }
        else if (c == '_')
        {
            // Allow underscores as separators
            i++;
            continue;
        }
        else
        {
            break;
        }

        if (digit >= base)
            break;

        // result = result * base + digit
        uint64_t carry = (uint64_t)digit;
        for (int64_t j = 0; j < result->len; j++)
        {
            uint64_t prod = (uint64_t)result->digits[j] * (uint64_t)base + carry;
            result->digits[j] = (uint32_t)(prod & 0xFFFFFFFF);
            carry = prod >> 32;
        }

        while (carry > 0)
        {
            bigint_ensure_capacity(result, result->len + 1);
            result->digits[result->len] = (uint32_t)(carry & 0xFFFFFFFF);
            result->len++;
            carry >>= 32;
        }

        i++;
    }

    result->sign = (result->len > 0) ? sign : 0;
    bigint_normalize(result);
    return result;
}

void *rt_bigint_from_bytes(void *bytes)
{
    if (!bytes)
        return rt_bigint_zero();

    int64_t len = rt_bytes_len(bytes);
    if (len == 0)
        return rt_bigint_zero();

    // Get first byte to determine sign
    uint8_t first = (uint8_t)rt_bytes_get(bytes, 0);
    int sign = (first & 0x80) ? 1 : 0;

    // Find first significant byte
    int64_t start = 0;
    if (sign)
    {
        while (start < len - 1 && rt_bytes_get(bytes, start) == 0xFF)
            start++;
    }
    else
    {
        while (start < len - 1 && rt_bytes_get(bytes, start) == 0)
            start++;
    }

    int64_t significant_len = len - start;
    int64_t num_digits = (significant_len + 3) / 4;

    bigint_t *bi = bigint_alloc(num_digits + 1);
    if (!bi)
        return NULL;

    if (sign)
    {
        // Two's complement negative: invert and add 1
        uint64_t carry = 1;
        for (int64_t i = len - 1, d = 0; i >= 0; i -= 4)
        {
            uint32_t word = 0;
            for (int j = 0; j < 4 && i - j >= 0; j++)
            {
                uint8_t b = (uint8_t)rt_bytes_get(bytes, i - j);
                word |= ((uint32_t)(~b & 0xFF)) << (j * 8);
            }
            uint64_t sum = (uint64_t)word + carry;
            bigint_ensure_capacity(bi, d + 1);
            bi->digits[d] = (uint32_t)sum;
            carry = sum >> 32;
            d++;
            if (bi->len < d)
                bi->len = d;
        }
        bi->sign = 1;
    }
    else
    {
        // Positive: straightforward
        for (int64_t i = len - 1, d = 0; i >= 0; i -= 4)
        {
            uint32_t word = 0;
            for (int j = 0; j < 4 && i - j >= 0; j++)
            {
                uint8_t b = (uint8_t)rt_bytes_get(bytes, i - j);
                word |= ((uint32_t)b) << (j * 8);
            }
            bigint_ensure_capacity(bi, d + 1);
            bi->digits[d] = word;
            d++;
            if (bi->len < d)
                bi->len = d;
        }
        bi->sign = 0;
    }

    bigint_normalize(bi);
    return bi;
}

void *rt_bigint_zero(void)
{
    return rt_bigint_from_i64(0);
}

void *rt_bigint_one(void)
{
    return rt_bigint_from_i64(1);
}

//=============================================================================
// Conversion
//=============================================================================

int64_t rt_bigint_to_i64(void *a)
{
    if (!a)
        return 0;

    bigint_t *bi = (bigint_t *)a;

    if (bi->len == 0)
        return 0;

    uint64_t val = 0;
    for (int64_t i = bi->len - 1; i >= 0 && i < 2; i--)
    {
        val = (val << 32) | bi->digits[i];
    }

    if (bi->len > 2 || val > (uint64_t)INT64_MAX + (bi->sign ? 1ULL : 0ULL))
    {
        // Overflow - truncate
        if (bi->sign)
            return INT64_MIN;
        return INT64_MAX;
    }

    return bi->sign ? -(int64_t)val : (int64_t)val;
}

rt_string rt_bigint_to_str(void *a)
{
    return rt_bigint_to_str_base(a, 10);
}

rt_string rt_bigint_to_str_base(void *a, int64_t base)
{
    if (!a)
        return rt_string_from_bytes("0", 1);

    if (base < 2 || base > 36)
        base = 10;

    bigint_t *bi = (bigint_t *)a;

    if (bi->len == 0)
        return rt_string_from_bytes("0", 1);

    // Work with a copy
    bigint_t *tmp = bigint_clone(bi);
    if (!tmp)
        return rt_str_empty();

    // Estimate size: safe upper bound covering all bases (base 2 = 32 bits/limb)
    int64_t max_chars = bi->len * 33 + 4;
    char *buf = malloc((size_t)max_chars);
    int64_t pos = max_chars - 1;
    buf[pos--] = '\0';

    const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";

    while (tmp->len > 0)
    {
        uint64_t remainder = 0;
        for (int64_t i = tmp->len - 1; i >= 0; i--)
        {
            uint64_t cur = (remainder << 32) | tmp->digits[i];
            tmp->digits[i] = (uint32_t)(cur / (uint64_t)base);
            remainder = cur % (uint64_t)base;
        }
        buf[pos--] = digits[remainder];
        bigint_normalize(tmp);
    }

    if (bi->sign)
        buf[pos--] = '-';

    rt_string result = rt_string_from_bytes(buf + pos + 1, max_chars - pos - 2);
    free(buf);

    if (rt_obj_release_check0(tmp))
        rt_obj_free(tmp);

    return result;
}

void *rt_bigint_to_bytes(void *a)
{
    if (!a)
    {
        void *b = rt_bytes_new(1);
        rt_bytes_set(b, 0, 0);
        return b;
    }

    bigint_t *bi = (bigint_t *)a;

    if (bi->len == 0)
    {
        void *b = rt_bytes_new(1);
        rt_bytes_set(b, 0, 0);
        return b;
    }

    // Calculate byte length
    int64_t byte_len = bi->len * 4;
    // Trim leading zeros
    while (byte_len > 1)
    {
        int64_t idx = byte_len - 1;
        int64_t digit_idx = idx / 4;
        int64_t byte_idx = idx % 4;
        if (digit_idx >= bi->len)
            break;
        uint8_t b = (bi->digits[digit_idx] >> (byte_idx * 8)) & 0xFF;
        if (b != 0)
            break;
        byte_len--;
    }

    // Add sign byte if needed
    int need_sign = 0;
    {
        int64_t idx = byte_len - 1;
        int64_t digit_idx = idx / 4;
        int64_t byte_idx = idx % 4;
        uint8_t msb = 0;
        if (digit_idx < bi->len)
            msb = (bi->digits[digit_idx] >> (byte_idx * 8)) & 0xFF;
        if (bi->sign && !(msb & 0x80))
            need_sign = 1;
        if (!bi->sign && (msb & 0x80))
            need_sign = 1;
    }

    void *result = rt_bytes_new(byte_len + need_sign);

    if (bi->sign)
    {
        // Two's complement: invert and add 1
        uint32_t carry = 1;
        for (int64_t i = 0; i < byte_len; i++)
        {
            int64_t digit_idx = i / 4;
            int64_t byte_idx = i % 4;
            uint8_t b = 0;
            if (digit_idx < bi->len)
                b = (bi->digits[digit_idx] >> (byte_idx * 8)) & 0xFF;
            uint32_t inv = (~b) & 0xFF;
            uint32_t sum = inv + carry;
            rt_bytes_set(result, byte_len - 1 - i + need_sign, sum & 0xFF);
            carry = sum >> 8;
        }
        if (need_sign)
            rt_bytes_set(result, 0, 0xFF);
    }
    else
    {
        for (int64_t i = 0; i < byte_len; i++)
        {
            int64_t digit_idx = i / 4;
            int64_t byte_idx = i % 4;
            uint8_t b = 0;
            if (digit_idx < bi->len)
                b = (bi->digits[digit_idx] >> (byte_idx * 8)) & 0xFF;
            rt_bytes_set(result, byte_len - 1 - i + need_sign, b);
        }
        if (need_sign)
            rt_bytes_set(result, 0, 0x00);
    }

    return result;
}

int8_t rt_bigint_fits_i64(void *a)
{
    if (!a)
        return 1;

    bigint_t *bi = (bigint_t *)a;

    if (bi->len == 0)
        return 1;
    if (bi->len > 2)
        return 0;

    uint64_t val = bi->digits[0];
    if (bi->len == 2)
        val |= ((uint64_t)bi->digits[1]) << 32;

    uint64_t max = bi->sign ? ((uint64_t)INT64_MAX + 1) : (uint64_t)INT64_MAX;
    return val <= max ? 1 : 0;
}

//=============================================================================
// Internal Arithmetic Helpers
//=============================================================================

// Compare magnitudes (ignoring sign)
static int bigint_cmp_mag(bigint_t *a, bigint_t *b)
{
    if (a->len != b->len)
        return (a->len > b->len) ? 1 : -1;

    for (int64_t i = a->len - 1; i >= 0; i--)
    {
        if (a->digits[i] != b->digits[i])
            return (a->digits[i] > b->digits[i]) ? 1 : -1;
    }
    return 0;
}

// Add magnitudes (result = |a| + |b|)
static bigint_t *bigint_add_mag(bigint_t *a, bigint_t *b)
{
    int64_t max_len = (a->len > b->len) ? a->len : b->len;
    bigint_t *result = bigint_alloc(max_len + 1);
    if (!result)
        return NULL;

    uint64_t carry = 0;
    for (int64_t i = 0; i < max_len || carry; i++)
    {
        bigint_ensure_capacity(result, i + 1);
        uint64_t sum = carry;
        if (i < a->len)
            sum += a->digits[i];
        if (i < b->len)
            sum += b->digits[i];
        result->digits[i] = (uint32_t)(sum & 0xFFFFFFFF);
        carry = sum >> 32;
        if (result->len <= i)
            result->len = i + 1;
    }

    bigint_normalize(result);
    return result;
}

// Subtract magnitudes (result = |a| - |b|), assumes |a| >= |b|
static bigint_t *bigint_sub_mag(bigint_t *a, bigint_t *b)
{
    bigint_t *result = bigint_alloc(a->len);
    if (!result)
        return NULL;

    int64_t borrow = 0;
    for (int64_t i = 0; i < a->len; i++)
    {
        int64_t diff = (int64_t)a->digits[i] - borrow;
        if (i < b->len)
            diff -= b->digits[i];

        if (diff < 0)
        {
            diff += BIGINT_BASE;
            borrow = 1;
        }
        else
        {
            borrow = 0;
        }

        bigint_ensure_capacity(result, i + 1);
        result->digits[i] = (uint32_t)diff;
        if (result->len <= i)
            result->len = i + 1;
    }

    bigint_normalize(result);
    return result;
}

//=============================================================================
// Basic Arithmetic
//=============================================================================

void *rt_bigint_add(void *a, void *b)
{
    if (!a)
        return b ? bigint_clone((bigint_t *)b) : rt_bigint_zero();
    if (!b)
        return bigint_clone((bigint_t *)a);

    bigint_t *bi_a = (bigint_t *)a;
    bigint_t *bi_b = (bigint_t *)b;

    if (bi_a->sign == bi_b->sign)
    {
        // Same sign: add magnitudes
        bigint_t *result = bigint_add_mag(bi_a, bi_b);
        if (result)
            result->sign = bi_a->sign;
        return result;
    }
    else
    {
        // Different signs: subtract magnitudes
        int cmp = bigint_cmp_mag(bi_a, bi_b);
        if (cmp == 0)
            return rt_bigint_zero();

        bigint_t *result;
        if (cmp > 0)
        {
            result = bigint_sub_mag(bi_a, bi_b);
            if (result)
                result->sign = bi_a->sign;
        }
        else
        {
            result = bigint_sub_mag(bi_b, bi_a);
            if (result)
                result->sign = bi_b->sign;
        }
        return result;
    }
}

void *rt_bigint_sub(void *a, void *b)
{
    if (!b)
        return a ? bigint_clone((bigint_t *)a) : rt_bigint_zero();

    bigint_t *bi_b = (bigint_t *)b;

    // Negate b and add
    bigint_t neg_b = *bi_b;
    neg_b.sign = bi_b->sign ? 0 : 1;
    if (neg_b.len == 0)
        neg_b.sign = 0;

    return rt_bigint_add(a, &neg_b);
}

void *rt_bigint_mul(void *a, void *b)
{
    if (!a || !b)
        return rt_bigint_zero();

    bigint_t *bi_a = (bigint_t *)a;
    bigint_t *bi_b = (bigint_t *)b;

    if (bi_a->len == 0 || bi_b->len == 0)
        return rt_bigint_zero();

    bigint_t *result = bigint_alloc(bi_a->len + bi_b->len);
    if (!result)
        return NULL;

    result->len = bi_a->len + bi_b->len;
    memset(result->digits, 0, (size_t)result->len * sizeof(uint32_t));

    for (int64_t i = 0; i < bi_a->len; i++)
    {
        uint64_t carry = 0;
        for (int64_t j = 0; j < bi_b->len || carry; j++)
        {
            uint64_t prod = result->digits[i + j] + carry;
            if (j < bi_b->len)
                prod += (uint64_t)bi_a->digits[i] * bi_b->digits[j];
            result->digits[i + j] = (uint32_t)(prod & 0xFFFFFFFF);
            carry = prod >> 32;
        }
    }

    result->sign = (bi_a->sign != bi_b->sign) ? 1 : 0;
    bigint_normalize(result);
    return result;
}

void *rt_bigint_divmod(void *a, void *b, void **remainder)
{
    if (!b)
    {
        rt_trap("BigInt division by zero");
        return NULL;
    }

    bigint_t *bi_b = (bigint_t *)b;
    if (bi_b->len == 0)
    {
        rt_trap("BigInt division by zero");
        return NULL;
    }

    if (!a)
    {
        if (remainder)
            *remainder = rt_bigint_zero();
        return rt_bigint_zero();
    }

    bigint_t *bi_a = (bigint_t *)a;
    if (bi_a->len == 0)
    {
        if (remainder)
            *remainder = rt_bigint_zero();
        return rt_bigint_zero();
    }

    int cmp = bigint_cmp_mag(bi_a, bi_b);
    if (cmp < 0)
    {
        // |a| < |b|: quotient = 0, remainder = a
        if (remainder)
            *remainder = bigint_clone(bi_a);
        return rt_bigint_zero();
    }

    if (cmp == 0)
    {
        // |a| == |b|: quotient = +-1, remainder = 0
        if (remainder)
            *remainder = rt_bigint_zero();
        bigint_t *q = (bigint_t *)rt_bigint_one();
        q->sign = (bi_a->sign != bi_b->sign) ? 1 : 0;
        return q;
    }

    // Simple long division for single-digit divisor
    if (bi_b->len == 1)
    {
        bigint_t *quot = bigint_alloc(bi_a->len);
        if (!quot)
            return NULL;

        uint64_t divisor = bi_b->digits[0];
        uint64_t rem = 0;

        for (int64_t i = bi_a->len - 1; i >= 0; i--)
        {
            uint64_t cur = (rem << 32) | bi_a->digits[i];
            quot->digits[i] = (uint32_t)(cur / divisor);
            rem = cur % divisor;
        }
        quot->len = bi_a->len;
        quot->sign = (bi_a->sign != bi_b->sign) ? 1 : 0;
        bigint_normalize(quot);

        if (remainder)
        {
            bigint_t *r = (bigint_t *)rt_bigint_from_i64((int64_t)rem);
            r->sign = bi_a->sign;
            bigint_normalize(r);
            *remainder = r;
        }

        return quot;
    }

    // General case: Knuth Algorithm D (simplified)
    // Make copies to work with
    int64_t n = bi_b->len;
    int64_t m = bi_a->len - n;

    bigint_t *quot = bigint_alloc(m + 1);
    bigint_t *rem = bigint_clone(bi_a);
    if (!quot || !rem)
    {
        if (quot)
        {
            if (rt_obj_release_check0(quot))
                rt_obj_free(quot);
        }
        if (rem)
        {
            if (rt_obj_release_check0(rem))
                rt_obj_free(rem);
        }
        return NULL;
    }

    // Normalize
    int shift = 0;
    uint32_t high = bi_b->digits[n - 1];
    while ((high & 0x80000000) == 0)
    {
        high <<= 1;
        shift++;
    }

    // Left shift both numbers
    if (shift > 0)
    {
        bigint_ensure_capacity(rem, rem->len + 1);
        uint32_t carry = 0;
        for (int64_t i = 0; i < rem->len; i++)
        {
            uint64_t val = ((uint64_t)rem->digits[i] << shift) | carry;
            rem->digits[i] = (uint32_t)(val & 0xFFFFFFFF);
            carry = (uint32_t)(val >> 32);
        }
        if (carry)
        {
            rem->digits[rem->len] = carry;
            rem->len++;
        }

        // Create shifted divisor
        bigint_t *d = bigint_clone(bi_b);
        carry = 0;
        for (int64_t i = 0; i < d->len; i++)
        {
            uint64_t val = ((uint64_t)d->digits[i] << shift) | carry;
            d->digits[i] = (uint32_t)(val & 0xFFFFFFFF);
            carry = (uint32_t)(val >> 32);
        }
        if (carry)
        {
            bigint_ensure_capacity(d, d->len + 1);
            d->digits[d->len] = carry;
            d->len++;
        }

        // Division loop
        for (int64_t j = m; j >= 0; j--)
        {
            // Estimate quotient digit
            uint64_t qhat;
            int64_t idx = j + n;
            uint64_t num = 0;
            if (idx < rem->len)
                num = rem->digits[idx];
            num = (num << 32);
            if (idx - 1 >= 0 && idx - 1 < rem->len)
                num |= rem->digits[idx - 1];

            qhat = num / d->digits[n - 1];
            if (qhat > 0xFFFFFFFF)
                qhat = 0xFFFFFFFF;

            // Multiply and subtract
            int64_t borrow = 0;
            for (int64_t i = 0; i < n; i++)
            {
                uint64_t prod = qhat * d->digits[i];
                int64_t diff = 0;
                if (j + i < rem->len)
                    diff = rem->digits[j + i];
                diff = diff - (prod & 0xFFFFFFFF) - borrow;
                if (diff < 0)
                {
                    diff += BIGINT_BASE;
                    borrow = (prod >> 32) + 1;
                }
                else
                {
                    borrow = prod >> 32;
                }
                if (j + i < rem->len)
                    rem->digits[j + i] = (uint32_t)diff;
            }

            if (j + n < rem->len)
            {
                int64_t diff = rem->digits[j + n] - borrow;
                if (diff < 0)
                {
                    // qhat was too big, add back
                    qhat--;
                    uint64_t carry = 0;
                    for (int64_t i = 0; i < n; i++)
                    {
                        uint64_t sum =
                            (j + i < rem->len ? rem->digits[j + i] : 0) + d->digits[i] + carry;
                        if (j + i < rem->len)
                            rem->digits[j + i] = (uint32_t)(sum & 0xFFFFFFFF);
                        carry = sum >> 32;
                    }
                    rem->digits[j + n] = 0;
                }
                else
                {
                    rem->digits[j + n] = (uint32_t)diff;
                }
            }

            bigint_ensure_capacity(quot, j + 1);
            quot->digits[j] = (uint32_t)qhat;
            if (quot->len <= j)
                quot->len = j + 1;
        }

        // Right shift remainder
        if (shift > 0)
        {
            uint32_t carry = 0;
            for (int64_t i = rem->len - 1; i >= 0; i--)
            {
                uint64_t val = ((uint64_t)carry << 32) | rem->digits[i];
                rem->digits[i] = (uint32_t)(val >> shift);
                carry = (uint32_t)(val & ((1 << shift) - 1));
            }
        }

        if (rt_obj_release_check0(d))
            rt_obj_free(d);
    }
    else
    {
        // No shift needed - simplified division
        for (int64_t j = m; j >= 0; j--)
        {
            uint64_t qhat;
            int64_t idx = j + n;
            uint64_t num = 0;
            if (idx < rem->len)
                num = rem->digits[idx];
            num = (num << 32);
            if (idx - 1 >= 0 && idx - 1 < rem->len)
                num |= rem->digits[idx - 1];

            qhat = num / bi_b->digits[n - 1];
            if (qhat > 0xFFFFFFFF)
                qhat = 0xFFFFFFFF;

            int64_t borrow = 0;
            for (int64_t i = 0; i < n; i++)
            {
                uint64_t prod = qhat * bi_b->digits[i];
                int64_t diff = 0;
                if (j + i < rem->len)
                    diff = rem->digits[j + i];
                diff = diff - (prod & 0xFFFFFFFF) - borrow;
                if (diff < 0)
                {
                    diff += BIGINT_BASE;
                    borrow = (prod >> 32) + 1;
                }
                else
                {
                    borrow = prod >> 32;
                }
                if (j + i < rem->len)
                    rem->digits[j + i] = (uint32_t)diff;
            }

            if (j + n < rem->len)
            {
                int64_t diff = rem->digits[j + n] - borrow;
                if (diff < 0)
                {
                    qhat--;
                    uint64_t carry = 0;
                    for (int64_t i = 0; i < n; i++)
                    {
                        uint64_t sum =
                            (j + i < rem->len ? rem->digits[j + i] : 0) + bi_b->digits[i] + carry;
                        if (j + i < rem->len)
                            rem->digits[j + i] = (uint32_t)(sum & 0xFFFFFFFF);
                        carry = sum >> 32;
                    }
                    rem->digits[j + n] = 0;
                }
                else
                {
                    rem->digits[j + n] = (uint32_t)diff;
                }
            }

            bigint_ensure_capacity(quot, j + 1);
            quot->digits[j] = (uint32_t)qhat;
            if (quot->len <= j)
                quot->len = j + 1;
        }
    }

    quot->sign = (bi_a->sign != bi_b->sign) ? 1 : 0;
    rem->sign = bi_a->sign;
    bigint_normalize(quot);
    bigint_normalize(rem);

    if (remainder)
        *remainder = rem;
    else if (rt_obj_release_check0(rem))
        rt_obj_free(rem);

    return quot;
}

void *rt_bigint_div(void *a, void *b)
{
    return rt_bigint_divmod(a, b, NULL);
}

void *rt_bigint_mod(void *a, void *b)
{
    void *rem = NULL;
    void *quot = rt_bigint_divmod(a, b, &rem);
    if (quot)
    {
        if (rt_obj_release_check0(quot))
            rt_obj_free(quot);
    }
    return rem;
}

void *rt_bigint_neg(void *a)
{
    if (!a)
        return rt_bigint_zero();

    bigint_t *result = bigint_clone((bigint_t *)a);
    if (!result)
        return NULL;

    if (result->len > 0)
        result->sign = result->sign ? 0 : 1;
    return result;
}

void *rt_bigint_abs(void *a)
{
    if (!a)
        return rt_bigint_zero();

    bigint_t *result = bigint_clone((bigint_t *)a);
    if (!result)
        return NULL;

    result->sign = 0;
    return result;
}

//=============================================================================
// Comparison
//=============================================================================

int64_t rt_bigint_cmp(void *a, void *b)
{
    if (!a && !b)
        return 0;
    if (!a)
        return rt_bigint_is_negative(b) ? 1 : -1;
    if (!b)
        return rt_bigint_is_negative(a) ? -1 : 1;

    bigint_t *bi_a = (bigint_t *)a;
    bigint_t *bi_b = (bigint_t *)b;

    // Check signs first
    if (bi_a->sign != bi_b->sign)
    {
        if (bi_a->len == 0 && bi_b->len == 0)
            return 0;
        return bi_a->sign ? -1 : 1;
    }

    // Same sign
    int mag_cmp = bigint_cmp_mag(bi_a, bi_b);
    return bi_a->sign ? -mag_cmp : mag_cmp;
}

int8_t rt_bigint_eq(void *a, void *b)
{
    return rt_bigint_cmp(a, b) == 0 ? 1 : 0;
}

int8_t rt_bigint_is_zero(void *a)
{
    if (!a)
        return 1;
    return ((bigint_t *)a)->len == 0 ? 1 : 0;
}

int8_t rt_bigint_is_negative(void *a)
{
    if (!a)
        return 0;
    bigint_t *bi = (bigint_t *)a;
    return (bi->len > 0 && bi->sign) ? 1 : 0;
}

int64_t rt_bigint_sign(void *a)
{
    if (!a)
        return 0;
    bigint_t *bi = (bigint_t *)a;
    if (bi->len == 0)
        return 0;
    return bi->sign ? -1 : 1;
}

//=============================================================================
// Bitwise Operations
//=============================================================================

void *rt_bigint_and(void *a, void *b)
{
    if (!a || !b)
        return rt_bigint_zero();

    bigint_t *bi_a = (bigint_t *)a;
    bigint_t *bi_b = (bigint_t *)b;

    // For simplicity, handle non-negative only
    // Negative numbers would need two's complement representation
    if (bi_a->sign || bi_b->sign)
    {
        if (rt_bigint_fits_i64(a) && rt_bigint_fits_i64(b))
            return rt_bigint_from_i64(rt_bigint_to_i64(a) & rt_bigint_to_i64(b));
        return rt_bigint_zero();
    }

    int64_t min_len = (bi_a->len < bi_b->len) ? bi_a->len : bi_b->len;
    bigint_t *result = bigint_alloc(min_len);
    if (!result)
        return NULL;

    for (int64_t i = 0; i < min_len; i++)
    {
        result->digits[i] = bi_a->digits[i] & bi_b->digits[i];
    }
    result->len = min_len;
    result->sign = 0;

    bigint_normalize(result);
    return result;
}

void *rt_bigint_or(void *a, void *b)
{
    if (!a)
        return b ? bigint_clone((bigint_t *)b) : rt_bigint_zero();
    if (!b)
        return bigint_clone((bigint_t *)a);

    bigint_t *bi_a = (bigint_t *)a;
    bigint_t *bi_b = (bigint_t *)b;

    if (bi_a->sign || bi_b->sign)
    {
        if (rt_bigint_fits_i64(a) && rt_bigint_fits_i64(b))
            return rt_bigint_from_i64(rt_bigint_to_i64(a) | rt_bigint_to_i64(b));
        return rt_bigint_zero();
    }

    int64_t max_len = (bi_a->len > bi_b->len) ? bi_a->len : bi_b->len;
    bigint_t *result = bigint_alloc(max_len);
    if (!result)
        return NULL;

    for (int64_t i = 0; i < max_len; i++)
    {
        uint32_t da = (i < bi_a->len) ? bi_a->digits[i] : 0;
        uint32_t db = (i < bi_b->len) ? bi_b->digits[i] : 0;
        result->digits[i] = da | db;
    }
    result->len = max_len;
    result->sign = 0;

    bigint_normalize(result);
    return result;
}

void *rt_bigint_xor(void *a, void *b)
{
    if (!a)
        return b ? bigint_clone((bigint_t *)b) : rt_bigint_zero();
    if (!b)
        return bigint_clone((bigint_t *)a);

    bigint_t *bi_a = (bigint_t *)a;
    bigint_t *bi_b = (bigint_t *)b;

    if (bi_a->sign || bi_b->sign)
    {
        if (rt_bigint_fits_i64(a) && rt_bigint_fits_i64(b))
            return rt_bigint_from_i64(rt_bigint_to_i64(a) ^ rt_bigint_to_i64(b));
        return rt_bigint_zero();
    }

    int64_t max_len = (bi_a->len > bi_b->len) ? bi_a->len : bi_b->len;
    bigint_t *result = bigint_alloc(max_len);
    if (!result)
        return NULL;

    for (int64_t i = 0; i < max_len; i++)
    {
        uint32_t da = (i < bi_a->len) ? bi_a->digits[i] : 0;
        uint32_t db = (i < bi_b->len) ? bi_b->digits[i] : 0;
        result->digits[i] = da ^ db;
    }
    result->len = max_len;
    result->sign = 0;

    bigint_normalize(result);
    return result;
}

void *rt_bigint_not(void *a)
{
    // For arbitrary precision, NOT doesn't have fixed meaning
    // Return -(a + 1) as two's complement would
    void *one = rt_bigint_one();
    void *sum = rt_bigint_add(a, one);
    void *result = rt_bigint_neg(sum);

    if (rt_obj_release_check0(one))
        rt_obj_free(one);
    if (rt_obj_release_check0(sum))
        rt_obj_free(sum);

    return result;
}

void *rt_bigint_shl(void *a, int64_t n)
{
    if (!a || n <= 0)
        return a ? bigint_clone((bigint_t *)a) : rt_bigint_zero();

    bigint_t *bi = (bigint_t *)a;
    if (bi->len == 0)
        return rt_bigint_zero();

    int64_t word_shift = n / 32;
    int bit_shift = (int)(n % 32);

    int64_t new_len = bi->len + word_shift + 1;
    bigint_t *result = bigint_alloc(new_len);
    if (!result)
        return NULL;

    result->len = new_len;
    result->sign = bi->sign;

    // Word shift
    for (int64_t i = 0; i < word_shift; i++)
        result->digits[i] = 0;

    // Bit shift
    uint32_t carry = 0;
    for (int64_t i = 0; i < bi->len; i++)
    {
        uint64_t val = ((uint64_t)bi->digits[i] << bit_shift) | carry;
        result->digits[i + word_shift] = (uint32_t)(val & 0xFFFFFFFF);
        carry = (uint32_t)(val >> 32);
    }
    result->digits[bi->len + word_shift] = carry;

    bigint_normalize(result);
    return result;
}

void *rt_bigint_shr(void *a, int64_t n)
{
    if (!a || n <= 0)
        return a ? bigint_clone((bigint_t *)a) : rt_bigint_zero();

    bigint_t *bi = (bigint_t *)a;
    if (bi->len == 0)
        return rt_bigint_zero();

    int64_t word_shift = n / 32;
    int bit_shift = (int)(n % 32);

    if (word_shift >= bi->len)
    {
        // All bits shifted out
        return bi->sign ? rt_bigint_from_i64(-1) : rt_bigint_zero();
    }

    int64_t new_len = bi->len - word_shift;
    bigint_t *result = bigint_alloc(new_len);
    if (!result)
        return NULL;

    result->len = new_len;
    result->sign = bi->sign;

    // Bit shift
    uint32_t carry = 0;
    for (int64_t i = new_len - 1; i >= 0; i--)
    {
        uint64_t val = ((uint64_t)carry << 32) | bi->digits[i + word_shift];
        result->digits[i] = (uint32_t)(val >> bit_shift);
        carry = (uint32_t)(val & ((1ULL << bit_shift) - 1));
    }

    bigint_normalize(result);
    return result;
}

//=============================================================================
// Advanced Operations
//=============================================================================

void *rt_bigint_pow(void *a, int64_t n)
{
    if (n < 0)
    {
        rt_trap("BigInt.Pow: negative exponent");
        return NULL;
    }

    if (n == 0)
        return rt_bigint_one();
    if (!a)
        return rt_bigint_zero();

    bigint_t *bi = (bigint_t *)a;
    if (bi->len == 0)
        return rt_bigint_zero();

    // Binary exponentiation
    void *result = rt_bigint_one();
    void *base = bigint_clone(bi);

    while (n > 0)
    {
        if (n & 1)
        {
            void *tmp = rt_bigint_mul(result, base);
            if (rt_obj_release_check0(result))
                rt_obj_free(result);
            result = tmp;
        }
        n >>= 1;
        if (n > 0)
        {
            void *tmp = rt_bigint_mul(base, base);
            if (rt_obj_release_check0(base))
                rt_obj_free(base);
            base = tmp;
        }
    }

    if (rt_obj_release_check0(base))
        rt_obj_free(base);
    return result;
}

void *rt_bigint_pow_mod(void *a, void *n, void *m)
{
    if (!m || rt_bigint_is_zero(m))
    {
        rt_trap("BigInt.PowMod: zero modulus");
        return NULL;
    }

    if (!n || rt_bigint_is_zero(n))
        return rt_bigint_one();
    if (!a || rt_bigint_is_zero(a))
        return rt_bigint_zero();

    /* S-23: Montgomery ladder — always executes exactly 2 modular multiplications
     * per exponent bit (MSB to LSB), preventing timing-based exponent recovery.
     * Invariant: r1 - r0 = base^(2^k) at each step.
     * if bit==1: r0 = r0*r1 mod m;  r1 = r1^2 mod m
     * if bit==0: r1 = r0*r1 mod m;  r0 = r0^2 mod m */
    int64_t nbits = rt_bigint_bit_length(n);

    void *r0 = rt_bigint_one();
    void *r1 = rt_bigint_mod(a, m);

    for (int64_t i = nbits - 1; i >= 0; i--)
    {
        int8_t bit = rt_bigint_test_bit(n, i);

        void *cross = rt_bigint_mul(r0, r1);
        void *cross_m = rt_bigint_mod(cross, m);
        if (rt_obj_release_check0(cross))
            rt_obj_free(cross);

        void *sq0 = rt_bigint_mul(r0, r0);
        void *sq0_m = rt_bigint_mod(sq0, m);
        if (rt_obj_release_check0(sq0))
            rt_obj_free(sq0);

        void *sq1 = rt_bigint_mul(r1, r1);
        void *sq1_m = rt_bigint_mod(sq1, m);
        if (rt_obj_release_check0(sq1))
            rt_obj_free(sq1);

        if (rt_obj_release_check0(r0))
            rt_obj_free(r0);
        if (rt_obj_release_check0(r1))
            rt_obj_free(r1);

        if (bit)
        {
            r0 = cross_m;
            r1 = sq1_m;
            if (rt_obj_release_check0(sq0_m))
                rt_obj_free(sq0_m);
        }
        else
        {
            r1 = cross_m;
            r0 = sq0_m;
            if (rt_obj_release_check0(sq1_m))
                rt_obj_free(sq1_m);
        }
    }

    if (rt_obj_release_check0(r1))
        rt_obj_free(r1);

    return r0;
}

void *rt_bigint_gcd(void *a, void *b)
{
    if (!a)
        return b ? rt_bigint_abs(b) : rt_bigint_zero();
    if (!b)
        return rt_bigint_abs(a);

    void *x = rt_bigint_abs(a);
    void *y = rt_bigint_abs(b);

    // Binary GCD algorithm
    while (!rt_bigint_is_zero(y))
    {
        void *rem = rt_bigint_mod(x, y);
        if (rt_obj_release_check0(x))
            rt_obj_free(x);
        x = y;
        y = rem;
    }

    if (rt_obj_release_check0(y))
        rt_obj_free(y);
    return x;
}

void *rt_bigint_lcm(void *a, void *b)
{
    if (!a || !b)
        return rt_bigint_zero();

    void *gcd = rt_bigint_gcd(a, b);
    if (rt_bigint_is_zero(gcd))
    {
        if (rt_obj_release_check0(gcd))
            rt_obj_free(gcd);
        return rt_bigint_zero();
    }

    void *prod = rt_bigint_mul(a, b);
    void *abs_prod = rt_bigint_abs(prod);
    if (rt_obj_release_check0(prod))
        rt_obj_free(prod);

    void *result = rt_bigint_div(abs_prod, gcd);
    if (rt_obj_release_check0(abs_prod))
        rt_obj_free(abs_prod);
    if (rt_obj_release_check0(gcd))
        rt_obj_free(gcd);

    return result;
}

int64_t rt_bigint_bit_length(void *a)
{
    if (!a)
        return 0;

    bigint_t *bi = (bigint_t *)a;
    if (bi->len == 0)
        return 0;

    int64_t bits = (bi->len - 1) * 32;
    uint32_t high = bi->digits[bi->len - 1];

    while (high > 0)
    {
        bits++;
        high >>= 1;
    }

    return bits;
}

int8_t rt_bigint_test_bit(void *a, int64_t n)
{
    if (!a || n < 0)
        return 0;

    bigint_t *bi = (bigint_t *)a;
    int64_t word = n / 32;
    int bit = (int)(n % 32);

    if (word >= bi->len)
        return 0;

    return (bi->digits[word] >> bit) & 1 ? 1 : 0;
}

void *rt_bigint_set_bit(void *a, int64_t n)
{
    if (n < 0)
        return a ? bigint_clone((bigint_t *)a) : rt_bigint_zero();

    bigint_t *bi = a ? (bigint_t *)a : NULL;
    int64_t word = n / 32;
    int bit = (int)(n % 32);

    int64_t new_len = word + 1;
    if (bi && bi->len > new_len)
        new_len = bi->len;

    bigint_t *result = bigint_alloc(new_len);
    if (!result)
        return NULL;

    if (bi)
    {
        memcpy(result->digits, bi->digits, (size_t)bi->len * sizeof(uint32_t));
        result->len = bi->len;
        result->sign = bi->sign;
    }

    bigint_ensure_capacity(result, word + 1);
    if (result->len <= word)
    {
        memset(
            result->digits + result->len, 0, (size_t)(word + 1 - result->len) * sizeof(uint32_t));
        result->len = word + 1;
    }

    result->digits[word] |= (1U << bit);
    bigint_normalize(result);
    return result;
}

void *rt_bigint_clear_bit(void *a, int64_t n)
{
    if (!a || n < 0)
        return a ? bigint_clone((bigint_t *)a) : rt_bigint_zero();

    bigint_t *bi = (bigint_t *)a;
    int64_t word = n / 32;
    int bit = (int)(n % 32);

    if (word >= bi->len)
        return bigint_clone(bi);

    bigint_t *result = bigint_clone(bi);
    if (!result)
        return NULL;

    result->digits[word] &= ~(1U << bit);
    bigint_normalize(result);
    return result;
}

void *rt_bigint_sqrt(void *a)
{
    if (!a)
        return rt_bigint_zero();

    bigint_t *bi = (bigint_t *)a;
    if (bi->sign)
    {
        rt_trap("BigInt.Sqrt: negative input");
        return NULL;
    }

    if (bi->len == 0)
        return rt_bigint_zero();

    // Newton's method
    int64_t bits = rt_bigint_bit_length(a);
    void *x = rt_bigint_shl(rt_bigint_one(), (bits + 1) / 2);

    while (1)
    {
        void *q = rt_bigint_div(a, x);
        void *sum = rt_bigint_add(x, q);
        void *next = rt_bigint_shr(sum, 1);

        if (rt_obj_release_check0(q))
            rt_obj_free(q);
        if (rt_obj_release_check0(sum))
            rt_obj_free(sum);

        if (rt_bigint_cmp(next, x) >= 0)
        {
            if (rt_obj_release_check0(next))
                rt_obj_free(next);
            break;
        }

        if (rt_obj_release_check0(x))
            rt_obj_free(x);
        x = next;
    }

    return x;
}
