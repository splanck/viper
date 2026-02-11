//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/runtime/rt.h
// Purpose: Provide a single stable umbrella include for the C runtime APIs.
// Key invariants: Aggregates only public rt_*.h headers in a deterministic order.
// Ownership/Lifetime: Header-only aggregator; does not own resources.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_args.h"
#include "rt_array.h"
#include "rt_array_str.h"
#include "rt_debug.h"
#include "rt_error.h"
#include "rt_file.h"
#include "rt_format.h"
#include "rt_fp.h"
#include "rt_heap.h"
#include "rt_int_format.h"
#include "rt_list.h"
#include "rt_math.h"
#include "rt_modvar.h"
#include "rt_ns_bridge.h"
#include "rt_numeric.h"
#include "rt_object.h" /* plain include; functions are no-ops when not linked */
#include "rt_random.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_threads.h"
#include "rt_trap.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Sleep for approximately @p ms milliseconds.
    /// @details Provides a simple timing primitive for BASIC and runtime consumers.
    ///          Uses a monotonic/steady clock where available; clamps negatives to 0.
    /// @param ms Milliseconds to sleep; negative values are treated as 0.
    /// @note Thread-safe; blocks the calling thread only.
    void rt_sleep_ms(int32_t ms);

    /// @brief Return monotonic time in milliseconds since an unspecified epoch.
    /// @details Reads a steady clock whose values are non-decreasing, suitable
    ///          for measuring elapsed durations without wall-clock adjustments.
    /// @return Milliseconds from a monotonic source (non-decreasing).
    int64_t rt_timer_ms(void);

    //=============================================================================
    // Viper.Time.Clock functions
    //=============================================================================

    /// @brief Sleep for approximately @p ms milliseconds (i64 interface).
    /// @details Viper.Time.Clock.Sleep entry point. Delegates to rt_sleep_ms
    ///          after clamping to the int32 range.
    /// @param ms Milliseconds to sleep; negative values are treated as 0.
    void rt_clock_sleep(int64_t ms);

    /// @brief Return monotonic time in milliseconds since an unspecified epoch.
    /// @details Viper.Time.Clock.Ticks entry point. Delegates to rt_timer_ms.
    /// @return Milliseconds from a monotonic source (non-decreasing).
    int64_t rt_clock_ticks(void);

    /// @brief Return monotonic time in microseconds since an unspecified epoch.
    /// @details Viper.Time.Clock.TicksUs entry point for high-precision timing.
    ///          Reads a steady clock at microsecond resolution.
    /// @return Microseconds from a monotonic source (non-decreasing).
    int64_t rt_clock_ticks_us(void);

    // --- High-level file helpers for Viper.IO.File ---

    /// @brief Return 1 if the file at @p path exists, 0 otherwise.
    /// @details Converts to host path and calls stat/access. Provides a fast
    ///          existence check for BASIC IO helpers.
    /// @param path Runtime string containing a path (platform encoding).
    /// @return 1 when the file exists; 0 otherwise.
    /// @note Silently returns 0 on invalid paths or conversion failures.
    int64_t rt_io_file_exists(rt_string path);

    /// @brief Read entire file contents into a runtime string (empty on error).
    /// @details Opens in binary mode, reads all bytes, and constructs an rt_string.
    ///          Convenience for simple file-based programs and tests.
    /// @param path Runtime string containing a path (platform encoding).
    /// @return New runtime string with file contents; empty string on errors.
    /// @note Intended for text; binary data is preserved but may include NULs.
    rt_string rt_io_file_read_all_text(rt_string path);

    /// @brief Write entire @p contents to @p path, truncating or creating the file.
    /// @details Opens with O_TRUNC|O_CREAT in binary mode, writes fully, retrying
    ///          on EINTR. Provides a simple save operation.
    /// @param path     Target path as a runtime string.
    /// @param contents Text to write verbatim (bytes copied as-is).
    /// @note Silently ignores failures; verify via subsequent existence/read.
    ///       Operation is not atomic across crashes; callers can write to a temp
    ///       file and rename for safety.
    void rt_io_file_write_all_text(rt_string path, rt_string contents);

    /// @brief Delete the file at @p path; errors are silently ignored.
    /// @details Converts to host path and calls unlink. Allows BASIC programs
    ///          to clean up temporary files.
    /// @param path Runtime string path.
    /// @note No error is reported for non-existent paths or permission issues.
    void rt_io_file_delete(rt_string path);

#ifdef __cplusplus
} // extern "C"
#endif
