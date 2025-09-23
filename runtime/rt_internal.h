// File: runtime/rt_internal.h
// Purpose: Defines internal runtime structures shared across implementation files.
// Key invariants: Strings use reference counts; structure layout is stable.
// Ownership/Lifetime: Caller manages lifetime of rt_string instances.
// Links: docs/codemap.md

#pragma once

#include "rt.hpp"

struct rt_string_impl
{
    int64_t refcnt;
    int64_t size;
    int64_t capacity;
    const char *data;
};
