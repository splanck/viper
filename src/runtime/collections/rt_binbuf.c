//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_binbuf.c
// Purpose: Implements a positioned binary read/write buffer with dynamic growth.
//   BinaryBuffer maintains a heap-allocated byte array, a logical length (the
//   highest byte written + 1), and a read/write cursor position. Supports typed
//   reads and writes (i8, i16, i32, i64, f32, f64, bytes) at the cursor, with
//   automatic capacity doubling when the buffer is too small.
//
// Key invariants:
//   - Initial capacity is BINBUF_DEFAULT_CAPACITY (256) bytes.
//   - Capacity doubles on each growth; overflow beyond INT64_MAX traps with
//     "BinaryBuffer: capacity overflow".
//   - Newly allocated bytes beyond the old capacity are zero-filled (memset).
//   - `len` tracks the highest written byte index + 1; it does not shrink on
//     seek-back writes but does grow forward on writes past the current `len`.
//   - `position` is a free-seek cursor: Seek() can move it anywhere in [0, len].
//     Writing past `len` extends `len` to position + bytes_written.
//   - Read operations at or beyond `len` return 0/0.0 and do not advance cursor.
//   - Not thread-safe; external synchronization required for concurrent access.
//
// Ownership/Lifetime:
//   - BinaryBuffer objects are GC-managed (rt_obj_new_i64). The data array is
//     realloc-managed and freed by the GC finalizer (binbuf_finalizer).
//
// Links: src/runtime/collections/rt_binbuf.h (public API),
//        src/runtime/collections/rt_bytes.h (byte array type used for bulk I/O)
//
//===----------------------------------------------------------------------===//

#include "rt_binbuf.h"

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/// @brief Internal bytes layout — must match rt_bytes.c (same as rt_binfile.c pattern).
typedef struct {
    int64_t len;
    uint8_t *data;
} binbuf_bytes_impl;

static inline uint8_t *binbuf_bytes_data(void *obj) {
    return obj ? ((binbuf_bytes_impl *)obj)->data : NULL;
}

/// Default initial capacity for new binary buffers.
#define BINBUF_DEFAULT_CAPACITY 256

/// @brief Internal implementation structure for the BinaryBuffer type.
typedef struct rt_binbuf_impl {
    void **vptr;      ///< Vtable pointer placeholder (for OOP compatibility).
    uint8_t *data;    ///< Pointer to heap-allocated byte storage.
    int64_t len;      ///< Logical length (highest byte written + 1).
    int64_t capacity; ///< Allocated capacity in bytes.
    int64_t position; ///< Read/write cursor position.
} rt_binbuf_impl;

/// @brief Ensure the buffer has room for `needed` bytes starting at position.
/// @param buf Buffer implementation pointer.
/// @param needed Number of bytes needed from the current position.
static void binbuf_ensure(rt_binbuf_impl *buf, int64_t needed) {
    if (needed < 0)
        rt_trap("BinaryBuffer: negative size");
    if (buf->position > INT64_MAX - needed)
        rt_trap("BinaryBuffer: position overflow");
    int64_t required = buf->position + needed;
    if (required <= buf->capacity)
        return;

    int64_t new_cap = buf->capacity;
    if (new_cap < 1)
        new_cap = 1;
    // IO-H-3: guard against int64 overflow before doubling
    while (new_cap < required) {
        if (new_cap > (int64_t)(INT64_MAX / 2))
            rt_trap("BinaryBuffer: capacity overflow");
        new_cap *= 2;
    }

    uint8_t *new_data = (uint8_t *)realloc(buf->data, (size_t)new_cap);
    if (!new_data)
        rt_trap("BinaryBuffer: memory allocation failed");

    // Zero-fill newly allocated region
    memset(new_data + buf->capacity, 0, (size_t)(new_cap - buf->capacity));
    buf->data = new_data;
    buf->capacity = new_cap;
}

/// @brief Finalizer callback invoked when a BinaryBuffer is garbage collected.
/// @param obj Pointer to the BinaryBuffer object being finalized.
static void binbuf_finalize(void *obj) {
    if (!obj)
        return;
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->capacity = 0;
    buf->position = 0;
}

/// @brief Advance position after a write and extend len if needed.
/// @param buf Buffer implementation pointer.
/// @param n Number of bytes just written.
static void binbuf_advance_write(rt_binbuf_impl *buf, int64_t n) {
    buf->position += n;
    if (buf->position > buf->len)
        buf->len = buf->position;
}

/// @brief Check that `count` bytes can be read from the current position.
/// @param buf Buffer implementation pointer.
/// @param count Number of bytes to read.
static void binbuf_check_read(rt_binbuf_impl *buf, int64_t count) {
    if (count < 0)
        rt_trap("BinaryBuffer: negative count");
    if (buf->position > INT64_MAX - count)
        rt_trap("BinaryBuffer: read position overflow");
    if (buf->position + count > buf->len)
        rt_trap("BinaryBuffer: read past end");
}

//=============================================================================
// Constructors
//=============================================================================

void *rt_binbuf_new(void) {
    return rt_binbuf_new_cap(BINBUF_DEFAULT_CAPACITY);
}

void *rt_binbuf_new_cap(int64_t capacity) {
    if (capacity < 1)
        capacity = 1;

    rt_binbuf_impl *buf = (rt_binbuf_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_binbuf_impl));
    if (!buf)
        rt_trap("BinaryBuffer: memory allocation failed");

    buf->vptr = NULL;
    buf->data = (uint8_t *)calloc((size_t)capacity, 1);
    if (!buf->data) {
        rt_obj_free(buf);
        rt_trap("BinaryBuffer: memory allocation failed");
    }
    buf->len = 0;
    buf->capacity = capacity;
    buf->position = 0;
    rt_obj_set_finalizer(buf, binbuf_finalize);
    return buf;
}

void *rt_binbuf_from_bytes(void *bytes_obj) {
    int64_t blen = bytes_obj ? rt_bytes_len(bytes_obj) : 0;
    int64_t cap = blen > BINBUF_DEFAULT_CAPACITY ? blen : BINBUF_DEFAULT_CAPACITY;

    rt_binbuf_impl *buf = (rt_binbuf_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_binbuf_impl));
    if (!buf)
        rt_trap("BinaryBuffer: memory allocation failed");

    buf->vptr = NULL;
    buf->data = (uint8_t *)calloc((size_t)cap, 1);
    if (!buf->data) {
        rt_obj_free(buf);
        rt_trap("BinaryBuffer: memory allocation failed");
    }
    buf->capacity = cap;
    buf->len = blen;
    buf->position = 0;
    rt_obj_set_finalizer(buf, binbuf_finalize);

    // IO-H-2: use memcpy via raw pointer instead of O(n) rt_bytes_get() calls
    const uint8_t *src = binbuf_bytes_data(bytes_obj);
    if (src && blen > 0)
        memcpy(buf->data, src, (size_t)blen);

    return buf;
}

//=============================================================================
// Write Operations
//
// Each writer below appends `value` at the current cursor, growing the buffer's
// capacity as needed, then advances the cursor past the bytes just written.
// All writers trap on a NULL handle. The little-endian (LE) variants store the
// least-significant byte first; big-endian (BE) variants store the most-
// significant byte first. Width matches the function name (i16/i32/i64).
//=============================================================================

/// @brief Append the low 8 bits of `value` as a single byte.
void rt_binbuf_write_byte(void *obj, int64_t value) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_ensure(buf, 1);
    buf->data[buf->position] = (uint8_t)(value & 0xFF);
    binbuf_advance_write(buf, 1);
}

/// @brief Append a 16-bit signed integer in little-endian byte order.
void rt_binbuf_write_i16le(void *obj, int64_t value) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_ensure(buf, 2);
    buf->data[buf->position] = (uint8_t)(value & 0xFF);
    buf->data[buf->position + 1] = (uint8_t)((value >> 8) & 0xFF);
    binbuf_advance_write(buf, 2);
}

/// @brief Append a 16-bit signed integer in big-endian byte order.
void rt_binbuf_write_i16be(void *obj, int64_t value) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_ensure(buf, 2);
    buf->data[buf->position] = (uint8_t)((value >> 8) & 0xFF);
    buf->data[buf->position + 1] = (uint8_t)(value & 0xFF);
    binbuf_advance_write(buf, 2);
}

/// @brief Append a 32-bit signed integer in little-endian byte order.
void rt_binbuf_write_i32le(void *obj, int64_t value) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_ensure(buf, 4);
    buf->data[buf->position] = (uint8_t)(value & 0xFF);
    buf->data[buf->position + 1] = (uint8_t)((value >> 8) & 0xFF);
    buf->data[buf->position + 2] = (uint8_t)((value >> 16) & 0xFF);
    buf->data[buf->position + 3] = (uint8_t)((value >> 24) & 0xFF);
    binbuf_advance_write(buf, 4);
}

/// @brief Append a 32-bit signed integer in big-endian byte order.
void rt_binbuf_write_i32be(void *obj, int64_t value) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_ensure(buf, 4);
    buf->data[buf->position] = (uint8_t)((value >> 24) & 0xFF);
    buf->data[buf->position + 1] = (uint8_t)((value >> 16) & 0xFF);
    buf->data[buf->position + 2] = (uint8_t)((value >> 8) & 0xFF);
    buf->data[buf->position + 3] = (uint8_t)(value & 0xFF);
    binbuf_advance_write(buf, 4);
}

/// @brief Append a 64-bit signed integer in little-endian byte order.
void rt_binbuf_write_i64le(void *obj, int64_t value) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_ensure(buf, 8);
    buf->data[buf->position] = (uint8_t)(value & 0xFF);
    buf->data[buf->position + 1] = (uint8_t)((value >> 8) & 0xFF);
    buf->data[buf->position + 2] = (uint8_t)((value >> 16) & 0xFF);
    buf->data[buf->position + 3] = (uint8_t)((value >> 24) & 0xFF);
    buf->data[buf->position + 4] = (uint8_t)((value >> 32) & 0xFF);
    buf->data[buf->position + 5] = (uint8_t)((value >> 40) & 0xFF);
    buf->data[buf->position + 6] = (uint8_t)((value >> 48) & 0xFF);
    buf->data[buf->position + 7] = (uint8_t)((value >> 56) & 0xFF);
    binbuf_advance_write(buf, 8);
}

/// @brief Append a 64-bit signed integer in big-endian byte order.
void rt_binbuf_write_i64be(void *obj, int64_t value) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_ensure(buf, 8);
    buf->data[buf->position] = (uint8_t)((value >> 56) & 0xFF);
    buf->data[buf->position + 1] = (uint8_t)((value >> 48) & 0xFF);
    buf->data[buf->position + 2] = (uint8_t)((value >> 40) & 0xFF);
    buf->data[buf->position + 3] = (uint8_t)((value >> 32) & 0xFF);
    buf->data[buf->position + 4] = (uint8_t)((value >> 24) & 0xFF);
    buf->data[buf->position + 5] = (uint8_t)((value >> 16) & 0xFF);
    buf->data[buf->position + 6] = (uint8_t)((value >> 8) & 0xFF);
    buf->data[buf->position + 7] = (uint8_t)(value & 0xFF);
    binbuf_advance_write(buf, 8);
}

/// @brief Append a UTF-8 string with a 4-byte little-endian length prefix. NULL-equivalent
/// strings serialize as length 0 with no payload.
void rt_binbuf_write_str(void *obj, rt_string value) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");

    const char *cstr = rt_string_cstr(value);
    int64_t slen = cstr ? (int64_t)strlen(cstr) : 0;

    // Write 4-byte LE length prefix
    rt_binbuf_write_i32le(obj, slen);

    // Write UTF-8 bytes
    if (slen > 0) {
        rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
        binbuf_ensure(buf, slen);
        memcpy(buf->data + buf->position, cstr, (size_t)slen);
        binbuf_advance_write(buf, slen);
    }
}

/// @brief Append a Bytes blob with a 4-byte little-endian length prefix. NULL data writes
/// length 0. Faster than per-byte writes — uses a single memcpy after the prefix.
void rt_binbuf_write_bytes(void *obj, void *data) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");

    int64_t blen = data ? rt_bytes_len(data) : 0;

    // Write 4-byte LE length prefix
    rt_binbuf_write_i32le(obj, blen);

    // Write raw bytes — use memcpy via raw pointer (avoids O(n) rt_bytes_get calls)
    if (blen > 0) {
        rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
        binbuf_ensure(buf, blen);
        const uint8_t *src = binbuf_bytes_data(data);
        if (src)
            memcpy(buf->data + buf->position, src, (size_t)blen);
        binbuf_advance_write(buf, blen);
    }
}

//=============================================================================
// Read Operations
//
// Each reader consumes the appropriate number of bytes at the current cursor
// and returns the decoded value, advancing the cursor. Endianness and width
// are encoded in the function name and must match the writer used. All readers
// trap on a NULL handle or on insufficient remaining bytes (caller must check
// `rt_binbuf_get_position` against `rt_binbuf_get_len` for control-flow reads).
//=============================================================================

/// @brief Read a single byte (returned as an int64 in [0, 255]).
int64_t rt_binbuf_read_byte(void *obj) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_check_read(buf, 1);
    int64_t val = buf->data[buf->position];
    buf->position += 1;
    return val;
}

/// @brief Read a 16-bit signed integer in little-endian byte order.
int64_t rt_binbuf_read_i16le(void *obj) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_check_read(buf, 2);
    int64_t pos = buf->position;
    int64_t val = (int64_t)buf->data[pos] | ((int64_t)buf->data[pos + 1] << 8);
    buf->position += 2;
    return val;
}

/// @brief Read a 16-bit signed integer in big-endian byte order.
int64_t rt_binbuf_read_i16be(void *obj) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_check_read(buf, 2);
    int64_t pos = buf->position;
    int64_t val = ((int64_t)buf->data[pos] << 8) | (int64_t)buf->data[pos + 1];
    buf->position += 2;
    return val;
}

/// @brief Read a 32-bit signed integer in little-endian byte order.
int64_t rt_binbuf_read_i32le(void *obj) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_check_read(buf, 4);
    int64_t pos = buf->position;
    int64_t val = (int64_t)buf->data[pos] | ((int64_t)buf->data[pos + 1] << 8) |
                  ((int64_t)buf->data[pos + 2] << 16) | ((int64_t)buf->data[pos + 3] << 24);
    buf->position += 4;
    return val;
}

/// @brief Read a 32-bit signed integer in big-endian byte order.
int64_t rt_binbuf_read_i32be(void *obj) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_check_read(buf, 4);
    int64_t pos = buf->position;
    int64_t val = ((int64_t)buf->data[pos] << 24) | ((int64_t)buf->data[pos + 1] << 16) |
                  ((int64_t)buf->data[pos + 2] << 8) | (int64_t)buf->data[pos + 3];
    buf->position += 4;
    return val;
}

/// @brief Read a 64-bit signed integer in little-endian byte order.
int64_t rt_binbuf_read_i64le(void *obj) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_check_read(buf, 8);
    int64_t pos = buf->position;
    int64_t val = (int64_t)buf->data[pos] | ((int64_t)buf->data[pos + 1] << 8) |
                  ((int64_t)buf->data[pos + 2] << 16) | ((int64_t)buf->data[pos + 3] << 24) |
                  ((int64_t)buf->data[pos + 4] << 32) | ((int64_t)buf->data[pos + 5] << 40) |
                  ((int64_t)buf->data[pos + 6] << 48) | ((int64_t)buf->data[pos + 7] << 56);
    buf->position += 8;
    return val;
}

/// @brief Read a 64-bit signed integer in big-endian byte order.
int64_t rt_binbuf_read_i64be(void *obj) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_check_read(buf, 8);
    int64_t pos = buf->position;
    int64_t val = ((int64_t)buf->data[pos] << 56) | ((int64_t)buf->data[pos + 1] << 48) |
                  ((int64_t)buf->data[pos + 2] << 40) | ((int64_t)buf->data[pos + 3] << 32) |
                  ((int64_t)buf->data[pos + 4] << 24) | ((int64_t)buf->data[pos + 5] << 16) |
                  ((int64_t)buf->data[pos + 6] << 8) | (int64_t)buf->data[pos + 7];
    buf->position += 8;
    return val;
}

/// @brief Read a length-prefixed UTF-8 string written by `_write_str`. Traps on negative or
/// out-of-bounds length. Returns a fresh rt_string copy of the bytes.
rt_string rt_binbuf_read_str(void *obj) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");

    // Read 4-byte LE length prefix
    int64_t slen = rt_binbuf_read_i32le(obj);
    if (slen < 0)
        rt_trap("BinaryBuffer: invalid string length");

    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_check_read(buf, slen);

    rt_string result =
        rt_string_from_bytes((const char *)(buf->data + buf->position), (size_t)slen);
    buf->position += slen;
    return result;
}

/// @brief Read `count` raw bytes into a fresh Bytes blob (no length prefix — caller owns the
/// framing). Traps on negative count or under-run.
void *rt_binbuf_read_bytes(void *obj, int64_t count) {
    if (!obj)
        rt_trap("BinaryBuffer: null buffer");
    if (count < 0)
        rt_trap("BinaryBuffer: negative read count");

    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    binbuf_check_read(buf, count);

    void *result = rt_bytes_new(count);
    uint8_t *dst = binbuf_bytes_data(result);
    if (dst && count > 0)
        memcpy(dst, buf->data + buf->position, (size_t)count);

    buf->position += count;
    return result;
}

//=============================================================================
// Properties / Control
//=============================================================================

/// @brief Return the read/write cursor position in bytes from the buffer start. 0 for NULL.
int64_t rt_binbuf_get_position(void *obj) {
    if (!obj)
        return 0;
    return ((rt_binbuf_impl *)obj)->position;
}

/// @brief Seek the cursor to a byte offset (clamped to [0, len]). Subsequent reads/writes
/// happen from this position.
void rt_binbuf_set_position(void *obj, int64_t pos) {
    if (!obj)
        return;
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    if (pos < 0)
        pos = 0;
    if (pos > buf->len)
        pos = buf->len;
    buf->position = pos;
}

/// @brief Return the total written size in bytes (the high-water mark, not capacity).
int64_t rt_binbuf_get_len(void *obj) {
    if (!obj)
        return 0;
    return ((rt_binbuf_impl *)obj)->len;
}

/// @brief Snapshot the buffer's full contents as a fresh Bytes blob (memcpy, not a view).
/// Returns an empty Bytes for a NULL handle.
void *rt_binbuf_to_bytes(void *obj) {
    if (!obj)
        return rt_bytes_new(0);

    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    void *result = rt_bytes_new(buf->len);
    // IO-M-2: use memcpy via raw pointer instead of O(n) rt_bytes_set() calls
    uint8_t *dst = binbuf_bytes_data(result);
    if (dst && buf->len > 0)
        memcpy(dst, buf->data, (size_t)buf->len);

    return result;
}

/// @brief Reset position and length to 0 (logical truncation). Capacity is not freed; the
/// backing buffer can be re-used for the next batch without reallocation.
void rt_binbuf_reset(void *obj) {
    if (!obj)
        return;
    rt_binbuf_impl *buf = (rt_binbuf_impl *)obj;
    buf->position = 0;
    buf->len = 0;
}
