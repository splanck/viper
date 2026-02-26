//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_linewriter.h
// Purpose: Buffered text file writing for Viper.IO.LineWriter, supporting creation or appending
// with Write, WriteLn, WriteChar operations and configurable line endings.
//
// Key invariants:
//   - Open mode 'append' preserves existing content; default mode truncates.
//   - WriteLn appends the configured line ending (defaults to platform native).
//   - The writer buffers output internally; Flush sends buffered data to disk.
//   - WriteChar accepts a single-character string; only the first byte is used.
//
// Ownership/Lifetime:
//   - LineWriter objects are heap-allocated; caller must close and free when done.
//   - Returned strings from read operations are newly allocated; caller must release.
//
// Links: src/runtime/io/rt_linewriter.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Open a text file for writing (creates or truncates).
    /// @param path File path as runtime string.
    /// @return LineWriter object or traps on failure.
    void *rt_linewriter_open(rt_string path);

    /// @brief Open a text file for appending (creates if needed).
    /// @param path File path as runtime string.
    /// @return LineWriter object or traps on failure.
    void *rt_linewriter_append(rt_string path);

    /// @brief Close the line writer and release resources.
    /// @param obj LineWriter object.
    void rt_linewriter_close(void *obj);

    /// @brief Write a string without newline.
    /// @param obj LineWriter object.
    /// @param text String to write.
    void rt_linewriter_write(void *obj, rt_string text);

    /// @brief Write a string followed by newline.
    /// @param obj LineWriter object.
    /// @param text String to write.
    void rt_linewriter_write_ln(void *obj, rt_string text);

    /// @brief Write a single character.
    /// @param obj LineWriter object.
    /// @param ch Character code (0-255).
    void rt_linewriter_write_char(void *obj, int64_t ch);

    /// @brief Flush buffered output to disk.
    /// @param obj LineWriter object.
    void rt_linewriter_flush(void *obj);

    /// @brief Get the current newline string.
    /// @param obj LineWriter object.
    /// @return Current newline string.
    rt_string rt_linewriter_newline(void *obj);

    /// @brief Set the newline string.
    /// @param obj LineWriter object.
    /// @param nl New newline string.
    void rt_linewriter_set_newline(void *obj, rt_string nl);

#ifdef __cplusplus
}
#endif
