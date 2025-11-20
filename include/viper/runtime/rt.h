// File: include/viper/runtime/rt.h
// Purpose: Provide a single stable umbrella include for the C runtime APIs.
// Key invariants: Aggregates only public rt_*.h headers in a deterministic order.
// Ownership/Lifetime: Header-only aggregator; does not own resources.
// Links: docs/codemap.md

#pragma once

#include "rt_array.h"
#include "rt_array_str.h"
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
#include "rt_ns_bridge.h"
#include "rt_modvar.h"
#include "rt_args.h"
#include "rt_trap.h"

#ifdef __cplusplus
extern "C" {
#endif

// Sleep for the specified number of milliseconds.
// Clamps negative values to zero; uses monotonic clock where available.
void rt_sleep_ms(int32_t ms);

// Return monotonic time in milliseconds since an unspecified epoch.
// Uses a steady clock; values are non-decreasing and suitable for measuring durations.
int64_t rt_timer_ms(void);

#ifdef __cplusplus
} // extern "C"
#endif
