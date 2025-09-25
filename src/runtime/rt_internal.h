// File: src/runtime/rt_internal.h
// Purpose: Defines internal runtime structures shared across implementation files.
// Key invariants: Strings use reference counts; structure layout is stable.
// Ownership/Lifetime: Caller manages lifetime of rt_string instances.
// Links: docs/codemap.md

#pragma once

#include "rt.hpp"
#include "rt_heap.h"

struct rt_string_impl
{
    char *data;
    rt_heap_hdr_t *heap;
    size_t literal_len;
    size_t literal_refs;
};
