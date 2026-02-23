//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_seq_functional.c
// Purpose: Provides C-callable wrapper functions for Seq's higher-order
//   operations (Keep/filter, Reject, Apply/map, All, Any, Fold/reduce) that
//   accept function pointers as void* and internally cast them to the correct
//   typed function pointer before delegating to the corresponding rt_seq_*
//   implementation in rt_seq.c. This indirection is required because the IL
//   runtime signature system passes all callables as untyped void* pointers.
//
// Key invariants:
//   - Each wrapper has a 1:1 correspondence with an rt_seq_* function in rt_seq.c.
//   - The cast from void* to typed function pointer is safe because the IL
//     frontend and runtime.def ensure the actual function passed always has
//     the matching signature (predicate_fn, transform_fn, or reducer_fn).
//   - No state is held in this file; all wrappers are pure forwarding functions.
//   - Adding a new functional Seq operation requires: an rt_seq_* implementation
//     in rt_seq.c, a new typedef here if the callback type is new, and a new
//     rt_seq_*_wrapper function here.
//
// Ownership/Lifetime:
//   - No objects are allocated or freed here. Ownership follows the semantics
//     of the underlying rt_seq_* functions (returned Seqs are GC-managed).
//
// Links: src/runtime/collections/rt_seq.h (functional API declarations),
//        src/runtime/collections/rt_seq.c (actual implementations)
//
//===----------------------------------------------------------------------===//

#include "rt_seq.h"

#include <stdint.h>

// Type definitions for callbacks
typedef int8_t (*predicate_fn)(void *);
typedef void *(*transform_fn)(void *);
typedef void *(*reducer_fn)(void *, void *);

//=============================================================================
// Wrapper Functions
//=============================================================================

void *rt_seq_keep_wrapper(void *seq, void *pred)
{
    return rt_seq_keep(seq, (predicate_fn)pred);
}

void *rt_seq_reject_wrapper(void *seq, void *pred)
{
    return rt_seq_reject(seq, (predicate_fn)pred);
}

void *rt_seq_apply_wrapper(void *seq, void *fn)
{
    return rt_seq_apply(seq, (transform_fn)fn);
}

int8_t rt_seq_all_wrapper(void *seq, void *pred)
{
    return rt_seq_all(seq, (predicate_fn)pred);
}

int8_t rt_seq_any_wrapper(void *seq, void *pred)
{
    return rt_seq_any(seq, (predicate_fn)pred);
}

int8_t rt_seq_none_wrapper(void *seq, void *pred)
{
    return rt_seq_none(seq, (predicate_fn)pred);
}

int64_t rt_seq_count_where_wrapper(void *seq, void *pred)
{
    return rt_seq_count_where(seq, (predicate_fn)pred);
}

void *rt_seq_find_where_wrapper(void *seq, void *pred)
{
    return rt_seq_find_where(seq, (predicate_fn)pred);
}

void *rt_seq_take_while_wrapper(void *seq, void *pred)
{
    return rt_seq_take_while(seq, (predicate_fn)pred);
}

void *rt_seq_drop_while_wrapper(void *seq, void *pred)
{
    return rt_seq_drop_while(seq, (predicate_fn)pred);
}

void *rt_seq_fold_wrapper(void *seq, void *init, void *fn)
{
    return rt_seq_fold(seq, init, (reducer_fn)fn);
}
