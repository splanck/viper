//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_bitset.c
// Purpose: Implement an arbitrary-size bit array backed by a uint64_t array.
//          Provides set operations (AND, OR, XOR, NOT) and individual bit
//          manipulation with automatic growth.
// Structure: [vptr | words | word_count | bit_count]
//
//===----------------------------------------------------------------------===//

#include "rt_bitset.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

/// Bits per word.
#define BITS_PER_WORD 64

/// Convert bit count to word count (ceiling division).
#define WORDS_FOR_BITS(n) (((n) + BITS_PER_WORD - 1) / BITS_PER_WORD)

/// @brief BitSet implementation structure.
typedef struct rt_bitset_impl
{
    void **vptr;        ///< Vtable pointer placeholder.
    uint64_t *words;    ///< Array of 64-bit words storing the bits.
    size_t word_count;  ///< Number of words allocated.
    size_t bit_count;   ///< Logical number of bits.
} rt_bitset_impl;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Popcount for a 64-bit word.
static int popcount64(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(x);
#else
    // Hamming weight
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (int)((x * 0x0101010101010101ULL) >> 56);
#endif
}

/// Grow the bitset to accommodate at least `min_bits` bits.
static void bitset_grow(rt_bitset_impl *bs, size_t min_bits)
{
    size_t new_word_count = WORDS_FOR_BITS(min_bits);
    if (new_word_count <= bs->word_count)
    {
        if (min_bits > bs->bit_count)
            bs->bit_count = min_bits;
        return;
    }

    // Double or use min, whichever is larger
    size_t grow = bs->word_count * 2;
    if (grow < new_word_count)
        grow = new_word_count;

    uint64_t *new_words = (uint64_t *)realloc(bs->words, grow * sizeof(uint64_t));
    if (!new_words)
        return;

    // Zero new words
    memset(new_words + bs->word_count, 0,
           (grow - bs->word_count) * sizeof(uint64_t));

    bs->words = new_words;
    bs->word_count = grow;
    bs->bit_count = min_bits;
}

// ---------------------------------------------------------------------------
// Finalizer
// ---------------------------------------------------------------------------

static void rt_bitset_finalize(void *obj)
{
    if (!obj)
        return;
    rt_bitset_impl *bs = (rt_bitset_impl *)obj;
    free(bs->words);
    bs->words = NULL;
    bs->word_count = 0;
    bs->bit_count = 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void *rt_bitset_new(int64_t nbits)
{
    if (nbits <= 0)
        nbits = 64; // Default to 64 bits

    rt_bitset_impl *bs = (rt_bitset_impl *)rt_obj_new_i64(
        0, (int64_t)sizeof(rt_bitset_impl));
    if (!bs)
        return NULL;

    bs->vptr = NULL;
    size_t wc = WORDS_FOR_BITS((size_t)nbits);
    bs->words = (uint64_t *)calloc(wc, sizeof(uint64_t));
    if (!bs->words)
    {
        bs->word_count = 0;
        bs->bit_count = 0;
        rt_obj_set_finalizer(bs, rt_bitset_finalize);
        return bs;
    }
    bs->word_count = wc;
    bs->bit_count = (size_t)nbits;
    rt_obj_set_finalizer(bs, rt_bitset_finalize);
    return bs;
}

int64_t rt_bitset_len(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_bitset_impl *)obj)->bit_count;
}

int64_t rt_bitset_count(void *obj)
{
    if (!obj)
        return 0;
    rt_bitset_impl *bs = (rt_bitset_impl *)obj;
    int64_t total = 0;
    for (size_t i = 0; i < bs->word_count; ++i)
        total += popcount64(bs->words[i]);
    return total;
}

int8_t rt_bitset_is_empty(void *obj)
{
    return rt_bitset_count(obj) == 0;
}

int8_t rt_bitset_get(void *obj, int64_t idx)
{
    if (!obj || idx < 0)
        return 0;
    rt_bitset_impl *bs = (rt_bitset_impl *)obj;
    if ((size_t)idx >= bs->bit_count)
        return 0;
    size_t w = (size_t)idx / BITS_PER_WORD;
    size_t b = (size_t)idx % BITS_PER_WORD;
    return (bs->words[w] >> b) & 1;
}

void rt_bitset_set(void *obj, int64_t idx)
{
    if (!obj || idx < 0)
        return;
    rt_bitset_impl *bs = (rt_bitset_impl *)obj;
    if ((size_t)idx >= bs->bit_count)
        bitset_grow(bs, (size_t)idx + 1);
    size_t w = (size_t)idx / BITS_PER_WORD;
    size_t b = (size_t)idx % BITS_PER_WORD;
    if (w < bs->word_count)
        bs->words[w] |= (1ULL << b);
}

void rt_bitset_clear(void *obj, int64_t idx)
{
    if (!obj || idx < 0)
        return;
    rt_bitset_impl *bs = (rt_bitset_impl *)obj;
    if ((size_t)idx >= bs->bit_count)
        return; // Nothing to clear
    size_t w = (size_t)idx / BITS_PER_WORD;
    size_t b = (size_t)idx % BITS_PER_WORD;
    bs->words[w] &= ~(1ULL << b);
}

void rt_bitset_toggle(void *obj, int64_t idx)
{
    if (!obj || idx < 0)
        return;
    rt_bitset_impl *bs = (rt_bitset_impl *)obj;
    if ((size_t)idx >= bs->bit_count)
        bitset_grow(bs, (size_t)idx + 1);
    size_t w = (size_t)idx / BITS_PER_WORD;
    size_t b = (size_t)idx % BITS_PER_WORD;
    if (w < bs->word_count)
        bs->words[w] ^= (1ULL << b);
}

void rt_bitset_set_all(void *obj)
{
    if (!obj)
        return;
    rt_bitset_impl *bs = (rt_bitset_impl *)obj;
    if (bs->word_count == 0)
        return;
    memset(bs->words, 0xFF, bs->word_count * sizeof(uint64_t));
    // Mask off excess bits in the last word
    size_t extra = bs->bit_count % BITS_PER_WORD;
    if (extra > 0)
        bs->words[bs->word_count - 1] &= (1ULL << extra) - 1;
}

void rt_bitset_clear_all(void *obj)
{
    if (!obj)
        return;
    rt_bitset_impl *bs = (rt_bitset_impl *)obj;
    if (bs->word_count == 0)
        return;
    memset(bs->words, 0, bs->word_count * sizeof(uint64_t));
}

void *rt_bitset_and(void *a, void *b)
{
    if (!a || !b)
        return rt_bitset_new(64);

    rt_bitset_impl *ba = (rt_bitset_impl *)a;
    rt_bitset_impl *bb = (rt_bitset_impl *)b;
    size_t max_bits = ba->bit_count > bb->bit_count ? ba->bit_count : bb->bit_count;

    void *result = rt_bitset_new((int64_t)max_bits);
    if (!result)
        return NULL;

    rt_bitset_impl *br = (rt_bitset_impl *)result;
    size_t min_words = ba->word_count < bb->word_count ? ba->word_count : bb->word_count;
    for (size_t i = 0; i < min_words; ++i)
        br->words[i] = ba->words[i] & bb->words[i];
    // Remaining words stay 0 (AND with 0 = 0)

    return result;
}

void *rt_bitset_or(void *a, void *b)
{
    if (!a || !b)
        return rt_bitset_new(64);

    rt_bitset_impl *ba = (rt_bitset_impl *)a;
    rt_bitset_impl *bb = (rt_bitset_impl *)b;
    size_t max_bits = ba->bit_count > bb->bit_count ? ba->bit_count : bb->bit_count;

    void *result = rt_bitset_new((int64_t)max_bits);
    if (!result)
        return NULL;

    rt_bitset_impl *br = (rt_bitset_impl *)result;
    size_t min_words = ba->word_count < bb->word_count ? ba->word_count : bb->word_count;
    size_t i = 0;
    for (; i < min_words; ++i)
        br->words[i] = ba->words[i] | bb->words[i];
    // Copy remaining from the longer one
    rt_bitset_impl *longer = ba->word_count > bb->word_count ? ba : bb;
    for (; i < longer->word_count && i < br->word_count; ++i)
        br->words[i] = longer->words[i];

    return result;
}

void *rt_bitset_xor(void *a, void *b)
{
    if (!a || !b)
        return rt_bitset_new(64);

    rt_bitset_impl *ba = (rt_bitset_impl *)a;
    rt_bitset_impl *bb = (rt_bitset_impl *)b;
    size_t max_bits = ba->bit_count > bb->bit_count ? ba->bit_count : bb->bit_count;

    void *result = rt_bitset_new((int64_t)max_bits);
    if (!result)
        return NULL;

    rt_bitset_impl *br = (rt_bitset_impl *)result;
    size_t min_words = ba->word_count < bb->word_count ? ba->word_count : bb->word_count;
    size_t i = 0;
    for (; i < min_words; ++i)
        br->words[i] = ba->words[i] ^ bb->words[i];
    // XOR with 0 = copy
    rt_bitset_impl *longer = ba->word_count > bb->word_count ? ba : bb;
    for (; i < longer->word_count && i < br->word_count; ++i)
        br->words[i] = longer->words[i];

    return result;
}

void *rt_bitset_not(void *obj)
{
    if (!obj)
        return rt_bitset_new(64);

    rt_bitset_impl *bs = (rt_bitset_impl *)obj;
    void *result = rt_bitset_new((int64_t)bs->bit_count);
    if (!result)
        return NULL;

    rt_bitset_impl *br = (rt_bitset_impl *)result;
    for (size_t i = 0; i < bs->word_count && i < br->word_count; ++i)
        br->words[i] = ~bs->words[i];

    // Mask off excess bits in the last word
    size_t extra = bs->bit_count % BITS_PER_WORD;
    if (extra > 0 && br->word_count > 0)
        br->words[br->word_count - 1] &= (1ULL << extra) - 1;

    return result;
}

rt_string rt_bitset_to_string(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("0", 1);

    rt_bitset_impl *bs = (rt_bitset_impl *)obj;
    if (bs->bit_count == 0)
        return rt_string_from_bytes("0", 1);

    // Build string from MSB to LSB
    size_t len = bs->bit_count;
    char *buf = (char *)malloc(len + 1);
    if (!buf)
        return rt_string_from_bytes("0", 1);

    for (size_t i = 0; i < len; ++i)
    {
        size_t bit_idx = len - 1 - i;
        size_t w = bit_idx / BITS_PER_WORD;
        size_t b = bit_idx % BITS_PER_WORD;
        buf[i] = (w < bs->word_count && (bs->words[w] >> b) & 1) ? '1' : '0';
    }
    buf[len] = '\0';

    // Skip leading zeros (but keep at least one character)
    size_t start = 0;
    while (start < len - 1 && buf[start] == '0')
        ++start;

    rt_string result = rt_string_from_bytes(buf + start, len - start);
    free(buf);
    return result;
}
