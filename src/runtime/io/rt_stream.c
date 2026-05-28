//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_stream.c
// Purpose: Implements the unified stream abstraction that wraps either a BinFile
//          (disk-backed) or a MemStream (in-memory buffer) behind a common
//          read/write/seek/tell interface used by the Viper.IO.Stream class.
//
// Key invariants:
//   - A stream wraps exactly one underlying object (BinFile or MemStream).
//   - The type tag (RT_STREAM_TYPE_BINFILE or RT_STREAM_TYPE_MEMSTREAM) is set
//     at construction and never changes.
//   - When owns==1, the stream holds a reference and releases it on close.
//   - All operations trap on NULL handles rather than silently succeeding.
//   - Seek positions are byte offsets; negative offsets from SEEK_END are valid.
//
// Ownership/Lifetime:
//   - If the stream owns the wrapped object, it releases it when the stream is
//     closed or garbage collected.
//   - Callers that pass an existing BinFile/MemStream with owns=0 retain
//     responsibility for closing the underlying object.
//
// Links: src/runtime/io/rt_stream.h (public API),
//        src/runtime/io/rt_binfile.h (disk-backed binary file),
//        src/runtime/io/rt_memstream.h (in-memory binary stream)
//
//===----------------------------------------------------------------------===//

#include "rt_stream.h"
#include "rt_binfile.h"
#include "rt_bytes.h"
#include "rt_io_class_ids.h"
#include "rt_memstream.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// External trap function (defined in rt_io.c)
#include "rt_trap.h"

//=============================================================================
// Internal Stream Structure
//=============================================================================

/// @brief Internal representation of a Stream handle.
typedef struct {
    int64_t type;          ///< RT_STREAM_TYPE_BINFILE or RT_STREAM_TYPE_MEMSTREAM
    void *wrapped;         ///< The wrapped BinFile or MemStream
    int8_t owns;           ///< Whether this Stream holds a reference on the wrapped object
    int8_t closes_wrapped; ///< Whether Close should close the wrapped object first.
    int8_t closed;         ///< Set to 1 once Close has been called
} stream_impl;

static int stream_is_handle(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_STREAM_CLASS_ID;
}

/// @brief Decrement the refcount on a GC object and free it when it reaches zero.
static void stream_release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Tear down the stream's reference to its underlying BinFile/MemStream.
///
/// Clears `wrapped` and marks the stream closed *before* releasing,
/// so a finalizer running in parallel with an explicit `Close` call
/// can't double-release. Only drops the reference when the stream
/// owns the wrapped object — externally-wrapped streams (see
/// `_from_binfile` / `_from_memstream`) leave cleanup to the owner.
static void stream_release_wrapped(stream_impl *s) {
    if (!s)
        return;

    void *wrapped = s->wrapped;
    int64_t type = s->type;
    int8_t closes_wrapped = s->closes_wrapped;
    s->wrapped = NULL;
    s->closed = 1;
    s->closes_wrapped = 0;
    if (s->owns && closes_wrapped && type == RT_STREAM_TYPE_BINFILE && wrapped)
        rt_binfile_close(wrapped);
    if (s->owns)
        stream_release_object(wrapped);
}

/// @brief Release a Bytes object returned from a BinFile read, freeing it when no longer
/// referenced.
static void stream_dispose_bytes(void *bytes) {
    if (bytes && rt_obj_release_check0(bytes))
        rt_obj_free(bytes);
}

/// @brief Right-size a read buffer after a possibly-short read.
///
/// BinFile reads allocate a buffer sized to the requested count, but
/// may actually return fewer bytes if EOF is hit mid-call. Rather
/// than returning a buffer with trailing garbage, this helper slices
/// the first `len` bytes into a fresh Bytes object and drops the
/// oversized original. Fast-paths the equal-length case so full
/// reads don't pay an allocation penalty.
static void *stream_shrink_bytes(void *bytes, int64_t len) {
    if (!bytes)
        return rt_bytes_new(0);
    if (len <= 0) {
        stream_dispose_bytes(bytes);
        return rt_bytes_new(0);
    }
    if (rt_bytes_len(bytes) == len)
        return bytes;

    void *slice = rt_bytes_slice(bytes, 0, len);
    stream_dispose_bytes(bytes);
    return slice;
}

/// @brief Allocate and zero-initialize a new stream_impl GC object; traps on OOM.
static stream_impl *stream_alloc(void) {
    stream_impl *s = (stream_impl *)rt_obj_new_i64(RT_STREAM_CLASS_ID, sizeof(stream_impl));
    if (!s) {
        rt_trap("Stream: memory allocation failed");
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    return s;
}

/// @brief Trap-guarded dereference: returns the `stream_impl` or traps.
///
/// Two failure modes: NULL handle (use-after-new-fail or uninitialized
/// field) traps with the caller-provided context, and already-closed
/// stream (use-after-close) traps with a generic message. Saves every
/// public entry point from having to open-code these two checks.
static stream_impl *stream_require_open(void *stream, const char *context) {
    if (!stream_is_handle(stream)) {
        rt_trap(context);
        return NULL;
    }
    stream_impl *s = (stream_impl *)stream;
    if (s->closed || !s->wrapped) {
        rt_trap("Stream: stream is closed");
        return NULL;
    }
    return s;
}

//=============================================================================
// Finalizer
//=============================================================================

/// @brief GC finalizer: releases the wrapped BinFile/MemStream when the stream object is collected.
static void stream_finalizer(void *obj) {
    stream_impl *s = (stream_impl *)obj;
    stream_release_wrapped(s);
}

//=============================================================================
// Stream Creation
//=============================================================================

/// @brief Open a file as a Stream. `mode` follows the same convention as `rt_binfile_open`
/// ("r"/"w"/"rb"/"wb"/"a"/"r+"). The Stream owns the underlying BinFile and closes it on finalize.
void *rt_stream_open_file(rt_string path, rt_string mode) {
    void *binfile = rt_binfile_open(path, mode);
    if (!binfile)
        return NULL;

    stream_impl *s = stream_alloc();
    if (!s) {
        stream_release_object(binfile);
        return NULL;
    }
    s->type = RT_STREAM_TYPE_BINFILE;
    s->wrapped = binfile;
    s->owns = 1;
    s->closes_wrapped = 1;
    s->closed = 0;

    rt_obj_set_finalizer(s, stream_finalizer);
    return s;
}

/// @brief Open a Stream backed by a fresh in-memory buffer (zero-length, growable).
void *rt_stream_open_memory(void) {
    void *memstream = rt_memstream_new();
    if (!memstream)
        return NULL;

    stream_impl *s = stream_alloc();
    if (!s) {
        stream_release_object(memstream);
        return NULL;
    }
    s->type = RT_STREAM_TYPE_MEMSTREAM;
    s->wrapped = memstream;
    s->owns = 1;
    s->closes_wrapped = 0;
    s->closed = 0;

    rt_obj_set_finalizer(s, stream_finalizer);
    return s;
}

/// @brief Open a Stream backed by a copy of the provided Bytes (positioned at 0). The Bytes
/// itself is not retained — subsequent edits to the original don't affect the Stream.
void *rt_stream_open_bytes(void *bytes) {
    void *memstream = rt_memstream_from_bytes(bytes);
    if (!memstream)
        return NULL;

    stream_impl *s = stream_alloc();
    if (!s) {
        stream_release_object(memstream);
        return NULL;
    }
    s->type = RT_STREAM_TYPE_MEMSTREAM;
    s->wrapped = memstream;
    s->owns = 1;
    s->closes_wrapped = 0;
    s->closed = 0;

    rt_obj_set_finalizer(s, stream_finalizer);
    return s;
}

/// @brief Wrap an existing BinFile as a Stream, retaining it for the wrapper's lifetime.
void *rt_stream_from_binfile(void *binfile) {
    if (!rt_binfile_is_handle(binfile)) {
        rt_trap("Stream.FromBinFile: invalid binfile");
        return NULL;
    }

    stream_impl *s = stream_alloc();
    if (!s)
        return NULL;
    s->type = RT_STREAM_TYPE_BINFILE;
    rt_obj_retain_maybe(binfile);
    s->wrapped = binfile;
    s->owns = 1;
    s->closes_wrapped = 0;
    s->closed = 0;

    rt_obj_set_finalizer(s, stream_finalizer);
    return s;
}

/// @brief Wrap an existing MemStream as a Stream, retaining it for the wrapper's lifetime.
void *rt_stream_from_memstream(void *memstream) {
    if (!rt_memstream_is_handle(memstream)) {
        rt_trap("Stream.FromMemStream: invalid memstream");
        return NULL;
    }

    stream_impl *s = stream_alloc();
    if (!s)
        return NULL;
    s->type = RT_STREAM_TYPE_MEMSTREAM;
    rt_obj_retain_maybe(memstream);
    s->wrapped = memstream;
    s->owns = 1;
    s->closes_wrapped = 0;
    s->closed = 0;

    rt_obj_set_finalizer(s, stream_finalizer);
    return s;
}

//=============================================================================
// Stream Properties
//=============================================================================

/// @brief Return the stream type: RT_STREAM_TYPE_BINFILE or RT_STREAM_TYPE_MEMSTREAM.
int64_t rt_stream_get_type(void *stream) {
    stream_impl *s = stream_require_open(stream, "Stream.Type: null stream");
    if (!s)
        return -1;
    return s->type;
}

/// @brief Return the current read/write position within the stream.
int64_t rt_stream_get_pos(void *stream) {
    stream_impl *s = stream_require_open(stream, "Stream.Pos: null stream");
    if (!s)
        return -1;
    if (s->type == RT_STREAM_TYPE_BINFILE) {
        return rt_binfile_pos(s->wrapped);
    } else {
        return rt_memstream_get_pos(s->wrapped);
    }
}

/// @brief Seek to an absolute byte position within the stream.
void rt_stream_set_pos(void *stream, int64_t pos) {
    stream_impl *s = stream_require_open(stream, "Stream.set_Pos: null stream");
    if (!s)
        return;

    if (s->type == RT_STREAM_TYPE_BINFILE) {
        if (rt_binfile_seek(s->wrapped, pos, 0) < 0) {
            rt_trap("Stream.set_Pos: seek failed");
            return;
        }
    } else {
        rt_memstream_set_pos(s->wrapped, pos);
    }
}

/// @brief Return the number of elements in the stream.
int64_t rt_stream_get_len(void *stream) {
    stream_impl *s = stream_require_open(stream, "Stream.Len: null stream");
    if (!s)
        return -1;
    if (s->type == RT_STREAM_TYPE_BINFILE) {
        return rt_binfile_size(s->wrapped);
    } else {
        return rt_memstream_get_len(s->wrapped);
    }
}

/// @brief Check whether the stream has reached end-of-file (position >= length).
int8_t rt_stream_is_eof(void *stream) {
    stream_impl *s = stream_require_open(stream, "Stream.Eof: null stream");
    if (!s)
        return 1;

    if (s->type == RT_STREAM_TYPE_BINFILE) {
        return rt_binfile_eof(s->wrapped);
    } else {
        // MemStream: EOF when pos >= len
        int64_t pos = rt_memstream_get_pos(s->wrapped);
        int64_t len = rt_memstream_get_len(s->wrapped);
        return pos >= len ? 1 : 0;
    }
}

//=============================================================================
// Stream Operations
//=============================================================================

/// @brief Read up to `count` bytes from the current position. Returns a Bytes object sized to
/// the actual count read (which may be smaller than `count` if EOF is hit).
void *rt_stream_read(void *stream, int64_t count) {
    stream_impl *s = stream_require_open(stream, "Stream.Read: null stream");
    if (!s)
        return rt_bytes_new(0);
    if (count < 0) {
        rt_trap("Stream.Read: negative count");
        return rt_bytes_new(0);
    }
    if (count == 0)
        return rt_bytes_new(0);

    if (s->type == RT_STREAM_TYPE_BINFILE) {
        void *bytes = rt_bytes_new(count);
        int64_t read = rt_binfile_read(s->wrapped, bytes, 0, count);
        if (read < 0) {
            stream_dispose_bytes(bytes);
            rt_trap("Stream.Read: read failed");
            return rt_bytes_new(0);
        }
        return stream_shrink_bytes(bytes, read);
    } else {
        int64_t pos = rt_memstream_get_pos(s->wrapped);
        int64_t len = rt_memstream_get_len(s->wrapped);
        int64_t remaining = len > pos ? len - pos : 0;
        if (remaining <= 0)
            return rt_bytes_new(0);
        if (count > remaining)
            count = remaining;
        return rt_memstream_read_bytes(s->wrapped, count);
    }
}

/// @brief Read every remaining byte from the current position to EOF. Convenience for
/// "slurp the rest" operations on a partially-consumed stream.
void *rt_stream_read_all(void *stream) {
    stream_impl *s = stream_require_open(stream, "Stream.ReadAll: null stream");
    if (!s)
        return rt_bytes_new(0);

    if (s->type == RT_STREAM_TYPE_BINFILE) {
        // Read remaining bytes from current position
        int64_t pos = rt_binfile_pos(s->wrapped);
        int64_t size = rt_binfile_size(s->wrapped);
        if (pos < 0 || size < 0 || size < pos) {
            rt_trap("Stream.ReadAll: invalid file position or size");
            return rt_bytes_new(0);
        }
        int64_t remaining = size - pos;

        if (remaining <= 0)
            return rt_bytes_new(0);

        void *bytes = rt_bytes_new(remaining);
        int64_t read = rt_binfile_read(s->wrapped, bytes, 0, remaining);
        if (read < 0) {
            stream_dispose_bytes(bytes);
            rt_trap("Stream.ReadAll: read failed");
            return rt_bytes_new(0);
        }
        return stream_shrink_bytes(bytes, read);
    } else {
        // Read remaining bytes from MemStream
        int64_t pos = rt_memstream_get_pos(s->wrapped);
        int64_t len = rt_memstream_get_len(s->wrapped);
        int64_t remaining = len - pos;

        if (remaining <= 0)
            return rt_bytes_new(0);

        return rt_memstream_read_bytes(s->wrapped, remaining);
    }
}

/// @brief Write the stream.
void rt_stream_write(void *stream, void *bytes) {
    stream_impl *s = stream_require_open(stream, "Stream.Write: null stream");
    if (!s)
        return;
    if (!bytes || !rt_bytes_is_bytes(bytes)) {
        rt_trap("Stream.Write: invalid bytes");
        return;
    }

    if (s->type == RT_STREAM_TYPE_BINFILE) {
        int64_t len = rt_bytes_len(bytes);
        rt_binfile_write(s->wrapped, bytes, 0, len);
    } else {
        rt_memstream_write_bytes(s->wrapped, bytes);
    }
}

/// @brief Read a single byte from the stream at the current position and advance.
/// @details Returns -1 if the stream is at EOF.
int64_t rt_stream_read_byte(void *stream) {
    stream_impl *s = stream_require_open(stream, "Stream.ReadByte: null stream");
    if (!s)
        return -1;

    if (s->type == RT_STREAM_TYPE_BINFILE) {
        return rt_binfile_read_byte(s->wrapped);
    } else {
        // MemStream: read u8 or return -1 if at end
        int64_t pos = rt_memstream_get_pos(s->wrapped);
        int64_t len = rt_memstream_get_len(s->wrapped);
        if (pos >= len)
            return -1;
        return rt_memstream_read_u8(s->wrapped);
    }
}

/// @brief Write a single byte to the stream at the current position and advance.
void rt_stream_write_byte(void *stream, int64_t byte) {
    stream_impl *s = stream_require_open(stream, "Stream.WriteByte: null stream");
    if (!s)
        return;
    if (byte < 0 || byte > 255) {
        rt_trap("Stream.WriteByte: byte value out of range");
        return;
    }

    if (s->type == RT_STREAM_TYPE_BINFILE) {
        rt_binfile_write_byte(s->wrapped, byte);
    } else {
        rt_memstream_write_u8(s->wrapped, byte);
    }
}

/// @brief Flush the stream.
void rt_stream_flush(void *stream) {
    stream_impl *s = stream_require_open(stream, "Stream.Flush: null stream");
    if (!s)
        return;

    if (s->type == RT_STREAM_TYPE_BINFILE) {
        rt_binfile_flush(s->wrapped);
    }
    // MemStream doesn't need flushing
}

/// @brief Close the stream.
void rt_stream_close(void *stream) {
    if (!stream)
        return;
    if (!stream_is_handle(stream)) {
        rt_trap("Stream.Close: invalid stream");
        return;
    }
    stream_impl *s = (stream_impl *)stream;
    stream_release_wrapped(s);
}

//=============================================================================
// Conversion
//=============================================================================

/// @brief Unwrap to the underlying BinFile (if the Stream is file-backed). Returns NULL for
/// MemStream-backed Streams. The reference is borrowed — caller must NOT release it.
void *rt_stream_as_binfile(void *stream) {
    stream_impl *s = stream_require_open(stream, "Stream.AsBinFile: null stream");
    if (!s)
        return NULL;

    if (s->type == RT_STREAM_TYPE_BINFILE) {
        return s->wrapped;
    }
    rt_trap("Stream.AsBinFile: stream is not file-backed");
    return NULL;
}

/// @brief Unwrap to the underlying MemStream (if memory-backed). Returns NULL otherwise.
void *rt_stream_as_memstream(void *stream) {
    stream_impl *s = stream_require_open(stream, "Stream.AsMemStream: null stream");
    if (!s)
        return NULL;

    if (s->type == RT_STREAM_TYPE_MEMSTREAM) {
        return s->wrapped;
    }
    rt_trap("Stream.AsMemStream: stream is not memory-backed");
    return NULL;
}

/// @brief Snapshot a MemStream's contents as a Bytes object. NULL for file-backed Streams (use
/// `_set_pos(0)` then `_read_all` instead).
void *rt_stream_to_bytes(void *stream) {
    stream_impl *s = stream_require_open(stream, "Stream.ToBytes: null stream");
    if (!s)
        return NULL;

    if (s->type == RT_STREAM_TYPE_MEMSTREAM) {
        return rt_memstream_to_bytes(s->wrapped);
    }
    rt_trap("Stream.ToBytes: stream is not memory-backed");
    return NULL;
}
