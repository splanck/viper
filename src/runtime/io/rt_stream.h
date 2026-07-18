//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_stream.h
// Purpose: Unified stream interface abstracting BinFile and MemStream, providing a common API for
// read/write/seek regardless of the backing storage type.
//
// Key invariants:
//   - Stream type is one of STREAM_TYPE_BINFILE (0) or STREAM_TYPE_MEMSTREAM (1).
//   - The stream wraps its underlying object and forwards all calls transparently.
//   - Open* streams own their wrapped object; From* streams retain a reference
//     without assuming responsibility for explicitly closing an existing file.
//   - Operations on null or closed streams trap, except Close(NULL) is a no-op.
//   - All primitive read/write operations use little-endian byte order.
//
// Ownership/Lifetime:
//   - Stream objects are heap-allocated; caller is responsible for lifetime management.
//   - Destroying or closing an owning stream releases the wrapped backing object.
//   - Destroying or closing a From* wrapper releases its retained reference but
//     does not explicitly close the original object.
//
// Links: src/runtime/io/rt_stream.c (implementation), src/runtime/io/rt_binfile.h,
// src/runtime/io/rt_memstream.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// Stream Type Constants
//=========================================================================

/// @brief Discriminant tag identifying the backing store of a Stream object.
typedef enum {
    RT_STREAM_TYPE_BINFILE = 0,
    RT_STREAM_TYPE_MEMSTREAM = 1,
} rt_stream_type_t;

//=========================================================================
// Stream Creation
//=========================================================================

/// @brief Create an owning stream wrapping a newly opened file.
/// @details Constructor allocation is transactional: if wrapper allocation
///          traps, the newly opened BinFile and native handle are released.
/// @param path File path.
/// @param mode Open mode ("r", "w", "rw", "a").
/// @return Stream object or NULL on failure.
void *rt_stream_open_file(rt_string path, rt_string mode);

/// @brief Create an owning stream wrapping a new in-memory buffer.
/// @details A wrapper-allocation trap releases the fresh MemStream before it
///          propagates, so no partially constructed backing object escapes.
/// @return Stream object with empty buffer.
void *rt_stream_open_memory(void);

/// @brief Create a stream wrapping an existing Bytes object.
/// @param bytes Initial data for the buffer.
/// @return Stream object initialized with bytes.
void *rt_stream_open_bytes(void *bytes);

/// @brief Wrap and retain an existing BinFile in a Stream.
/// @param binfile Valid open or closed BinFile object to retain.
/// @return Owned Stream wrapper; Close releases its reference but does not
///         explicitly close the caller's existing BinFile.
void *rt_stream_from_binfile(void *binfile);

/// @brief Wrap and retain an existing MemStream in a Stream.
/// @param memstream Valid MemStream object to retain.
/// @return Owned Stream wrapper whose Close releases the retained reference.
void *rt_stream_from_memstream(void *memstream);

//=========================================================================
// Stream Properties
//=========================================================================

/// @brief Get the type of stream (BINFILE or MEMSTREAM).
/// @param stream Stream object.
/// @return Stream type constant.
int64_t rt_stream_get_type(void *stream);

/// @brief Get current position in stream.
/// @param stream Stream object.
/// @return Current position.
int64_t rt_stream_get_pos(void *stream);

/// @brief Set position in stream.
/// @param stream Stream object.
/// @param pos New position.
void rt_stream_set_pos(void *stream, int64_t pos);

/// @brief Get length/size of stream data.
/// @param stream Stream object.
/// @return Length in bytes.
int64_t rt_stream_get_len(void *stream);

/// @brief Check if stream is at end.
/// @param stream Stream object.
/// @return 1 if at end, 0 otherwise.
int8_t rt_stream_is_eof(void *stream);

//=========================================================================
// Stream Operations
//=========================================================================

/// @brief Read bytes from stream.
/// @details Allocates no more than the bytes measured from the current position
///          to EOF, even when @p count is much larger. A concurrently changing
///          file may still produce a shorter result.
/// @param stream Stream object.
/// @param count Number of bytes to read.
/// @return Bytes object with data read (may be shorter if EOF).
void *rt_stream_read(void *stream, int64_t count);

/// @brief Read all remaining bytes from stream.
/// @param stream Stream object.
/// @return Bytes object with all remaining data.
void *rt_stream_read_all(void *stream);

/// @brief Write bytes to stream.
/// @param stream Stream object.
/// @param bytes Bytes object to write.
void rt_stream_write(void *stream, void *bytes);

/// @brief Read a single byte.
/// @param stream Stream object.
/// @return Byte value (0-255) or -1 on EOF.
int64_t rt_stream_read_byte(void *stream);

/// @brief Write a single byte.
/// @param stream Stream object.
/// @param byte Byte value to write (0-255).
void rt_stream_write_byte(void *stream, int64_t byte);

/// @brief Flush any buffered writes.
/// @param stream Stream object.
void rt_stream_flush(void *stream);

/// @brief Close the stream.
/// @param stream Stream object.
void rt_stream_close(void *stream);

//=========================================================================
// Conversion
//=========================================================================

/// @brief Get a retained reference to the underlying BinFile.
/// @param stream Stream object.
/// @return Owned BinFile reference; traps if this is not a file stream.
void *rt_stream_as_binfile(void *stream);

/// @brief Get a retained reference to the underlying MemStream.
/// @param stream Stream object.
/// @return Owned MemStream reference; traps if this is not a memory stream.
void *rt_stream_as_memstream(void *stream);

/// @brief Convert memory stream contents to Bytes.
/// @param stream Stream object.
/// @return Fresh Bytes copy; traps if this is not a memory stream.
void *rt_stream_to_bytes(void *stream);

#ifdef __cplusplus
}
#endif
