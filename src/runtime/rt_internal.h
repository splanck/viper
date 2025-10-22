// File: src/runtime/rt_internal.h
// Purpose: Defines internal runtime structures shared across implementation files.
// Key invariants: Strings use reference counts; structure layout is stable.
// Ownership/Lifetime: Caller manages lifetime of rt_string instances.
// Links: docs/codemap.md

#pragma once

#include "rt_heap.h"
#include "rt.hpp"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rt_input_grow_result
{
    RT_INPUT_GROW_OK = 0,
    RT_INPUT_GROW_ALLOC_FAILED = 1,
    RT_INPUT_GROW_OVERFLOW = 2
} rt_input_grow_result;

rt_input_grow_result rt_input_try_grow(char **buf, size_t *cap);

struct RtFileChannelEntry;

typedef struct RtFileChannelTestState
{
    struct RtFileChannelEntry *entries;
    size_t count;
    size_t capacity;
} RtFileChannelTestState;

RtFileChannelTestState rt_file_test_capture_state(void);
void rt_file_test_restore_state(RtFileChannelTestState state);
void rt_file_test_preset_growth_overflow(size_t capacity);
size_t rt_file_test_max_capacity(void);

#ifdef __cplusplus
}
#endif

struct rt_string_impl
{
    char *data;
    rt_heap_hdr_t *heap;
    size_t literal_len;
    size_t literal_refs;
};
