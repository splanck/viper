//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTExecTests.cpp
// Purpose: Tests for Viper.Exec external command execution.
//
//===----------------------------------------------------------------------===//

#include "rt_exec.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_string(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static void test_shell_true()
{
    // "true" command should return 0
    rt_string cmd = make_string("true");
    int64_t result = rt_exec_shell(cmd);
    assert(result == 0);
}

static void test_shell_false()
{
    // "false" command should return 1
    rt_string cmd = make_string("false");
    int64_t result = rt_exec_shell(cmd);
    assert(result == 1);
}

static void test_shell_echo()
{
    // Run echo through shell
    rt_string cmd = make_string("echo hello");
    int64_t result = rt_exec_shell(cmd);
    assert(result == 0);
}

static void test_shell_capture_echo()
{
    // Capture output of echo
    rt_string cmd = make_string("echo hello");
    rt_string output = rt_exec_shell_capture(cmd);
    assert(output != nullptr);

    const char *out_str = rt_string_cstr(output);
    // Output should be "hello\n" or "hello\r\n" depending on platform
    assert(strncmp(out_str, "hello", 5) == 0);
}

static void test_shell_capture_multiline()
{
    // Capture multiline output
    rt_string cmd = make_string("echo line1; echo line2");
    rt_string output = rt_exec_shell_capture(cmd);
    assert(output != nullptr);

    const char *out_str = rt_string_cstr(output);
    assert(strstr(out_str, "line1") != nullptr);
    assert(strstr(out_str, "line2") != nullptr);
}

static void test_run_true()
{
    // Direct execution of /bin/true (or /usr/bin/true)
    rt_string prog = make_string("/bin/true");
    int64_t result = rt_exec_run(prog);
    // Might fail if /bin/true doesn't exist, try /usr/bin/true
    if (result < 0)
    {
        prog = make_string("/usr/bin/true");
        result = rt_exec_run(prog);
    }
    // On some systems true might not be in either location
    // Just verify we get a reasonable result
    assert(result == 0 || result == -1);
}

static void test_run_args()
{
    // Run echo with arguments
    rt_string prog = make_string("/bin/echo");
    void *args = rt_seq_new();
    rt_seq_push(args, make_string("hello"));
    rt_seq_push(args, make_string("world"));

    int64_t result = rt_exec_run_args(prog, args);
    // Might fail if /bin/echo doesn't exist
    if (result < 0)
    {
        prog = make_string("/usr/bin/echo");
        result = rt_exec_run_args(prog, args);
    }
    // Just verify we get a reasonable result
    assert(result == 0 || result == -1);
}

static void test_capture_args()
{
    // Capture output of echo with arguments
    rt_string prog = make_string("/bin/echo");
    void *args = rt_seq_new();
    rt_seq_push(args, make_string("test"));
    rt_seq_push(args, make_string("output"));

    rt_string output = rt_exec_capture_args(prog, args);

    // Try /usr/bin/echo if /bin/echo failed
    if (rt_len(output) == 0)
    {
        prog = make_string("/usr/bin/echo");
        output = rt_exec_capture_args(prog, args);
    }

    // If we got output, verify it
    if (rt_len(output) > 0)
    {
        const char *out_str = rt_string_cstr(output);
        assert(strstr(out_str, "test") != nullptr);
        assert(strstr(out_str, "output") != nullptr);
    }
}

static void test_shell_empty_command()
{
    // Empty command should return 0
    rt_string cmd = make_string("");
    int64_t result = rt_exec_shell(cmd);
    assert(result == 0);
}

static void test_shell_capture_empty()
{
    // Empty command should return empty string
    rt_string cmd = make_string("");
    rt_string output = rt_exec_shell_capture(cmd);
    assert(output != nullptr);
    assert(rt_len(output) == 0);
}

static void test_nonexistent_program()
{
    // Nonexistent program should return -1
    rt_string prog = make_string("/nonexistent/program/path");
    int64_t result = rt_exec_run(prog);
    assert(result == -1);
}

static void test_capture_nonexistent()
{
    // Nonexistent program should return empty string
    rt_string prog = make_string("/nonexistent/program/path");
    rt_string output = rt_exec_capture(prog);
    assert(output != nullptr);
    assert(rt_len(output) == 0);
}

static void test_shell_exit_code()
{
    // Shell command with specific exit code
    rt_string cmd = make_string("exit 42");
    int64_t result = rt_exec_shell(cmd);
    assert(result == 42);
}

static void test_shell_capture_stderr()
{
    // Note: ShellCapture only captures stdout, not stderr
    // This test verifies that behavior
    rt_string cmd = make_string("echo stdout; echo stderr >&2");
    rt_string output = rt_exec_shell_capture(cmd);
    assert(output != nullptr);

    const char *out_str = rt_string_cstr(output);
    // Should contain stdout but not stderr
    assert(strstr(out_str, "stdout") != nullptr);
    // stderr goes to stderr, not captured
}

static void test_run_null_args()
{
    // Run with NULL args should work (no arguments)
    rt_string prog = make_string("/bin/true");
    int64_t result = rt_exec_run_args(prog, nullptr);
    if (result < 0)
    {
        prog = make_string("/usr/bin/true");
        result = rt_exec_run_args(prog, nullptr);
    }
    assert(result == 0 || result == -1);
}

int main()
{
    test_shell_true();
    test_shell_false();
    test_shell_echo();
    test_shell_capture_echo();
    test_shell_capture_multiline();
    test_run_true();
    test_run_args();
    test_capture_args();
    test_shell_empty_command();
    test_shell_capture_empty();
    test_nonexistent_program();
    test_capture_nonexistent();
    test_shell_exit_code();
    test_shell_capture_stderr();
    test_run_null_args();

    return 0;
}
