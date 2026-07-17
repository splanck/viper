//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_asset_error.h
// Purpose: Thread-local diagnostics for recoverable runtime asset load failures.
// Key invariants:
//   - Content failures are reported as last-error state instead of traps.
//   - Partial degradation is reported through a bounded warning list.
// Ownership/Lifetime:
//   - Diagnostic strings live in thread-local storage owned by this module.
//   - Runtime string getters return freshly allocated rt_string handles.
// Links: rt_asset_error.c, docs/zannalib/graphics/rendering3d.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define RT_ASSET_ERROR_PRINTF(fmt_index, first_arg)                                                \
    __attribute__((format(printf, fmt_index, first_arg)))
#else
#define RT_ASSET_ERROR_PRINTF(fmt_index, first_arg)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rt_asset_error_code {
    RT_ASSET_ERROR_NONE = 0,
    RT_ASSET_ERROR_NOT_FOUND = 1,
    RT_ASSET_ERROR_UNREADABLE = 2,
    RT_ASSET_ERROR_BAD_MAGIC = 3,
    RT_ASSET_ERROR_CORRUPT = 4,
    RT_ASSET_ERROR_UNSUPPORTED = 5,
    RT_ASSET_ERROR_TOO_LARGE = 6
} rt_asset_error_code;

void rt_asset_error_clear(void);
void rt_asset_error_clear_error(void);
int rt_asset_error_begin_load(void);
void rt_asset_error_end_load_success(void);
void rt_asset_error_end_load_failure(void);
void rt_asset_error_set(rt_asset_error_code code, const char *message);
void rt_asset_error_setf(rt_asset_error_code code, const char *fmt, ...)
    RT_ASSET_ERROR_PRINTF(2, 3);
void rt_asset_error_set_if_empty(rt_asset_error_code code, const char *message);
void rt_asset_error_setf_if_empty(rt_asset_error_code code, const char *fmt, ...)
    RT_ASSET_ERROR_PRINTF(2, 3);
rt_asset_error_code rt_asset_error_get_code(void);
const char *rt_asset_error_get_message(void);
void rt_asset_error_add_warning(const char *message);
void rt_asset_error_add_warningf(const char *fmt, ...) RT_ASSET_ERROR_PRINTF(1, 2);
int64_t rt_asset_error_get_warning_count(void);
const char *rt_asset_error_get_warning(int64_t index);

/// @brief Structured import-degradation counters recorded alongside load warnings.
/// @details Loaders bump these whenever content is dropped or approximated during
///          import so tools can inspect degradation without parsing warning text.
///          Reset with the warning list at the start of each top-level load.
typedef enum rt_asset_import_stat {
    /// Primitives skipped because their mode is unsupported (glTF POINTS/LINES/...).
    RT_ASSET_IMPORT_STAT_SKIPPED_PRIMITIVES = 0,
    /// Vertices whose >4 meaningful bone influences were folded to the top four.
    RT_ASSET_IMPORT_STAT_TRUNCATED_INFLUENCE_VERTICES,
    /// Vertices whose meaningful joints exceeded the runtime bone limit and were dropped.
    RT_ASSET_IMPORT_STAT_OUT_OF_RANGE_JOINT_VERTICES,
    /// Optional (extensionsUsed) extensions the loader does not interpret.
    RT_ASSET_IMPORT_STAT_IGNORED_EXTENSIONS,
    /// Skeletal CUBICSPLINE channels baked to sampled keys (playback is linear/slerp).
    RT_ASSET_IMPORT_STAT_BAKED_CUBIC_SPLINE_CHANNELS,
    RT_ASSET_IMPORT_STAT_COMPRESSED_ANIMATION_KEYS_DROPPED,
    RT_ASSET_IMPORT_STAT_COUNT
} rt_asset_import_stat;

/// @brief Add @p amount to one structured import counter (negative/invalid inputs ignored).
void rt_asset_error_add_import_stat(rt_asset_import_stat stat, int64_t amount);
/// @brief Read one structured import counter for the current thread's last load.
int64_t rt_asset_error_get_import_stat(rt_asset_import_stat stat);
/// @brief Return whether the current load-error message was truncated to fit storage.
int rt_asset_error_get_message_was_truncated(void);
/// @brief Return whether warning @p index was truncated to fit storage.
int rt_asset_error_get_warning_was_truncated(int64_t index);
/// @brief Return how many warnings were suppressed after the bounded warning list filled.
int64_t rt_asset_error_get_warning_suppressed_count(void);

rt_string rt_assets3d_get_last_load_error(void);
int64_t rt_assets3d_get_last_load_error_code(void);
int64_t rt_assets3d_get_load_warning_count(void);
rt_string rt_assets3d_get_load_warning(int64_t index);
rt_string rt_assets3d_get_load_warnings(void);
/// @brief JSON summary of the last load's degradation: the structured counters plus
///        the warning strings, e.g. `{"skippedPrimitives":1,...,"warnings":["..."]}`.
rt_string rt_assets3d_get_import_report(void);

#ifdef __cplusplus
}
#endif

#undef RT_ASSET_ERROR_PRINTF
