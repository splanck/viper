// File: tests/unit/runtime/RTFileChannelOverflowTests.c
// Purpose: Verify rt_open_err_vstr detects channel table growth overflow.
// Key invariants: Overflow guard prevents realloc and reports runtime error.
// Ownership: Test saves/restores global channel table state around mutation.
// Links: docs/codemap.md

#include "rt.hpp"
#include "rt_internal.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    RtFileChannelTestState saved = rt_file_test_capture_state();

    size_t max_capacity = rt_file_test_max_capacity();
    assert(max_capacity > 0);

    rt_file_test_preset_growth_overflow(max_capacity);

    const char path_bytes[] = "overflow_guard";
    rt_string path = rt_string_from_bytes(path_bytes, sizeof(path_bytes) - 1);
    assert(path != NULL);

    int32_t result = rt_open_err_vstr(path, RT_F_INPUT, 7);
    assert(result == (int32_t)Err_RuntimeError);

    rt_string_unref(path);
    rt_file_test_restore_state(saved);
    return 0;
}
