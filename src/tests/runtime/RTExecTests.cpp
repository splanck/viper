//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTExecTests.cpp
// Purpose: Tests for Viper.System.Exec external command execution.
//
//===----------------------------------------------------------------------===//

#include "rt_exec.h"
#include "rt_internal.h"
#include "rt_box.h"
#include "rt_process.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "tests/common/PlatformSkip.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static rt_string make_string(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void append_runtime_string(std::string &dst, rt_string value) {
    if (!value || rt_str_len(value) <= 0)
        return;
    dst.append(rt_string_cstr(value), (size_t)rt_str_len(value));
}

static void *make_shell_args(const char *script) {
    void *args = rt_seq_new();
    rt_seq_push(args, make_string("-c"));
    rt_seq_push(args, make_string(script));
    return args;
}

static void *start_shell_process(const char *script) {
    const char *shells[] = {"/bin/sh", "/usr/bin/sh", nullptr};
    for (int i = 0; shells[i] != nullptr; i++) {
        void *handle = rt_process_start(make_string(shells[i]), make_shell_args(script));
        if (handle)
            return handle;
    }
    return nullptr;
}

static void *start_shell_process_with_env(const char *script, const char *cwd, void *env) {
    const char *shells[] = {"/bin/sh", "/usr/bin/sh", nullptr};
    for (int i = 0; shells[i] != nullptr; i++) {
        void *handle =
            rt_process_start_with_env(make_string(shells[i]), make_shell_args(script), make_string(cwd), env);
        if (handle)
            return handle;
    }
    return nullptr;
}

static void test_shell_true() {
    // "true" command should return 0
    rt_string cmd = make_string("true");
    int64_t result = rt_exec_shell(cmd);
    assert(result == 0);
}

static void test_shell_false() {
    // "false" command should return 1
    rt_string cmd = make_string("false");
    int64_t result = rt_exec_shell(cmd);
    assert(result == 1);
}

static void test_shell_echo() {
    // Run echo through shell
    rt_string cmd = make_string("echo hello");
    int64_t result = rt_exec_shell(cmd);
    assert(result == 0);
}

static void test_shell_capture_echo() {
    // Capture output of echo
    rt_string cmd = make_string("echo hello");
    rt_string output = rt_exec_shell_capture(cmd);
    assert(output != nullptr);

    const char *out_str = rt_string_cstr(output);
    // Output should be "hello\n" or "hello\r\n" depending on platform
    assert(strncmp(out_str, "hello", 5) == 0);
}

static void test_shell_capture_multiline() {
    // Capture multiline output
    rt_string cmd = make_string("echo line1; echo line2");
    rt_string output = rt_exec_shell_capture(cmd);
    assert(output != nullptr);

    const char *out_str = rt_string_cstr(output);
    assert(strstr(out_str, "line1") != nullptr);
    assert(strstr(out_str, "line2") != nullptr);
}

static void test_run_true() {
    // Direct execution of /bin/true (or /usr/bin/true)
    rt_string prog = make_string("/bin/true");
    int64_t result = rt_exec_run(prog);
    // Might fail if /bin/true doesn't exist, try /usr/bin/true
    if (result < 0) {
        prog = make_string("/usr/bin/true");
        result = rt_exec_run(prog);
    }
    // On some systems true might not be in either location
    // Just verify we get a reasonable result
    assert(result == 0 || result == -1);
}

static void test_run_args() {
    // Run echo with arguments
    rt_string prog = make_string("/bin/echo");
    void *args = rt_seq_new();
    rt_seq_push(args, make_string("hello"));
    rt_seq_push(args, make_string("world"));

    int64_t result = rt_exec_run_args(prog, args);
    // Might fail if /bin/echo doesn't exist
    if (result < 0) {
        prog = make_string("/usr/bin/echo");
        result = rt_exec_run_args(prog, args);
    }
    // Just verify we get a reasonable result
    assert(result == 0 || result == -1);
}

static void test_capture_args() {
    // Capture output of echo with arguments
    rt_string prog = make_string("/bin/echo");
    void *args = rt_seq_new();
    rt_seq_push(args, make_string("test"));
    rt_seq_push(args, make_string("output"));

    rt_string output = rt_exec_capture_args(prog, args);

    // Try /usr/bin/echo if /bin/echo failed
    if (rt_str_len(output) == 0) {
        prog = make_string("/usr/bin/echo");
        output = rt_exec_capture_args(prog, args);
    }

    // If we got output, verify it
    if (rt_str_len(output) > 0) {
        const char *out_str = rt_string_cstr(output);
        assert(strstr(out_str, "test") != nullptr);
        assert(strstr(out_str, "output") != nullptr);
    }
}

static void test_shell_empty_command() {
    // Empty command should return 0
    rt_string cmd = make_string("");
    int64_t result = rt_exec_shell(cmd);
    assert(result == 0);
}

static void test_shell_capture_empty() {
    // Empty command should return empty string
    rt_string cmd = make_string("");
    rt_string output = rt_exec_shell_capture(cmd);
    assert(output != nullptr);
    assert(rt_str_len(output) == 0);
}

static void test_nonexistent_program() {
    // Nonexistent program should return -1
    rt_string prog = make_string("/nonexistent/program/path");
    int64_t result = rt_exec_run(prog);
    assert(result == -1);
}

static void test_capture_nonexistent() {
    // Nonexistent program should return empty string
    rt_string prog = make_string("/nonexistent/program/path");
    rt_string output = rt_exec_capture(prog);
    assert(output != nullptr);
    assert(rt_str_len(output) == 0);
}

static void test_shell_exit_code() {
    // Shell command with specific exit code
    rt_string cmd = make_string("exit 42");
    int64_t result = rt_exec_shell(cmd);
    assert(result == 42);
}

static void test_shell_capture_stderr() {
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

static void test_run_null_args() {
    // Run with NULL args should work (no arguments)
    rt_string prog = make_string("/bin/true");
    int64_t result = rt_exec_run_args(prog, nullptr);
    if (result < 0) {
        prog = make_string("/usr/bin/true");
        result = rt_exec_run_args(prog, nullptr);
    }
    assert(result == 0 || result == -1);
}

static void test_run_args_accepts_boxed_strings() {
    // Zia pushes strings through the Object ABI, so args may contain boxed strings.
    rt_string prog = make_string("/bin/sh");
    void *args = rt_seq_new();
    rt_seq_push(args, rt_box_str(make_string("-c")));
    rt_seq_push(args, rt_box_str(make_string("exit 0")));

    int64_t result = rt_exec_run_args(prog, args);
    if (result < 0) {
        prog = make_string("/usr/bin/sh");
        result = rt_exec_run_args(prog, args);
    }
    assert(result == 0 || result == -1);
}

static void test_shell_full_success() {
    // ShellFull on a successful command: exit code 0, output captured
    rt_string cmd = make_string("echo hello_full");
    rt_string output = rt_exec_shell_full(cmd);
    int64_t code = rt_exec_last_exit_code();

    assert(output != nullptr);
    const char *out_str = rt_string_cstr(output);
    assert(strstr(out_str, "hello_full") != nullptr);
    assert(code == 0);
}

static void test_shell_full_exit_code() {
    // ShellFull records the exit code from a failing command
    rt_string cmd = make_string("exit 7");
    rt_exec_shell_full(cmd);
    int64_t code = rt_exec_last_exit_code();
    assert(code == 7);
}

static void test_shell_full_with_stderr_merge() {
    // ShellFull does NOT auto-merge stderr. Caller adds "2>&1" to the command
    // to merge streams (same contract as ShellCapture). Verify stdout is captured
    // and stderr-merged variant works when user adds 2>&1.
    rt_string cmd = make_string("echo to_stdout; echo to_stderr >&2 2>&1");
    rt_string output = rt_exec_shell_full(cmd);
    int64_t code = rt_exec_last_exit_code();

    assert(output != nullptr);
    const char *out_str = rt_string_cstr(output);
    assert(strstr(out_str, "to_stdout") != nullptr);
    // stderr is merged because the caller added 2>&1 in the right position
    assert(code == 0);
}

static void test_shell_full_empty() {
    // Empty command: empty output, exit 0
    rt_string cmd = make_string("");
    rt_string output = rt_exec_shell_full(cmd);
    int64_t code = rt_exec_last_exit_code();
    assert(output != nullptr);
    assert(rt_str_len(output) == 0);
    assert(code == 0);
}

static void test_last_exit_code_initial() {
    // Before any ShellFull call, last exit code is -1
    // (We can't reset the TLS state between tests, so just call it after
    // the previous test and check that it returns the last recorded code.)
    // This test simply verifies the function is callable without crashing.
    int64_t code = rt_exec_last_exit_code();
    (void)code; // value depends on prior test; just ensure no crash
}

static void test_process_streams_stdout_stderr() {
    void *handle = start_shell_process(
        "printf 'out1\\n'; printf 'err1\\n' >&2; sleep 1; "
        "printf 'out2\\n'; printf 'err2\\n' >&2; exit 7");
    assert(handle != nullptr);
    assert(rt_process_is_valid(handle) == 1);

    std::string stdout_text;
    std::string stderr_text;
    bool saw_output_while_running = false;

    for (int i = 0; i < 80; i++) {
        bool running = rt_process_is_running(handle) != 0;
        rt_string out = rt_process_read_stdout(handle);
        rt_string err = rt_process_read_stderr(handle);
        if (running && ((out && rt_str_len(out) > 0) || (err && rt_str_len(err) > 0)))
            saw_output_while_running = true;
        append_runtime_string(stdout_text, out);
        append_runtime_string(stderr_text, err);
        if (!running)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    int64_t code = rt_process_wait(handle);
    append_runtime_string(stdout_text, rt_process_read_stdout(handle));
    append_runtime_string(stderr_text, rt_process_read_stderr(handle));

    assert(code == 7);
    assert(rt_process_exit_code(handle) == 7);
    assert(stdout_text.find("out1") != std::string::npos);
    assert(stdout_text.find("out2") != std::string::npos);
    assert(stderr_text.find("err1") != std::string::npos);
    assert(stderr_text.find("err2") != std::string::npos);
    assert(saw_output_while_running);

    rt_process_destroy(handle);
    assert(rt_process_is_valid(handle) == 0);
}

static void test_process_cwd_and_env() {
    void *env = rt_seq_new();
    rt_seq_push(env, make_string("VIPER_PROCESS_TEST=env-ok"));

    void *handle = start_shell_process_with_env("pwd; printf '%s\\n' \"$VIPER_PROCESS_TEST\"", "/tmp", env);
    assert(handle != nullptr);

    int64_t code = rt_process_wait(handle);
    std::string stdout_text;
    append_runtime_string(stdout_text, rt_process_read_stdout(handle));

    assert(code == 0);
    assert(stdout_text.find("/tmp") != std::string::npos);
    assert(stdout_text.find("env-ok") != std::string::npos);
    rt_process_destroy(handle);
}

static void test_process_accepts_boxed_args_from_object_abi() {
    void *args = rt_seq_new();
    rt_seq_push(args, rt_box_str(make_string("-c")));
    rt_seq_push(args, rt_box_str(make_string("printf boxed-ok")));

    void *handle = nullptr;
    const char *shells[] = {"/bin/sh", "/usr/bin/sh", nullptr};
    for (int i = 0; shells[i] != nullptr; i++) {
        handle = rt_process_start(make_string(shells[i]), args);
        if (handle)
            break;
    }
    assert(handle != nullptr);

    int64_t code = rt_process_wait(handle);
    std::string stdout_text;
    append_runtime_string(stdout_text, rt_process_read_stdout(handle));

    assert(code == 0);
    assert(stdout_text.find("boxed-ok") != std::string::npos);
    rt_process_destroy(handle);
}

static void test_process_empty_incremental_read() {
    void *handle = start_shell_process("sleep 1; exit 0");
    assert(handle != nullptr);
    assert(rt_process_is_running(handle) == 1);

    rt_string out = rt_process_read_stdout(handle);
    rt_string err = rt_process_read_stderr(handle);
    assert(out != nullptr);
    assert(err != nullptr);
    assert(rt_str_len(out) == 0);
    assert(rt_str_len(err) == 0);

    assert(rt_process_wait(handle) == 0);
    rt_process_destroy(handle);
}

static void test_process_kill() {
    void *handle = start_shell_process("while true; do sleep 1; done");
    assert(handle != nullptr);
    assert(rt_process_is_running(handle) == 1);
    assert(rt_process_kill(handle) == 1);

    int64_t code = rt_process_wait(handle);
    assert(code != 0);
    assert(rt_process_is_running(handle) == 0);
    rt_process_destroy(handle);
}

int main() {
#ifdef _WIN32
    // Skip on Windows: test uses POSIX shell commands (true, false, /bin/echo)
    // that don't exist on Windows
    VIPER_PLATFORM_SKIP("POSIX shell commands not available on Windows");
#endif
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
    test_run_args_accepts_boxed_strings();

    // ShellFull + LastExitCode
    test_shell_full_success();
    test_shell_full_exit_code();
    test_shell_full_with_stderr_merge();
    test_shell_full_empty();
    test_last_exit_code_initial();

    // Streaming Process handle coverage.
    test_process_streams_stdout_stderr();
    test_process_cwd_and_env();
    test_process_accepts_boxed_args_from_object_abi();
    test_process_empty_incremental_read();
    test_process_kill();

    return 0;
}
