// File: tests/runtime/FileWrappersTests.c
// Purpose: Validate runtime file wrappers using Viper string inputs.
// Key invariants: Missing files return Err_FileNotFound; closing unopened channel is invalid.
// Ownership/Lifetime: Uses runtime library; relies on shared literals.
// Links: docs/codemap.md
#include "rt_file.h"
#include "rt_string.h"

#include <assert.h>

int main(void)
{
    rt_string missing = rt_const_cstr("tests/runtime/does-not-exist.txt");
    int32_t code = rt_open_err_vstr(missing, RT_F_INPUT, 7);
    assert(code == Err_FileNotFound);

    int32_t close_code = rt_close_err(7);
    assert(close_code == Err_InvalidOperation);
    return 0;
}
