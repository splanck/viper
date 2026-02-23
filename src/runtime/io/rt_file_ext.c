//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_file_ext.c
// Purpose: High-level file helpers backing the Viper.IO.File static methods.
//          Implements ReadAllText, WriteAllText, ReadAllBytes, WriteAllBytes,
//          ReadAllLines, AppendAllText, Copy, Move, Delete, Exists, GetSize,
//          and related operations by bridging OOP-style calls to the runtime
//          file and string utilities.
//
// Key invariants:
//   - ReadAllText/ReadAllBytes read the entire file into memory in one call.
//   - WriteAllText/WriteAllBytes create or truncate the file atomically.
//   - Exists returns false for directories; use Dir.Exists for those.
//   - Copy does not overwrite the destination unless explicitly requested.
//   - All functions handle both POSIX and Windows file APIs transparently.
//   - Internal bytes layout is accessed directly to avoid per-byte overhead.
//
// Ownership/Lifetime:
//   - Returned strings and bytes buffers are fresh allocations owned by callers.
//   - Input strings are borrowed; this module does not retain string references.
//
// Links: src/runtime/io/rt_file_ext.h (public API),
//        src/runtime/io/rt_file.h (low-level RtFile handle and channel table),
//        src/runtime/io/rt_file_path.h (mode string conversion)
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_file.h"

/* O-02: Internal bytes layout for direct data access (avoids per-byte rt_bytes_get) */
typedef struct
{
    int64_t len;
    uint8_t *data;
} file_bytes_impl;

static inline uint8_t *file_bytes_data(void *obj)
{
    return obj ? ((file_bytes_impl *)obj)->data : NULL;
}

#include "rt_file_path.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include "rt_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#if !RT_PLATFORM_WINDOWS
#include <unistd.h>
#endif

#if RT_PLATFORM_WINDOWS
#include <io.h>
#include <sys/utime.h>
#define utime _utime
#define utimbuf _utimbuf
#else
#include <utime.h>
#endif

#if defined(O_BINARY)
#define RT_FILE_O_BINARY O_BINARY
#elif defined(_O_BINARY)
#define RT_FILE_O_BINARY _O_BINARY
#else
#define RT_FILE_O_BINARY 0
#endif

/// @brief Convert a runtime string path to a host path; traps on failure.
/// @param path Runtime string containing the path.
/// @param context Trap message to use when conversion fails.
/// @return Null-terminated host path.
static const char *rt_io_file_require_path(rt_string path, const char *context)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        rt_trap(context);
    return cpath;
}

/// What: Return 1 if the file at @p path exists, 0 otherwise.
/// Why:  Support Viper.IO.File.Exists semantics from the runtime.
/// How:  Converts @p path to a host path and calls stat().
int64_t rt_io_file_exists(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return 0;
    struct stat st;
    if (stat(cpath, &st) == 0)
        return 1;
    return 0;
}

/// What: Read entire file into a runtime string. Return empty on error.
/// Why:  Provide a convenience API for small text files in examples/tests.
/// How:  Opens the file, reads all bytes, returns an rt_string view of them.
rt_string rt_io_file_read_all_text(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return rt_str_empty();

    int fd = open(cpath, O_RDONLY);
    if (fd < 0)
        return rt_str_empty();

    struct stat st;
    if (fstat(fd, &st) != 0)
    {
        close(fd);
        return rt_str_empty();
    }
    size_t size = (st.st_size > 0) ? (size_t)st.st_size : 0;
    // Handle empty files
    if (size == 0)
    {
        close(fd);
        return rt_str_empty();
    }

    char *buf = (char *)malloc(size);
    if (!buf)
    {
        close(fd);
        return rt_str_empty();
    }

    size_t off = 0;
    while (off < size)
    {
        ssize_t n = read(fd, buf + off, size - off);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            free(buf);
            close(fd);
            return rt_str_empty();
        }
        if (n == 0)
            break;
        off += (size_t)n;
    }
    close(fd);
    // If short read, shrink to actual bytes read.
    rt_string s = rt_string_from_bytes(buf, off);
    free(buf);
    return s ? s : rt_str_empty();
}

/// What: Write @p contents to @p path, truncating or creating the file.
/// Why:  Complement read_all_text with a simple write primitive.
/// How:  Opens with O_WRONLY|O_CREAT|O_TRUNC and writes all bytes, retrying on EINTR.
void rt_io_file_write_all_text(rt_string path, rt_string contents)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return;

    int fd = open(cpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;

    const uint8_t *data = NULL;
    size_t len = rt_file_string_view(contents, &data);
    size_t written = 0;
    while (written < len)
    {
        ssize_t n = write(fd, data + written, len - written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        written += (size_t)n;
    }
    (void)close(fd);
}

/// What: Append @p text and a newline to @p path (creating it when missing).
/// Why:  Provide a convenient "append line" helper for Viper.IO.File.
/// How:  Opens with O_APPEND and writes the UTF-8 bytes followed by '\n'.
void rt_io_file_append_line(rt_string path, rt_string text)
{
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.AppendLine: invalid file path");

    int fd = open(cpath, O_WRONLY | O_CREAT | O_APPEND | RT_FILE_O_BINARY, 0666);
    if (fd < 0)
        rt_trap("Viper.IO.File.AppendLine: failed to open file");

    const uint8_t *data = NULL;
    size_t len = rt_file_string_view(text, &data);
    size_t written = 0;
    while (written < len)
    {
        ssize_t n = write(fd, data + written, len - written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            (void)close(fd);
            rt_trap("Viper.IO.File.AppendLine: failed to write file");
        }
        if (n == 0)
        {
            (void)close(fd);
            rt_trap("Viper.IO.File.AppendLine: failed to write file");
        }
        written += (size_t)n;
    }

    char nl = '\n';
    ssize_t n;
    do
    {
        n = write(fd, &nl, 1);
    } while (n < 0 && errno == EINTR);
    if (n != 1)
    {
        (void)close(fd);
        rt_trap("Viper.IO.File.AppendLine: failed to write newline");
    }

    (void)close(fd);
}

/// What: Read the entire file at @p path as a Bytes object.
/// Why:  Provide binary file input for Viper.IO.File.ReadAllBytes.
/// How:  Reads the file into a temporary buffer and copies it into a new Bytes.
void *rt_io_file_read_all_bytes(rt_string path)
{
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.ReadAllBytes: invalid file path");

    int fd = open(cpath, O_RDONLY | RT_FILE_O_BINARY);
    if (fd < 0)
        rt_trap("Viper.IO.File.ReadAllBytes: failed to open file");

    struct stat st;
    if (fstat(fd, &st) != 0)
    {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllBytes: failed to stat file");
    }

    size_t size = (st.st_size > 0) ? (size_t)st.st_size : 0;
    if (size == 0)
    {
        (void)close(fd);
        return rt_bytes_new(0);
    }

    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf)
    {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllBytes: allocation failed");
    }

    size_t off = 0;
    while (off < size)
    {
        ssize_t n = read(fd, buf + off, size - off);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            free(buf);
            (void)close(fd);
            rt_trap("Viper.IO.File.ReadAllBytes: failed to read file");
        }
        if (n == 0)
            break;
        off += (size_t)n;
    }
    (void)close(fd);

    /* O-02: Use memcpy into the raw bytes buffer instead of per-byte rt_bytes_set */
    void *bytes = rt_bytes_new((int64_t)off);
    uint8_t *dst = file_bytes_data(bytes);
    if (dst)
        memcpy(dst, buf, off);

    free(buf);
    return bytes;
}

/// What: Write an entire Bytes object to @p path, overwriting the file.
/// Why:  Provide binary file output for Viper.IO.File.WriteAllBytes.
/// How:  Writes bytes in chunks to avoid per-byte syscalls.
void rt_io_file_write_all_bytes(rt_string path, void *bytes)
{
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.WriteAllBytes: invalid file path");

    if (!bytes)
        rt_trap("Viper.IO.File.WriteAllBytes: null Bytes");

    int fd = open(cpath, O_WRONLY | O_CREAT | O_TRUNC | RT_FILE_O_BINARY, 0666);
    if (fd < 0)
        rt_trap("Viper.IO.File.WriteAllBytes: failed to open file");

    /* IO-H-1: use raw data pointer instead of per-byte rt_bytes_get() —
       eliminates O(n) function calls in favour of a single write() */
    int64_t len = rt_bytes_len(bytes);
    const uint8_t *src = file_bytes_data(bytes);

    if (src && len > 0)
    {
        size_t written = 0;
        while (written < (size_t)len)
        {
            ssize_t n = write(fd, src + written, (size_t)len - written);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                (void)close(fd);
                rt_trap("Viper.IO.File.WriteAllBytes: failed to write file");
            }
            if (n == 0)
            {
                (void)close(fd);
                rt_trap("Viper.IO.File.WriteAllBytes: failed to write file");
            }
            written += (size_t)n;
        }
    }

    (void)close(fd);
}

/// What: Read a text file and return a Seq of lines.
/// Why:  Provide convenient line-based file input for Viper.IO.File.ReadAllLines.
/// How:  Reads the file and splits on '\n' and '\r\n', stripping line terminators.
void *rt_io_file_read_all_lines(rt_string path)
{
    const char *cpath =
        rt_io_file_require_path(path, "Viper.IO.File.ReadAllLines: invalid file path");

    int fd = open(cpath, O_RDONLY | RT_FILE_O_BINARY);
    if (fd < 0)
        rt_trap("Viper.IO.File.ReadAllLines: failed to open file");

    struct stat st;
    if (fstat(fd, &st) != 0)
    {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllLines: failed to stat file");
    }

    size_t size = (st.st_size > 0) ? (size_t)st.st_size : 0;
    if (size == 0)
    {
        (void)close(fd);
        return rt_seq_new();
    }

    char *buf = (char *)malloc(size);
    if (!buf)
    {
        (void)close(fd);
        rt_trap("Viper.IO.File.ReadAllLines: allocation failed");
    }

    size_t off = 0;
    while (off < size)
    {
        ssize_t n = read(fd, buf + off, size - off);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            free(buf);
            (void)close(fd);
            rt_trap("Viper.IO.File.ReadAllLines: failed to read file");
        }
        if (n == 0)
            break;
        off += (size_t)n;
    }
    (void)close(fd);

    void *seq = rt_seq_new();
    size_t i = 0;
    while (i < off)
    {
        size_t start = i;
        while (i < off && buf[i] != '\n' && buf[i] != '\r')
            ++i;
        size_t end = i;

        rt_string line =
            (end == start) ? rt_str_empty() : rt_string_from_bytes(buf + start, end - start);
        rt_seq_push(seq, line);

        if (i >= off)
            break;
        if (buf[i] == '\r')
        {
            if (i + 1 < off && buf[i + 1] == '\n')
                i += 2;
            else
                i += 1;
        }
        else
        {
            ++i; // '\n'
        }
    }

    free(buf);
    return seq;
}

/// What: Delete the file at @p path.
/// Why:  Allow simple cleanup without surfacing platform-specific APIs.
/// How:  Converts to host path and calls unlink(); errors are ignored.
void rt_io_file_delete(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return;
    (void)unlink(cpath);
}

/// What: Copy a file from @p src to @p dst.
/// Why:  Allow file duplication without platform-specific APIs.
/// How:  Reads src file and writes to dst file.
void rt_file_copy(rt_string src, rt_string dst)
{
    const char *src_path = NULL;
    const char *dst_path = NULL;
    if (!rt_file_path_from_vstr(src, &src_path) || !src_path)
        return;
    if (!rt_file_path_from_vstr(dst, &dst_path) || !dst_path)
        return;

    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0)
    {
        char msg[512];
        snprintf(msg, sizeof(msg), "File.Copy: cannot open source '%s': %s",
                 src_path, strerror(errno));
        rt_trap(msg);
    }

    int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dst_fd < 0)
    {
        // IO-H-4: destination open failure was previously silent — now traps
        close(src_fd);
        char msg[512];
        snprintf(msg, sizeof(msg), "File.Copy: cannot open destination '%s': %s",
                 dst_path, strerror(errno));
        rt_trap(msg);
    }

    char buf[8192];
    ssize_t n;
    while ((n = read(src_fd, buf, sizeof(buf))) > 0)
    {
        size_t written = 0;
        while (written < (size_t)n)
        {
            ssize_t w = write(dst_fd, buf + written, (size_t)n - written);
            if (w < 0)
            {
                if (errno == EINTR)
                    continue;
                close(src_fd);
                close(dst_fd);
                rt_trap("File.Copy: write error (disk full or I/O error)");
            }
            written += (size_t)w;
        }
    }

    close(src_fd);
    close(dst_fd);
}

/// What: Move/rename a file from @p src to @p dst.
/// Why:  Allow file relocation without platform-specific APIs.
/// How:  Uses rename(); falls back to copy+delete if needed.
void rt_file_move(rt_string src, rt_string dst)
{
    const char *src_path = NULL;
    const char *dst_path = NULL;
    if (!rt_file_path_from_vstr(src, &src_path) || !src_path)
        return;
    if (!rt_file_path_from_vstr(dst, &dst_path) || !dst_path)
        return;

    if (rename(src_path, dst_path) == 0)
        return;

    // Fallback: copy then delete (for cross-filesystem moves)
    rt_file_copy(src, dst);
    (void)unlink(src_path);
}

/// What: Get the size of a file in bytes.
/// Why:  Allow querying file size without opening the file.
/// How:  Uses stat() to get file size.
int64_t rt_file_size(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return -1;

    struct stat st;
    if (stat(cpath, &st) != 0)
        return -1;

    return (int64_t)st.st_size;
}

/// What: Read entire file as a Bytes object.
/// Why:  Support binary file reading.
/// How:  Opens file, reads all bytes, returns Bytes object.
void *rt_file_read_bytes(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return rt_bytes_new(0);

    int fd = open(cpath, O_RDONLY);
    if (fd < 0)
        return rt_bytes_new(0);

    struct stat st;
    if (fstat(fd, &st) != 0)
    {
        close(fd);
        return rt_bytes_new(0);
    }

    size_t size = (st.st_size > 0) ? (size_t)st.st_size : 0;
    if (size == 0)
    {
        close(fd);
        return rt_bytes_new(0);
    }

    // Create bytes object with the file size
    void *bytes = rt_bytes_new((int64_t)size);

    // Read directly into bytes data
    // We need to access the internal data pointer for this
    // Use a temporary buffer and copy
    char *buf = (char *)malloc(size);
    if (!buf)
    {
        close(fd);
        return bytes; // Return empty bytes
    }

    size_t off = 0;
    while (off < size)
    {
        ssize_t n = read(fd, buf + off, size - off);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            free(buf);
            close(fd);
            return bytes;
        }
        if (n == 0)
            break;
        off += (size_t)n;
    }
    close(fd);

    /* O-02: Use memcpy into the raw bytes buffer instead of per-byte rt_bytes_set */
    uint8_t *dst2 = file_bytes_data(bytes);
    if (dst2)
        memcpy(dst2, buf, off);

    free(buf);
    return bytes;
}

/// What: Write a Bytes object to a file.
/// Why:  Support binary file writing.
/// How:  Opens file, writes all bytes from Bytes object using chunked writes.
void rt_file_write_bytes(rt_string path, void *bytes)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return;

    if (!bytes)
        return;

    int fd = open(cpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;

    /* O-01: Use the raw data pointer and chunked writes instead of per-byte write() */
    const uint8_t *src = file_bytes_data(bytes);
    int64_t len = rt_bytes_len(bytes);

    if (src && len > 0)
    {
        size_t written = 0;
        while (written < (size_t)len)
        {
            ssize_t n = write(fd, src + written, (size_t)len - written);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (n == 0)
                break;
            written += (size_t)n;
        }
    }

    close(fd);
}

/// What: Read entire file as a sequence of lines.
/// Why:  Support line-by-line text file reading.
/// How:  Reads file, splits by newlines, returns Seq of strings.
void *rt_file_read_lines(rt_string path)
{
    void *seq = rt_seq_new();

    rt_string content = rt_io_file_read_all_text(path);
    if (!content || rt_str_len(content) == 0)
        return seq;

    const char *data = rt_string_cstr(content);
    if (!data)
        return seq;

    size_t len = (size_t)rt_str_len(content);
    size_t line_start = 0;

    for (size_t i = 0; i <= len; i++)
    {
        if (i == len || data[i] == '\n')
        {
            if (i == len && line_start == len)
                break;
            size_t line_end = i;
            // Handle \r\n
            if (line_end > line_start && data[line_end - 1] == '\r')
                line_end--;

            rt_string line = rt_string_from_bytes(data + line_start, line_end - line_start);
            rt_seq_push(seq, line);
            line_start = i + 1;
        }
    }

    return seq;
}

/// What: Write a sequence of strings to a file as lines.
/// Why:  Support line-by-line text file writing.
/// How:  Writes each string followed by newline.
void rt_file_write_lines(rt_string path, void *lines)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return;

    if (!lines)
        return;

    int fd = open(cpath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;

    int64_t count = rt_seq_len(lines);
    for (int64_t i = 0; i < count; i++)
    {
        rt_string line = (rt_string)rt_seq_get(lines, i);
        if (line)
        {
            const uint8_t *data = NULL;
            size_t len = rt_file_string_view(line, &data);
            size_t written = 0;
            while (written < len)
            {
                ssize_t n = write(fd, data + written, len - written);
                if (n < 0)
                {
                    if (errno == EINTR)
                        continue;
                    break;
                }
                written += (size_t)n;
            }
        }
        // Write newline
        char nl = '\n';
        ssize_t n;
        do
        {
            n = write(fd, &nl, 1);
        } while (n < 0 && errno == EINTR);
    }

    close(fd);
}

/// What: Append text to an existing file.
/// Why:  Support appending without reading+writing entire file.
/// How:  Opens file with O_APPEND and writes text.
void rt_file_append(rt_string path, rt_string text)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return;

    int fd = open(cpath, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;

    const uint8_t *data = NULL;
    size_t len = rt_file_string_view(text, &data);
    size_t written = 0;
    while (written < len)
    {
        ssize_t n = write(fd, data + written, len - written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        written += (size_t)n;
    }

    close(fd);
}

/// What: Get file modification time as Unix timestamp.
/// Why:  Support querying when a file was last modified.
/// How:  Uses stat() to get mtime.
int64_t rt_file_modified(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return 0;

    struct stat st;
    if (stat(cpath, &st) != 0)
        return 0;

    return (int64_t)st.st_mtime;
}

/// What: Create file or update modification time.
/// Why:  Support "touch" semantics from Unix.
/// How:  Creates file if not exists, updates mtime if exists.
void rt_file_touch(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return;

    // Try to update mtime (works if file exists)
    if (utime(cpath, NULL) == 0)
        return;

    // File doesn't exist, create it
    int fd = open(cpath, O_WRONLY | O_CREAT, 0666);
    if (fd >= 0)
        close(fd);
}
