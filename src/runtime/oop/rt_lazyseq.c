//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_lazyseq.c
// Purpose: Implements the lazy sequence (LazySeq) type for the Viper collections
//          runtime. A LazySeq wraps a source sequence and a pipeline of
//          functional transformations (Map, Filter, TakeWhile, SkipWhile, etc.)
//          that are applied only when elements are materialized.
//
// Key invariants:
//   - Transformations are chained by wrapping a LazySeq in another LazySeq.
//   - Materialization (ToList, Count, ForEach) applies the full pipeline once.
//   - The source sequence is borrowed; the LazySeq does not retain it.
//   - Map and Filter callback function pointers are not retained; callers
//     must ensure their lifetimes exceed the LazySeq's use.
//   - Wrapper functions (rt_lazyseq_w_*) adapt Zia-style fn pointers to C
//     function pointer types expected by the LazySeq API.
//
// Ownership/Lifetime:
//   - LazySeq objects are heap-allocated and managed by the runtime GC.
//   - The source sequence is retained by the LazySeq for its lifetime.
//
// Links: src/runtime/oop/rt_lazyseq.h (public API),
//        src/runtime/rt_seq.h (eager sequence used as source and output)
//
//===----------------------------------------------------------------------===//

#include "rt_lazyseq.h"
#include "rt_heap.h"
#include "rt_object.h"
#include "rt_seq.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// LazySeq Types
//=============================================================================

typedef enum {
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
struct rt_lazyseq_impl {
    lazyseq_type type;
    int64_t index;    // Current position
    int8_t exhausted; // 1 if sequence ended
    int8_t peeked;    // 1 if we have a peeked value
    void *peek_value; // Cached peeked value

    union {
        // Generator
        struct {
            rt_lazyseq_gen_fn gen;
            void *state;
        } generator;

        // Range
        struct {
            int64_t current;
            int64_t end;
            int64_t step;
            int64_t value_storage; // per-seq storage for returned value
        } range;

        // Repeat
        struct {
            void *value;
            int64_t remaining; // -1 for infinite
        } repeat;

        // Iterate
        struct {
            void *current;
            void *(*fn)(void *);
            int8_t started;
        } iterate;

        // Map/Filter
        struct {
            rt_lazyseq source;

            union {
                void *(*map_fn)(void *);
                int8_t (*filter_fn)(void *);
            };
        } transform;

        // Take/Drop
        struct {
            rt_lazyseq source;
            int64_t limit;
            int64_t consumed;
        } bounded;

        // TakeWhile/DropWhile
        struct {
            rt_lazyseq source;
            int8_t (*pred)(void *);
            int8_t done;
        } predicated;

        // Concat
        struct {
            rt_lazyseq first;
            rt_lazyseq second;
            int8_t on_second;
        } concat;

        // Zip
        struct {
            rt_lazyseq seq1;
            rt_lazyseq seq2;
            void *(*combine)(void *, void *);
        } zip;
    } data;
};

//=============================================================================
// Internal helpers
//=============================================================================

/// @brief Release a source LazySeq reference via the object release path.
/// @details Decrements the refcount and, if it reaches zero, runs the
///          source's finalizer (which recursively releases its own children)
///          before freeing the memory.
static void release_source(rt_lazyseq source) {
    if (!source)
        return;
    if (rt_obj_release_check0(source))
        rt_obj_free(source);
}

/// @brief Finalizer for LazySeq objects.
/// @details Releases retained source sequences for composite types.
///          Installed on every LazySeq via alloc_lazyseq().
static void lazyseq_finalizer(void *obj) {
    struct rt_lazyseq_impl *seq = (struct rt_lazyseq_impl *)obj;
    if (!seq)
        return;

    switch (seq->type) {
        case LAZYSEQ_MAP:
        case LAZYSEQ_FILTER:
            release_source(seq->data.transform.source);
            seq->data.transform.source = NULL;
            break;
        case LAZYSEQ_TAKE:
        case LAZYSEQ_DROP:
            release_source(seq->data.bounded.source);
            seq->data.bounded.source = NULL;
            break;
        case LAZYSEQ_TAKE_WHILE:
        case LAZYSEQ_DROP_WHILE:
            release_source(seq->data.predicated.source);
            seq->data.predicated.source = NULL;
            break;
        case LAZYSEQ_CONCAT:
            release_source(seq->data.concat.first);
            release_source(seq->data.concat.second);
            seq->data.concat.first = NULL;
            seq->data.concat.second = NULL;
            break;
        case LAZYSEQ_ZIP:
            release_source(seq->data.zip.seq1);
            release_source(seq->data.zip.seq2);
            seq->data.zip.seq1 = NULL;
            seq->data.zip.seq2 = NULL;
            break;
        default:
            break;
    }
}

/// @brief Allocate a fresh LazySeq node with the given variant tag, zero-initialized union, and
/// the GC finalizer wired in. Caller fills the appropriate `seq->data.<variant>` fields.
static rt_lazyseq alloc_lazyseq(lazyseq_type type) {
    struct rt_lazyseq_impl *seq =
        (struct rt_lazyseq_impl *)rt_obj_new_i64(0, (int64_t)sizeof(struct rt_lazyseq_impl));
    memset(seq, 0, sizeof(struct rt_lazyseq_impl));
    seq->type = type;
    rt_obj_set_finalizer(seq, lazyseq_finalizer);
    return seq;
}

//=============================================================================
// Creation
//=============================================================================

/// @brief Construct a generator-driven LazySeq. `gen(state, index, has_more)` is invoked on each
/// `_next` call; setting `*has_more = 0` ends the sequence. Returns NULL if `gen` is NULL.
/// Useful for arbitrary computed sources (file readers, network streams, infinite series).
rt_lazyseq rt_lazyseq_new(rt_lazyseq_gen_fn gen, void *state) {
    if (!gen)
        return NULL;

    rt_lazyseq seq = alloc_lazyseq(LAZYSEQ_GENERATOR);
    if (!seq)
        return NULL;

    seq->data.generator.gen = gen;
    seq->data.generator.state = state;
    return seq;
}

/// @brief Construct an integer range LazySeq from `[start, end)` with the given `step` (positive
/// or negative; zero is rejected with NULL). Each `_next` returns a pointer to internal storage
/// holding the current value (so callers must consume before the next call). **Caveat:** can
/// overflow at INT64 boundaries — caller must keep `start + step` in range.
rt_lazyseq rt_lazyseq_range(int64_t start, int64_t end, int64_t step) {
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

/// @brief Construct a LazySeq that yields `value` exactly `count` times. Pass `count = -1` for an
/// infinite repeat (the canonical way to build infinite seeds for combinator pipelines).
rt_lazyseq rt_lazyseq_repeat(void *value, int64_t count) {
    rt_lazyseq seq = alloc_lazyseq(LAZYSEQ_REPEAT);
    if (!seq)
        return NULL;

    seq->data.repeat.value = value;
    seq->data.repeat.remaining = count;
    return seq;
}

/// @brief Construct an iterative LazySeq: yields `seed`, `fn(seed)`, `fn(fn(seed))`, ... infinitely.
/// Useful for sequences defined by recurrence (Fibonacci, geometric series, walk states).
/// Combine with `rt_lazyseq_take(n)` to get a finite prefix.
rt_lazyseq rt_lazyseq_iterate(void *seed, void *(*fn)(void *)) {
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

/// @brief Release the LazySeq; the registered finalizer recursively releases retained source
/// references. Idempotent — safe to call multiple times (refcount-based).
void rt_lazyseq_destroy(rt_lazyseq seq) {
    if (!seq)
        return;

    // Release one reference. If this is the last reference, the finalizer
    // (lazyseq_finalizer) recursively releases child sources and frees the
    // object. This replaces the old manual recursive cleanup.
    if (rt_obj_release_check0(seq))
        rt_obj_free(seq);
}

//=============================================================================
// Element Access
//=============================================================================

/// @brief Pull the next element from the LazySeq, applying the full transformation pipeline.
/// Sets `*has_more = 0` and returns NULL when the underlying source is exhausted (after which
/// further calls also return NULL). Returns a previously-cached `peek_value` if `_peek` was
/// called without a subsequent `_next`. Composite types (MAP/FILTER/TAKE_WHILE/...) recursively
/// pull from `seq->data.<variant>.source`; FILTER loops until a passing element is found.
void *rt_lazyseq_next(rt_lazyseq seq, int8_t *has_more) {
    if (!seq || seq->exhausted) {
        if (has_more)
            *has_more = 0;
        return NULL;
    }

    // Return peeked value if available
    if (seq->peeked) {
        seq->peeked = 0;
        seq->index++;
        if (has_more)
            *has_more = 1;
        return seq->peek_value;
    }

    void *result = NULL;
    int8_t more = 1;

    switch (seq->type) {
        case LAZYSEQ_GENERATOR: {
            result = seq->data.generator.gen(seq->data.generator.state, seq->index, &more);
            break;
        }

        case LAZYSEQ_RANGE: {
            int64_t cur = seq->data.range.current;
            int64_t end = seq->data.range.end;
            int64_t step = seq->data.range.step;

            if ((step > 0 && cur >= end) || (step < 0 && cur <= end)) {
                more = 0;
            } else {
                seq->data.range.value_storage = cur;
                result = &seq->data.range.value_storage;
                // Note: cur + step can overflow near INT64 boundaries. Caller should
                // ensure range bounds don't cause wraparound.
                seq->data.range.current = cur + step;
            }
            break;
        }

        case LAZYSEQ_REPEAT: {
            if (seq->data.repeat.remaining == 0) {
                more = 0;
            } else {
                result = seq->data.repeat.value;
                if (seq->data.repeat.remaining > 0) {
                    seq->data.repeat.remaining--;
                }
            }
            break;
        }

        case LAZYSEQ_ITERATE: {
            if (!seq->data.iterate.started) {
                seq->data.iterate.started = 1;
                result = seq->data.iterate.current;
            } else {
                seq->data.iterate.current = seq->data.iterate.fn(seq->data.iterate.current);
                result = seq->data.iterate.current;
            }
            break;
        }

        case LAZYSEQ_MAP: {
            int8_t src_more;
            void *elem = rt_lazyseq_next(seq->data.transform.source, &src_more);
            if (src_more) {
                result = seq->data.transform.map_fn(elem);
            } else {
                more = 0;
            }
            break;
        }

        case LAZYSEQ_FILTER: {
            while (1) {
                int8_t src_more;
                void *elem = rt_lazyseq_next(seq->data.transform.source, &src_more);
                if (!src_more) {
                    more = 0;
                    break;
                }
                if (seq->data.transform.filter_fn(elem)) {
                    result = elem;
                    break;
                }
            }
            break;
        }

        case LAZYSEQ_TAKE: {
            if (seq->data.bounded.consumed >= seq->data.bounded.limit) {
                more = 0;
            } else {
                int8_t src_more;
                result = rt_lazyseq_next(seq->data.bounded.source, &src_more);
                if (src_more) {
                    seq->data.bounded.consumed++;
                } else {
                    more = 0;
                }
            }
            break;
        }

        case LAZYSEQ_DROP: {
            // Skip elements on first access
            while (seq->data.bounded.consumed < seq->data.bounded.limit) {
                int8_t src_more;
                rt_lazyseq_next(seq->data.bounded.source, &src_more);
                if (!src_more) {
                    more = 0;
                    break;
                }
                seq->data.bounded.consumed++;
            }
            if (more) {
                int8_t src_more;
                result = rt_lazyseq_next(seq->data.bounded.source, &src_more);
                more = src_more;
            }
            break;
        }

        case LAZYSEQ_TAKE_WHILE: {
            if (seq->data.predicated.done) {
                more = 0;
            } else {
                int8_t src_more;
                void *elem = rt_lazyseq_next(seq->data.predicated.source, &src_more);
                if (!src_more || !seq->data.predicated.pred(elem)) {
                    seq->data.predicated.done = 1;
                    more = 0;
                } else {
                    result = elem;
                }
            }
            break;
        }

        case LAZYSEQ_DROP_WHILE: {
            if (!seq->data.predicated.done) {
                // Skip elements while predicate is true
                while (1) {
                    int8_t src_more;
                    void *elem = rt_lazyseq_next(seq->data.predicated.source, &src_more);
                    if (!src_more) {
                        more = 0;
                        break;
                    }
                    if (!seq->data.predicated.pred(elem)) {
                        seq->data.predicated.done = 1;
                        result = elem;
                        break;
                    }
                }
            } else {
                int8_t src_more;
                result = rt_lazyseq_next(seq->data.predicated.source, &src_more);
                more = src_more;
            }
            break;
        }

        case LAZYSEQ_CONCAT: {
            if (!seq->data.concat.on_second) {
                int8_t src_more;
                result = rt_lazyseq_next(seq->data.concat.first, &src_more);
                if (src_more) {
                    break;
                }
                seq->data.concat.on_second = 1;
            }
            int8_t src_more;
            result = rt_lazyseq_next(seq->data.concat.second, &src_more);
            more = src_more;
            break;
        }

        case LAZYSEQ_ZIP: {
            int8_t more1, more2;
            void *elem1 = rt_lazyseq_next(seq->data.zip.seq1, &more1);
            void *elem2 = rt_lazyseq_next(seq->data.zip.seq2, &more2);
            if (more1 && more2) {
                result = seq->data.zip.combine(elem1, elem2);
            } else {
                more = 0;
            }
            break;
        }
    }

    if (!more) {
        seq->exhausted = 1;
    } else {
        seq->index++;
    }

    if (has_more)
        *has_more = more;
    return result;
}

/// @brief Look at the next element without consuming it. Internally calls `_next` and caches the
/// result in `peek_value` (rolling `index` back by 1 to preserve "advance per `_next` call"
/// semantics). Subsequent `_next` returns the cached value and re-increments. NULL/exhausted
/// sequences return NULL with `*has_more = 0`.
void *rt_lazyseq_peek(rt_lazyseq seq, int8_t *has_more) {
    if (!seq) {
        if (has_more)
            *has_more = 0;
        return NULL;
    }

    if (seq->peeked) {
        if (has_more)
            *has_more = 1;
        return seq->peek_value;
    }

    int8_t more;
    void *val = rt_lazyseq_next(seq, &more);

    if (more) {
        seq->peeked = 1;
        seq->peek_value = val;
        seq->index--; // Undo the increment from next
    }

    if (has_more)
        *has_more = more;
    return val;
}

/// @brief Restart traversal from the beginning where possible. Recursively resets composite
/// sources (MAP/FILTER/TAKE/...). **Limitations:** RANGE and REPEAT cannot be reset (they don't
/// preserve their original `start`/`count` separately from the running state); GENERATOR resets
/// only the index, not the user-state. Use cases: re-running a pipeline over a stable source.
void rt_lazyseq_reset(rt_lazyseq seq) {
    if (!seq)
        return;

    seq->index = 0;
    seq->exhausted = 0;
    seq->peeked = 0;
    seq->peek_value = NULL;

    switch (seq->type) {
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

/// @brief Return the count of elements yielded so far (advances on every successful `_next`).
int64_t rt_lazyseq_index(rt_lazyseq seq) {
    return seq ? seq->index : 0;
}

/// @brief Returns 1 once the underlying source has signaled end-of-stream. NULL handle → 1
/// (treats absence as exhaustion so loop guards `while (!is_exhausted)` are tolerant).
int8_t rt_lazyseq_is_exhausted(rt_lazyseq seq) {
    return seq ? seq->exhausted : 1;
}

//=============================================================================
// Transformations
//=============================================================================

// =============================================================================
// Transformations — each combinator wraps the source in a new LazySeq node,
// retaining the source so it stays alive for the lifetime of the wrapper.
// All combinators are O(1) construction; work happens lazily during traversal.
// Returns NULL on bad input (NULL source, NULL function, negative limits).
// =============================================================================

/// @brief Wrap a source LazySeq so each `_next` returns `fn(source.next())`. The function pointer
/// is borrowed (caller must keep it alive); the source is retained.
rt_lazyseq rt_lazyseq_map(rt_lazyseq seq, void *(*fn)(void *)) {
    if (!seq || !fn)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_MAP);
    if (!result)
        return NULL;

    rt_heap_retain(seq);
    result->data.transform.source = seq;
    result->data.transform.map_fn = fn;
    return result;
}

/// @brief Wrap a source so `_next` skips elements where `pred(elem) == 0`. May call the source's
/// `_next` arbitrarily many times per call (depending on selectivity); inner loop has no upper
/// bound, so combine with `_take` for infinite filtered sources.
rt_lazyseq rt_lazyseq_filter(rt_lazyseq seq, int8_t (*pred)(void *)) {
    if (!seq || !pred)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_FILTER);
    if (!result)
        return NULL;

    rt_heap_retain(seq);
    result->data.transform.source = seq;
    result->data.transform.filter_fn = pred;
    return result;
}

/// @brief Wrap a source so the resulting LazySeq exhausts after `n` elements (or sooner if the
/// source ends first). Cheap way to truncate infinite sequences to a known prefix.
rt_lazyseq rt_lazyseq_take(rt_lazyseq seq, int64_t n) {
    if (!seq || n < 0)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_TAKE);
    if (!result)
        return NULL;

    rt_heap_retain(seq);
    result->data.bounded.source = seq;
    result->data.bounded.limit = n;
    result->data.bounded.consumed = 0;
    return result;
}

/// @brief Wrap a source so the first `n` elements are skipped on first traversal. The skip
/// happens lazily — `n` source `_next` calls are issued the first time a value is requested.
rt_lazyseq rt_lazyseq_drop(rt_lazyseq seq, int64_t n) {
    if (!seq || n < 0)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_DROP);
    if (!result)
        return NULL;

    rt_heap_retain(seq);
    result->data.bounded.source = seq;
    result->data.bounded.limit = n;
    result->data.bounded.consumed = 0;
    return result;
}

/// @brief Wrap a source so traversal stops at the first element where `pred(elem) == 0`. Useful
/// for "consume until X" patterns over a streaming source.
rt_lazyseq rt_lazyseq_take_while(rt_lazyseq seq, int8_t (*pred)(void *)) {
    if (!seq || !pred)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_TAKE_WHILE);
    if (!result)
        return NULL;

    rt_heap_retain(seq);
    result->data.predicated.source = seq;
    result->data.predicated.pred = pred;
    result->data.predicated.done = 0;
    return result;
}

/// @brief Wrap a source so leading elements satisfying `pred` are skipped, then the rest is
/// emitted unchanged (including elements that would have failed the predicate). Mirror of
/// `take_while` for trailing data; e.g. skip-leading-whitespace patterns.
rt_lazyseq rt_lazyseq_drop_while(rt_lazyseq seq, int8_t (*pred)(void *)) {
    if (!seq || !pred)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_DROP_WHILE);
    if (!result)
        return NULL;

    rt_heap_retain(seq);
    result->data.predicated.source = seq;
    result->data.predicated.pred = pred;
    result->data.predicated.done = 0;
    return result;
}

/// @brief Wrap two sources so the result yields all of `first`'s elements, then all of `second`'s.
/// Both sources are retained until the wrapper is released. Stops early if either source is NULL.
rt_lazyseq rt_lazyseq_concat(rt_lazyseq first, rt_lazyseq second) {
    if (!first || !second)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_CONCAT);
    if (!result)
        return NULL;

    rt_heap_retain(first);
    rt_heap_retain(second);
    result->data.concat.first = first;
    result->data.concat.second = second;
    result->data.concat.on_second = 0;
    return result;
}

/// @brief Pairwise-zip two sources via `combine(a, b)`. Stops as soon as either source is
/// exhausted (length = min). Both sources are retained.
rt_lazyseq rt_lazyseq_zip(rt_lazyseq seq1, rt_lazyseq seq2, void *(*combine)(void *, void *)) {
    if (!seq1 || !seq2 || !combine)
        return NULL;

    rt_lazyseq result = alloc_lazyseq(LAZYSEQ_ZIP);
    if (!result)
        return NULL;

    rt_heap_retain(seq1);
    rt_heap_retain(seq2);
    result->data.zip.seq1 = seq1;
    result->data.zip.seq2 = seq2;
    result->data.zip.combine = combine;
    return result;
}

//=============================================================================
// Collectors
//=============================================================================

// =============================================================================
// Collectors — terminal operations that traverse the LazySeq pipeline once and
// produce an eager result. Each call exhausts its input (or the take-prefix);
// re-traversal requires a `_reset` first.
// =============================================================================

/// @brief Materialize the entire LazySeq into a Seq[Box]. Drives `_next` until exhaustion,
/// pushing each element. Beware of infinite sources — pair with `_take` first.
void *rt_lazyseq_to_seq(rt_lazyseq seq) {
    if (!seq)
        return rt_seq_new();

    void *result = rt_seq_new();
    int8_t has_more;

    while (1) {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        rt_seq_push(result, elem);
    }

    return result;
}

/// @brief Materialize at most `n` elements into a pre-sized Seq[Box]. Safe over infinite sources;
/// the underlying source state advances by the actual number of items consumed.
void *rt_lazyseq_to_seq_n(rt_lazyseq seq, int64_t n) {
    if (!seq || n <= 0)
        return rt_seq_new();

    void *result = rt_seq_with_capacity(n);
    int8_t has_more;
    int64_t count = 0;

    while (count < n) {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        rt_seq_push(result, elem);
        count++;
    }

    return result;
}

/// @brief Left-fold (a.k.a. reduce). Applies `acc = fn(acc, elem)` for each element, starting
/// from `init`, returning the final accumulator. Returns `init` unchanged if `seq` or `fn` is NULL.
void *rt_lazyseq_fold(rt_lazyseq seq, void *init, void *(*fn)(void *, void *)) {
    if (!seq || !fn)
        return init;

    void *acc = init;
    int8_t has_more;

    while (1) {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        acc = fn(acc, elem);
    }

    return acc;
}

/// @brief Count elements by exhausting the LazySeq. Discards the actual elements (no allocation
/// for materialization). Same caveat as `to_seq`: do not call on infinite sources.
int64_t rt_lazyseq_count(rt_lazyseq seq) {
    if (!seq)
        return 0;

    int64_t count = 0;
    int8_t has_more;

    while (1) {
        rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        count++;
    }

    return count;
}

/// @brief Apply a side-effecting function to every element. The element pointer is borrowed —
/// `fn` must not retain it past its call (no lifetime guarantee after `_next`).
void rt_lazyseq_foreach(rt_lazyseq seq, void (*fn)(void *)) {
    if (!seq || !fn)
        return;

    int8_t has_more;

    while (1) {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        fn(elem);
    }
}

/// @brief Short-circuiting search: returns the first element where `pred(elem) == 1` and sets
/// `*found = 1`; returns NULL with `*found = 0` otherwise. Stops traversal at the first hit
/// (safe for infinite sources if a match exists).
void *rt_lazyseq_find(rt_lazyseq seq, int8_t (*pred)(void *), int8_t *found) {
    if (!seq || !pred) {
        if (found)
            *found = 0;
        return NULL;
    }

    int8_t has_more;

    while (1) {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        if (pred(elem)) {
            if (found)
                *found = 1;
            return elem;
        }
    }

    if (found)
        *found = 0;
    return NULL;
}

/// @brief Short-circuiting "exists": returns 1 as soon as any element satisfies `pred`. Equivalent
/// to `find != NULL` but doesn't return the matching element.
int8_t rt_lazyseq_any(rt_lazyseq seq, int8_t (*pred)(void *)) {
    if (!seq || !pred)
        return 0;

    int8_t has_more;

    while (1) {
        void *elem = rt_lazyseq_next(seq, &has_more);
        if (!has_more)
            break;
        if (pred(elem))
            return 1;
    }

    return 0;
}

/// @brief Short-circuiting "forall": returns 0 on the first element that fails `pred`, otherwise
/// 1. Empty sequence vacuously returns 1; NULL `pred` also returns 1 by convention.
int8_t rt_lazyseq_all(rt_lazyseq seq, int8_t (*pred)(void *)) {
    if (!seq || !pred)
        return 1;

    int8_t has_more;

    while (1) {
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
//
// Each `rt_lazyseq_w_*` is a thin trampoline that re-types pointers between the
// IL's `void *` ABI and the typed C functions above. The IL signature dispatcher
// only knows pointer-sized arguments; these wrappers cast in/out of `rt_lazyseq`
// (themselves typedef'd to a struct pointer) and `void *(*)()` callbacks. They
// add no logic beyond the cast — see the typed function for actual semantics.
//=============================================================================

/// @brief IL trampoline: dispatch to `rt_lazyseq_range` returning the result as `void *`.
void *rt_lazyseq_w_range(int64_t start, int64_t end, int64_t step) {
    return (void *)rt_lazyseq_range(start, end, step);
}

/// @brief IL trampoline for `rt_lazyseq_repeat`.
void *rt_lazyseq_w_repeat(void *value, int64_t count) {
    return (void *)rt_lazyseq_repeat(value, count);
}

/// @brief IL trampoline for `rt_lazyseq_next`. Discards `has_more` (caller uses `_is_exhausted`).
void *rt_lazyseq_w_next(void *seq) {
    int8_t has_more;
    return rt_lazyseq_next((rt_lazyseq)seq, &has_more);
}

/// @brief IL trampoline for `rt_lazyseq_peek`. Discards `has_more`.
void *rt_lazyseq_w_peek(void *seq) {
    int8_t has_more;
    return rt_lazyseq_peek((rt_lazyseq)seq, &has_more);
}

/// @brief IL trampoline for `rt_lazyseq_reset`.
void rt_lazyseq_w_reset(void *seq) {
    rt_lazyseq_reset((rt_lazyseq)seq);
}

/// @brief IL trampoline for `rt_lazyseq_index`.
int64_t rt_lazyseq_w_index(void *seq) {
    return rt_lazyseq_index((rt_lazyseq)seq);
}

/// @brief IL trampoline for `rt_lazyseq_is_exhausted`.
int8_t rt_lazyseq_w_is_exhausted(void *seq) {
    return rt_lazyseq_is_exhausted((rt_lazyseq)seq);
}

/// @brief IL trampoline for `rt_lazyseq_take`.
void *rt_lazyseq_w_take(void *seq, int64_t n) {
    return (void *)rt_lazyseq_take((rt_lazyseq)seq, n);
}

/// @brief IL trampoline for `rt_lazyseq_drop`.
void *rt_lazyseq_w_drop(void *seq, int64_t n) {
    return (void *)rt_lazyseq_drop((rt_lazyseq)seq, n);
}

/// @brief IL trampoline for `rt_lazyseq_concat`.
void *rt_lazyseq_w_concat(void *first, void *second) {
    return (void *)rt_lazyseq_concat((rt_lazyseq)first, (rt_lazyseq)second);
}

/// @brief IL trampoline for `rt_lazyseq_to_seq`.
void *rt_lazyseq_w_to_seq(void *seq) {
    return rt_lazyseq_to_seq((rt_lazyseq)seq);
}

/// @brief IL trampoline for `rt_lazyseq_to_seq_n`.
void *rt_lazyseq_w_to_seq_n(void *seq, int64_t n) {
    return rt_lazyseq_to_seq_n((rt_lazyseq)seq, n);
}

/// @brief IL trampoline for `rt_lazyseq_count`.
int64_t rt_lazyseq_w_count(void *seq) {
    return rt_lazyseq_count((rt_lazyseq)seq);
}

/// @brief IL trampoline for `rt_lazyseq_map`. Re-types the user fn pointer for the typed call.
void *rt_lazyseq_w_map(void *seq, void *fn) {
    return (void *)rt_lazyseq_map((rt_lazyseq)seq, (void *(*)(void *))fn);
}

/// @brief IL trampoline for `rt_lazyseq_filter`.
void *rt_lazyseq_w_filter(void *seq, void *pred) {
    return (void *)rt_lazyseq_filter((rt_lazyseq)seq, (int8_t (*)(void *))pred);
}

/// @brief IL trampoline for `rt_lazyseq_take_while`.
void *rt_lazyseq_w_take_while(void *seq, void *pred) {
    return (void *)rt_lazyseq_take_while((rt_lazyseq)seq, (int8_t (*)(void *))pred);
}

/// @brief IL trampoline for `rt_lazyseq_drop_while`.
void *rt_lazyseq_w_drop_while(void *seq, void *pred) {
    return (void *)rt_lazyseq_drop_while((rt_lazyseq)seq, (int8_t (*)(void *))pred);
}

/// @brief IL trampoline for `rt_lazyseq_find`. Discards the `found` flag (caller checks NULL).
void *rt_lazyseq_w_find(void *seq, void *pred) {
    int8_t found;
    return rt_lazyseq_find((rt_lazyseq)seq, (int8_t (*)(void *))pred, &found);
}

/// @brief IL trampoline for `rt_lazyseq_any`.
int8_t rt_lazyseq_w_any(void *seq, void *pred) {
    return rt_lazyseq_any((rt_lazyseq)seq, (int8_t (*)(void *))pred);
}

/// @brief IL trampoline for `rt_lazyseq_all`.
int8_t rt_lazyseq_w_all(void *seq, void *pred) {
    return rt_lazyseq_all((rt_lazyseq)seq, (int8_t (*)(void *))pred);
}
