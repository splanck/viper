// File: include/viper/runtime/rt.h
// Purpose: Provide a single stable umbrella include for the C runtime APIs.
// Key invariants: Aggregates only public rt_*.h headers in a deterministic order.
// Ownership/Lifetime: Header-only aggregator; does not own resources.
// Links: docs/codemap.md

#pragma once

#include "rt_array.h"
#include "rt_debug.h"
#include "rt_error.h"
#include "rt_file.h"
#include "rt_format.h"
#include "rt_fp.h"
#include "rt_heap.h"
#include "rt_int_format.h"
#include "rt_math.h"
#include "rt_numeric.h"
#include "rt_object.h" /* plain include; functions are no-ops when not linked */
#include "rt_random.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_trap.h"
