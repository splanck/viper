//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_memstream.c
// Purpose: Implement in-memory binary stream operations.
//
// MemStream provides a resizable in-memory buffer with stream semantics:
// - Automatic growth when writing past end
// - Little-endian encoding for multi-byte integers
// - IEEE 754 encoding for floats
// - Position can be set beyond length (gap filled with zeros on write)
//
//===----------------------------------------------------------------------===//

#include "rt_memstream.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

/// @brief Initial buffer capacity for new streams.
#define MEMSTREAM_INITIAL_CAPACITY 64

/// @brief Bytes implementation structure (must match rt_bytes.c).
typedef struct rt_bytes_impl
{
    int64_t len;   ///< Number of bytes.
    uint8_t *data; ///< Byte storage.
} rt_bytes_impl;

/// @brief MemStream implementation structure.
typedef struct rt_memstream_impl
{
    uint8_t *data;    ///< Buffer storage.
    int64_t len;      ///< Current data length.
    int64_t capacity; ///< Buffer capacity.
    int64_t pos;      ///< Current position.
} rt_memstream_impl;

/// @brief Finalizer callback to free the buffer when collected.
static void rt_memstream_finalize(void *obj)
{
    if (!obj)
        return;
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    if (ms->data)
    {
        free(ms->data);
        ms->data = NULL;
    }
}

/// @brief Ensure buffer has at least required capacity.
static void ensure_capacity(rt_memstream_impl *ms, int64_t required)
{
    if (required <= ms->capacity)
        return;

    int64_t new_cap = ms->capacity * 2;
    if (new_cap < required)
        new_cap = required;
    if (new_cap < MEMSTREAM_INITIAL_CAPACITY)
        new_cap = MEMSTREAM_INITIAL_CAPACITY;

    uint8_t *new_data = (uint8_t *)realloc(ms->data, (size_t)new_cap);
    if (!new_data)
    {
        rt_trap("MemStream: memory allocation failed");
        return;
    }

    // Zero new portion
    if (new_cap > ms->capacity)
        memset(new_data + ms->capacity, 0, (size_t)(new_cap - ms->capacity));

    ms->data = new_data;
    ms->capacity = new_cap;
}

/// @brief Ensure we can write 'count' bytes at current position.
/// Expands buffer and fills gaps with zeros if needed.
static void prepare_write(rt_memstream_impl *ms, int64_t count)
{
    int64_t end_pos = ms->pos + count;
    ensure_capacity(ms, end_pos);

    // If writing past current length, zero the gap
    if (ms->pos > ms->len)
    {
        memset(ms->data + ms->len, 0, (size_t)(ms->pos - ms->len));
    }

    // Update length if we're extending
    if (end_pos > ms->len)
        ms->len = end_pos;
}

/// @brief Check that we have enough bytes to read.
static void check_read(rt_memstream_impl *ms, int64_t count, const char *op)
{
    if (ms->pos + count > ms->len)
    {
        rt_trap(op);
    }
}

//=============================================================================
// Constructors
//=============================================================================

void *rt_memstream_new(void)
{
    rt_memstream_impl *ms =
        (rt_memstream_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_memstream_impl));
    if (!ms)
    {
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

void *rt_memstream_new_capacity(int64_t capacity)
{
    if (capacity < 0)
        capacity = 0;

    rt_memstream_impl *ms =
        (rt_memstream_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_memstream_impl));
    if (!ms)
    {
        rt_trap("MemStream.New: memory allocation failed");
        return NULL;
    }

    ms->data = NULL;
    ms->len = 0;
    ms->capacity = 0;
    ms->pos = 0;
    rt_obj_set_finalizer(ms, rt_memstream_finalize);

    if (capacity > 0)
        ensure_capacity(ms, capacity);

    return ms;
}

void *rt_memstream_from_bytes(void *bytes)
{
    if (!bytes)
    {
        rt_trap("MemStream.FromBytes: null bytes");
        return NULL;
    }

    rt_bytes_impl *b = (rt_bytes_impl *)bytes;

    rt_memstream_impl *ms =
        (rt_memstream_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_memstream_impl));
    if (!ms)
    {
        rt_trap("MemStream.FromBytes: memory allocation failed");
        return NULL;
    }

    ms->data = NULL;
    ms->len = 0;
    ms->capacity = 0;
    ms->pos = 0;
    rt_obj_set_finalizer(ms, rt_memstream_finalize);

    if (b->len > 0)
    {
        ensure_capacity(ms, b->len);
        memcpy(ms->data, b->data, (size_t)b->len);
        ms->len = b->len;
    }

    return ms;
}

//=============================================================================
// Properties
//=============================================================================

int64_t rt_memstream_get_pos(void *obj)
{
    if (!obj)
        return 0;
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    return ms->pos;
}

void rt_memstream_set_pos(void *obj, int64_t pos)
{
    if (!obj)
    {
        rt_trap("MemStream.set_Pos: null stream");
        return;
    }
    if (pos < 0)
    {
        rt_trap("MemStream.set_Pos: negative position");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    ms->pos = pos;
}

int64_t rt_memstream_get_len(void *obj)
{
    if (!obj)
        return 0;
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    return ms->len;
}

int64_t rt_memstream_get_capacity(void *obj)
{
    if (!obj)
        return 0;
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    return ms->capacity;
}

//=============================================================================
// Integer Read/Write
//=============================================================================

int64_t rt_memstream_read_i8(void *obj)
{
    if (!obj)
    {
        rt_trap("MemStream.ReadI8: null stream");
        return 0;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    check_read(ms, 1, "MemStream.ReadI8: insufficient bytes");
    int8_t val = (int8_t)ms->data[ms->pos];
    ms->pos++;
    return (int64_t)val;
}

void rt_memstream_write_i8(void *obj, int64_t value)
{
    if (!obj)
    {
        rt_trap("MemStream.WriteI8: null stream");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    prepare_write(ms, 1);
    ms->data[ms->pos] = (uint8_t)(value & 0xFF);
    ms->pos++;
}

int64_t rt_memstream_read_u8(void *obj)
{
    if (!obj)
    {
        rt_trap("MemStream.ReadU8: null stream");
        return 0;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    check_read(ms, 1, "MemStream.ReadU8: insufficient bytes");
    uint8_t val = ms->data[ms->pos];
    ms->pos++;
    return (int64_t)val;
}

void rt_memstream_write_u8(void *obj, int64_t value)
{
    if (!obj)
    {
        rt_trap("MemStream.WriteU8: null stream");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    prepare_write(ms, 1);
    ms->data[ms->pos] = (uint8_t)(value & 0xFF);
    ms->pos++;
}

int64_t rt_memstream_read_i16(void *obj)
{
    if (!obj)
    {
        rt_trap("MemStream.ReadI16: null stream");
        return 0;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    check_read(ms, 2, "MemStream.ReadI16: insufficient bytes");
    uint8_t *p = ms->data + ms->pos;
    int16_t val = (int16_t)(p[0] | ((uint16_t)p[1] << 8));
    ms->pos += 2;
    return (int64_t)val;
}

void rt_memstream_write_i16(void *obj, int64_t value)
{
    if (!obj)
    {
        rt_trap("MemStream.WriteI16: null stream");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    prepare_write(ms, 2);
    uint8_t *p = ms->data + ms->pos;
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    ms->pos += 2;
}

int64_t rt_memstream_read_u16(void *obj)
{
    if (!obj)
    {
        rt_trap("MemStream.ReadU16: null stream");
        return 0;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    check_read(ms, 2, "MemStream.ReadU16: insufficient bytes");
    uint8_t *p = ms->data + ms->pos;
    uint16_t val = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    ms->pos += 2;
    return (int64_t)val;
}

void rt_memstream_write_u16(void *obj, int64_t value)
{
    if (!obj)
    {
        rt_trap("MemStream.WriteU16: null stream");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    prepare_write(ms, 2);
    uint8_t *p = ms->data + ms->pos;
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    ms->pos += 2;
}

int64_t rt_memstream_read_i32(void *obj)
{
    if (!obj)
    {
        rt_trap("MemStream.ReadI32: null stream");
        return 0;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    check_read(ms, 4, "MemStream.ReadI32: insufficient bytes");
    uint8_t *p = ms->data + ms->pos;
    int32_t val =
        (int32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    ms->pos += 4;
    return (int64_t)val;
}

void rt_memstream_write_i32(void *obj, int64_t value)
{
    if (!obj)
    {
        rt_trap("MemStream.WriteI32: null stream");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    prepare_write(ms, 4);
    uint8_t *p = ms->data + ms->pos;
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    p[2] = (uint8_t)((value >> 16) & 0xFF);
    p[3] = (uint8_t)((value >> 24) & 0xFF);
    ms->pos += 4;
}

int64_t rt_memstream_read_u32(void *obj)
{
    if (!obj)
    {
        rt_trap("MemStream.ReadU32: null stream");
        return 0;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    check_read(ms, 4, "MemStream.ReadU32: insufficient bytes");
    uint8_t *p = ms->data + ms->pos;
    uint32_t val =
        (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    ms->pos += 4;
    return (int64_t)val;
}

void rt_memstream_write_u32(void *obj, int64_t value)
{
    if (!obj)
    {
        rt_trap("MemStream.WriteU32: null stream");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    prepare_write(ms, 4);
    uint8_t *p = ms->data + ms->pos;
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    p[2] = (uint8_t)((value >> 16) & 0xFF);
    p[3] = (uint8_t)((value >> 24) & 0xFF);
    ms->pos += 4;
}

int64_t rt_memstream_read_i64(void *obj)
{
    if (!obj)
    {
        rt_trap("MemStream.ReadI64: null stream");
        return 0;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    check_read(ms, 8, "MemStream.ReadI64: insufficient bytes");
    uint8_t *p = ms->data + ms->pos;
    uint64_t val = (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
                   ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
                   ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
    ms->pos += 8;
    return (int64_t)val;
}

void rt_memstream_write_i64(void *obj, int64_t value)
{
    if (!obj)
    {
        rt_trap("MemStream.WriteI64: null stream");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    prepare_write(ms, 8);
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

double rt_memstream_read_f32(void *obj)
{
    if (!obj)
    {
        rt_trap("MemStream.ReadF32: null stream");
        return 0.0;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    check_read(ms, 4, "MemStream.ReadF32: insufficient bytes");
    uint8_t *p = ms->data + ms->pos;
    uint32_t bits =
        (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    float f;
    memcpy(&f, &bits, sizeof(float));
    ms->pos += 4;
    return (double)f;
}

void rt_memstream_write_f32(void *obj, double value)
{
    if (!obj)
    {
        rt_trap("MemStream.WriteF32: null stream");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    prepare_write(ms, 4);
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

double rt_memstream_read_f64(void *obj)
{
    if (!obj)
    {
        rt_trap("MemStream.ReadF64: null stream");
        return 0.0;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    check_read(ms, 8, "MemStream.ReadF64: insufficient bytes");
    uint8_t *p = ms->data + ms->pos;
    uint64_t bits = (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
                    ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
                    ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
    double d;
    memcpy(&d, &bits, sizeof(double));
    ms->pos += 8;
    return d;
}

void rt_memstream_write_f64(void *obj, double value)
{
    if (!obj)
    {
        rt_trap("MemStream.WriteF64: null stream");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    prepare_write(ms, 8);
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

// Forward declaration for rt_bytes_new
extern void *rt_bytes_new(int64_t len);

void *rt_memstream_read_bytes(void *obj, int64_t count)
{
    if (!obj)
    {
        rt_trap("MemStream.ReadBytes: null stream");
        return NULL;
    }
    if (count < 0)
    {
        rt_trap("MemStream.ReadBytes: negative count");
        return NULL;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    check_read(ms, count, "MemStream.ReadBytes: insufficient bytes");

    void *bytes = rt_bytes_new(count);
    if (!bytes)
        return NULL;

    rt_bytes_impl *b = (rt_bytes_impl *)bytes;
    if (count > 0)
    {
        memcpy(b->data, ms->data + ms->pos, (size_t)count);
        ms->pos += count;
    }
    return bytes;
}

void rt_memstream_write_bytes(void *obj, void *bytes)
{
    if (!obj)
    {
        rt_trap("MemStream.WriteBytes: null stream");
        return;
    }
    if (!bytes)
    {
        rt_trap("MemStream.WriteBytes: null bytes");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    rt_bytes_impl *b = (rt_bytes_impl *)bytes;

    if (b->len > 0)
    {
        prepare_write(ms, b->len);
        memcpy(ms->data + ms->pos, b->data, (size_t)b->len);
        ms->pos += b->len;
    }
}

rt_string rt_memstream_read_str(void *obj, int64_t count)
{
    if (!obj)
    {
        rt_trap("MemStream.ReadStr: null stream");
        return NULL;
    }
    if (count < 0)
    {
        rt_trap("MemStream.ReadStr: negative count");
        return NULL;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    check_read(ms, count, "MemStream.ReadStr: insufficient bytes");

    rt_string str = rt_string_from_bytes((const char *)(ms->data + ms->pos), (size_t)count);
    ms->pos += count;
    return str;
}

void rt_memstream_write_str(void *obj, rt_string text)
{
    if (!obj)
    {
        rt_trap("MemStream.WriteStr: null stream");
        return;
    }
    if (!text)
    {
        rt_trap("MemStream.WriteStr: null string");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;

    const char *cstr = rt_string_cstr(text);
    size_t len = strlen(cstr);
    if (len > 0)
    {
        prepare_write(ms, (int64_t)len);
        memcpy(ms->data + ms->pos, cstr, len);
        ms->pos += (int64_t)len;
    }
}

//=============================================================================
// Stream Operations
//=============================================================================

void *rt_memstream_to_bytes(void *obj)
{
    if (!obj)
    {
        rt_trap("MemStream.ToBytes: null stream");
        return NULL;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;

    void *bytes = rt_bytes_new(ms->len);
    if (!bytes)
        return NULL;

    rt_bytes_impl *b = (rt_bytes_impl *)bytes;
    if (ms->len > 0)
        memcpy(b->data, ms->data, (size_t)ms->len);

    return bytes;
}

void rt_memstream_clear(void *obj)
{
    if (!obj)
        return;
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    ms->len = 0;
    ms->pos = 0;
    // Keep capacity/buffer for reuse
}

void rt_memstream_seek(void *obj, int64_t pos)
{
    rt_memstream_set_pos(obj, pos);
}

void rt_memstream_skip(void *obj, int64_t count)
{
    if (!obj)
    {
        rt_trap("MemStream.Skip: null stream");
        return;
    }
    rt_memstream_impl *ms = (rt_memstream_impl *)obj;
    int64_t new_pos = ms->pos + count;
    if (new_pos < 0)
    {
        rt_trap("MemStream.Skip: would result in negative position");
        return;
    }
    ms->pos = new_pos;
}
