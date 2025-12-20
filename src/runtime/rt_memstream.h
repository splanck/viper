//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_memstream.h
// Purpose: In-memory binary stream operations for Viper.IO.MemStream.
//
// MemStream provides stream-based binary I/O backed by memory, allowing:
// - Creating empty or pre-sized streams
// - Creating streams from existing Bytes objects
// - Reading/writing primitive types with little-endian encoding
// - Reading/writing byte arrays and strings
// - Seeking to arbitrary positions
// - Automatic expansion when writing past end
//
// This is useful for:
// - Serialization and deserialization
// - Network protocol buffers
// - Testing code that uses binary streams
// - Building binary data structures in memory
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Constructors
    //=========================================================================

    /// @brief Create a new empty expandable memory stream.
    /// @return MemStream object.
    void *rt_memstream_new(void);

    /// @brief Create a new memory stream with initial capacity hint.
    /// @param capacity Initial buffer capacity (may be 0).
    /// @return MemStream object.
    void *rt_memstream_new_capacity(int64_t capacity);

    /// @brief Create a memory stream from an existing Bytes object.
    /// @param bytes Bytes object to copy data from.
    /// @return MemStream object containing a copy of the bytes.
    void *rt_memstream_from_bytes(void *bytes);

    //=========================================================================
    // Properties
    //=========================================================================

    /// @brief Get the current position in the stream.
    /// @param obj MemStream object.
    /// @return Current position.
    int64_t rt_memstream_get_pos(void *obj);

    /// @brief Set the current position in the stream.
    /// @param obj MemStream object.
    /// @param pos New position (traps if negative).
    void rt_memstream_set_pos(void *obj, int64_t pos);

    /// @brief Get the length of data in the stream.
    /// @param obj MemStream object.
    /// @return Data length in bytes.
    int64_t rt_memstream_get_len(void *obj);

    /// @brief Get the current buffer capacity.
    /// @param obj MemStream object.
    /// @return Buffer capacity in bytes.
    int64_t rt_memstream_get_capacity(void *obj);

    //=========================================================================
    // Integer Read/Write (little-endian)
    //=========================================================================

    /// @brief Read a signed 8-bit integer.
    /// @param obj MemStream object.
    /// @return Signed byte value (-128 to 127). Traps if insufficient bytes.
    int64_t rt_memstream_read_i8(void *obj);

    /// @brief Write a signed 8-bit integer.
    /// @param obj MemStream object.
    /// @param value Value to write (truncated to 8 bits).
    void rt_memstream_write_i8(void *obj, int64_t value);

    /// @brief Read an unsigned 8-bit integer.
    /// @param obj MemStream object.
    /// @return Unsigned byte value (0 to 255). Traps if insufficient bytes.
    int64_t rt_memstream_read_u8(void *obj);

    /// @brief Write an unsigned 8-bit integer.
    /// @param obj MemStream object.
    /// @param value Value to write (truncated to 8 bits).
    void rt_memstream_write_u8(void *obj, int64_t value);

    /// @brief Read a signed 16-bit integer (little-endian).
    /// @param obj MemStream object.
    /// @return Signed 16-bit value. Traps if insufficient bytes.
    int64_t rt_memstream_read_i16(void *obj);

    /// @brief Write a signed 16-bit integer (little-endian).
    /// @param obj MemStream object.
    /// @param value Value to write (truncated to 16 bits).
    void rt_memstream_write_i16(void *obj, int64_t value);

    /// @brief Read an unsigned 16-bit integer (little-endian).
    /// @param obj MemStream object.
    /// @return Unsigned 16-bit value. Traps if insufficient bytes.
    int64_t rt_memstream_read_u16(void *obj);

    /// @brief Write an unsigned 16-bit integer (little-endian).
    /// @param obj MemStream object.
    /// @param value Value to write (truncated to 16 bits).
    void rt_memstream_write_u16(void *obj, int64_t value);

    /// @brief Read a signed 32-bit integer (little-endian).
    /// @param obj MemStream object.
    /// @return Signed 32-bit value. Traps if insufficient bytes.
    int64_t rt_memstream_read_i32(void *obj);

    /// @brief Write a signed 32-bit integer (little-endian).
    /// @param obj MemStream object.
    /// @param value Value to write (truncated to 32 bits).
    void rt_memstream_write_i32(void *obj, int64_t value);

    /// @brief Read an unsigned 32-bit integer (little-endian).
    /// @param obj MemStream object.
    /// @return Unsigned 32-bit value. Traps if insufficient bytes.
    int64_t rt_memstream_read_u32(void *obj);

    /// @brief Write an unsigned 32-bit integer (little-endian).
    /// @param obj MemStream object.
    /// @param value Value to write (truncated to 32 bits).
    void rt_memstream_write_u32(void *obj, int64_t value);

    /// @brief Read a signed 64-bit integer (little-endian).
    /// @param obj MemStream object.
    /// @return Signed 64-bit value. Traps if insufficient bytes.
    int64_t rt_memstream_read_i64(void *obj);

    /// @brief Write a signed 64-bit integer (little-endian).
    /// @param obj MemStream object.
    /// @param value Value to write.
    void rt_memstream_write_i64(void *obj, int64_t value);

    //=========================================================================
    // Float Read/Write (little-endian IEEE 754)
    //=========================================================================

    /// @brief Read a 32-bit float (little-endian).
    /// @param obj MemStream object.
    /// @return Float value (as f64). Traps if insufficient bytes.
    double rt_memstream_read_f32(void *obj);

    /// @brief Write a 32-bit float (little-endian).
    /// @param obj MemStream object.
    /// @param value Value to write (converted to 32-bit float).
    void rt_memstream_write_f32(void *obj, double value);

    /// @brief Read a 64-bit double (little-endian).
    /// @param obj MemStream object.
    /// @return Double value. Traps if insufficient bytes.
    double rt_memstream_read_f64(void *obj);

    /// @brief Write a 64-bit double (little-endian).
    /// @param obj MemStream object.
    /// @param value Value to write.
    void rt_memstream_write_f64(void *obj, double value);

    //=========================================================================
    // Bytes/String Read/Write
    //=========================================================================

    /// @brief Read count bytes as a Bytes object.
    /// @param obj MemStream object.
    /// @param count Number of bytes to read. Traps if negative or insufficient.
    /// @return New Bytes object.
    void *rt_memstream_read_bytes(void *obj, int64_t count);

    /// @brief Write a Bytes object to the stream.
    /// @param obj MemStream object.
    /// @param bytes Bytes object to write.
    void rt_memstream_write_bytes(void *obj, void *bytes);

    /// @brief Read count bytes as a string.
    /// @param obj MemStream object.
    /// @param count Number of bytes to read. Traps if negative or insufficient.
    /// @return New string.
    void *rt_memstream_read_str(void *obj, int64_t count);

    /// @brief Write a string to the stream (no length prefix).
    /// @param obj MemStream object.
    /// @param text String to write.
    void rt_memstream_write_str(void *obj, void *text);

    //=========================================================================
    // Stream Operations
    //=========================================================================

    /// @brief Get entire stream contents as a Bytes object.
    /// @param obj MemStream object.
    /// @return New Bytes object (copy of internal buffer).
    void *rt_memstream_to_bytes(void *obj);

    /// @brief Reset the stream to empty state.
    /// @param obj MemStream object.
    void rt_memstream_clear(void *obj);

    /// @brief Set position (alias for set_pos).
    /// @param obj MemStream object.
    /// @param pos New position. Traps if negative.
    void rt_memstream_seek(void *obj, int64_t pos);

    /// @brief Advance position by count bytes.
    /// @param obj MemStream object.
    /// @param count Number of bytes to skip. Traps if result would be negative.
    void rt_memstream_skip(void *obj, int64_t count);

#ifdef __cplusplus
}
#endif
