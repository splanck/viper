//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_bytes.h
// Purpose: Efficient byte array type for binary data manipulation, providing creation, element
// access, search, encoding/decoding, and conversion to/from strings and hex.
//
// Key invariants:
//   - Byte values are stored as uint8_t; set operations clamp input to [0, 255].
//   - Bytes are stored contiguously in memory.
//   - String conversion produces a copy of the UTF-8 bytes; the original is not modified.
//   - Hex encoding produces uppercase hex digits.
//
// Ownership/Lifetime:
//   - Bytes objects are heap-allocated; caller is responsible for lifetime management.
//   - rt_bytes_from_str and rt_bytes_from_hex allocate new objects; caller must free.
//
// Links: src/runtime/collections/rt_bytes.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new zero-filled byte array of given length.
    /// @param len Number of bytes to allocate (clamped to 0 if negative).
    /// @return Pointer to new Bytes object.
    void *rt_bytes_new(int64_t len);

    /// @brief Create a byte array from a string (UTF-8 bytes).
    /// @param str Source string.
    /// @return Pointer to new Bytes object containing string bytes.
    void *rt_bytes_from_str(rt_string str);

    /// @brief Create a byte array from a hexadecimal string.
    /// @param hex Hex string (must have even length).
    /// @return Pointer to new Bytes object.
    void *rt_bytes_from_hex(rt_string hex);

    /// @brief Create a byte array from an RFC 4648 Base64 string.
    /// @details Uses the standard Base64 alphabet (A–Z a–z 0–9 + /) with '=' padding.
    ///          Traps on invalid characters, invalid padding, or invalid length.
    /// @param b64 Base64 string to decode.
    /// @return Pointer to new Bytes object containing the decoded bytes.
    void *rt_bytes_from_base64(rt_string b64);

    /// @brief Get the length of a byte array.
    /// @param obj Bytes object pointer.
    /// @return Number of bytes.
    int64_t rt_bytes_len(void *obj);

    /// @brief Get a byte value at the specified index.
    /// @param obj Bytes object pointer.
    /// @param idx Index to read from.
    /// @return Byte value (0-255); traps if out of bounds.
    int64_t rt_bytes_get(void *obj, int64_t idx);

    /// @brief Set a byte value at the specified index.
    /// @param obj Bytes object pointer.
    /// @param idx Index to write to.
    /// @param val Value to write (clamped to 0-255).
    void rt_bytes_set(void *obj, int64_t idx, int64_t val);

    /// @brief Create a new byte array from a slice of this one.
    /// @param obj Source Bytes object pointer.
    /// @param start Start index (inclusive, clamped to 0).
    /// @param end End index (exclusive, clamped to len).
    /// @return Pointer to new Bytes object.
    void *rt_bytes_slice(void *obj, int64_t start, int64_t end);

    /// @brief Copy bytes from source to destination array.
    /// @param dst Destination Bytes object.
    /// @param dst_idx Destination start index.
    /// @param src Source Bytes object.
    /// @param src_idx Source start index.
    /// @param count Number of bytes to copy.
    void rt_bytes_copy(void *dst, int64_t dst_idx, void *src, int64_t src_idx, int64_t count);

    /// @brief Convert byte array to string (interprets as UTF-8).
    /// @param obj Bytes object pointer.
    /// @return New string containing the bytes.
    rt_string rt_bytes_to_str(void *obj);

    /// @brief Convert byte array to hexadecimal string.
    /// @param obj Bytes object pointer.
    /// @return New string with hex representation.
    rt_string rt_bytes_to_hex(void *obj);

    /// @brief Convert byte array to an RFC 4648 Base64 string.
    /// @details Uses the standard Base64 alphabet (A–Z a–z 0–9 + /) with '=' padding and
    ///          emits no line breaks.
    /// @param obj Bytes object pointer.
    /// @return New string with Base64 representation.
    rt_string rt_bytes_to_base64(void *obj);

    /// @brief Fill all bytes with the given value.
    /// @param obj Bytes object pointer.
    /// @param val Value to fill with (clamped to 0-255).
    void rt_bytes_fill(void *obj, int64_t val);

    /// @brief Find first occurrence of a byte value.
    /// @param obj Bytes object pointer.
    /// @param val Value to find (clamped to 0-255).
    /// @return Index of first occurrence, or -1 if not found.
    int64_t rt_bytes_find(void *obj, int64_t val);

    /// @brief Create a copy of the byte array.
    /// @param obj Bytes object pointer.
    /// @return Pointer to new Bytes object with same contents.
    void *rt_bytes_clone(void *obj);

    //=========================================================================
    // Binary Integer Read/Write Operations
    //=========================================================================

    /// @brief Read a 16-bit little-endian integer at the given offset.
    int64_t rt_bytes_read_i16le(void *obj, int64_t offset);
    /// @brief Read a 16-bit big-endian integer at the given offset.
    int64_t rt_bytes_read_i16be(void *obj, int64_t offset);
    /// @brief Read a 32-bit little-endian integer at the given offset.
    int64_t rt_bytes_read_i32le(void *obj, int64_t offset);
    /// @brief Read a 32-bit big-endian integer at the given offset.
    int64_t rt_bytes_read_i32be(void *obj, int64_t offset);
    /// @brief Read a 64-bit little-endian integer at the given offset.
    int64_t rt_bytes_read_i64le(void *obj, int64_t offset);
    /// @brief Read a 64-bit big-endian integer at the given offset.
    int64_t rt_bytes_read_i64be(void *obj, int64_t offset);

    /// @brief Write a 16-bit little-endian integer at the given offset.
    void rt_bytes_write_i16le(void *obj, int64_t offset, int64_t value);
    /// @brief Write a 16-bit big-endian integer at the given offset.
    void rt_bytes_write_i16be(void *obj, int64_t offset, int64_t value);
    /// @brief Write a 32-bit little-endian integer at the given offset.
    void rt_bytes_write_i32le(void *obj, int64_t offset, int64_t value);
    /// @brief Write a 32-bit big-endian integer at the given offset.
    void rt_bytes_write_i32be(void *obj, int64_t offset, int64_t value);
    /// @brief Write a 64-bit little-endian integer at the given offset.
    void rt_bytes_write_i64le(void *obj, int64_t offset, int64_t value);
    /// @brief Write a 64-bit big-endian integer at the given offset.
    void rt_bytes_write_i64be(void *obj, int64_t offset, int64_t value);

#ifdef __cplusplus
}
#endif
