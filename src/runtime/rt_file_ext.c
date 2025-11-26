//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_file_ext.c
// Purpose: High-level file helpers backing Viper.IO.File static methods.
//          These thin wrappers bridge OOP-style calls to the existing runtime
//          file and string utilities.
//
//===----------------------------------------------------------------------===//

#include "rt_file.h"
#include "rt_file_path.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
