//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_binfile.h
// Purpose: Binary file stream operations for Viper.IO.BinFile, providing seekable stream-based I/O with read/write modes, position queries, and EOF detection.
//
// Key invariants:
//   - Open modes: 'r' (read-only), 'w' (write/truncate), 'rw' (read-write), 'a' (append).
//   - Seek origins: 0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END.
//   - rt_binfile_eof returns 1 only after a read that reached end-of-file.
//   - Reads past EOF return 0 bytes and set the EOF flag.
//
// Ownership/Lifetime:
//   - BinFile objects are heap-allocated; caller is responsible for closing and freeing.
//   - rt_binfile_close releases the OS file handle; the object must still be freed separately.
//
// Links: src/runtime/io/rt_binfile.c (implementation), src/runtime/core/rt_string.h
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
