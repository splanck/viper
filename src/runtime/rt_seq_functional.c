//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_seq_functional.c
// Purpose: Wrapper functions for Seq functional operations.
//
// These wrappers accept function pointers as void* and cast them internally
// to work with the IL runtime signature system.
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
