//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_stream.c
// Purpose: Unified stream interface implementation.
//
//===----------------------------------------------------------------------===//

#include "rt_stream.h"
#include "rt_binfile.h"
#include "rt_bytes.h"
#include "rt_memstream.h"
#include "rt_object.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// External trap function (defined in rt_io.c)
extern void rt_trap(const char *msg);

//=============================================================================
// Internal Stream Structure
//=============================================================================

typedef struct
{
    int64_t type;    // RT_STREAM_TYPE_BINFILE or RT_STREAM_TYPE_MEMSTREAM
    void *wrapped;   // The wrapped BinFile or MemStream
    int8_t owns;     // Whether we own the wrapped object
} stream_impl;

//=============================================================================
// Bytes Access (for MemStream interaction)
//=============================================================================

typedef struct
{
    int64_t len;
    uint8_t *data;
} bytes_impl;

static inline uint8_t *bytes_data(void *obj)
{
    if (!obj)
        return NULL;
    return ((bytes_impl *)obj)->data;
}

static inline int64_t bytes_len(void *obj)
{
    if (!obj)
        return 0;
    return ((bytes_impl *)obj)->len;
}

//=============================================================================
// Finalizer
//=============================================================================

static void stream_finalizer(void *obj)
{
    stream_impl *s = (stream_impl *)obj;
    if (s && s->owns && s->wrapped)
    {
        if (s->type == RT_STREAM_TYPE_BINFILE)
        {
            rt_binfile_close(s->wrapped);
        }
        // MemStream doesn't need explicit close
        s->wrapped = NULL;
    }
}

//=============================================================================
// Stream Creation
//=============================================================================

void *rt_stream_open_file(rt_string path, rt_string mode)
{
    void *binfile = rt_binfile_open(path, mode);
    if (!binfile)
        return NULL;

    stream_impl *s = (stream_impl *)rt_obj_new_i64(0, sizeof(stream_impl));
    s->type = RT_STREAM_TYPE_BINFILE;
    s->wrapped = binfile;
    s->owns = 1;

    rt_obj_set_finalizer(s, stream_finalizer);
    return s;
}

void *rt_stream_open_memory(void)
{
    void *memstream = rt_memstream_new();

    stream_impl *s = (stream_impl *)rt_obj_new_i64(0, sizeof(stream_impl));
    s->type = RT_STREAM_TYPE_MEMSTREAM;
    s->wrapped = memstream;
    s->owns = 1;

    rt_obj_set_finalizer(s, stream_finalizer);
    return s;
}

void *rt_stream_open_bytes(void *bytes)
{
    void *memstream = rt_memstream_from_bytes(bytes);

    stream_impl *s = (stream_impl *)rt_obj_new_i64(0, sizeof(stream_impl));
    s->type = RT_STREAM_TYPE_MEMSTREAM;
    s->wrapped = memstream;
    s->owns = 1;

    rt_obj_set_finalizer(s, stream_finalizer);
    return s;
}

void *rt_stream_from_binfile(void *binfile)
{
    if (!binfile)
    {
        rt_trap("Stream.FromBinFile: binfile is null");
        return NULL;
    }

    stream_impl *s = (stream_impl *)rt_obj_new_i64(0, sizeof(stream_impl));
    s->type = RT_STREAM_TYPE_BINFILE;
    s->wrapped = binfile;
    s->owns = 0; // Don't own it, caller retains ownership

    rt_obj_set_finalizer(s, stream_finalizer);
    return s;
}

void *rt_stream_from_memstream(void *memstream)
{
    if (!memstream)
    {
        rt_trap("Stream.FromMemStream: memstream is null");
        return NULL;
    }

    stream_impl *s = (stream_impl *)rt_obj_new_i64(0, sizeof(stream_impl));
    s->type = RT_STREAM_TYPE_MEMSTREAM;
    s->wrapped = memstream;
    s->owns = 0; // Don't own it, caller retains ownership

    rt_obj_set_finalizer(s, stream_finalizer);
    return s;
}

//=============================================================================
// Stream Properties
//=============================================================================

int64_t rt_stream_get_type(void *stream)
{
    if (!stream)
        return -1;
    return ((stream_impl *)stream)->type;
}

int64_t rt_stream_get_pos(void *stream)
{
    if (!stream)
        return 0;

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_BINFILE)
    {
        return rt_binfile_pos(s->wrapped);
    }
    else
    {
        return rt_memstream_get_pos(s->wrapped);
    }
}

void rt_stream_set_pos(void *stream, int64_t pos)
{
    if (!stream)
        return;

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_BINFILE)
    {
        rt_binfile_seek(s->wrapped, pos, 0); // SEEK_SET
    }
    else
    {
        rt_memstream_set_pos(s->wrapped, pos);
    }
}

int64_t rt_stream_get_len(void *stream)
{
    if (!stream)
        return 0;

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_BINFILE)
    {
        return rt_binfile_size(s->wrapped);
    }
    else
    {
        return rt_memstream_get_len(s->wrapped);
    }
}

int8_t rt_stream_is_eof(void *stream)
{
    if (!stream)
        return 1;

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_BINFILE)
    {
        return rt_binfile_eof(s->wrapped);
    }
    else
    {
        // MemStream: EOF when pos >= len
        int64_t pos = rt_memstream_get_pos(s->wrapped);
        int64_t len = rt_memstream_get_len(s->wrapped);
        return pos >= len ? 1 : 0;
    }
}

//=============================================================================
// Stream Operations
//=============================================================================

void *rt_stream_read(void *stream, int64_t count)
{
    if (!stream || count <= 0)
        return rt_bytes_new(0);

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_BINFILE)
    {
        void *bytes = rt_bytes_new(count);
        int64_t read = rt_binfile_read(s->wrapped, bytes, 0, count);

        // If we read less than requested, return a slice
        if (read < count && read > 0)
        {
            return rt_bytes_slice(bytes, 0, read);
        }
        else if (read <= 0)
        {
            return rt_bytes_new(0);
        }
        return bytes;
    }
    else
    {
        return rt_memstream_read_bytes(s->wrapped, count);
    }
}

void *rt_stream_read_all(void *stream)
{
    if (!stream)
        return rt_bytes_new(0);

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_BINFILE)
    {
        // Read remaining bytes from current position
        int64_t pos = rt_binfile_pos(s->wrapped);
        int64_t size = rt_binfile_size(s->wrapped);
        int64_t remaining = size - pos;

        if (remaining <= 0)
            return rt_bytes_new(0);

        void *bytes = rt_bytes_new(remaining);
        rt_binfile_read(s->wrapped, bytes, 0, remaining);
        return bytes;
    }
    else
    {
        // Read remaining bytes from MemStream
        int64_t pos = rt_memstream_get_pos(s->wrapped);
        int64_t len = rt_memstream_get_len(s->wrapped);
        int64_t remaining = len - pos;

        if (remaining <= 0)
            return rt_bytes_new(0);

        return rt_memstream_read_bytes(s->wrapped, remaining);
    }
}

void rt_stream_write(void *stream, void *bytes)
{
    if (!stream || !bytes)
        return;

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_BINFILE)
    {
        int64_t len = bytes_len(bytes);
        rt_binfile_write(s->wrapped, bytes, 0, len);
    }
    else
    {
        rt_memstream_write_bytes(s->wrapped, bytes);
    }
}

int64_t rt_stream_read_byte(void *stream)
{
    if (!stream)
        return -1;

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_BINFILE)
    {
        return rt_binfile_read_byte(s->wrapped);
    }
    else
    {
        // MemStream: read u8 or return -1 if at end
        int64_t pos = rt_memstream_get_pos(s->wrapped);
        int64_t len = rt_memstream_get_len(s->wrapped);
        if (pos >= len)
            return -1;
        return rt_memstream_read_u8(s->wrapped);
    }
}

void rt_stream_write_byte(void *stream, int64_t byte)
{
    if (!stream)
        return;

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_BINFILE)
    {
        rt_binfile_write_byte(s->wrapped, byte);
    }
    else
    {
        rt_memstream_write_u8(s->wrapped, byte);
    }
}

void rt_stream_flush(void *stream)
{
    if (!stream)
        return;

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_BINFILE)
    {
        rt_binfile_flush(s->wrapped);
    }
    // MemStream doesn't need flushing
}

void rt_stream_close(void *stream)
{
    if (!stream)
        return;

    stream_impl *s = (stream_impl *)stream;
    if (s->wrapped && s->owns)
    {
        if (s->type == RT_STREAM_TYPE_BINFILE)
        {
            rt_binfile_close(s->wrapped);
        }
        s->wrapped = NULL;
    }
}

//=============================================================================
// Conversion
//=============================================================================

void *rt_stream_as_binfile(void *stream)
{
    if (!stream)
        return NULL;

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_BINFILE)
    {
        return s->wrapped;
    }
    return NULL;
}

void *rt_stream_as_memstream(void *stream)
{
    if (!stream)
        return NULL;

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_MEMSTREAM)
    {
        return s->wrapped;
    }
    return NULL;
}

void *rt_stream_to_bytes(void *stream)
{
    if (!stream)
        return NULL;

    stream_impl *s = (stream_impl *)stream;
    if (s->type == RT_STREAM_TYPE_MEMSTREAM)
    {
        return rt_memstream_to_bytes(s->wrapped);
    }
    return NULL;
}
