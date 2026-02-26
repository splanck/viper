//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_linereader.h
// Purpose: Line-by-line text file reading for Viper.IO.LineReader, stripping newline characters
// (CR, LF, CRLF) and providing character-level peek/read access.
//
// Key invariants:
//   - Read strips the line terminator (CR, LF, or CRLF) from each line.
//   - ReadChar returns the raw character as i64, or -1 at EOF.
//   - PeekChar views the next character without advancing the position.
//   - The reader buffers input internally for efficiency.
//
// Ownership/Lifetime:
//   - LineReader objects are heap-allocated; caller must close and free when done.
//   - Returned strings from Read are newly allocated; caller must release them.
//
// Links: src/runtime/io/rt_linereader.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Open a text file for line-by-line reading.
    /// @param path File path as runtime string.
    /// @return LineReader object or traps on failure.
    void *rt_linereader_open(rt_string path);

    /// @brief Close the line reader and release resources.
    /// @param obj LineReader object.
    void rt_linereader_close(void *obj);

    /// @brief Read one line from the file.
    /// @param obj LineReader object.
    /// @return Line as string (without newline), or empty string at EOF.
    rt_string rt_linereader_read(void *obj);

    /// @brief Read a single character from the file.
    /// @param obj LineReader object.
    /// @return Character code (0-255) or -1 on EOF.
    int64_t rt_linereader_read_char(void *obj);

    /// @brief Peek at the next character without consuming it.
    /// @param obj LineReader object.
    /// @return Character code (0-255) or -1 on EOF.
    int64_t rt_linereader_peek_char(void *obj);

    /// @brief Read all remaining content from the file.
    /// @param obj LineReader object.
    /// @return Remaining content as string.
    rt_string rt_linereader_read_all(void *obj);

    /// @brief Check if at end of file.
    /// @param obj LineReader object.
    /// @return 1 if at EOF, 0 otherwise.
    int8_t rt_linereader_eof(void *obj);

#ifdef __cplusplus
}
#endif
