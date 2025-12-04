//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/FileWrappersTests.c
// Purpose: Validate runtime file wrappers using Viper string inputs.
// Key invariants: Missing files return Err_FileNotFound; closing unopened channel is invalid.
// Ownership/Lifetime: Uses runtime library; relies on shared literals.
// Links: docs/codemap.md
#include "viper/runtime/rt.h"

#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

/// @brief Entry point for validating basic file wrapper behaviours.
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
    return 0;
}
