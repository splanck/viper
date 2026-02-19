//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_binbuf.h
// Purpose: Runtime functions for a positioned binary read/write buffer.
// Key invariants: Position advances on read/write. Reads past len trap.
// Ownership/Lifetime: BinaryBuffer manages its own memory.
// Links: src/il/runtime/classes/RuntimeClasses.inc
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // ---- Constructors ----

    /// @brief Create a new binary buffer with default capacity (256).
    /// @return Pointer to new BinaryBuffer object.
    void *rt_binbuf_new(void);

    /// @brief Create a new binary buffer with custom initial capacity.
    /// @param capacity Initial capacity in bytes (clamped to 1 if < 1).
    /// @return Pointer to new BinaryBuffer object.
    void *rt_binbuf_new_cap(int64_t capacity);

    /// @brief Create a binary buffer from existing bytes data.
    /// @param bytes_obj Bytes object to copy from.
    /// @return Pointer to new BinaryBuffer with position=0, len=bytes len.
    void *rt_binbuf_from_bytes(void *bytes_obj);

    // ---- Write operations ----

    /// @brief Write a single byte (low 8 bits of value).
    /// @param obj BinaryBuffer pointer.
    /// @param value Byte value to write.
    void rt_binbuf_write_byte(void *obj, int64_t value);

    /// @brief Write a 16-bit integer in little-endian byte order.
    /// @param obj BinaryBuffer pointer.
    /// @param value Value to write (low 16 bits used).
    void rt_binbuf_write_i16le(void *obj, int64_t value);

    /// @brief Write a 16-bit integer in big-endian byte order.
    /// @param obj BinaryBuffer pointer.
    /// @param value Value to write (low 16 bits used).
    void rt_binbuf_write_i16be(void *obj, int64_t value);

    /// @brief Write a 32-bit integer in little-endian byte order.
    /// @param obj BinaryBuffer pointer.
    /// @param value Value to write (low 32 bits used).
    void rt_binbuf_write_i32le(void *obj, int64_t value);

    /// @brief Write a 32-bit integer in big-endian byte order.
    /// @param obj BinaryBuffer pointer.
    /// @param value Value to write (low 32 bits used).
    void rt_binbuf_write_i32be(void *obj, int64_t value);

    /// @brief Write a 64-bit integer in little-endian byte order.
    /// @param obj BinaryBuffer pointer.
    /// @param value Value to write.
    void rt_binbuf_write_i64le(void *obj, int64_t value);

    /// @brief Write a 64-bit integer in big-endian byte order.
    /// @param obj BinaryBuffer pointer.
    /// @param value Value to write.
    void rt_binbuf_write_i64be(void *obj, int64_t value);

    /// @brief Write a length-prefixed string (4-byte LE length + UTF-8 bytes).
    /// @param obj BinaryBuffer pointer.
    /// @param value String to write.
    void rt_binbuf_write_str(void *obj, rt_string value);

    /// @brief Write length-prefixed bytes (4-byte LE length + raw bytes).
    /// @param obj BinaryBuffer pointer.
    /// @param data Bytes object to write.
    void rt_binbuf_write_bytes(void *obj, void *data);

    // ---- Read operations ----

    /// @brief Read a single byte and advance position.
    /// @param obj BinaryBuffer pointer.
    /// @return Byte value (0-255); traps if reading past len.
    int64_t rt_binbuf_read_byte(void *obj);

    /// @brief Read a 16-bit little-endian integer and advance position.
    /// @param obj BinaryBuffer pointer.
    /// @return Decoded value; traps if reading past len.
    int64_t rt_binbuf_read_i16le(void *obj);

    /// @brief Read a 16-bit big-endian integer and advance position.
    /// @param obj BinaryBuffer pointer.
    /// @return Decoded value; traps if reading past len.
    int64_t rt_binbuf_read_i16be(void *obj);

    /// @brief Read a 32-bit little-endian integer and advance position.
    /// @param obj BinaryBuffer pointer.
    /// @return Decoded value; traps if reading past len.
    int64_t rt_binbuf_read_i32le(void *obj);

    /// @brief Read a 32-bit big-endian integer and advance position.
    /// @param obj BinaryBuffer pointer.
    /// @return Decoded value; traps if reading past len.
    int64_t rt_binbuf_read_i32be(void *obj);

    /// @brief Read a 64-bit little-endian integer and advance position.
    /// @param obj BinaryBuffer pointer.
    /// @return Decoded value; traps if reading past len.
    int64_t rt_binbuf_read_i64le(void *obj);

    /// @brief Read a 64-bit big-endian integer and advance position.
    /// @param obj BinaryBuffer pointer.
    /// @return Decoded value; traps if reading past len.
    int64_t rt_binbuf_read_i64be(void *obj);

    /// @brief Read a length-prefixed string (4-byte LE length + UTF-8 bytes).
    /// @param obj BinaryBuffer pointer.
    /// @return Decoded string; traps if reading past len.
    rt_string rt_binbuf_read_str(void *obj);

    /// @brief Read count bytes into a new Bytes object.
    /// @param obj BinaryBuffer pointer.
    /// @param count Number of bytes to read.
    /// @return New Bytes object; traps if reading past len.
    void *rt_binbuf_read_bytes(void *obj, int64_t count);

    // ---- Properties / Control ----

    /// @brief Get the current read/write position.
    /// @param obj BinaryBuffer pointer.
    /// @return Current position.
    int64_t rt_binbuf_get_position(void *obj);

    /// @brief Set the read/write position (clamped to 0..len).
    /// @param obj BinaryBuffer pointer.
    /// @param pos New position value.
    void rt_binbuf_set_position(void *obj, int64_t pos);

    /// @brief Get the logical length of the buffer.
    /// @param obj BinaryBuffer pointer.
    /// @return Number of meaningful bytes in the buffer.
    int64_t rt_binbuf_get_len(void *obj);

    /// @brief Create a Bytes object from the buffer content (0..len).
    /// @param obj BinaryBuffer pointer.
    /// @return New Bytes object containing a copy of the data.
    void *rt_binbuf_to_bytes(void *obj);

    /// @brief Reset the buffer (position=0, len=0).
    /// @param obj BinaryBuffer pointer.
    void rt_binbuf_reset(void *obj);

#ifdef __cplusplus
}
#endif
