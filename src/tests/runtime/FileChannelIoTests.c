// File: tests/runtime/FileChannelIoTests.c
// Purpose: Exercise runtime channel I/O helpers with success paths.
// Key invariants: Wrappers return Err_None on success and allocate readable strings.
// Ownership/Lifetime: Runtime owns allocations; test releases acquired strings.
// Links: docs/codemap.md
#include "viper/runtime/rt.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int main(void)
{
    char template_path[128];
    int written =
        snprintf(template_path, sizeof(template_path), "tmp_channel_io_%ld.txt", (long)getpid());
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

    int32_t open_random = rt_open_err_vstr(path, RT_F_RANDOM, 6);
    assert(open_random == Err_None);

    ViperString *random_line = NULL;
    int32_t random_read_rc = rt_line_input_ch_err(6, &random_line);
    assert(random_read_rc == Err_None);
    assert(random_line != NULL);
    rt_string_unref(random_line);

    ViperString *eof_line = NULL;
    int32_t eof_rc = rt_line_input_ch_err(6, &eof_line);
    assert(eof_rc == Err_EOF);
    assert(eof_line == NULL);

    bool at_eof = false;
    int32_t eof_query_rc = rt_file_channel_get_eof(6, &at_eof);
    assert(eof_query_rc == Err_None);
    assert(at_eof);

    const char *suffix_cstr = "again";
    size_t suffix_len = strlen(suffix_cstr);
    ViperString *suffix = rt_const_cstr(suffix_cstr);
    assert(suffix != NULL);

    int32_t write_after_eof_rc = rt_println_ch_err(6, suffix);
    assert(write_after_eof_rc == Err_None);

    bool at_eof_after_write = true;
    int32_t eof_after_write_rc = rt_file_channel_get_eof(6, &at_eof_after_write);
    assert(eof_after_write_rc == Err_None);
    assert(!at_eof_after_write);

    int fd = -1;
    int32_t channel_fd_rc = rt_file_channel_fd(6, &fd);
    assert(channel_fd_rc == Err_None);
    assert(fd >= 0);

    off_t seek_rc = lseek(fd, -(off_t)(suffix_len + 1), SEEK_END);
    assert(seek_rc >= 0);

    ViperString *again_line = NULL;
    int32_t read_again_rc = rt_line_input_ch_err(6, &again_line);
    assert(read_again_rc == Err_None);
    assert(again_line != NULL);
    const char *again_view = rt_string_cstr(again_line);
    assert(again_view != NULL);
    assert(strcmp(again_view, suffix_cstr) == 0);
    rt_string_unref(again_line);

    ViperString *final_line = NULL;
    int32_t final_eof_rc = rt_line_input_ch_err(6, &final_line);
    assert(final_eof_rc == Err_EOF);
    assert(final_line == NULL);

    rt_string_unref(suffix);

    int32_t close_random = rt_close_err(6);
    assert(close_random == Err_None);

    int32_t open_fail_close = rt_open_err_vstr(path, RT_F_OUTPUT, 7);
    assert(open_fail_close == Err_None);

    int failure_fd = -1;
    int32_t failure_fd_rc = rt_file_channel_fd(7, &failure_fd);
    assert(failure_fd_rc == Err_None);
    assert(failure_fd >= 0);

    int manual_close_rc = close(failure_fd);
    assert(manual_close_rc == 0);

    int32_t close_fail_rc = rt_close_err(7);
    assert(close_fail_rc == Err_IOError);

    int still_fd = -1;
    int32_t still_in_use_rc = rt_file_channel_fd(7, &still_fd);
    assert(still_in_use_rc == Err_None);
    assert(still_fd == failure_fd);

    int replacement_fd = open(template_path, O_WRONLY);
    assert(replacement_fd >= 0);
    if (replacement_fd != failure_fd)
    {
        int dup_rc = dup2(replacement_fd, failure_fd);
        assert(dup_rc == failure_fd);
        int replacement_close_rc = close(replacement_fd);
        assert(replacement_close_rc == 0);
    }

    int32_t close_recovery_rc = rt_close_err(7);
    assert(close_recovery_rc == Err_None);

    remove(template_path);
    rt_string_unref(world);
    rt_string_unref(hello);
    rt_string_unref(path);
    return 0;
}
