// File: tests/runtime/FileChannelIoTests.c
// Purpose: Exercise runtime channel I/O helpers with success paths.
// Key invariants: Wrappers return Err_None on success and allocate readable strings.
// Ownership/Lifetime: Runtime owns allocations; test releases acquired strings.
// Links: docs/codemap.md
#include "rt_file.h"
#include "rt_string.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char template_path[128];
    int written = snprintf(template_path,
                           sizeof(template_path),
                           "tmp_channel_io_%ld.txt",
                           (long)getpid());
    assert(written > 0 && written < (int)sizeof(template_path));
    remove(template_path);

    ViperString *path = rt_const_cstr(template_path);
    assert(path != NULL);

    int32_t open_out = rt_open_err_vstr(path, RT_F_OUTPUT, 5);
    assert(open_out == Err_None);

    ViperString *hello = rt_const_cstr("hello ");
    int32_t write_rc = rt_write_ch_err(5, hello);
    assert(write_rc == Err_None);

    ViperString *world = rt_const_cstr("world");
    int32_t println_rc = rt_println_ch_err(5, world);
    assert(println_rc == Err_None);

    int32_t close_out = rt_close_err(5);
    assert(close_out == Err_None);

    int32_t open_in = rt_open_err_vstr(path, RT_F_INPUT, 5);
    assert(open_in == Err_None);

    ViperString *line = NULL;
    int32_t input_rc = rt_line_input_ch_err(5, &line);
    assert(input_rc == Err_None);
    assert(line != NULL);
    const char *line_view = rt_string_cstr(line);
    assert(line_view != NULL);
    assert(strcmp(line_view, "hello world") == 0);
    rt_string_unref(line);

    int32_t close_in = rt_close_err(5);
    assert(close_in == Err_None);

    remove(template_path);
    rt_string_unref(world);
    rt_string_unref(hello);
    rt_string_unref(path);
    return 0;
}
