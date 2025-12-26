//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_linereader.h
// Purpose: Line-by-line text file reading for Viper.IO.LineReader.
//
// LineReader provides convenient text file reading with:
// - Read(): Read one line, strips newline (handles CR, LF, CRLF)
// - ReadChar(): Read single character as i64, -1 on EOF
// - PeekChar(): View next character without consuming it
// - ReadAll(): Read all remaining content as a string
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
