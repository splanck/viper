// File: src/runtime/rt.hpp
// Purpose: Declares C runtime utilities for memory, strings, and I/O.
// Key invariants: Reference counts remain non-negative.
// Ownership/Lifetime: Caller manages returned strings.
// Links: docs/codemap.md
#pragma once

#include <stdint.h>

#include "rt_array.h"
#include "rt_string.h"

#ifdef __cplusplus
extern "C" {
#endif

    /// @brief Abort execution with message @p msg.
    /// @param msg Null-terminated message string.
    void rt_trap(const char *msg);

    /// @brief Print trap message and terminate process.
    /// @param msg Null-terminated message string.
    void rt_abort(const char *msg);

    /// @brief Print string @p s to stdout.
    /// @param s Reference-counted string.
    void rt_print_str(rt_string s);

    /// @brief Print signed 64-bit integer @p v to stdout.
    /// @param v Value to print.
    void rt_print_i64(int64_t v);

    /// @brief Print 64-bit float @p v to stdout.
    /// @param v Value to print.
    void rt_print_f64(double v);

    /// @brief Read a line from stdin.
    /// @return Newly allocated string without trailing newline.
    rt_string rt_input_line(void);

    /// @brief Allocate @p bytes of zeroed memory.
    /// @param bytes Number of bytes to allocate.
    /// @return Pointer to zeroed block or trap on failure.
    void *rt_alloc(int64_t bytes);

#ifdef __cplusplus
}
#endif

