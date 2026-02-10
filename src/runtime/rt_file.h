//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the runtime library's file I/O API, providing the
// implementation for BASIC's OPEN, CLOSE, INPUT, PRINT #, and file-related
// statements. The file API wraps POSIX file descriptors with BASIC-specific
// semantics and integrates with the runtime's error handling system.
//
// BASIC File I/O Model:
// BASIC programs access files through numbered file handles assigned by
// OPEN statements. The runtime maps these logical file numbers to OS-level
// file descriptors and manages the file handle lifecycle (open, read, write,
// close, seeking).
//
// File Handle Structure (RtFile):
// The RtFile struct is a lightweight handle wrapping a POSIX file descriptor.
// A negative fd value (-1) indicates the handle is closed or uninitialized.
// Handles are stack-allocated by callers and initialized through rt_file_init.
//
// File Operations Supported:
// - Opening: rt_file_open with mode strings ("r", "w", "a", "r+", "w+", "a+")
// - Reading: rt_file_read_byte, rt_file_read_line, rt_file_read_all
// - Writing: rt_file_write_bytes, rt_file_write_str, rt_file_print
// - Positioning: rt_file_seek, rt_file_tell (for random access files)
// - Closing: rt_file_close (releases OS resources)
// - Queries: rt_file_is_open, rt_file_eof
//
// BASIC Mode Mapping:
// BASIC's OPEN statement supports multiple access modes:
// - INPUT mode → "r" (read-only, must exist)
// - OUTPUT mode → "w" (write-only, truncate or create)
// - APPEND mode → "a" (write-only, append to end)
// - BINARY mode → random access with seeking
//
// The basic_mode parameter in rt_file_open maps these BASIC modes to
// appropriate POSIX open flags and buffering strategies.
//
// Error Handling Integration:
// All file operations use the RtError system for failure reporting. Common
// errors include:
// - Err_FileNotFound: File doesn't exist when opening for read
// - Err_IOError: Permission denied, disk full, device error
// - Err_EOF: Read attempted at end of file
//
// These errors map to BASIC ERR codes, enabling ON ERROR GOTO handlers
// to catch and handle file I/O failures.
//
// Platform Abstraction:
// The API abstracts platform differences (Unix vs Windows file APIs) through
// conditional compilation. On Unix, it uses POSIX file descriptors. On Windows,
// it uses file handles with appropriate Win32 API calls. This ensures consistent
// behavior across platforms while using native APIs efficiently.
//
// Buffering Strategy:
// File I/O uses line buffering for text files and unbuffered access for binary
// files. This matches BASIC's expectation that PRINT # outputs appear immediately
// while maintaining performance for text processing.
//
// Resource Management:
// Files are NOT automatically closed. BASIC programs must explicitly CLOSE
// files or close them at program termination. The runtime tracks open files
// and ensures cleanup on normal and abnormal termination.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_error.h"
#include "rt_string.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Lightweight handle representing an open runtime file.
    typedef struct RtFile
    {
        int fd; ///< Underlying POSIX file descriptor or -1 when closed.
    } RtFile;

    /// @brief Initialize @p file to a closed handle state.
    /// @param file Handle to initialize; ignored when NULL.
    void rt_file_init(RtFile *file);

    /// @brief Open @p path with the specified @p mode string and BASIC semantics.
    /// @param file Output handle populated on success.
    /// @param path Null-terminated filesystem path.
    /// @param mode fopen-style mode string (e.g., "r", "w", "a", variants with '+').
    /// @param basic_mode BASIC OPEN mode enumerator when available; pass RT_F_UNSPECIFIED for plain
    /// stdio semantics.
    /// @param out_err Optional error record receiving failure details.
    /// @return 1 on success; 0 when an error is reported via @p out_err.
    int8_t rt_file_open(
        RtFile *file, const char *path, const char *mode, int32_t basic_mode, RtError *out_err);

    /// @brief Close @p file when open.
    /// @param file Handle to close; remains valid for reuse after success.
    /// @param out_err Optional error record receiving failure details.
    /// @return 1 on success; 0 when closing fails.
    int8_t rt_file_close(RtFile *file, RtError *out_err);

    /// @brief Read a single byte from @p file.
    /// @param file Handle to read.
    /// @param out_byte Output byte when successful.
    /// @param out_err Optional error record receiving failure details.
    /// @return 1 when a byte is read; 0 on EOF or error.
    int8_t rt_file_read_byte(RtFile *file, uint8_t *out_byte, RtError *out_err);

    /// @brief Read a single line terminated by '\n' (newline excluded) from @p file.
    /// @param file Handle to read.
    /// @param out_line Receives newly allocated runtime string; NULL on failure.
    /// @param out_err Optional error record receiving failure details.
    /// @return 1 when a line is produced; 0 on EOF or error.
    int8_t rt_file_read_line(RtFile *file, rt_string *out_line, RtError *out_err);

    /// @brief Seek to @p offset relative to @p origin within @p file.
    /// @param file Handle to reposition.
    /// @param offset Byte offset to apply.
    /// @param origin One of SEEK_SET, SEEK_CUR, or SEEK_END.
    /// @param out_err Optional error record receiving failure details.
    /// @return 1 on success; 0 on error.
    int8_t rt_file_seek(RtFile *file, int64_t offset, int origin, RtError *out_err);

    /// @brief Write @p len bytes from @p data to @p file.
    /// @param file Handle to write.
    /// @param data Buffer containing bytes to write.
    /// @param len Number of bytes to write.
    /// @param out_err Optional error record receiving failure details.
    /// @return 1 when the entire buffer is written; 0 otherwise.
    int8_t rt_file_write(RtFile *file, const uint8_t *data, size_t len, RtError *out_err);

    /// @brief Enumerates BASIC OPEN modes understood by the runtime wrapper API.
    enum RtFileMode
    {
        RT_F_UNSPECIFIED = -1, ///< Mode not associated with BASIC OPEN semantics.
        RT_F_INPUT = 0,        ///< OPEN ... FOR INPUT
        RT_F_OUTPUT = 1,       ///< OPEN ... FOR OUTPUT
        RT_F_APPEND = 2,       ///< OPEN ... FOR APPEND
        RT_F_BINARY = 3,       ///< OPEN ... FOR BINARY
        RT_F_RANDOM = 4,       ///< OPEN ... FOR RANDOM
    };

    /// @brief Open @p path for the specified BASIC @p mode on @p channel.
    /// @param path Runtime string describing the filesystem path.
    /// @param mode Mode enumerator matching BASIC OPEN semantics.
    /// @param channel Numeric channel identifier provided by the caller.
    /// @return 0 on success; error code aligned with @ref Err otherwise.
    int32_t rt_open_err_vstr(ViperString *path, int32_t mode, int32_t channel);

    /// @brief Close the runtime file associated with @p channel when present.
    /// @param channel Numeric channel identifier previously passed to OPEN.
    /// @return 0 on success; error code aligned with @ref Err otherwise.
    int32_t rt_close_err(int32_t channel);

    /// @brief Write @p s to the file bound to @p channel without a trailing newline.
    /// @param channel Numeric channel identifier previously passed to OPEN.
    /// @param s Runtime string to write; NULL strings are ignored.
    /// @return 0 on success; error code aligned with @ref Err otherwise.
    int32_t rt_write_ch_err(int32_t channel, ViperString *s);

    /// @brief Write @p s to the file bound to @p channel followed by a newline.
    /// @param channel Numeric channel identifier previously passed to OPEN.
    /// @param s Runtime string to write; NULL strings are treated as empty.
    /// @return 0 on success; error code aligned with @ref Err otherwise.
    int32_t rt_println_ch_err(int32_t channel, ViperString *s);

    /// @brief Read a line of text from @p channel into a newly allocated runtime string.
    /// @param channel Numeric channel identifier previously passed to OPEN FOR INPUT.
    /// @param out Receives allocated string on success; set to NULL on failure.
    /// @return 0 on success; error code aligned with @ref Err otherwise.
    int32_t rt_line_input_ch_err(int32_t channel, ViperString **out);

    /// @brief Query the file descriptor associated with @p channel.
    /// @param channel Numeric channel identifier previously passed to OPEN.
    /// @param out_fd Receives the file descriptor on success.
    /// @return 0 on success; error code aligned with @ref Err otherwise.
    int32_t rt_file_channel_fd(int32_t channel, int *out_fd);

    /// @brief Read the cached EOF flag for @p channel.
    /// @param channel Numeric channel identifier previously passed to OPEN.
    /// @param out_at_eof Receives the cached EOF state on success.
    /// @return 0 on success; error code aligned with @ref Err otherwise.
    int32_t rt_file_channel_get_eof(int32_t channel, int8_t *out_at_eof);

    /// @brief Update the cached EOF flag for @p channel.
    /// @param channel Numeric channel identifier previously passed to OPEN.
    /// @param at_eof New EOF state to record.
    /// @return 0 on success; error code aligned with @ref Err otherwise.
    int32_t rt_file_channel_set_eof(int32_t channel, int8_t at_eof);

#ifdef __cplusplus
}
#endif
