// File: src/runtime/rt_file.h
// Purpose: Declare BASIC runtime file I/O helpers using the shared error model.
// Key invariants: File handles encapsulate POSIX descriptors; negative fd marks closed state.
// Ownership/Lifetime: Callers initialize RtFile and close via rt_file_close when finished.
// Links: docs/specs/errors.md
#pragma once

#include "rt_error.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    /// @brief Lightweight handle representing an open runtime file.
    typedef struct RtFile
    {
        int fd; ///< Underlying POSIX file descriptor or -1 when closed.
    } RtFile;

    /// @brief Initialize @p file to a closed handle state.
    /// @param file Handle to initialize; ignored when NULL.
    void rt_file_init(RtFile *file);

    /// @brief Open @p path with the specified @p mode string.
    /// @param file Output handle populated on success.
    /// @param path Null-terminated filesystem path.
    /// @param mode fopen-style mode string (e.g., "r", "w", "a", variants with '+').
    /// @param out_err Optional error record receiving failure details.
    /// @return True on success; false when an error is reported via @p out_err.
    bool rt_file_open(RtFile *file, const char *path, const char *mode, RtError *out_err);

    /// @brief Close @p file when open.
    /// @param file Handle to close; remains valid for reuse after success.
    /// @param out_err Optional error record receiving failure details.
    /// @return True on success; false when closing fails.
    bool rt_file_close(RtFile *file, RtError *out_err);

    /// @brief Read a single byte from @p file.
    /// @param file Handle to read.
    /// @param out_byte Output byte when successful.
    /// @param out_err Optional error record receiving failure details.
    /// @return True when a byte is read; false on EOF or error.
    bool rt_file_read_byte(RtFile *file, uint8_t *out_byte, RtError *out_err);

    /// @brief Read a single line terminated by '\n' (newline excluded) from @p file.
    /// @param file Handle to read.
    /// @param out_line Receives newly allocated runtime string; NULL on failure.
    /// @param out_err Optional error record receiving failure details.
    /// @return True when a line is produced; false on EOF or error.
    bool rt_file_read_line(RtFile *file, rt_string *out_line, RtError *out_err);

    /// @brief Seek to @p offset relative to @p origin within @p file.
    /// @param file Handle to reposition.
    /// @param offset Byte offset to apply.
    /// @param origin One of SEEK_SET, SEEK_CUR, or SEEK_END.
    /// @param out_err Optional error record receiving failure details.
    /// @return True on success; false on error.
    bool rt_file_seek(RtFile *file, int64_t offset, int origin, RtError *out_err);

    /// @brief Write @p len bytes from @p data to @p file.
    /// @param file Handle to write.
    /// @param data Buffer containing bytes to write.
    /// @param len Number of bytes to write.
    /// @param out_err Optional error record receiving failure details.
    /// @return True when the entire buffer is written; false otherwise.
    bool rt_file_write(RtFile *file, const uint8_t *data, size_t len, RtError *out_err);

#ifdef __cplusplus
}
#endif

