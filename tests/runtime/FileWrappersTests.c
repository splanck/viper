// File: tests/runtime/FileWrappersTests.c
// Purpose: Validate runtime file wrappers using Viper string inputs.
// Key invariants: Missing files return Err_FileNotFound; closing unopened channel is invalid.
// Ownership/Lifetime: Uses runtime library; relies on shared literals.
// Links: docs/codemap.md
#include "rt_file.h"
#include "rt_string.h"

#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    rt_string missing = rt_const_cstr("tests/runtime/does-not-exist.txt");
    int32_t code = rt_open_err_vstr(missing, RT_F_INPUT, 7);
    assert(code == Err_FileNotFound);

    int32_t close_code = rt_close_err(7);
    assert(close_code == Err_InvalidOperation);

    const char *binary_path = "tmp-rt-file-binary.dat";
    unlink(binary_path);
    rt_string binary = rt_const_cstr(binary_path);
    int32_t binary_code = rt_open_err_vstr(binary, RT_F_BINARY, 8);
    assert(binary_code == 0);
    struct stat st;
    assert(stat(binary_path, &st) == 0);
    assert(S_ISREG(st.st_mode));
    assert(rt_close_err(8) == 0);
    unlink(binary_path);

    const char *random_path = "tmp-rt-file-random.dat";
    unlink(random_path);
    rt_string random = rt_const_cstr(random_path);
    int32_t random_code = rt_open_err_vstr(random, RT_F_RANDOM, 9);
    assert(random_code == 0);
    assert(stat(random_path, &st) == 0);
    assert(S_ISREG(st.st_mode));
    assert(rt_close_err(9) == 0);
    unlink(random_path);

#ifdef _WIN32
    const char *binary_roundtrip_path = "tmp-rt-file-binary-roundtrip.dat";
    remove(binary_roundtrip_path);
    rt_string binary_roundtrip = rt_const_cstr(binary_roundtrip_path);
    assert(rt_open_err_vstr(binary_roundtrip, RT_F_BINARY, 10) == 0);

    int fd = -1;
    assert(rt_file_channel_fd(10, &fd) == 0);
    RtFile write_file;
    write_file.fd = fd;
    uint8_t carriage = '\r';
    RtError err = RT_ERROR_NONE;
    assert(rt_file_write(&write_file, &carriage, 1, &err));
    assert(rt_close_err(10) == 0);

    assert(rt_open_err_vstr(binary_roundtrip, RT_F_BINARY, 10) == 0);
    assert(rt_file_channel_fd(10, &fd) == 0);
    RtFile read_file;
    read_file.fd = fd;
    uint8_t read_back = 0;
    err = RT_ERROR_NONE;
    assert(rt_file_read_byte(&read_file, &read_back, &err));
    assert(read_back == '\r');
    assert(rt_close_err(10) == 0);
    remove(binary_roundtrip_path);
#endif
    return 0;
}
