//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_seq.c
// Purpose: Implement Viper.Collections.Seq - a dynamic sequence (growable array).
//
// Structure:
// - Internal representation uses a header structure with len, cap, and items[]
// - Items are stored as void* (generic object pointers)
// - Automatic growth when capacity is exceeded
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_random.h"

#include <stdlib.h>
#include <string.h>

#define SEQ_DEFAULT_CAP 16
#define SEQ_GROWTH_FACTOR 2

/// Internal sequence structure.
typedef struct rt_seq_impl
{
    int64_t len;  ///< Number of elements currently in the sequence
    int64_t cap;  ///< Current capacity (allocated slots)
    void **items; ///< Array of element pointers
} rt_seq_impl;

/// @brief Ensure the sequence has capacity for at least `needed` elements.
/// @param seq Sequence to potentially grow.
/// @param needed Minimum required capacity.
static void seq_ensure_capacity(rt_seq_impl *seq, int64_t needed)
{
    if (needed <= seq->cap)
        return;

    int64_t new_cap = seq->cap;
    while (new_cap < needed)
    {
        new_cap *= SEQ_GROWTH_FACTOR;
    }

    void **new_items = realloc(seq->items, (size_t)new_cap * sizeof(void *));
    if (!new_items)
    {
        rt_trap("Seq: memory allocation failed");
    }

    seq->items = new_items;
    seq->cap = new_cap;
}

/// @brief Create a new empty sequence with default capacity.
/// @return New sequence object.
void *rt_seq_new(void)
{
    rt_seq_impl *seq = (rt_seq_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_seq_impl));
    if (!seq)
    {
        rt_trap("Seq: memory allocation failed");
    }

    seq->len = 0;
    seq->cap = SEQ_DEFAULT_CAP;
    seq->items = malloc((size_t)SEQ_DEFAULT_CAP * sizeof(void *));

    if (!seq->items)
    {
        rt_obj_free(seq);
        rt_trap("Seq: memory allocation failed");
    }

    return seq;
}

/// @brief Create a new empty sequence with specified initial capacity.
/// @param cap Initial capacity (minimum 1).
/// @return New sequence object.
void *rt_seq_with_capacity(int64_t cap)
{
    if (cap < 1)
        cap = 1;

    rt_seq_impl *seq = (rt_seq_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_seq_impl));
    if (!seq)
    {
        rt_trap("Seq: memory allocation failed");
    }

    seq->len = 0;
    seq->cap = cap;
    seq->items = malloc((size_t)cap * sizeof(void *));

    if (!seq->items)
    {
        rt_obj_free(seq);
        rt_trap("Seq: memory allocation failed");
    }

    return seq;
}

/// @brief Get the number of elements in the sequence.
/// @param obj Sequence object.
/// @return Number of elements.
int64_t rt_seq_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_seq_impl *)obj)->len;
}

/// @brief Get the current capacity of the sequence.
/// @param obj Sequence object.
/// @return Current capacity.
int64_t rt_seq_cap(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_seq_impl *)obj)->cap;
}

/// @brief Check if the sequence is empty.
/// @param obj Sequence object.
/// @return 1 if empty, 0 otherwise.
int8_t rt_seq_is_empty(void *obj)
{
    if (!obj)
        return 1;
    return ((rt_seq_impl *)obj)->len == 0 ? 1 : 0;
}

/// @brief Get the element at the specified index.
/// @param obj Sequence object.
/// @param idx Index of element to retrieve.
/// @return Element at the index.
void *rt_seq_get(void *obj, int64_t idx)
{
    if (!obj)
        rt_trap("Seq.Get: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (idx < 0 || idx >= seq->len)
    {
        rt_trap("Seq.Get: index out of bounds");
    }

    return seq->items[idx];
}

/// @brief Set the element at the specified index.
/// @param obj Sequence object.
/// @param idx Index of element to set.
/// @param val Value to store.
void rt_seq_set(void *obj, int64_t idx, void *val)
{
    if (!obj)
        rt_trap("Seq.Set: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (idx < 0 || idx >= seq->len)
    {
        rt_trap("Seq.Set: index out of bounds");
    }

    seq->items[idx] = val;
}

/// @brief Add an element to the end of the sequence.
/// @param obj Sequence object.
/// @param val Element to add.
void rt_seq_push(void *obj, void *val)
{
    if (!obj)
        rt_trap("Seq.Push: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    seq_ensure_capacity(seq, seq->len + 1);
    seq->items[seq->len] = val;
    seq->len++;
}

/// @brief Append all elements of @p other onto @p obj.
/// @details Preserves element ordering. When @p obj == @p other, the operation is defined as
///          doubling the original sequence contents and must not loop indefinitely.
/// @param obj Destination sequence object (mutated).
/// @param other Source sequence object whose elements will be appended; treated as empty when NULL.
void rt_seq_push_all(void *obj, void *other)
{
    if (!obj)
        rt_trap("Seq.PushAll: null sequence");
    if (!other)
        return;

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    rt_seq_impl *src = (rt_seq_impl *)other;

    if (src->len <= 0)
        return;

    if (seq == src)
    {
        int64_t original_len = seq->len;
        seq_ensure_capacity(seq, original_len + original_len);
        memcpy(&seq->items[original_len], seq->items, (size_t)original_len * sizeof(void *));
        seq->len = original_len + original_len;
        return;
    }

    seq_ensure_capacity(seq, seq->len + src->len);
    memcpy(&seq->items[seq->len], src->items, (size_t)src->len * sizeof(void *));
    seq->len += src->len;
}

/// @brief Remove and return the last element from the sequence.
/// @param obj Sequence object.
/// @return The removed element.
void *rt_seq_pop(void *obj)
{
    if (!obj)
        rt_trap("Seq.Pop: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (seq->len == 0)
    {
        rt_trap("Seq.Pop: sequence is empty");
    }

    seq->len--;
    return seq->items[seq->len];
}

/// @brief Get the last element without removing it.
/// @param obj Sequence object.
/// @return The last element.
void *rt_seq_peek(void *obj)
{
    if (!obj)
        rt_trap("Seq.Peek: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (seq->len == 0)
    {
        rt_trap("Seq.Peek: sequence is empty");
    }

    return seq->items[seq->len - 1];
}

/// @brief Get the first element.
/// @param obj Sequence object.
/// @return The first element.
void *rt_seq_first(void *obj)
{
    if (!obj)
        rt_trap("Seq.First: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (seq->len == 0)
    {
        rt_trap("Seq.First: sequence is empty");
    }

    return seq->items[0];
}

/// @brief Get the last element.
/// @param obj Sequence object.
/// @return The last element.
void *rt_seq_last(void *obj)
{
    if (!obj)
        rt_trap("Seq.Last: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (seq->len == 0)
    {
        rt_trap("Seq.Last: sequence is empty");
    }

    return seq->items[seq->len - 1];
}

/// @brief Insert an element at the specified position.
/// @param obj Sequence object.
/// @param idx Position to insert at (0 to len inclusive).
/// @param val Element to insert.
void rt_seq_insert(void *obj, int64_t idx, void *val)
{
    if (!obj)
        rt_trap("Seq.Insert: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (idx < 0 || idx > seq->len)
    {
        rt_trap("Seq.Insert: index out of bounds");
    }

    seq_ensure_capacity(seq, seq->len + 1);

    // Shift elements to the right
    if (idx < seq->len)
    {
        memmove(&seq->items[idx + 1], &seq->items[idx], (size_t)(seq->len - idx) * sizeof(void *));
    }

    seq->items[idx] = val;
    seq->len++;
}

/// @brief Remove and return the element at the specified position.
/// @param obj Sequence object.
/// @param idx Position to remove from.
/// @return The removed element.
void *rt_seq_remove(void *obj, int64_t idx)
{
    if (!obj)
        rt_trap("Seq.Remove: null sequence");

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (idx < 0 || idx >= seq->len)
    {
        rt_trap("Seq.Remove: index out of bounds");
    }

    void *val = seq->items[idx];

    // Shift elements to the left
    if (idx < seq->len - 1)
    {
        memmove(
            &seq->items[idx], &seq->items[idx + 1], (size_t)(seq->len - idx - 1) * sizeof(void *));
    }

    seq->len--;
    return val;
}

/// @brief Remove all elements from the sequence.
/// @param obj Sequence object.
void rt_seq_clear(void *obj)
{
    if (!obj)
        return;
    ((rt_seq_impl *)obj)->len = 0;
}

/// @brief Find the index of an element in the sequence.
/// @param obj Sequence object.
/// @param val Element to find (compared by pointer equality).
/// @return Index of element, or -1 if not found.
int64_t rt_seq_find(void *obj, void *val)
{
    if (!obj)
        return -1;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    for (int64_t i = 0; i < seq->len; i++)
    {
        if (seq->items[i] == val)
        {
            return i;
        }
    }

    return -1;
}

/// @brief Check if the sequence contains an element.
/// @param obj Sequence object.
/// @param val Element to check for (compared by pointer equality).
/// @return 1 if found, 0 otherwise.
int8_t rt_seq_has(void *obj, void *val)
{
    return rt_seq_find(obj, val) >= 0 ? 1 : 0;
}

/// @brief Reverse the elements in the sequence in place.
/// @param obj Sequence object.
void rt_seq_reverse(void *obj)
{
    if (!obj)
        return;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    for (int64_t i = 0; i < seq->len / 2; i++)
    {
        int64_t j = seq->len - 1 - i;
        void *tmp = seq->items[i];
        seq->items[i] = seq->items[j];
        seq->items[j] = tmp;
    }
}

/// @brief Shuffle the sequence in place using Fisher–Yates.
/// @details Randomness is sourced from @ref rt_rand_int (Viper.Random.NextInt), so seeding via
///          Viper.Random.Seed produces deterministic shuffles.
/// @param obj Sequence object (mutated).
void rt_seq_shuffle(void *obj)
{
    if (!obj)
        return;

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    if (seq->len <= 1)
        return;

    for (int64_t i = seq->len - 1; i > 0; --i)
    {
        int64_t j = (int64_t)rt_rand_int((long long)(i + 1));
        void *tmp = seq->items[i];
        seq->items[i] = seq->items[j];
        seq->items[j] = tmp;
    }
}

/// @brief Create a new sequence containing elements from [start, end).
/// @param obj Source sequence object.
/// @param start Start index (inclusive, clamped to 0).
/// @param end End index (exclusive, clamped to len).
/// @return New sequence containing the slice.
void *rt_seq_slice(void *obj, int64_t start, int64_t end)
{
    if (!obj)
        return rt_seq_new();

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    // Clamp bounds
    if (start < 0)
        start = 0;
    if (end > seq->len)
        end = seq->len;
    if (start >= end)
    {
        return rt_seq_new();
    }

    int64_t new_len = end - start;
    rt_seq_impl *result = rt_seq_with_capacity(new_len);

    memcpy(result->items, &seq->items[start], (size_t)new_len * sizeof(void *));
    ((rt_seq_impl *)result)->len = new_len;

    return result;
}

/// @brief Create a shallow copy of the sequence.
/// @param obj Source sequence object.
/// @return New sequence with same elements.
void *rt_seq_clone(void *obj)
{
    if (!obj)
        return rt_seq_new();

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    return rt_seq_slice(obj, 0, seq->len);
}
