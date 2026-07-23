//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_file_ext.h
// Purpose: High-level static file operations for Zanna.IO.File, providing ReadAllText,
// WriteAllText, ReadAllLines, AppendText, Copy, Move, Delete, and Exists.
//
// Key invariants:
//   - Operations are platform-independent and use UTF-8 for text encoding.
//   - ReadAllLines returns a Seq of strings, one per line, stripping line terminators.
//   - Whole-file writers atomically replace existing contents and preserve permission modes.
//   - All returning-functions allocate new objects; caller must release them.
//
// Ownership/Lifetime:
//   - All functions returning strings or objects allocate new instances that the caller must
//   release.
//   - Error conditions are reported via RtError out-parameters or NULL returns.
//
// Links: src/runtime/io/rt_file_ext.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Check if a file exists.
/// @param path Path to check.
/// @return 1 if file exists, 0 otherwise.
int64_t rt_io_file_exists(rt_string path);

/// @brief Test whether two paths name the same existing regular file.
/// @details Compares stable filesystem identity rather than path spelling.
/// @param left First candidate path.
/// @param right Second candidate path.
/// @return 1 when both paths resolve to the same file, otherwise 0.
int64_t rt_file_same(rt_string left, rt_string right);

/// @brief Read entire file contents as a string.
/// @param path File path to read.
/// @return File contents as string, or empty string on error.
rt_string rt_io_file_read_all_text(rt_string path);

/// @brief Atomically replace a file with string contents.
/// @details Preserves the permission mode when @p path already names a regular file.
/// @param path File path to write.
/// @param contents String contents to write.
void rt_io_file_write_all_text(rt_string path, rt_string contents);

/// @brief Append a line of text to a file.
/// @details Appends @p text followed by a single '\n' byte. Creates the file
///          if it does not already exist.
/// @param path File path to append to.
/// @param text Line contents to append (may be empty).
void rt_io_file_append_line(rt_string path, rt_string text);

/// @brief Read entire file as binary data.
/// @details Traps on I/O failures (missing file, permission errors, etc.).
/// @param path File path to read.
/// @return Bytes object containing file contents.
void *rt_io_file_read_all_bytes(rt_string path);

/// @brief Write an entire Bytes object to a file.
/// @details Atomically replaces existing contents, preserves the permission mode of an existing
///          regular file, and traps on I/O failures.
/// @param path File path to write.
/// @param bytes Bytes object to write (must be non-null).
void rt_io_file_write_all_bytes(rt_string path, void *bytes);

/// @brief Read entire file and split it into lines.
/// @details Returns a Seq of strings with line endings removed. Accepts '\n'
///          and '\r\n'. Traps on I/O failures.
/// @param path File path to read.
/// @return Seq of strings (one per line).
void *rt_io_file_read_all_lines(rt_string path);

/// @brief Write a sequence of strings as lines to a file.
/// @details Atomically replaces existing contents and preserves the permission mode when @p path
///          already names a regular file.
/// @param path File path to write.
/// @param lines Seq of strings to write.
void rt_io_file_write_all_lines(rt_string path, void *lines);

/// @brief Delete a file.
/// @param path File path to delete.
void rt_io_file_delete(rt_string path);

/// @brief Copy a file from src to dst.
/// @param src Source file path.
/// @param dst Destination file path.
void rt_file_copy(rt_string src, rt_string dst);

/// @brief Move/rename a file from src to dst.
/// @details Traps if @p dst already exists. Use rt_file_move_over to replace.
/// @param src Source file path.
/// @param dst Destination file path.
void rt_file_move(rt_string src, rt_string dst);

/// @brief Move/rename a file from src to dst, replacing dst if it exists.
/// @param src Source file path.
/// @param dst Destination file path.
void rt_file_move_over(rt_string src, rt_string dst);

/// @brief Get file size in bytes.
/// @param path File path.
/// @return File size in bytes, or -1 on error.
int64_t rt_file_size(rt_string path);

/// @brief Read entire file as binary data.
/// @param path File path to read.
/// @return Bytes object containing file contents.
void *rt_file_read_bytes(rt_string path);

/// @brief Write binary data to a file.
/// @details Atomically replaces existing contents and preserves the permission mode when @p path
///          already names a regular file.
/// @param path File path to write.
/// @param bytes Bytes object to write.
void rt_file_write_bytes(rt_string path, void *bytes);

/// @brief Read file as a sequence of lines.
/// @param path File path to read.
/// @return Seq of strings (one per line).
void *rt_file_read_lines(rt_string path);

/// @brief Write a sequence of strings as lines to a file.
/// @details Atomically replaces existing contents and preserves the permission mode when @p path
///          already names a regular file.
/// @param path File path to write.
/// @param lines Seq of strings to write.
void rt_file_write_lines(rt_string path, void *lines);

/// @brief Append text to a file.
/// @param path File path.
/// @param text Text to append.
void rt_file_append(rt_string path, rt_string text);

/// @brief Get file modification time as Unix timestamp.
/// @param path File path.
/// @return Modification time as Unix timestamp, or -1 when missing/not a regular file.
int64_t rt_file_modified(rt_string path);

/// @brief Create file or update its modification time.
/// @param path File path.
void rt_file_touch(rt_string path);

#ifdef __cplusplus
}
#endif
