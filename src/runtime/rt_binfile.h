//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_binfile.h
// Purpose: Binary file stream operations for Viper.IO.BinFile.
//
// BinFile provides stream-based binary file I/O, allowing:
// - Opening files with different modes (read, write, read/write, append)
// - Reading/writing raw bytes and Bytes objects
// - Seeking to arbitrary positions
// - Querying position, size, and EOF status
//
// Open modes:
//   "r"  - Read only (file must exist)
//   "w"  - Write only (creates or truncates)
//   "rw" - Read/write (file must exist)
//   "a"  - Append (creates if needed, writes at end)
//
// Seek origins:
//   0 - SEEK_SET (from beginning)
//   1 - SEEK_CUR (from current position)
//   2 - SEEK_END (from end of file)
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Open a binary file for streaming I/O.
    /// @param path File path as runtime string.
    /// @param mode Mode string: "r", "w", "rw", or "a".
    /// @return BinFile object or NULL on failure.
    void *rt_binfile_open(void *path, void *mode);

    /// @brief Close the binary file and release resources.
    /// @param obj BinFile object.
    void rt_binfile_close(void *obj);

    /// @brief Read bytes from file into a Bytes object.
    /// @param obj BinFile object.
    /// @param bytes Target Bytes object.
    /// @param offset Starting offset in the Bytes object.
    /// @param count Maximum number of bytes to read.
    /// @return Number of bytes actually read.
    int64_t rt_binfile_read(void *obj, void *bytes, int64_t offset, int64_t count);

    /// @brief Write bytes from a Bytes object to file.
    /// @param obj BinFile object.
    /// @param bytes Source Bytes object.
    /// @param offset Starting offset in the Bytes object.
    /// @param count Number of bytes to write.
    void rt_binfile_write(void *obj, void *bytes, int64_t offset, int64_t count);

    /// @brief Read a single byte from the file.
    /// @param obj BinFile object.
    /// @return Byte value (0-255) or -1 on EOF/error.
    int64_t rt_binfile_read_byte(void *obj);

    /// @brief Write a single byte to the file.
    /// @param obj BinFile object.
    /// @param byte Byte value to write (0-255).
    void rt_binfile_write_byte(void *obj, int64_t byte);

    /// @brief Seek to a position in the file.
    /// @param obj BinFile object.
    /// @param offset Byte offset.
    /// @param origin 0=start, 1=current, 2=end.
    /// @return New position or -1 on error.
    int64_t rt_binfile_seek(void *obj, int64_t offset, int64_t origin);

    /// @brief Get the current file position.
    /// @param obj BinFile object.
    /// @return Current position or -1 on error.
    int64_t rt_binfile_pos(void *obj);

    /// @brief Get the file size.
    /// @param obj BinFile object.
    /// @return File size in bytes or -1 on error.
    int64_t rt_binfile_size(void *obj);

    /// @brief Flush any buffered writes to disk.
    /// @param obj BinFile object.
    void rt_binfile_flush(void *obj);

    /// @brief Check if at end of file.
    /// @param obj BinFile object.
    /// @return 1 if at EOF, 0 otherwise.
    int8_t rt_binfile_eof(void *obj);

#ifdef __cplusplus
}
#endif
