//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_linewriter.h
// Purpose: Buffered text file writing for Viper.IO.LineWriter.
//
// LineWriter provides convenient text file writing with:
// - Open/Append: Create or append to files
// - Write: Output string without newline
// - WriteLn: Output string with newline
// - WriteChar: Output single character
// - NewLine: Configurable line ending (defaults to platform)
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
