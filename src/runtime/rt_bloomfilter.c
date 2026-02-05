//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_bloomfilter.h"
#include "rt_internal.h"
#include "rt_object.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal structure
// ---------------------------------------------------------------------------

typedef struct
{
    void *vptr;
    uint8_t *bits;
    int64_t bit_count;   // Total number of bits
    int64_t hash_count;  // Number of hash functions
    int64_t item_count;  // Items added
} rt_bloomfilter_impl;

// ---------------------------------------------------------------------------
// Hash functions (MurmurHash3-style with seed variation)
// ---------------------------------------------------------------------------

static uint64_t bloom_hash(const char *data, size_t len, uint64_t seed)
{
    uint64_t h = seed ^ (len * 0x9E3779B97F4A7C15ULL);
    for (size_t i = 0; i < len; i++)
    {
        h ^= (uint64_t)(unsigned char)data[i];
        h *= 0x9E3779B97F4A7C15ULL;
        h ^= h >> 27;
    }
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 31;
    h *= 0x94D049BB133111EBULL;
    h ^= h >> 32;
    return h;
}

// ---------------------------------------------------------------------------
// Finalizer
// ---------------------------------------------------------------------------

static void bloomfilter_finalizer(void *obj)
{
    rt_bloomfilter_impl *bf = (rt_bloomfilter_impl *)obj;
    if (bf->bits)
    {
        free(bf->bits);
        bf->bits = NULL;
    }
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

void *rt_bloomfilter_new(int64_t expected_items, double false_positive_rate)
{
    if (expected_items < 1) expected_items = 1;
    if (false_positive_rate <= 0.0) false_positive_rate = 0.01;
    if (false_positive_rate >= 1.0) false_positive_rate = 0.5;

    // Optimal bit count: m = -n * ln(p) / (ln(2)^2)
    double n = (double)expected_items;
    double m = -n * log(false_positive_rate) / (log(2.0) * log(2.0));
    int64_t bit_count = (int64_t)ceil(m);
    if (bit_count < 64) bit_count = 64;

    // Optimal hash count: k = (m/n) * ln(2)
    double k = ((double)bit_count / n) * log(2.0);
    int64_t hash_count = (int64_t)ceil(k);
    if (hash_count < 1) hash_count = 1;
    if (hash_count > 30) hash_count = 30;

    int64_t byte_count = (bit_count + 7) / 8;

    rt_bloomfilter_impl *bf =
        (rt_bloomfilter_impl *)rt_obj_new_i64(0, sizeof(rt_bloomfilter_impl));
    bf->bits = (uint8_t *)calloc((size_t)byte_count, 1);
    bf->bit_count = bit_count;
    bf->hash_count = hash_count;
    bf->item_count = 0;

    rt_obj_set_finalizer(bf, bloomfilter_finalizer);
    return bf;
}

// ---------------------------------------------------------------------------
// Add / query
// ---------------------------------------------------------------------------

void rt_bloomfilter_add(void *filter, rt_string item)
{
    if (!filter || !item) return;
    rt_bloomfilter_impl *bf = (rt_bloomfilter_impl *)filter;

    const char *data = rt_string_cstr(item);
    if (!data) return;
    size_t len = strlen(data);

    for (int64_t i = 0; i < bf->hash_count; i++)
    {
        uint64_t h = bloom_hash(data, len, (uint64_t)i);
        int64_t pos = (int64_t)(h % (uint64_t)bf->bit_count);
        bf->bits[pos / 8] |= (uint8_t)(1 << (pos % 8));
    }
    bf->item_count++;
}

int64_t rt_bloomfilter_might_contain(void *filter, rt_string item)
{
    if (!filter || !item) return 0;
    rt_bloomfilter_impl *bf = (rt_bloomfilter_impl *)filter;

    const char *data = rt_string_cstr(item);
    if (!data) return 0;
    size_t len = strlen(data);

    for (int64_t i = 0; i < bf->hash_count; i++)
    {
        uint64_t h = bloom_hash(data, len, (uint64_t)i);
        int64_t pos = (int64_t)(h % (uint64_t)bf->bit_count);
        if (!(bf->bits[pos / 8] & (1 << (pos % 8))))
            return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

int64_t rt_bloomfilter_count(void *filter)
{
    if (!filter) return 0;
    return ((rt_bloomfilter_impl *)filter)->item_count;
}

double rt_bloomfilter_fpr(void *filter)
{
    if (!filter) return 0.0;
    rt_bloomfilter_impl *bf = (rt_bloomfilter_impl *)filter;

    // Estimated FPR: (1 - e^(-kn/m))^k
    double m = (double)bf->bit_count;
    double n = (double)bf->item_count;
    double k = (double)bf->hash_count;

    if (n == 0.0) return 0.0;
    return pow(1.0 - exp(-k * n / m), k);
}

void rt_bloomfilter_clear(void *filter)
{
    if (!filter) return;
    rt_bloomfilter_impl *bf = (rt_bloomfilter_impl *)filter;
    int64_t byte_count = (bf->bit_count + 7) / 8;
    memset(bf->bits, 0, (size_t)byte_count);
    bf->item_count = 0;
}

int64_t rt_bloomfilter_merge(void *filter, void *other)
{
    if (!filter || !other) return 0;
    rt_bloomfilter_impl *a = (rt_bloomfilter_impl *)filter;
    rt_bloomfilter_impl *b = (rt_bloomfilter_impl *)other;

    if (a->bit_count != b->bit_count || a->hash_count != b->hash_count)
        return 0;

    int64_t byte_count = (a->bit_count + 7) / 8;
    for (int64_t i = 0; i < byte_count; i++)
        a->bits[i] |= b->bits[i];

    a->item_count += b->item_count;
    return 1;
}
