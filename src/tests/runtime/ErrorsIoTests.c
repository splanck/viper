// File: tests/runtime/ErrorsIoTests.c
// Purpose: Validate runtime file helpers return structured errors on failure paths.
// Key invariants: Missing files map to Err_FileNotFound, EOF detection yields Err_EOF, and OS
// failures surface Err_IOError. Ownership: Exercises the C runtime API directly without
// higher-level wrappers. Links: docs/specs/errors.md

#define _XOPEN_SOURCE 700

#include "viper/runtime/rt.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void ensure_missing_open_sets_file_not_found(void)
{
    char path[] = "/tmp/viper_io_missingXXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0)
    {
        close(fd);
        unlink(path);
    }

    RtFile file;
    rt_file_init(&file);
    RtError err = {Err_None, 0};
    bool ok = rt_file_open(&file, path, "r", RT_F_UNSPECIFIED, &err);
    assert(!ok);
    assert(err.kind == Err_FileNotFound);
    assert(err.code != 0);
}

static void ensure_read_byte_reports_eof(void)
{
    char path[] = "/tmp/viper_io_emptyXXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    RtFile file;
    rt_file_init(&file);
    RtError err = {Err_None, 0};
    bool ok = rt_file_open(&file, path, "r", RT_F_UNSPECIFIED, &err);
    assert(ok);

    uint8_t value = 0xFF;
    err.kind = Err_RuntimeError;
    err.code = -1;
    ok = rt_file_read_byte(&file, &value, &err);
    assert(!ok);
    assert(err.kind == Err_EOF);
    assert(err.code == 0);

    ok = rt_file_close(&file, &err);
    assert(ok);
    unlink(path);
}

static void ensure_read_line_reports_eof(void)
{
    char path[] = "/tmp/viper_io_lineXXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);

    RtFile file;
    rt_file_init(&file);
    RtError err = {Err_None, 0};
    bool ok = rt_file_open(&file, path, "r", RT_F_UNSPECIFIED, &err);
    assert(ok);

    rt_string line = NULL;
    err.kind = Err_RuntimeError;
    err.code = -1;
    ok = rt_file_read_line(&file, &line, &err);
    assert(!ok);
    assert(err.kind == Err_EOF);
    assert(err.code == 0);
    assert(line == NULL);

    ok = rt_file_close(&file, &err);
    assert(ok);
    unlink(path);
}

static void ensure_read_line_trims_crlf(void)
{
    char path[] = "/tmp/viper_io_crlfXXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);

    const char payload[] = "hello world\r\n";
    ssize_t written = write(fd, payload, sizeof(payload) - 1);
    assert(written == (ssize_t)(sizeof(payload) - 1));
    int rc = close(fd);
    assert(rc == 0);

    RtFile file;
    rt_file_init(&file);
    RtError err = {Err_None, 0};
    bool ok = rt_file_open(&file, path, "r", RT_F_UNSPECIFIED, &err);
    assert(ok);

    rt_string line = NULL;
    ok = rt_file_read_line(&file, &line, &err);
    assert(ok);
    assert(line != NULL);
    const char *cstr = rt_string_cstr(line);
    assert(strcmp(cstr, "hello world") == 0);
    assert(rt_len(line) == (int64_t)strlen("hello world"));

    rt_string_unref(line);

    ok = rt_file_close(&file, &err);
    assert(ok);
    unlink(path);
}

static void ensure_invalid_handle_surfaces_ioerror(void)
{
    RtFile file;
    rt_file_init(&file);
    file.fd = -1;

    RtError err = {Err_None, 0};
    bool ok = rt_file_seek(&file, 0, SEEK_SET, &err);
    assert(!ok);
    assert(err.kind == Err_IOError);
    assert(err.code != 0);
}

static void ensure_seek_out_of_range_reports_invalid_operation(void)
{
#if (defined(OFF_MAX) && (OFF_MAX < INT64_MAX)) || (defined(OFF_MIN) && (OFF_MIN > INT64_MIN))
    char path[] = "/tmp/viper_io_seek_rangeXXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    int rc = close(fd);
    assert(rc == 0);

    RtFile file;
    rt_file_init(&file);
    RtError err = {Err_None, 0};
    bool ok = rt_file_open(&file, path, "r", RT_F_UNSPECIFIED, &err);
    assert(ok);

#if defined(OFF_MAX) && (OFF_MAX < INT64_MAX)
    const int64_t overflow_offset = (int64_t)OFF_MAX + 1;
#else
    const int64_t overflow_offset = (int64_t)OFF_MIN - 1;
#endif
    err.kind = Err_None;
    err.code = 0;
    ok = rt_file_seek(&file, overflow_offset, SEEK_SET, &err);
    assert(!ok);
    assert(err.kind == Err_InvalidOperation);
    assert(err.code == ERANGE);

    ok = rt_file_close(&file, &err);
    assert(ok);
    unlink(path);
#else
    // No representable int64_t value falls outside off_t's range on this platform.
#endif
}

int main(void)
{
    ensure_missing_open_sets_file_not_found();
    ensure_read_byte_reports_eof();
    ensure_read_line_reports_eof();
    ensure_read_line_trims_crlf();
    ensure_invalid_handle_surfaces_ioerror();
    ensure_seek_out_of_range_reports_invalid_operation();
    return 0;
}
