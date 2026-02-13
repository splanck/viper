//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_lazyseq.c
/// @brief Implementation of lazy sequence type.
///
//===----------------------------------------------------------------------===//

#include "rt_lazyseq.h"
#include "rt_object.h"
#include "rt_seq.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// LazySeq Types
//=============================================================================

typedef enum
{
    LAZYSEQ_GENERATOR,  // User-provided generator function
    LAZYSEQ_RANGE,      // Integer range
    LAZYSEQ_REPEAT,     // Repeated value
    LAZYSEQ_ITERATE,    // Iterative function application
    LAZYSEQ_MAP,        // Transformed sequence
    LAZYSEQ_FILTER,     // Filtered sequence
    LAZYSEQ_TAKE,       // Bounded sequence
    LAZYSEQ_DROP,       // Skipping sequence
    LAZYSEQ_TAKE_WHILE, // Take while predicate
    LAZYSEQ_DROP_WHILE, // Drop while predicate
    LAZYSEQ_CONCAT,     // Concatenated sequences
    LAZYSEQ_ZIP,        // Zipped sequences
} lazyseq_type;

/// Internal structure for LazySeq.
struct rt_lazyseq_impl
{
    lazyseq_type type;
    int64_t index;    // Current position
    int8_t exhausted; // 1 if sequence ended
    int8_t peeked;    // 1 if we have a peeked value
    void *peek_value; // Cached peeked value

    union
    {
        // Generator
        struct
        {
            rt_lazyseq_gen_fn gen;
            void *state;
        } generator;

        // Range
        struct
        {
            int64_t current;
            int64_t end;
            int64_t step;
        } range;

        // Repeat
        struct
        {
            void *value;
            int64_t remaining; // -1 for infinite
        } repeat;

        // Iterate
        struct
        {
            void *current;
            void *(*fn)(void *);
            int8_t started;
        } iterate;

        // Map/Filter
        struct
        {
            rt_lazyseq source;

            union
            {
                void *(*map_fn)(void *);
                int8_t (*filter_fn)(void *);
            };
        } transform;

        // Take/Drop
        struct
        {
            rt_lazyseq source;
            int64_t limit;
            int64_t consumed;
        } bounded;

        // TakeWhile/DropWhile
        struct
        {
            rt_lazyseq source;
            int8_t (*pred)(void *);
            int8_t done;
        } predicated;

        // Concat
        struct
        {
            rt_lazyseq first;
            rt_lazyseq second;
            int8_t on_second;
        } concat;

        // Zip
        struct
        {
            rt_lazyseq seq1;
            rt_lazyseq seq2;
            void *(*combine)(void *, void *);
        } zip;
    } data;
};

//=============================================================================
// Internal helpers
//=============================================================================

static rt_lazyseq alloc_lazyseq(lazyseq_type type)
{
    struct rt_lazyseq_impl *seq =
        (struct rt_lazyseq_impl *)rt_obj_new_i64(0, (int64_t)sizeof(struct rt_lazyseq_impl));
    memset(seq, 0, sizeof(struct rt_lazyseq_impl));
    seq->type = type;
    return seq;
}

// Static storage for range values (thread-local would be better)
static int64_t range_value_storage;

static void *range_value_ptr(int64_t val)
{
    range_value_storage = val;
    return &range_value_storage;
}

//=============================================================================
// Creation
//=============================================================================

rt_lazyseq rt_lazyseq_new(rt_lazyseq_gen_fn gen, void *state)
{
    if (!gen)
        return NULL;

    rt_lazyseq seq = alloc_lazyseq(LAZYSEQ_GENERATOR);
    if (!seq)
        return NULL;

    seq->data.generator.gen = gen;
    seq->data.generator.state = state;
    return seq;
}

rt_lazyseq rt_lazyseq_range(int64_t start, int64_t end, int64_t step)
{
    if (step == 0)
        return NULL;

    rt_lazyseq seq = alloc_lazyseq(LAZYSEQ_RANGE);
    if (!seq)
        return NULL;

    seq->data.range.current = start;
    seq->data.range.end = end;
    seq->data.range.step = step;
    return seq;
}

rt_lazyseq rt_lazyseq_repeat(void *value, int64_t count)
{
    rt_lazyseq seq = alloc_lazyseq(LAZYSEQ_REPEAT);
    if (!seq)
        return NULL;

    seq->data.repeat.value = value;
    seq->data.repeat.remaining = count;
    return seq;
}

rt_lazyseq rt_lazyseq_iterate(void *seed, void *(*fn)(void *))
{
    if (!fn)
        return NULL;

    rt_lazyseq seq = alloc_lazyseq(LAZYSEQ_ITERATE);
    if (!seq)
        return NULL;

    seq->data.iterate.current = seed;
    seq->data.iterate.fn = fn;
    seq->data.iterate.started = 0;
    return seq;
}

void rt_lazyseq_destroy(rt_lazyseq seq)
{
    if (!seq)
        return;

    // Recursively destroy nested sequences
    switch (seq->type)
    {
        case LAZYSEQ_MAP:
        case LAZYSEQ_FILTER:
            rt_lazyseq_destroy(seq->data.transform.source);
            break;
        case LAZYSEQ_TAKE:
        case LAZYSEQ_DROP:
            rt_lazyseq_destroy(seq->data.bounded.source);
            break;
        case LAZYSEQ_TAKE_WHILE:
        case LAZYSEQ_DROP_WHILE:
            rt_lazyseq_destroy(seq->data.predicated.source);
            break;
        case LAZYSEQ_CONCAT:
            rt_lazyseq_destroy(seq->data.concat.first);
            rt_lazyseq_destroy(seq->data.concat.second);
            break;
        case LAZYSEQ_ZIP:
            rt_lazyseq_destroy(seq->data.zip.seq1);
            rt_lazyseq_destroy(seq->data.zip.seq2);
            break;
        default:
            break;
    }
}

//=============================================================================
// Element Access
//=============================================================================

void *rt_lazyseq_next(rt_lazyseq seq, int8_t *has_more)
{
    if (!seq || seq->exhausted)
    {
        if (has_more)
            *has_more = 0;
        return NULL;
    }

    // Return peeked value if available
    if (seq->peeked)
    {
        seq->peeked = 0;
        seq->index++;
        if (has_more)
            *has_more = 1;
        return seq->peek_value;
    }

    void *result = NULL;
    int8_t more = 1;

    switch (seq->type)
    {
        case LAZYSEQ_GENERATOR:
        {
            result = seq->data.generator.gen(seq->data.generator.state, seq->index, &more);
            break;
        }

        case LAZYSEQ_RANGE:
        {
            int64_t cur = seq->data.range.current;
            int64_t end = seq->data.range.end;
            int64_t step = seq->data.range.step;

            if ((step > 0 && cur >= end) || (step < 0 && cur <= end))
            {
                more = 0;
            }
            else
            {
                result = range_value_ptr(cur);
                seq->data.range.current = cur + step;
            }
            break;
        }

        case LAZYSEQ_REPEAT:
        {
            if (seq->data.repeat.remaining == 0)
            {
                more = 0;
            }
            else
            {
                result = seq->data.repeat.value;
                if (seq->data.repeat.remaining > 0)
                {
                    seq->data.repeat.remaining--;
                }
            }
            break;
        }

        case LAZYSEQ_ITERATE:
        {
            if (!seq->data.iterate.started)
            {
                seq->data.iterate.started = 1;
                result = seq->data.iterate.current;
            }
            else
            {
                seq->data.iterate.current = seq->data.iterate.fn(seq->data.iterate.current);
                result = seq->data.iterate.current;
            }
            break;
        }

        case LAZYSEQ_MAP:
        {
            int8_t src_more;
            void *elem = rt_lazyseq_next(seq->data.transform.source, &src_more);
            if (src_more)
            {
                result = seq->data.transform.map_fn(elem);
            }
            else
            {
                more = 0;
            }
            break;
        }

        case LAZYSEQ_FILTER:
        {
            while (1)
            {
                int8_t src_more;
                void *elem = rt_lazyseq_next(seq->data.transform.source, &src_more);
                if (!src_more)
                {
                    more = 0;
                    break;
                }
                if (seq->data.transform.filter_fn(elem))
                {
                    result = elem;
                    break;
                }
            }
            break;
        }

        case LAZYSEQ_TAKE:
        {
            if (seq->data.bounded.consumed >= seq->data.bounded.limit)
            {
                more = 0;
            }
            else
            {
                int8_t src_more;
                result = rt_lazyseq_next(seq->data.bounded.source, &src_more);
                if (src_more)
                {
                    seq->data.bounded.consumed++;
                }
                else
                {
                    more = 0;
                }
            }
            break;
        }

        case LAZYSEQ_DROP:
        {
            // Skip elements on first access
            while (seq->data.bounded.consumed < seq->data.bounded.limit)
            {
                int8_t src_more;
                rt_lazyseq_next(seq->data.bounded.source, &src_more);
                if (!src_more)
                {
                    more = 0;
                    break;
                }
                seq->data.bounded.consumed++;
            }
            if (more)
            {
                int8_t src_more;
                result = rt_lazyseq_next(seq->data.bounded.source, &src_more);
                more = src_more;
            }
            break;
        }

        case LAZYSEQ_TAKE_WHILE:
        {
            if (seq->data.predicated.done)
            {
                more = 0;
            }
            else
            {
                int8_t src_more;
                void *elem = rt_lazyseq_next(seq->data.predicated.source, &src_more);
                if (!src_more || !seq->data.predicated.pred(elem))
                {
                    seq->data.predicated.done = 1;
                    more = 0;
                }
                else
                {
                    result = elem;
                }
            }
            break;
        }

        case LAZYSEQ_DROP_WHILE:
        {
            if (!seq->data.predicated.done)
            {
                // Skip elements while predicate is true
                while (1)
                {
                    int8_t src_more;
                    void *elem = rt_lazyseq_next(seq->data.predicated.source, &src_more);
                    if (!src_more)
                    {
                        more = 0;
                        break;
                    }
                    if (!seq->data.predicated.pred(elem))
                    {
                        seq->data.predicated.done = 1;
                        result = elem;
                        break;
                    }
                }
            }
            else
            {
                int8_t src_more;
                result = rt_lazyseq_next(seq->data.predicated.source, &src_more);
                more = src_more;
            }
            break;
        }

        case LAZYSEQ_CONCAT:
        {
            if (!seq->data.concat.on_second)
            {
                int8_t src_more;
                result = rt_lazyseq_next(seq->data.concat.first, &src_more);
                if (src_more)
                {
                    break;
                }
                seq->data.concat.on_second = 1;
            }
            int8_t src_more;
            result = rt_lazyseq_next(seq->data.concat.second, &src_more);
            more = src_more;
            break;
        }

        case LAZYSEQ_ZIP:
        {
            int8_t more1, more2;
            void *elem1 = rt_lazyseq_next(seq->data.zip.seq1, &more1);
            void *elem2 = rt_lazyseq_next(seq->data.zip.seq2, &more2);
            if (more1 && more2)
            {
                result = seq->data.zip.combine(elem1, elem2);
            }
            else
            {
                more = 0;
            }
            break;
        }
    }

    if (!more)
    {
        seq->exhausted = 1;
    }
    else
    {
        seq->index++;
    }

    if (has_more)
        *has_more = more;
    return result;
}

void *rt_lazyseq_peek(rt_lazyseq seq, int8_t *has_more)
{
    if (!seq)
    {
        if (has_more)
            *has_more = 0;
        return NULL;
    }

    if (seq->peeked)
    {
        if (has_more)
            *has_more = 1;
        return seq->peek_value;
    }

    int8_t more;
    void *val = rt_lazyseq_next(seq, &more);

    if (more)
    {
        seq->peeked = 1;
        seq->peek_value = val;
        seq->index--; // Undo the increment from next
    }

    if (has_more)
        *has_more = more;
    return val;
}

void rt_lazyseq_reset(rt_lazyseq seq)
{
    if (!seq)
        return;

    seq->index = 0;
    seq->exhausted = 0;
    seq->peeked = 0;
    seq->peek_value = NULL;

    switch (seq->type)
    {
        case LAZYSEQ_RANGE:
            // Cannot reset range without original start value
            // This is a limitation
            break;
        case LAZYSEQ_REPEAT:
            // Cannot reset count
            break;
        case LAZYSEQ_ITERATE:
            seq->data.iterate.started = 0;
            break;
        case LAZYSEQ_MAP:
        case LAZYSEQ_FILTER:
            rt_lazyseq_reset(seq->data.transform.source);
            break;
        case LAZYSEQ_TAKE:
        case LAZYSEQ_DROP:
            seq->data.bounded.consumed = 0;
            rt_lazyseq_reset(seq->data.bounded.source);
            break;
        case LAZYSEQ_TAKE_WHILE:
        case LAZYSEQ_DROP_WHILE:
            seq->data.predicated.done = 0;
            rt_lazyseq_reset(seq->data.predicated.source);
            break;
        case LAZYSEQ_CONCAT:
            seq->data.concat.on_second = 0;
            rt_lazyseq_reset(seq->data.concat.first);
            rt_lazyseq_reset(seq->data.concat.second);
            break;
        case LAZYSEQ_ZIP:
            rt_lazyseq_reset(seq->data.zip.seq1);
            rt_lazyseq_reset(seq->data.zip.seq2);
            break;
        default:
            break;
    }
}

int64_t rt_lazyseq_index(rt_lazyseq seq)
{
    return seq ? seq->index : 0;
}

int8_t rt_lazyseq_is_exhausted(rt_lazyseq seq)
{
    return seq ? seq->exhausted : 1;
}

//=============================================================================
// Transformations
//=============================================================================

rt_lazyseq rt_lazyseq_map(rt_lazyseq seq, void *(*fn)(void *))
{
    if (!seq || !fn)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_MAP);
    if (!result)
        return NULL;

    result->data.transform.source = seq;
    result->data.transform.map_fn = fn;
    return result;
}

rt_lazyseq rt_lazyseq_filter(rt_lazyseq seq, int8_t (*pred)(void *))
{
    if (!seq || !pred)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_FILTER);
    if (!result)
        return NULL;

    result->data.transform.source = seq;
    result->data.transform.filter_fn = pred;
    return result;
}

rt_lazyseq rt_lazyseq_take(rt_lazyseq seq, int64_t n)
{
    if (!seq || n < 0)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_TAKE);
    if (!result)
        return NULL;

    result->data.bounded.source = seq;
    result->data.bounded.limit = n;
    result->data.bounded.consumed = 0;
    return result;
}

rt_lazyseq rt_lazyseq_drop(rt_lazyseq seq, int64_t n)
{
    if (!seq || n < 0)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_DROP);
    if (!result)
        return NULL;

    result->data.bounded.source = seq;
    result->data.bounded.limit = n;
    result->data.bounded.consumed = 0;
    return result;
}

rt_lazyseq rt_lazyseq_take_while(rt_lazyseq seq, int8_t (*pred)(void *))
{
    if (!seq || !pred)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_TAKE_WHILE);
    if (!result)
        return NULL;

    result->data.predicated.source = seq;
    result->data.predicated.pred = pred;
    result->data.predicated.done = 0;
    return result;
}

rt_lazyseq rt_lazyseq_drop_while(rt_lazyseq seq, int8_t (*pred)(void *))
{
    if (!seq || !pred)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_DROP_WHILE);
    if (!result)
        return NULL;

    result->data.predicated.source = seq;
    result->data.predicated.pred = pred;
    result->data.predicated.done = 0;
    return result;
}

rt_lazyseq rt_lazyseq_concat(rt_lazyseq first, rt_lazyseq second)
{
    if (!first || !second)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_CONCAT);
    if (!result)
        return NULL;

    result->data.concat.first = first;
    result->data.concat.second = second;
    result->data.concat.on_second = 0;
    return result;
}

rt_lazyseq rt_lazyseq_zip(rt_lazyseq seq1, rt_lazyseq seq2, void *(*combine)(void *, void *))
{
    if (!seq1 || !seq2 || !combine)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_ZIP);
    if (!result)
        return NULL;

    result->data.zip.seq1 = seq1;
    result->data.zip.seq2 = seq2;
    result->data.zip.combine = combine;
    return result;
}

//=============================================================================
// Collectors
//=============================================================================

void *rt_lazyseq_to_seq(rt_lazyseq seq)
{
    if (!seq)
        return rt_seq_new();

    void *result = rt_seq_new();
    int8_t has_more;

    while (1)
    {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        rt_seq_push(result, elem);
    }

    return result;
}

void *rt_lazyseq_to_seq_n(rt_lazyseq seq, int64_t n)
{
    if (!seq || n <= 0)
        return rt_seq_new();

    void *result = rt_seq_with_capacity(n);
    int8_t has_more;
    int64_t count = 0;

    while (count < n)
    {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        rt_seq_push(result, elem);
        count++;
    }

    return result;
}

void *rt_lazyseq_fold(rt_lazyseq seq, void *init, void *(*fn)(void *, void *))
{
    if (!seq || !fn)
        return init;

    void *acc = init;
    int8_t has_more;

    while (1)
    {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        acc = fn(acc, elem);
    }

    return acc;
}

int64_t rt_lazyseq_count(rt_lazyseq seq)
{
    if (!seq)
        return 0;

    int64_t count = 0;
    int8_t has_more;

    while (1)
    {
        rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        count++;
    }

    return count;
}

void rt_lazyseq_foreach(rt_lazyseq seq, void (*fn)(void *))
{
    if (!seq || !fn)
        return;

    int8_t has_more;

    while (1)
    {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        fn(elem);
    }
}

void *rt_lazyseq_find(rt_lazyseq seq, int8_t (*pred)(void *), int8_t *found)
{
    if (!seq || !pred)
    {
        if (found)
            *found = 0;
        return NULL;
    }

    int8_t has_more;

    while (1)
    {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        if (pred(elem))
        {
            if (found)
                *found = 1;
            return elem;
        }
    }

    if (found)
        *found = 0;
    return NULL;
}

int8_t rt_lazyseq_any(rt_lazyseq seq, int8_t (*pred)(void *))
{
    if (!seq || !pred)
        return 0;

    int8_t has_more;

    while (1)
    {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        if (pred(elem))
            return 1;
    }

    return 0;
}

int8_t rt_lazyseq_all(rt_lazyseq seq, int8_t (*pred)(void *))
{
    if (!seq || !pred)
        return 1;

    int8_t has_more;

    while (1)
    {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        if (!pred(elem))
            return 0;
    }

    return 1;
}

//=============================================================================
// IL ABI wrappers (void* interface for runtime signature handlers)
//=============================================================================

void *rt_lazyseq_w_range(int64_t start, int64_t end, int64_t step)
{
    return (void *)rt_lazyseq_range(start, end, step);
}

void *rt_lazyseq_w_repeat(void *value, int64_t count)
{
    return (void *)rt_lazyseq_repeat(value, count);
}

void *rt_lazyseq_w_next(void *seq)
{
    int8_t has_more;
    return rt_lazyseq_next((rt_lazyseq)seq, &has_more);
}

void *rt_lazyseq_w_peek(void *seq)
{
    int8_t has_more;
    return rt_lazyseq_peek((rt_lazyseq)seq, &has_more);
}

void rt_lazyseq_w_reset(void *seq)
{
    rt_lazyseq_reset((rt_lazyseq)seq);
}

int64_t rt_lazyseq_w_index(void *seq)
{
    return rt_lazyseq_index((rt_lazyseq)seq);
}

int8_t rt_lazyseq_w_is_exhausted(void *seq)
{
    return rt_lazyseq_is_exhausted((rt_lazyseq)seq);
}

void *rt_lazyseq_w_take(void *seq, int64_t n)
{
    return (void *)rt_lazyseq_take((rt_lazyseq)seq, n);
}

void *rt_lazyseq_w_drop(void *seq, int64_t n)
{
    return (void *)rt_lazyseq_drop((rt_lazyseq)seq, n);
}

void *rt_lazyseq_w_concat(void *first, void *second)
{
    return (void *)rt_lazyseq_concat((rt_lazyseq)first, (rt_lazyseq)second);
}

void *rt_lazyseq_w_to_seq(void *seq)
{
    return rt_lazyseq_to_seq((rt_lazyseq)seq);
}

void *rt_lazyseq_w_to_seq_n(void *seq, int64_t n)
{
    return rt_lazyseq_to_seq_n((rt_lazyseq)seq, n);
}

int64_t rt_lazyseq_w_count(void *seq)
{
    return rt_lazyseq_count((rt_lazyseq)seq);
}
