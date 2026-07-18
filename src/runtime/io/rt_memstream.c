//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_memstream.c
// Purpose: Implements an in-memory binary stream for the Zanna.IO.MemStream
//          class. The buffer grows automatically on write, supports random
//          seek/tell, and encodes multi-byte integers in little-endian order
//          and floats in IEEE 754 format.
//
// Key invariants:
//   - The buffer grows geometrically without touching bytes beyond logical length.
//   - Writing at a position beyond the current length zero-fills the gap.
//   - Position can be set to any non-negative value including beyond length.
//   - Little-endian byte order is used for all multi-byte integer writes/reads.
//   - Floats use IEEE 754 single precision; doubles use IEEE 754 double precision.
//   - New() starts at capacity zero and allocates at least
//     MEMSTREAM_INITIAL_CAPACITY (64) bytes on its first non-empty write.
//
// Ownership/Lifetime:
//   - MemStream objects are heap-allocated; the GC finalizer frees the data buffer.
//   - ToBytes returns a snapshot copy as a fresh rt_bytes owned by the caller.
//
// Links: src/runtime/io/rt_memstream.h (public API),
//        src/runtime/io/rt_binfile.h (disk-backed counterpart),
//        src/runtime/io/rt_stream.h (generic stream wrapping MemStream)
//
//===----------------------------------------------------------------------===//

#include "rt_memstream.h"

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_io_class_ids.h"
#include "rt_object.h"
#include "rt_string.h"

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

/// @brief Initial buffer capacity for new streams.
#define MEMSTREAM_INITIAL_CAPACITY 64

/// @brief MemStream implementation structure.
typedef struct rt_memstream_impl {
    uint8_t *data;    ///< Buffer storage.
    int64_t len;      ///< Current data length.
    int64_t capacity; ///< Buffer capacity.
    int64_t pos;      ///< Current position.
} rt_memstream_impl;

int8_t rt_memstream_is_handle(void *obj) {
    return rt_obj_is_instance(obj, RT_MEMSTREAM_CLASS_ID, sizeof(rt_memstream_impl)) ? 1 : 0;
}

static rt_memstream_impl *memstream_require(void *obj, const char *context) {
    if (!rt_memstream_is_handle(obj)) {
        rt_trap(context ? context : "MemStream: invalid stream");
        return NULL;
    }
    return (rt_memstream_impl *)obj;
}

/// @brief Finalizer callback to free the buffer when collected.
static void rt_memstream_finalize(void *obj) {
    if (!obj)
        return;
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    if (ms->data) {
        free(ms->data);
        ms->data = NULL;
    }
}

/// @brief Ensure the backing allocation can address @p required bytes.
/// @details Grows geometrically while doubling is representable; once the
///          capacity is above half of INT64_MAX it grows only to the requested
///          size instead of attempting an enormous saturating allocation.
///          Newly reserved bytes are intentionally left untouched because they
///          are outside logical `len`. @ref prepare_write zeroes exactly the gap
///          that becomes observable, avoiding needless page faults and memory
///          bandwidth during ordinary sequential growth.
/// @param ms Valid MemStream implementation.
/// @param required Required addressable byte count.
/// @return One on success, zero after trapping on overflow or allocation failure.
static int ensure_capacity(rt_memstream_impl *ms, int64_t required) {
    if (required < 0) {
        rt_trap("MemStream: capacity overflow");
        return 0;
    }
    if (required <= ms->capacity)
        return 1;

    int64_t new_cap = required;
    if (ms->capacity <= INT64_MAX / 2 && ms->capacity * 2 > new_cap)
        new_cap = ms->capacity * 2;
    if (new_cap < MEMSTREAM_INITIAL_CAPACITY)
        new_cap = MEMSTREAM_INITIAL_CAPACITY;
    if ((uint64_t)new_cap > (uint64_t)SIZE_MAX) {
        rt_trap("MemStream: capacity exceeds addressable memory");
        return 0;
    }

    uint8_t *new_data = (uint8_t *)realloc(ms->data, (size_t)new_cap);
    if (!new_data) {
        rt_trap("MemStream: memory allocation failed");
        return 0;
    }

    ms->data = new_data;
    ms->capacity = new_cap;
    return 1;
}

static void memstream_save_trap_error(char *buffer, size_t buffer_size, const char *fallback) {
    const char *err = rt_trap_get_error();
    snprintf(buffer, buffer_size, "%s", err && err[0] ? err : fallback);
}

static void memstream_release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static int memstream_ensure_capacity_or_release(rt_memstream_impl *ms,
                                                int64_t required,
                                                const char *fallback) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        memstream_save_trap_error(saved_error, sizeof(saved_error), fallback);
        rt_trap_clear_recovery();
        memstream_release_object(ms);
        rt_trap(saved_error);
        return 0;
    }

    if (!ensure_capacity(ms, required)) {
        rt_trap_clear_recovery();
        memstream_release_object(ms);
        rt_trap(fallback);
        return 0;
    }
    rt_trap_clear_recovery();
    return 1;
}

/// @brief Ensure we can write 'count' bytes at current position.
/// Expands buffer and fills gaps with zeros if needed.
static int prepare_write(rt_memstream_impl *ms, int64_t count) {
    if (count < 0 || ms->pos > INT64_MAX - count) {
        rt_trap("MemStream: write position overflow");
        return 0;
    }
    int64_t end_pos = ms->pos + count;
    if (!ensure_capacity(ms, end_pos))
        return 0;

    // If writing past current length, zero the gap
    if (ms->pos > ms->len) {
        memset(ms->data + ms->len, 0, (size_t)(ms->pos - ms->len));
    }

    // Update length if we're extending
    if (end_pos > ms->len)
        ms->len = end_pos;
    return 1;
}

/// @brief Check that we have enough bytes to read.
static int check_read(rt_memstream_impl *ms, int64_t count, const char *op) {
    if (count < 0 || count > ms->len || ms->pos > ms->len - count) {
        rt_trap(op);
        return 0;
    }
    return 1;
}

//=============================================================================
// Constructors
//=============================================================================

/// @brief Construct an empty in-memory stream. Buffer grows on demand. GC-managed.
void *rt_memstream_new(void) {
    rt_memstream_impl *ms = (rt_memstream_impl *)rt_obj_new_i64(RT_MEMSTREAM_CLASS_ID,
                                                                (int64_t)sizeof(rt_memstream_impl));
    if (!ms) {
        rt_trap("MemStream.New: memory allocation failed");
        return NULL;
    }

    ms->data = NULL;
    ms->len = 0;
    ms->capacity = 0;
    ms->pos = 0;
    rt_obj_set_finalizer(ms, rt_memstream_finalize);

    return ms;
}

/// @brief Construct a stream pre-allocated to `capacity` bytes — useful when the final size is
/// known up front to avoid mid-write reallocations.
void *rt_memstream_new_capacity(int64_t capacity) {
    if (capacity < 0) {
        rt_trap("MemStream.NewCapacity: negative capacity");
        return NULL;
    }

    rt_memstream_impl *ms = (rt_memstream_impl *)rt_obj_new_i64(RT_MEMSTREAM_CLASS_ID,
                                                                (int64_t)sizeof(rt_memstream_impl));
    if (!ms) {
        rt_trap("MemStream.New: memory allocation failed");
        return NULL;
    }

    ms->data = NULL;
    ms->len = 0;
    ms->capacity = 0;
    ms->pos = 0;
    rt_obj_set_finalizer(ms, rt_memstream_finalize);

    if (capacity > 0 && !memstream_ensure_capacity_or_release(
                            ms, capacity, "MemStream.NewCapacity: memory allocation failed"))
        return NULL;

    return ms;
}

/// @brief Construct a stream initialized with a copy of `bytes`. Position starts at 0; subsequent
/// reads consume the data, writes append/overwrite. The original Bytes is NOT retained.
void *rt_memstream_from_bytes(void *bytes) {
    if (!bytes || !rt_bytes_is_bytes(bytes)) {
        rt_trap("MemStream.FromBytes: invalid bytes");
        return NULL;
    }

    int64_t bytes_len = rt_bytes_len(bytes);
    const uint8_t *bytes_data = rt_bytes_data_const(bytes);
    if (bytes_len < 0 || (bytes_len > 0 && !bytes_data)) {
        rt_trap("MemStream.FromBytes: invalid bytes");
        return NULL;
    }

    rt_memstream_impl *ms = (rt_memstream_impl *)rt_obj_new_i64(RT_MEMSTREAM_CLASS_ID,
                                                                (int64_t)sizeof(rt_memstream_impl));
    if (!ms) {
        rt_trap("MemStream.FromBytes: memory allocation failed");
        return NULL;
    }

    ms->data = NULL;
    ms->len = 0;
    ms->capacity = 0;
    ms->pos = 0;
    rt_obj_set_finalizer(ms, rt_memstream_finalize);

    if (bytes_len > 0) {
        if (!memstream_ensure_capacity_or_release(
                ms, bytes_len, "MemStream.FromBytes: memory allocation failed"))
            return NULL;
        if (!ms->data) {
            rt_trap("MemStream.FromBytes: memory allocation failed");
            return NULL;
        }
        memcpy(ms->data, bytes_data, (size_t)bytes_len);
        ms->len = bytes_len;
    }

    return ms;
}

//=============================================================================
// Properties
//=============================================================================

/// @brief Read the current cursor position (0 = start of buffer).
int64_t rt_memstream_get_pos(void *obj) {
    rt_memstream_impl *ms = memstream_require(obj, "MemStream.Pos: invalid stream");
    return ms ? ms->pos : 0;
}

/// @brief Move the cursor to `pos`. Negative input traps; positions past the buffer end are
/// allowed (next write zero-fills the gap).
void rt_memstream_set_pos(void *obj, int64_t pos) {
    if (!obj) {
        rt_trap("MemStream.set_Pos: null stream");
        return;
    }
    if (pos < 0) {
        rt_trap("MemStream.set_Pos: negative position");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream.SetPos: invalid stream");
    if (!ms)
        return;
    ms->pos = pos;
}

/// @brief Read the logical length (high-water-mark of writes); distinct from capacity.
int64_t rt_memstream_get_len(void *obj) {
    rt_memstream_impl *ms = memstream_require(obj, "MemStream.Len: invalid stream");
    return ms ? ms->len : 0;
}

/// @brief Read the underlying buffer's allocated size (>= length). Doubles on each grow.
int64_t rt_memstream_get_capacity(void *obj) {
    rt_memstream_impl *ms = memstream_require(obj, "MemStream.Capacity: invalid stream");
    return ms ? ms->capacity : 0;
}

//=============================================================================
// Integer Read/Write
//=============================================================================

// =============================================================================
// Integer Read/Write — 12 functions in 6 pairs covering i8/u8/i16/u16/i32/u32/i64.
// All multi-byte reads/writes use **little-endian** byte order (the de-facto
// network/file convention for the runtime). Reads check bounds via `check_read`
// (traps with type-specific message on underflow); writes call `prepare_write`
// (grows buffer + zero-fills any gap from positions past the current length).
// All 8/16/32/u8/u16/u32 values are returned as int64 to match the IL ABI.
// =============================================================================

/// @brief Read 1 byte as signed int8 (sign-extended to int64). Advances pos by 1.
int64_t rt_memstream_read_i8(void *obj) {
    if (!obj) {
        rt_trap("MemStream.ReadI8: null stream");
        return 0;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return 0;
    if (!check_read(ms, 1, "MemStream.ReadI8: insufficient bytes"))
        return 0;
    uint8_t bits = ms->data[ms->pos];
    ms->pos++;
    return bits <= INT8_MAX ? (int64_t)bits : (int64_t)bits - INT64_C(256);
}

/// @brief Write one signed byte. Advances pos by 1.
void rt_memstream_write_i8(void *obj, int64_t value) {
    if (!obj) {
        rt_trap("MemStream.WriteI8: null stream");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    if (value < INT8_MIN || value > INT8_MAX) {
        rt_trap("MemStream.WriteI8: byte value out of range");
        return;
    }
    if (!prepare_write(ms, 1))
        return;
    ms->data[ms->pos] = (uint8_t)(int8_t)value;
    ms->pos++;
}

/// @brief Read 1 byte as unsigned uint8 (zero-extended to int64). Advances pos by 1.
int64_t rt_memstream_read_u8(void *obj) {
    if (!obj) {
        rt_trap("MemStream.ReadU8: null stream");
        return 0;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return 0;
    if (!check_read(ms, 1, "MemStream.ReadU8: insufficient bytes"))
        return 0;
    uint8_t val = ms->data[ms->pos];
    ms->pos++;
    return (int64_t)val;
}

/// @brief Write 1 byte (low 8 bits of `value`). Advances pos by 1.
void rt_memstream_write_u8(void *obj, int64_t value) {
    if (!obj) {
        rt_trap("MemStream.WriteU8: null stream");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    if (value < 0 || value > 255) {
        rt_trap("MemStream.WriteU8: byte value out of range");
        return;
    }
    if (!prepare_write(ms, 1))
        return;
    ms->data[ms->pos] = (uint8_t)value;
    ms->pos++;
}

/// @brief Read 2 bytes as signed int16 (little-endian, sign-extended to int64). Advances pos by 2.
int64_t rt_memstream_read_i16(void *obj) {
    if (!obj) {
        rt_trap("MemStream.ReadI16: null stream");
        return 0;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return 0;
    if (!check_read(ms, 2, "MemStream.ReadI16: insufficient bytes"))
        return 0;
    uint8_t *p = ms->data + ms->pos;
    uint16_t bits = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    ms->pos += 2;
    return bits <= INT16_MAX ? (int64_t)bits : (int64_t)bits - INT64_C(65536);
}

/// @brief Write a signed 16-bit value in little-endian order. Advances pos by 2.
void rt_memstream_write_i16(void *obj, int64_t value) {
    if (!obj) {
        rt_trap("MemStream.WriteI16: null stream");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    if (value < INT16_MIN || value > INT16_MAX) {
        rt_trap("MemStream.WriteI16: value out of range");
        return;
    }
    if (!prepare_write(ms, 2))
        return;
    uint16_t bits = (uint16_t)(int16_t)value;
    uint8_t *p = ms->data + ms->pos;
    p[0] = (uint8_t)(bits & 0xFFu);
    p[1] = (uint8_t)((bits >> 8) & 0xFFu);
    ms->pos += 2;
}

/// @brief Read 2 bytes as unsigned uint16 (little-endian, zero-extended to int64). Advances pos
/// by 2.
int64_t rt_memstream_read_u16(void *obj) {
    if (!obj) {
        rt_trap("MemStream.ReadU16: null stream");
        return 0;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return 0;
    if (!check_read(ms, 2, "MemStream.ReadU16: insufficient bytes"))
        return 0;
    uint8_t *p = ms->data + ms->pos;
    uint16_t val = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    ms->pos += 2;
    return (int64_t)val;
}

/// @brief Write an unsigned 16-bit value in little-endian order. Advances pos by 2.
void rt_memstream_write_u16(void *obj, int64_t value) {
    if (!obj) {
        rt_trap("MemStream.WriteU16: null stream");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    if (value < 0 || value > UINT16_MAX) {
        rt_trap("MemStream.WriteU16: value out of range");
        return;
    }
    if (!prepare_write(ms, 2))
        return;
    uint8_t *p = ms->data + ms->pos;
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    ms->pos += 2;
}

/// @brief Read 4 bytes as signed int32 (little-endian, sign-extended to int64). Advances pos by 4.
int64_t rt_memstream_read_i32(void *obj) {
    if (!obj) {
        rt_trap("MemStream.ReadI32: null stream");
        return 0;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return 0;
    if (!check_read(ms, 4, "MemStream.ReadI32: insufficient bytes"))
        return 0;
    uint8_t *p = ms->data + ms->pos;
    uint32_t bits =
        (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    ms->pos += 4;
    return bits <= INT32_MAX ? (int64_t)bits : (int64_t)bits - INT64_C(4294967296);
}

/// @brief Write a signed 32-bit value in little-endian order. Advances pos by 4.
void rt_memstream_write_i32(void *obj, int64_t value) {
    if (!obj) {
        rt_trap("MemStream.WriteI32: null stream");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    if (value < INT32_MIN || value > INT32_MAX) {
        rt_trap("MemStream.WriteI32: value out of range");
        return;
    }
    if (!prepare_write(ms, 4))
        return;
    uint32_t bits = (uint32_t)(int32_t)value;
    uint8_t *p = ms->data + ms->pos;
    p[0] = (uint8_t)(bits & 0xFFu);
    p[1] = (uint8_t)((bits >> 8) & 0xFFu);
    p[2] = (uint8_t)((bits >> 16) & 0xFFu);
    p[3] = (uint8_t)((bits >> 24) & 0xFFu);
    ms->pos += 4;
}

/// @brief Read 4 bytes as unsigned uint32 (little-endian, zero-extended to int64). Advances pos
/// by 4.
int64_t rt_memstream_read_u32(void *obj) {
    if (!obj) {
        rt_trap("MemStream.ReadU32: null stream");
        return 0;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return 0;
    if (!check_read(ms, 4, "MemStream.ReadU32: insufficient bytes"))
        return 0;
    uint8_t *p = ms->data + ms->pos;
    uint32_t val =
        (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    ms->pos += 4;
    return (int64_t)val;
}

/// @brief Write an unsigned 32-bit value in little-endian order. Advances pos by 4.
void rt_memstream_write_u32(void *obj, int64_t value) {
    if (!obj) {
        rt_trap("MemStream.WriteU32: null stream");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    if (value < 0 || (uint64_t)value > UINT32_MAX) {
        rt_trap("MemStream.WriteU32: value out of range");
        return;
    }
    if (!prepare_write(ms, 4))
        return;
    uint8_t *p = ms->data + ms->pos;
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    p[2] = (uint8_t)((value >> 16) & 0xFF);
    p[3] = (uint8_t)((value >> 24) & 0xFF);
    ms->pos += 4;
}

/// @brief Read 8 bytes as signed int64 (little-endian). Advances pos by 8.
int64_t rt_memstream_read_i64(void *obj) {
    if (!obj) {
        rt_trap("MemStream.ReadI64: null stream");
        return 0;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return 0;
    if (!check_read(ms, 8, "MemStream.ReadI64: insufficient bytes"))
        return 0;
    uint8_t *p = ms->data + ms->pos;
    uint64_t val = (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
                   ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
                   ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
    ms->pos += 8;
    if (val <= (uint64_t)INT64_MAX)
        return (int64_t)val;
    uint64_t magnitude = (~val) + UINT64_C(1);
    if (magnitude == (UINT64_C(1) << 63))
        return INT64_MIN;
    return -(int64_t)magnitude;
}

/// @brief Write 8 bytes (`value`, little-endian). Advances pos by 8.
void rt_memstream_write_i64(void *obj, int64_t value) {
    if (!obj) {
        rt_trap("MemStream.WriteI64: null stream");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    if (!prepare_write(ms, 8))
        return;
    uint8_t *p = ms->data + ms->pos;
    uint64_t v = (uint64_t)value;
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
    p[4] = (uint8_t)((v >> 32) & 0xFF);
    p[5] = (uint8_t)((v >> 40) & 0xFF);
    p[6] = (uint8_t)((v >> 48) & 0xFF);
    p[7] = (uint8_t)((v >> 56) & 0xFF);
    ms->pos += 8;
}

//=============================================================================
// Float Read/Write
//=============================================================================

/// @brief Read 4 bytes as IEEE-754 float, returned as double. Little-endian.
double rt_memstream_read_f32(void *obj) {
    if (!obj) {
        rt_trap("MemStream.ReadF32: null stream");
        return 0.0;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return 0.0;
    if (!check_read(ms, 4, "MemStream.ReadF32: insufficient bytes"))
        return 0.0;
    uint8_t *p = ms->data + ms->pos;
    uint32_t bits =
        (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    float f;
    memcpy(&f, &bits, sizeof(float));
    ms->pos += 4;
    return (double)f;
}

/// @brief Write 4 bytes as IEEE-754 float (double → float cast, little-endian). Advances pos by 4.
void rt_memstream_write_f32(void *obj, double value) {
    if (!obj) {
        rt_trap("MemStream.WriteF32: null stream");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    if (!prepare_write(ms, 4))
        return;
    float f = (float)value;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(float));
    uint8_t *p = ms->data + ms->pos;
    p[0] = (uint8_t)(bits & 0xFF);
    p[1] = (uint8_t)((bits >> 8) & 0xFF);
    p[2] = (uint8_t)((bits >> 16) & 0xFF);
    p[3] = (uint8_t)((bits >> 24) & 0xFF);
    ms->pos += 4;
}

/// @brief Read 8 bytes as IEEE-754 double. Little-endian.
double rt_memstream_read_f64(void *obj) {
    if (!obj) {
        rt_trap("MemStream.ReadF64: null stream");
        return 0.0;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return 0.0;
    if (!check_read(ms, 8, "MemStream.ReadF64: insufficient bytes"))
        return 0.0;
    uint8_t *p = ms->data + ms->pos;
    uint64_t bits = (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
                    ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
                    ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
    double d;
    memcpy(&d, &bits, sizeof(double));
    ms->pos += 8;
    return d;
}

/// @brief Write 8 bytes as IEEE-754 double (little-endian). Advances pos by 8.
void rt_memstream_write_f64(void *obj, double value) {
    if (!obj) {
        rt_trap("MemStream.WriteF64: null stream");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    if (!prepare_write(ms, 8))
        return;
    uint64_t bits;
    memcpy(&bits, &value, sizeof(double));
    uint8_t *p = ms->data + ms->pos;
    p[0] = (uint8_t)(bits & 0xFF);
    p[1] = (uint8_t)((bits >> 8) & 0xFF);
    p[2] = (uint8_t)((bits >> 16) & 0xFF);
    p[3] = (uint8_t)((bits >> 24) & 0xFF);
    p[4] = (uint8_t)((bits >> 32) & 0xFF);
    p[5] = (uint8_t)((bits >> 40) & 0xFF);
    p[6] = (uint8_t)((bits >> 48) & 0xFF);
    p[7] = (uint8_t)((bits >> 56) & 0xFF);
    ms->pos += 8;
}

//=============================================================================
// Bytes/String Read/Write
//=============================================================================

/// @brief Read `count` raw bytes into a fresh Bytes object. Advances pos by `count`.
void *rt_memstream_read_bytes(void *obj, int64_t count) {
    if (!obj) {
        rt_trap("MemStream.ReadBytes: null stream");
        return NULL;
    }
    if (count < 0) {
        rt_trap("MemStream.ReadBytes: negative count");
        return NULL;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return NULL;
    if (!check_read(ms, count, "MemStream.ReadBytes: insufficient bytes"))
        return NULL;

    void *bytes = rt_bytes_new(count);
    if (!bytes)
        return NULL;

    uint8_t *dst = rt_bytes_data(bytes);
    if (count > 0) {
        memcpy(dst, ms->data + ms->pos, (size_t)count);
        ms->pos += count;
    }
    return bytes;
}

/// @brief Write all bytes from a Bytes object at the current position. Grows buffer as needed.
void rt_memstream_write_bytes(void *obj, void *bytes) {
    if (!obj) {
        rt_trap("MemStream.WriteBytes: null stream");
        return;
    }
    if (!bytes || !rt_bytes_is_bytes(bytes)) {
        rt_trap("MemStream.WriteBytes: invalid bytes");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    int64_t bytes_len = rt_bytes_len(bytes);
    const uint8_t *bytes_data = rt_bytes_data_const(bytes);
    if (bytes_len < 0 || (bytes_len > 0 && !bytes_data)) {
        rt_trap("MemStream.WriteBytes: invalid bytes");
        return;
    }

    if (bytes_len > 0) {
        if (!prepare_write(ms, bytes_len))
            return;
        memcpy(ms->data + ms->pos, bytes_data, (size_t)bytes_len);
        ms->pos += bytes_len;
    }
}

/// @brief Read `count` bytes as a UTF-8 rt_string. Caller must know the byte count up front
/// (no length prefix; for self-describing strings, use a `write_i32(len)` + `write_str` pattern).
rt_string rt_memstream_read_str(void *obj, int64_t count) {
    if (!obj) {
        rt_trap("MemStream.ReadStr: null stream");
        return NULL;
    }
    if (count < 0) {
        rt_trap("MemStream.ReadStr: negative count");
        return NULL;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return NULL;
    if (!check_read(ms, count, "MemStream.ReadStr: insufficient bytes"))
        return NULL;

    const char *source = count > 0 ? (const char *)(ms->data + ms->pos) : "";
    rt_string str = rt_string_from_bytes(source, (size_t)count);
    if (!str) {
        rt_trap("MemStream.ReadStr: memory allocation failed");
        return NULL;
    }
    ms->pos += count;
    return str;
}

/// @brief Write a UTF-8 string's raw bytes (no length prefix, no terminator). Pair with
/// `read_str(byte_count)` — caller must track length out-of-band.
void rt_memstream_write_str(void *obj, rt_string text) {
    if (!obj) {
        rt_trap("MemStream.WriteStr: null stream");
        return;
    }
    if (!text) {
        rt_trap("MemStream.WriteStr: null string");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;

    // IO-C-1: use rt_str_len() not strlen() — Zanna strings can contain embedded null bytes
    int64_t len = rt_str_len(text);
    const char *cstr = rt_string_cstr(text);
    if (!cstr || len < 0) {
        rt_trap("MemStream.WriteStr: invalid string");
        return;
    }
    if (len > 0) {
        if (!prepare_write(ms, len))
            return;
        memcpy(ms->data + ms->pos, cstr, (size_t)len);
        ms->pos += len;
    }
}

//=============================================================================
// Stream Operations
//=============================================================================

/// @brief Snapshot the stream's current contents (positions 0..len-1) as a fresh Bytes object.
/// Doesn't affect cursor position. Use to extract the result of a build-up sequence of writes.
void *rt_memstream_to_bytes(void *obj) {
    if (!obj) {
        rt_trap("MemStream.ToBytes: null stream");
        return NULL;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return NULL;

    void *bytes = rt_bytes_new(ms->len);
    if (!bytes)
        return NULL;

    uint8_t *dst = rt_bytes_data(bytes);
    if (ms->len > 0)
        memcpy(dst, ms->data, (size_t)ms->len);

    return bytes;
}

/// @brief Reset length and position to 0. Does NOT shrink the buffer (so reuse keeps the
/// already-allocated capacity for the next batch of writes).
void rt_memstream_clear(void *obj) {
    if (!obj) {
        rt_trap("MemStream.Clear: null stream");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    ms->len = 0;
    ms->pos = 0;
    // Keep capacity/buffer for reuse
}

/// @brief Alias for `set_pos`. Familiar name for stdio-style users.
void rt_memstream_seek(void *obj, int64_t pos) {
    rt_memstream_set_pos(obj, pos);
}

/// @brief Advance the cursor by `count` bytes (relative seek). Like `set_pos(pos + count)`.
void rt_memstream_skip(void *obj, int64_t count) {
    if (!obj) {
        rt_trap("MemStream.Skip: null stream");
        return;
    }
    rt_memstream_impl *ms = memstream_require(obj, "MemStream: invalid stream");
    if (!ms)
        return;
    if (count > 0 && ms->pos > INT64_MAX - count) {
        rt_trap("MemStream.Skip: position overflow");
        return;
    }
    if (count == INT64_MIN || (count < 0 && ms->pos < -count)) {
        rt_trap("MemStream.Skip: would result in negative position");
        return;
    }
    int64_t new_pos = ms->pos + count;
    ms->pos = new_pos;
}
