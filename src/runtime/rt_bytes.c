//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_bytes.c
// Purpose: Implement efficient byte array storage for binary data.
// Structure: [len | data*]
// - len: number of bytes
// - data: contiguous byte storage
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"

#include "rt_codec.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

/// @brief Internal implementation structure for the Bytes type.
///
/// The Bytes container stores a contiguous array of raw bytes with O(1)
/// random access. Unlike strings which are immutable and UTF-8 encoded,
/// Bytes are mutable and hold raw binary data.
///
/// **Memory layout:**
/// ```
/// +------------------+---------------------------+
/// | rt_bytes_impl    | data bytes (inline)       |
/// | [len][data ptr]  | [b0][b1][b2]...[bN-1]     |
/// +------------------+---------------------------+
///                    ^
///                    |
///              data pointer points here
/// ```
///
/// The data array is allocated inline immediately after the structure header
/// for better cache locality and to avoid a separate heap allocation.
typedef struct rt_bytes_impl
{
    int64_t len;   ///< Number of bytes stored (0 to INT64_MAX).
    uint8_t *data; ///< Pointer to inline byte storage (immediately follows struct).
} rt_bytes_impl;

/// @brief Allocates a new Bytes object with the specified length.
///
/// This internal helper performs the actual memory allocation for a Bytes
/// object. It allocates a single contiguous block containing both the
/// rt_bytes_impl header and the byte array, setting up the data pointer
/// to reference the inline storage.
///
/// **Allocation calculation:**
/// ```
/// total_size = sizeof(rt_bytes_impl) + len
/// ```
///
/// @param len The number of bytes to allocate. Negative values are treated
///            as 0. Values exceeding available memory will trigger a trap.
///
/// @return A pointer to the newly allocated rt_bytes_impl structure.
///
/// @note Traps with "Bytes: memory allocation failed" if allocation fails
///       or if the length would cause integer overflow.
/// @note The allocated bytes are zero-initialized.
static rt_bytes_impl *rt_bytes_alloc(int64_t len)
{
    if (len < 0)
        len = 0;

    size_t total = sizeof(rt_bytes_impl);
    if (len > 0)
    {
        if ((uint64_t)len > (uint64_t)SIZE_MAX - total)
            rt_trap("Bytes: memory allocation failed");
        total += (size_t)len;
    }
    if (total > (size_t)INT64_MAX)
        rt_trap("Bytes: memory allocation failed");

    rt_bytes_impl *bytes = (rt_bytes_impl *)rt_obj_new_i64(0, (int64_t)total);
    if (!bytes)
        rt_trap("Bytes: memory allocation failed");

    bytes->len = len;
    bytes->data = len > 0 ? ((uint8_t *)bytes + sizeof(rt_bytes_impl)) : NULL;
    return bytes;
}

/// @brief Base64 character lookup table for encoding (RFC 4648).
///
/// Maps 6-bit values (0-63) to the standard Base64 alphabet:
/// - 0-25: 'A'-'Z'
/// - 26-51: 'a'-'z'
/// - 52-61: '0'-'9'
/// - 62: '+'
/// - 63: '/'
static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// @brief Converts a Base64 character to its numeric value.
///
/// Parses a single Base64 character and returns its 6-bit value according
/// to RFC 4648. The padding character '=' is treated specially.
///
/// @param c The character to convert.
///
/// @return One of:
///         - 0-63: The 6-bit value for valid Base64 characters
///         - -2: For the padding character '='
///         - -1: For any invalid character
static int b64_digit_value(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    if (c == '=')
        return -2;
    return -1;
}

/// @brief Creates a new Bytes object with the specified length.
///
/// Allocates and initializes a new byte array of the given length. All bytes
/// are initialized to zero. The Bytes object is allocated through Viper's
/// GC-managed object system and will be automatically freed when no longer
/// referenced.
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.New(1024)    ' Create 1KB buffer
/// data.Set(0, 255)              ' Set first byte
/// Print data.Get(0)             ' Outputs: 255
/// ```
///
/// @param len The number of bytes to allocate. Negative values are treated
///            as 0 (creates an empty Bytes object).
///
/// @return A pointer to the newly created Bytes object.
///
/// @note O(n) time complexity due to zero-initialization.
/// @note Traps if memory allocation fails.
///
/// @see rt_bytes_from_str For creating Bytes from a string
/// @see rt_bytes_from_hex For creating Bytes from hex encoding
/// @see rt_bytes_from_base64 For creating Bytes from Base64 encoding
void *rt_bytes_new(int64_t len)
{
    return rt_bytes_alloc(len);
}

/// @brief Creates a Bytes object from a string's UTF-8 bytes.
///
/// Converts a string to its raw UTF-8 byte representation. This is useful
/// for working with binary protocols or file I/O where you need the actual
/// bytes that make up the string.
///
/// **Important:** The resulting Bytes contains the UTF-8 encoded bytes of
/// the string, NOT the string's characters. Multi-byte UTF-8 characters
/// will occupy multiple bytes in the result.
///
/// **Usage example:**
/// ```
/// Dim str = "Hello"
/// Dim data = Bytes.FromString(str)
/// Print data.Len       ' Outputs: 5
/// Print data.Get(0)    ' Outputs: 72 (ASCII 'H')
/// ```
///
/// @param str The source string to convert. If NULL, returns empty Bytes.
///
/// @return A new Bytes object containing the UTF-8 bytes of the string.
///
/// @note O(n) time complexity where n is the string length.
/// @note The resulting Bytes does NOT include a null terminator.
///
/// @see rt_bytes_to_str For the reverse operation
void *rt_bytes_from_str(rt_string str)
{
    const char *cstr = rt_string_cstr(str);
    if (!cstr)
        return rt_bytes_new(0);

    size_t len = strlen(cstr);

    rt_bytes_impl *bytes = rt_bytes_alloc((int64_t)len);
    if (len > 0)
        memcpy(bytes->data, cstr, len);
    return bytes;
}

/// @brief Creates a Bytes object from a hexadecimal string.
///
/// Decodes a hexadecimal string into raw bytes. Each pair of hex characters
/// in the input string becomes one byte in the output. Both uppercase and
/// lowercase hex digits are accepted.
///
/// **Hex encoding format:**
/// ```
/// Input:  "48656c6c6f"
/// Output: [0x48, 0x65, 0x6c, 0x6c, 0x6f] = "Hello" in ASCII
/// ```
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.FromHex("deadbeef")
/// Print data.Len       ' Outputs: 4
/// Print data.Get(0)    ' Outputs: 222 (0xDE)
/// ```
///
/// @param hex The hexadecimal string to decode. Must have even length.
///            If NULL, returns empty Bytes.
///
/// @return A new Bytes object containing the decoded bytes.
///
/// @note O(n) time complexity where n is the hex string length.
/// @note Traps with "Bytes.FromHex: hex string length must be even" if
///       the input has odd length.
/// @note Traps with "Bytes.FromHex: invalid hex character" if the input
///       contains non-hexadecimal characters.
///
/// @see rt_bytes_to_hex For the reverse operation
void *rt_bytes_from_hex(rt_string hex)
{
    const char *hex_str = rt_string_cstr(hex);
    if (!hex_str)
        return rt_bytes_new(0);

    size_t hex_len = strlen(hex_str);

    if (hex_len % 2 != 0)
        rt_trap("Bytes.FromHex: hex string length must be even");

    int64_t len = (int64_t)(hex_len / 2);
    rt_bytes_impl *bytes = rt_bytes_alloc(len);
    for (int64_t i = 0; i < len; i++)
    {
        int hi = rt_hex_digit_value(hex_str[i * 2]);
        int lo = rt_hex_digit_value(hex_str[i * 2 + 1]);

        if (hi < 0 || lo < 0)
            rt_trap("Bytes.FromHex: invalid hex character");

        bytes->data[i] = (uint8_t)((hi << 4) | lo);
    }

    return bytes;
}

/// @brief Returns the length of the Bytes object in bytes.
///
/// Gets the number of bytes stored in the Bytes object. This is the value
/// specified when the object was created and does not change during the
/// object's lifetime.
///
/// @param obj Pointer to a Bytes object. If NULL, returns 0.
///
/// @return The number of bytes, or 0 if obj is NULL.
///
/// @note O(1) time complexity.
int64_t rt_bytes_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_bytes_impl *)obj)->len;
}

/// @brief Gets the byte value at the specified index.
///
/// Retrieves a single byte from the Bytes object. The returned value is
/// in the range 0-255 (unsigned byte).
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.FromHex("deadbeef")
/// Print data.Get(0)    ' Outputs: 222 (0xDE)
/// Print data.Get(1)    ' Outputs: 173 (0xAD)
/// ```
///
/// @param obj Pointer to a Bytes object. Must not be NULL.
/// @param idx Zero-based index of the byte to retrieve. Must be in range
///            [0, len-1].
///
/// @return The byte value at the specified index (0-255).
///
/// @note O(1) time complexity.
/// @note Traps with "Bytes.Get: null bytes" if obj is NULL.
/// @note Traps with "Bytes.Get: index out of bounds" if idx is negative
///       or >= len.
///
/// @see rt_bytes_set For modifying a byte
int64_t rt_bytes_get(void *obj, int64_t idx)
{
    if (!obj)
        rt_trap("Bytes.Get: null bytes");

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;

    if (idx < 0 || idx >= bytes->len)
        rt_trap("Bytes.Get: index out of bounds");

    return bytes->data[idx];
}

/// @brief Sets the byte value at the specified index.
///
/// Modifies a single byte in the Bytes object. The value is masked to
/// 8 bits (val & 0xFF), so values outside 0-255 are truncated.
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.New(4)
/// data.Set(0, 0xDE)
/// data.Set(1, 0xAD)
/// data.Set(2, 0xBE)
/// data.Set(3, 0xEF)
/// Print data.ToHex()   ' Outputs: "deadbeef"
/// ```
///
/// @param obj Pointer to a Bytes object. Must not be NULL.
/// @param idx Zero-based index of the byte to modify. Must be in range
///            [0, len-1].
/// @param val The value to set. Only the low 8 bits (val & 0xFF) are used.
///
/// @note O(1) time complexity.
/// @note Traps with "Bytes.Set: null bytes" if obj is NULL.
/// @note Traps with "Bytes.Set: index out of bounds" if idx is negative
///       or >= len.
///
/// @see rt_bytes_get For reading a byte
/// @see rt_bytes_fill For setting all bytes to the same value
void rt_bytes_set(void *obj, int64_t idx, int64_t val)
{
    if (!obj)
        rt_trap("Bytes.Set: null bytes");

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;

    if (idx < 0 || idx >= bytes->len)
        rt_trap("Bytes.Set: index out of bounds");

    bytes->data[idx] = (uint8_t)(val & 0xFF);
}

/// @brief Creates a new Bytes object containing a slice of the original.
///
/// Extracts a contiguous range of bytes from the source Bytes object and
/// returns them as a new Bytes object. The slice includes bytes from index
/// `start` up to but not including `end` (half-open interval [start, end)).
///
/// **Bounds handling:**
/// - Negative start values are clamped to 0
/// - End values beyond length are clamped to length
/// - If start >= end after clamping, returns empty Bytes
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.FromString("Hello, World!")
/// Dim slice = data.Slice(0, 5)
/// Print slice.ToString()    ' Outputs: "Hello"
/// ```
///
/// @param obj Pointer to a Bytes object. If NULL, returns empty Bytes.
/// @param start Starting index (inclusive). Clamped to [0, len].
/// @param end Ending index (exclusive). Clamped to [0, len].
///
/// @return A new Bytes object containing the sliced bytes.
///
/// @note O(n) time complexity where n is the slice length.
/// @note The original Bytes is not modified.
///
/// @see rt_bytes_copy For copying bytes between Bytes objects
/// @see rt_bytes_clone For creating a full copy
void *rt_bytes_slice(void *obj, int64_t start, int64_t end)
{
    if (!obj)
        return rt_bytes_new(0);

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;

    // Clamp bounds
    if (start < 0)
        start = 0;
    if (end > bytes->len)
        end = bytes->len;
    if (start >= end)
        return rt_bytes_new(0);

    int64_t new_len = end - start;

    rt_bytes_impl *result = rt_bytes_alloc(new_len);
    if (new_len > 0)
        memcpy(result->data, bytes->data + start, (size_t)new_len);
    return result;
}

/// @brief Copies bytes from one Bytes object to another.
///
/// Copies a range of bytes from the source Bytes object to a destination
/// Bytes object. The copy is performed using memmove, so overlapping copies
/// (when src and dst are the same object) are handled correctly.
///
/// **Usage example:**
/// ```
/// Dim src = Bytes.FromString("Hello")
/// Dim dst = Bytes.New(10)
/// Bytes.Copy(dst, 0, src, 0, 5)    ' Copy "Hello" to start of dst
/// Bytes.Copy(dst, 5, src, 0, 5)    ' Copy "Hello" again
/// Print dst.ToString()             ' Outputs: "HelloHello"
/// ```
///
/// @param dst Destination Bytes object. Must not be NULL.
/// @param dst_idx Starting index in the destination.
/// @param src Source Bytes object. Must not be NULL.
/// @param src_idx Starting index in the source.
/// @param count Number of bytes to copy.
///
/// @note O(n) time complexity where n is the count.
/// @note Traps with "Bytes.Copy: null destination" if dst is NULL.
/// @note Traps with "Bytes.Copy: null source" if src is NULL.
/// @note Traps with "Bytes.Copy: count cannot be negative" if count < 0.
/// @note Traps with "Bytes.Copy: source range out of bounds" if source range
///       exceeds source bounds.
/// @note Traps with "Bytes.Copy: destination range out of bounds" if dest
///       range exceeds destination bounds.
///
/// @see rt_bytes_slice For extracting bytes as a new object
void rt_bytes_copy(void *dst, int64_t dst_idx, void *src, int64_t src_idx, int64_t count)
{
    if (!dst)
        rt_trap("Bytes.Copy: null destination");
    if (!src)
        rt_trap("Bytes.Copy: null source");

    rt_bytes_impl *dst_bytes = (rt_bytes_impl *)dst;
    rt_bytes_impl *src_bytes = (rt_bytes_impl *)src;

    if (count < 0)
        rt_trap("Bytes.Copy: count cannot be negative");

    if (count == 0)
        return;

    if (src_idx < 0 || src_idx + count > src_bytes->len)
        rt_trap("Bytes.Copy: source range out of bounds");

    if (dst_idx < 0 || dst_idx + count > dst_bytes->len)
        rt_trap("Bytes.Copy: destination range out of bounds");

    memmove(dst_bytes->data + dst_idx, src_bytes->data + src_idx, (size_t)count);
}

/// @brief Converts the Bytes object to a string.
///
/// Interprets the bytes as UTF-8 encoded text and returns a string.
/// This is the inverse of rt_bytes_from_str.
///
/// **Warning:** If the bytes are not valid UTF-8, the resulting string
/// may be malformed. No UTF-8 validation is performed.
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.FromHex("48656c6c6f")
/// Print data.ToString()    ' Outputs: "Hello"
/// ```
///
/// @param obj Pointer to a Bytes object. If NULL, returns empty string.
///
/// @return A string containing the bytes interpreted as UTF-8.
///
/// @note O(n) time complexity where n is the byte count.
///
/// @see rt_bytes_from_str For the reverse operation
rt_string rt_bytes_to_str(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;
    return rt_string_from_bytes((const char *)bytes->data, (size_t)bytes->len);
}

/// @brief Converts the Bytes object to a hexadecimal string.
///
/// Encodes each byte as two lowercase hexadecimal characters. The resulting
/// string has exactly twice the length of the Bytes object.
///
/// **Hex encoding format:**
/// ```
/// Input:  [0xDE, 0xAD, 0xBE, 0xEF]
/// Output: "deadbeef"
/// ```
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.New(4)
/// data.Set(0, 0xDE)
/// data.Set(1, 0xAD)
/// data.Set(2, 0xBE)
/// data.Set(3, 0xEF)
/// Print data.ToHex()    ' Outputs: "deadbeef"
/// ```
///
/// @param obj Pointer to a Bytes object. If NULL, returns empty string.
///
/// @return A lowercase hexadecimal string representing the bytes.
///
/// @note O(n) time complexity where n is the byte count.
/// @note Always produces lowercase hex digits (a-f, not A-F).
///
/// @see rt_bytes_from_hex For the reverse operation
rt_string rt_bytes_to_hex(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;

    if (bytes->len == 0)
        return rt_string_from_bytes("", 0);

    // Use shared codec utility for hex encoding
    return rt_codec_hex_enc_bytes(bytes->data, (size_t)bytes->len);
}

/// @brief Converts the Bytes object to a Base64-encoded string.
///
/// Encodes the bytes using the standard Base64 alphabet as specified in
/// RFC 4648. The output uses standard padding ('=') and contains no line
/// breaks or whitespace.
///
/// **Base64 encoding:**
/// Every 3 bytes of input produce 4 characters of output. If the input
/// length is not a multiple of 3, padding is added:
/// - 1 byte input → 2 chars + "==" padding
/// - 2 bytes input → 3 chars + "=" padding
/// - 3 bytes input → 4 chars, no padding
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.FromString("Hello")
/// Print data.ToBase64()    ' Outputs: "SGVsbG8="
/// ```
///
/// @param obj Pointer to a Bytes object. If NULL or empty, returns empty string.
///
/// @return A Base64-encoded string representing the bytes.
///
/// @note O(n) time complexity where n is the byte count.
/// @note Uses standard Base64 alphabet (A-Za-z0-9+/) with '=' padding.
/// @note No line breaks are inserted (single continuous string).
///
/// @see rt_bytes_from_base64 For the reverse operation
rt_string rt_bytes_to_base64(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("", 0);

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;
    if (bytes->len <= 0 || !bytes->data)
        return rt_string_from_bytes("", 0);

    size_t input_len = (size_t)bytes->len;
    size_t output_len = ((input_len + 2) / 3) * 4;

    char *out = (char *)malloc(output_len + 1);
    if (!out)
        rt_trap("Bytes: memory allocation failed");

    size_t i = 0;
    size_t o = 0;
    while (i + 3 <= input_len)
    {
        uint32_t triple = ((uint32_t)bytes->data[i] << 16) | ((uint32_t)bytes->data[i + 1] << 8) |
                          bytes->data[i + 2];
        out[o++] = b64_chars[(triple >> 18) & 0x3F];
        out[o++] = b64_chars[(triple >> 12) & 0x3F];
        out[o++] = b64_chars[(triple >> 6) & 0x3F];
        out[o++] = b64_chars[triple & 0x3F];
        i += 3;
    }

    if (i < input_len)
    {
        uint32_t triple = (uint32_t)bytes->data[i] << 16;
        int two = 0;
        if (i + 1 < input_len)
        {
            triple |= (uint32_t)bytes->data[i + 1] << 8;
            two = 1;
        }

        out[o++] = b64_chars[(triple >> 18) & 0x3F];
        out[o++] = b64_chars[(triple >> 12) & 0x3F];
        if (two)
        {
            out[o++] = b64_chars[(triple >> 6) & 0x3F];
            out[o++] = '=';
        }
        else
        {
            out[o++] = '=';
            out[o++] = '=';
        }
    }

    out[o] = '\0';
    rt_string result = rt_string_from_bytes(out, o);
    free(out);
    return result;
}

/// @brief Creates a Bytes object by decoding a Base64-encoded string.
///
/// Decodes a Base64 string using the standard alphabet as specified in
/// RFC 4648. The input must be properly padded with '=' characters.
///
/// **Base64 decoding:**
/// Every 4 characters of input produce 3 bytes of output (less for
/// padded inputs):
/// - 4 chars with no padding → 3 bytes
/// - 4 chars with "=" padding → 2 bytes
/// - 4 chars with "==" padding → 1 byte
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.FromBase64("SGVsbG8=")
/// Print data.ToString()    ' Outputs: "Hello"
/// ```
///
/// @param b64 The Base64-encoded string to decode. If NULL or empty,
///            returns empty Bytes.
///
/// @return A new Bytes object containing the decoded bytes.
///
/// @note O(n) time complexity where n is the input string length.
/// @note Traps with "Bytes.FromBase64: base64 length must be a multiple of 4"
///       if the input length is not a multiple of 4.
/// @note Traps with "Bytes.FromBase64: invalid base64 character" if the input
///       contains characters outside the Base64 alphabet.
/// @note Traps with "Bytes.FromBase64: invalid padding" if the padding is
///       malformed (e.g., '=' in wrong position, non-zero padding bits).
///
/// @see rt_bytes_to_base64 For the reverse operation
void *rt_bytes_from_base64(rt_string b64)
{
    const char *b64_str = rt_string_cstr(b64);
    if (!b64_str)
        return rt_bytes_new(0);

    size_t b64_len = strlen(b64_str);
    if (b64_len == 0)
        return rt_bytes_new(0);

    if (b64_len % 4 != 0)
        rt_trap("Bytes.FromBase64: base64 length must be a multiple of 4");

    size_t padding = 0;
    if (b64_len >= 1 && b64_str[b64_len - 1] == '=')
    {
        padding = 1;
        if (b64_len >= 2 && b64_str[b64_len - 2] == '=')
            padding = 2;
    }

    for (size_t i = 0; i < b64_len - padding; ++i)
    {
        if (b64_str[i] == '=')
            rt_trap("Bytes.FromBase64: invalid padding");
    }

    size_t out_len = (b64_len / 4) * 3 - padding;
    if (out_len == 0)
        return rt_bytes_new(0);

    if (out_len > (size_t)INT64_MAX)
        rt_trap("Bytes.FromBase64: decoded data too large");

    rt_bytes_impl *bytes = rt_bytes_alloc((int64_t)out_len);

    size_t out_pos = 0;
    for (size_t i = 0; i < b64_len; i += 4)
    {
        char c0 = b64_str[i];
        char c1 = b64_str[i + 1];
        char c2 = b64_str[i + 2];
        char c3 = b64_str[i + 3];

        int v0 = b64_digit_value(c0);
        int v1 = b64_digit_value(c1);
        int v2 = b64_digit_value(c2);
        int v3 = b64_digit_value(c3);

        if (v0 < 0 || v1 < 0)
        {
            if (v0 == -2 || v1 == -2)
                rt_trap("Bytes.FromBase64: invalid padding");
            rt_trap("Bytes.FromBase64: invalid base64 character");
        }

        if (v2 == -1 || v3 == -1)
            rt_trap("Bytes.FromBase64: invalid base64 character");

        if (v2 == -2)
        {
            if (v3 != -2 || i + 4 != b64_len)
                rt_trap("Bytes.FromBase64: invalid padding");
            if ((v1 & 0x0F) != 0)
                rt_trap("Bytes.FromBase64: invalid padding");

            uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12);
            bytes->data[out_pos++] = (uint8_t)((triple >> 16) & 0xFF);
            break;
        }

        if (v3 == -2)
        {
            if (i + 4 != b64_len)
                rt_trap("Bytes.FromBase64: invalid padding");
            if ((v2 & 0x03) != 0)
                rt_trap("Bytes.FromBase64: invalid padding");

            uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6);
            bytes->data[out_pos++] = (uint8_t)((triple >> 16) & 0xFF);
            bytes->data[out_pos++] = (uint8_t)((triple >> 8) & 0xFF);
            break;
        }

        uint32_t triple =
            ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6) | (uint32_t)v3;
        bytes->data[out_pos++] = (uint8_t)((triple >> 16) & 0xFF);
        bytes->data[out_pos++] = (uint8_t)((triple >> 8) & 0xFF);
        bytes->data[out_pos++] = (uint8_t)(triple & 0xFF);
    }

    if (out_pos != out_len)
        rt_trap("Bytes.FromBase64: invalid padding");

    return bytes;
}

/// @brief Fills all bytes in the Bytes object with a single value.
///
/// Sets every byte in the Bytes object to the specified value. This is
/// useful for initializing buffers or clearing sensitive data.
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.New(10)
/// data.Fill(0xFF)              ' Fill with 255
/// Print data.Get(0)            ' Outputs: 255
/// Print data.Get(9)            ' Outputs: 255
///
/// data.Fill(0)                 ' Clear to zeros
/// ```
///
/// @param obj Pointer to a Bytes object. If NULL, this is a no-op.
/// @param val The byte value to fill with. Only the low 8 bits (val & 0xFF)
///            are used.
///
/// @note O(n) time complexity where n is the byte count.
/// @note Uses memset for efficient bulk memory operations.
///
/// @see rt_bytes_set For setting individual bytes
void rt_bytes_fill(void *obj, int64_t val)
{
    if (!obj)
        return;

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;
    if (bytes->len > 0 && bytes->data)
    {
        memset(bytes->data, (uint8_t)(val & 0xFF), (size_t)bytes->len);
    }
}

/// @brief Finds the first occurrence of a byte value.
///
/// Searches the Bytes object from the beginning for the first occurrence
/// of the specified byte value and returns its index.
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.FromString("Hello, World!")
/// Print data.Find(111)         ' Outputs: 4 (index of 'o')
/// Print data.Find(120)         ' Outputs: -1 (no 'x' found)
/// ```
///
/// @param obj Pointer to a Bytes object. If NULL, returns -1.
/// @param val The byte value to search for. Only the low 8 bits (val & 0xFF)
///            are used.
///
/// @return The zero-based index of the first occurrence, or -1 if the byte
///         is not found or obj is NULL.
///
/// @note O(n) time complexity in the worst case.
/// @note Linear search from the beginning; stops at first match.
int64_t rt_bytes_find(void *obj, int64_t val)
{
    if (!obj)
        return -1;

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;
    uint8_t byte = (uint8_t)(val & 0xFF);

    for (int64_t i = 0; i < bytes->len; i++)
    {
        if (bytes->data[i] == byte)
            return i;
    }

    return -1;
}

/// @brief Creates a copy of the Bytes object.
///
/// Allocates a new Bytes object with the same length and contents as
/// the original. Modifications to the clone do not affect the original,
/// and vice versa.
///
/// **Usage example:**
/// ```
/// Dim original = Bytes.FromString("Hello")
/// Dim copy = original.Clone()
/// copy.Set(0, 74)              ' Change 'H' to 'J'
/// Print original.ToString()   ' Outputs: "Hello" (unchanged)
/// Print copy.ToString()       ' Outputs: "Jello"
/// ```
///
/// @param obj Pointer to a Bytes object. If NULL, returns empty Bytes.
///
/// @return A new Bytes object containing a copy of all bytes.
///
/// @note O(n) time complexity where n is the byte count.
///
/// @see rt_bytes_slice For copying a portion of the bytes
void *rt_bytes_clone(void *obj)
{
    if (!obj)
        return rt_bytes_new(0);

    rt_bytes_impl *bytes = (rt_bytes_impl *)obj;
    return rt_bytes_slice(obj, 0, bytes->len);
}

//=============================================================================
// Internal Utilities (declared in rt_internal.h)
//=============================================================================

/// @brief Extract raw bytes from a Bytes object into a newly allocated buffer.
///
/// This utility function extracts the contents of a Bytes object into a
/// freshly allocated raw buffer. It's used internally by cryptographic and
/// encoding routines that need to work with raw byte arrays.
///
/// @param bytes Bytes object pointer. May be NULL.
/// @param out_len Output parameter that receives the length of the data.
///
/// @return Pointer to a newly allocated buffer containing the bytes data,
///         or NULL if the input is NULL or empty. Caller must free() the
///         returned buffer when done.
///
/// @note Traps on allocation failure.
uint8_t *rt_bytes_extract_raw(void *bytes, size_t *out_len)
{
    if (!bytes)
    {
        *out_len = 0;
        return NULL;
    }

    rt_bytes_impl *impl = (rt_bytes_impl *)bytes;
    *out_len = (size_t)impl->len;

    if (impl->len == 0)
        return NULL;

    uint8_t *data = (uint8_t *)malloc((size_t)impl->len);
    if (!data)
        rt_trap("Bytes: memory allocation failed");

    memcpy(data, impl->data, (size_t)impl->len);
    return data;
}

/// @brief Create a Bytes object from raw data.
///
/// This utility function creates a new Bytes object and initializes it
/// with a copy of the provided raw data. It's used internally by
/// cryptographic routines that produce raw byte arrays.
///
/// @param data Pointer to raw data buffer. May be NULL if len is 0.
/// @param len Length of the data in bytes.
///
/// @return New Bytes object containing a copy of the data.
void *rt_bytes_from_raw(const uint8_t *data, size_t len)
{
    rt_bytes_impl *bytes = rt_bytes_alloc((int64_t)len);
    if (len > 0 && data)
        memcpy(bytes->data, data, len);
    return bytes;
}
