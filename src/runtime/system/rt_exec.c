//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_exec.c
// Purpose: Implements external command execution for the Viper.System.Exec class.
//          Provides Run (fire-and-forget), Capture (capture stdout),
//          Shell (via system shell), and ShellFull (capture + exit code)
//          variants using posix_spawn (Unix) or CreateProcess (Win32).
//
// Key invariants:
//   - Direct execution (Run, RunArgs) bypasses the shell; arguments are passed
//     as an array, preventing shell injection.
//   - Shell execution (Shell, ShellCapture, ShellFull) runs via /bin/sh -c or
//     cmd.exe /c; the caller is responsible for input sanitization.
//   - Capture functions return stdout as a string; stderr is not captured.
//   - ShellFull stores the exit code in a per-context slot for LastExitCode().
//   - A NULL or empty program path causes a trap.
//   - All functions are thread-safe; the per-context exit code is stored in
//     the calling thread's RtContext, not in global state.
//
// Ownership/Lifetime:
//   - Returned stdout capture strings are fresh rt_string allocations owned
//     by the caller.
//   - No persistent state is held across calls except the last exit code in
//     the calling thread's context.
//
// Links: src/runtime/system/rt_exec.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_exec.h"

#include "rt_internal.h"
#include "rt_platform.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "rt_file_path.h"
#include <process.h>
#include <windows.h>
#define popen _popen
#define pclose _pclose
#elif defined(__viperdos__)
// ViperDOS provides POSIX-compatible process APIs via libc.
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

/// @brief Initial buffer size for capturing output.
#define CAPTURE_INITIAL_SIZE 4096

/// @brief Maximum buffer size for capturing output (16MB).
#define CAPTURE_MAX_SIZE (16 * 1024 * 1024)

/// @brief Thread-local exit code from the most recent rt_exec_shell_full() call.
static _Thread_local int64_t tl_last_exit_code = -1;

/// @brief Extract a runtime string as a C-string-safe byte view.
/// @details Execution APIs eventually cross OS interfaces that treat NUL bytes
///          as terminators. This helper rejects embedded NUL bytes before a
///          path, command, or argument can be silently truncated by libc or
///          Win32 process creation. Empty strings are allowed here; callers
///          decide whether emptiness is valid for their specific parameter.
/// @param value Runtime string to inspect.
/// @param out_text Receives the borrowed NUL-terminated byte pointer.
/// @param out_len Receives the byte length excluding the terminator.
/// @return 1 when @p value is a valid C-string-safe runtime string, 0 otherwise.
static int exec_string_cstr_view(rt_string value, const char **out_text, size_t *out_len) {
    const char *text = value ? rt_string_cstr(value) : NULL;
    int64_t len64 = value ? rt_str_len(value) : -1;
    if (out_text)
        *out_text = NULL;
    if (out_len)
        *out_len = 0;
    if (!text || len64 < 0 || (uint64_t)len64 > SIZE_MAX)
        return 0;
    size_t len = (size_t)len64;
    if (len > 0 && memchr(text, '\0', len))
        return 0;
    if (out_text)
        *out_text = text;
    if (out_len)
        *out_len = len;
    return 1;
}

/// @brief Validate every direct-execution argument for C-string safety.
/// @details Args are intentionally passed as an argv vector rather than through
///          a shell, but the OS argv ABI still cannot represent embedded NUL
///          bytes. Rejecting them up front avoids a confused-deputy situation
///          where validation sees one string and the child receives a prefix.
/// @param args Runtime sequence of argument strings; may be NULL.
/// @param trap_msg Trap text used when an invalid argument is found.
/// @return 1 when all arguments are valid, 0 when a trap was raised.
static int exec_validate_args_no_nul(void *args, const char *trap_msg) {
    int64_t count = args ? rt_seq_len(args) : 0;
    if (count < 0)
        return 1;
    for (int64_t i = 0; i < count; i++) {
        rt_string arg = rt_seq_get_str(args, i);
        const char *text = NULL;
        size_t len = 0;
        int ok = exec_string_cstr_view(arg, &text, &len);
        (void)text;
        (void)len;
        rt_str_release_maybe(arg);
        if (!ok) {
            rt_trap(trap_msg);
            return 0;
        }
    }
    return 1;
}

/// @brief Store a shell process exit status in the thread-local Exec state.
/// @details Handles platform differences and treats pclose/_pclose failure
///          (`status == -1`) as an execution failure instead of interpreting it
///          with wait-status macros.
/// @param status Status value returned by pclose/_pclose.
static void exec_set_last_exit_from_status(int status) {
    if (status == -1) {
        tl_last_exit_code = -1;
        return;
    }
#if RT_PLATFORM_WINDOWS
    tl_last_exit_code = (int64_t)status;
#else
    tl_last_exit_code = WIFEXITED(status) ? (int64_t)WEXITSTATUS(status) : (int64_t)-1;
#endif
}

/// @brief Decode a status value returned by `system()`.
/// @details POSIX `system()` returns a wait status while Windows returns the
///          command processor's result directly. This helper presents the public
///          `Exec.Shell` API with the same normalized "command exit code or -1"
///          contract used by `Exec.ShellFull`.
/// @param status Raw status returned by `system()`.
/// @return Normalized process exit code, or -1 for launch/signalled failure.
static int64_t exec_decode_system_status(int status) {
    if (status == -1)
        return -1;
#if RT_PLATFORM_WINDOWS
    return (int64_t)status;
#else
    if (WIFEXITED(status))
        return (int64_t)WEXITSTATUS(status);
    return -1;
#endif
}

/// @brief Read all output from a pipe into a dynamically allocated buffer.
/// @details Captures up to `CAPTURE_MAX_SIZE` bytes. When the cap is reached,
///          `*out_truncated` is set so callers can report a distinct truncation
///          condition instead of silently returning partial command output.
/// @param fp Open pipe to read from.
/// @param out_len Receives the number of captured bytes.
/// @param out_truncated Receives 1 if the capture limit was reached.
/// @param out_error Receives errno-style failure information for allocation/read errors.
/// @return Heap buffer containing captured bytes, or NULL on allocation failure.
static char *read_pipe_output(FILE *fp, size_t *out_len, int *out_truncated, int *out_error) {
    size_t cap = CAPTURE_INITIAL_SIZE;
    size_t len = 0;
    if (out_truncated)
        *out_truncated = 0;
    if (out_error)
        *out_error = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        *out_len = 0;
        if (out_error)
            *out_error = ENOMEM;
        return NULL;
    }

    while (!feof(fp)) {
        size_t space = cap - len;
        if (space < 256) {
            if (cap >= CAPTURE_MAX_SIZE) {
                if (out_truncated)
                    *out_truncated = 1;
                break;
            }
            size_t new_cap = cap * 2;
            if (new_cap > CAPTURE_MAX_SIZE)
                new_cap = CAPTURE_MAX_SIZE;
            char *new_buf = (char *)realloc(buf, new_cap);
            if (!new_buf) {
                free(buf);
                *out_len = 0;
                if (out_error)
                    *out_error = ENOMEM;
                return NULL;
            }
            buf = new_buf;
            cap = new_cap;
            space = cap - len;
        }

        size_t n = fread(buf + len, 1, space, fp);
        if (n == 0)
            break;
        len += n;
    }
    if (ferror(fp)) {
        free(buf);
        *out_len = 0;
        if (out_error)
            *out_error = EIO;
        return NULL;
    }

    *out_len = len;
    return buf;
}

#if !defined(_WIN32)

/// @brief Create a close-on-exec pipe for POSIX capture.
/// @details Sets `FD_CLOEXEC` on both descriptors. The capture child explicitly
///          duplicates the write end to stdout via posix_spawn file actions, so
///          only descriptors that must survive exec are made inheritable.
/// @param pipefd Receives read/write descriptors on success.
/// @return 0 on success, -1 on failure with errno set by the platform call.
static int exec_pipe_cloexec(int pipefd[2]) {
    if (pipe(pipefd) != 0)
        return -1;
#if defined(FD_CLOEXEC)
    for (int i = 0; i < 2; i++) {
        int flags = fcntl(pipefd[i], F_GETFD, 0);
        if (flags < 0 || fcntl(pipefd[i], F_SETFD, flags | FD_CLOEXEC) < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            pipefd[0] = -1;
            pipefd[1] = -1;
            return -1;
        }
    }
#endif
    return 0;
}

/// @brief Wait for one child process, retrying interruptions.
/// @param pid Child process id returned by posix_spawn/posix_spawnp.
/// @param status_out Receives the wait status on success.
/// @return 0 on success, -1 on wait failure.
static int exec_wait_child(pid_t pid, int *status_out) {
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != pid)
        return -1;
    if (status_out)
        *status_out = status;
    return 0;
}

/// @brief Build argv array from program and Seq of arguments.
/// Caller must free the returned array and release owned argument strings.
static char **build_argv(const char *program,
                         void *args,
                         rt_string **out_owned_args,
                         int64_t *out_argc) {
    int64_t nargs = args ? rt_seq_len(args) : 0;
    *out_owned_args = NULL;
    if (out_argc)
        *out_argc = 0;
    if (nargs < 0 || (uint64_t)nargs > ((uint64_t)SIZE_MAX / sizeof(char *)) - 2u)
        return NULL;
    int64_t total = 1 + nargs + 1; // program + args + NULL terminator

    char **argv = (char **)malloc((size_t)total * sizeof(char *));
    if (!argv) {
        *out_argc = 0;
        return NULL;
    }

    if (nargs > 0) {
        *out_owned_args = (rt_string *)calloc((size_t)nargs, sizeof(rt_string));
        if (!*out_owned_args) {
            free(argv);
            *out_argc = 0;
            return NULL;
        }
    }

    argv[0] = (char *)(uintptr_t)program;
    for (int64_t i = 0; i < nargs; i++) {
        rt_string arg_str = rt_seq_get_str(args, i);
        (*out_owned_args)[i] = arg_str;
        argv[1 + i] = (char *)(uintptr_t)rt_string_cstr(arg_str);
    }
    argv[total - 1] = NULL;

    *out_argc = total - 1;
    return argv;
}

static void free_argv(char **argv, rt_string *owned_args, int64_t argc) {
    int64_t nargs = argc > 0 ? argc - 1 : 0;
    if (owned_args) {
        for (int64_t i = 0; i < nargs; i++)
            rt_str_release_maybe(owned_args[i]);
        free(owned_args);
    }
    free(argv);
}

/// @brief Execute program with arguments using posix_spawn.
static int64_t exec_spawn(const char *program, void *args) {
    int64_t argc;
    rt_string *owned_args = NULL;
    char **argv = build_argv(program, args, &owned_args, &argc);
    if (!argv) {
        rt_trap("Exec: memory allocation failed");
        return -1;
    }

    pid_t pid;
    int status = posix_spawnp(&pid, program, NULL, NULL, argv, environ);

    if (status != 0) {
        free_argv(argv, owned_args, argc);
        // Return -1 if spawn failed (program not found, etc.)
        return -1;
    }

    // Wait for child to finish
    int exit_status;
    if (exec_wait_child(pid, &exit_status) == -1) {
        free_argv(argv, owned_args, argc);
        return -1;
    }

    free_argv(argv, owned_args, argc);

    if (WIFEXITED(exit_status)) {
        return WEXITSTATUS(exit_status);
    } else if (WIFSIGNALED(exit_status)) {
        return -WTERMSIG(exit_status);
    }
    return -1;
}

/// @brief Execute program with arguments and capture stdout using fork/exec.
static rt_string exec_capture_spawn(const char *program, void *args) {
    int64_t argc;
    rt_string *owned_args = NULL;
    char **argv = build_argv(program, args, &owned_args, &argc);
    if (!argv) {
        rt_trap("Exec: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    // Create pipe for stdout
    int pipefd[2];
    if (exec_pipe_cloexec(pipefd) == -1) {
        free_argv(argv, owned_args, argc);
        rt_trap("Exec: pipe creation failed");
        return rt_string_from_bytes("", 0);
    }

    posix_spawn_file_actions_t actions;
    int action_status = posix_spawn_file_actions_init(&actions);
    if (action_status == 0)
        action_status = posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    if (action_status == 0)
        action_status = posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    if (action_status == 0)
        action_status = posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    if (action_status != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[0]);
        close(pipefd[1]);
        free_argv(argv, owned_args, argc);
        rt_trap("Exec: spawn file action setup failed");
        return rt_string_from_bytes("", 0);
    }

    pid_t pid;
    int status = posix_spawnp(&pid, program, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);

    if (status != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        free_argv(argv, owned_args, argc);
        return rt_string_from_bytes("", 0);
    }

    // Close write end in parent
    close(pipefd[1]);

    // Read all output
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        exec_wait_child(pid, NULL);
        free_argv(argv, owned_args, argc);
        return rt_string_from_bytes("", 0);
    }

    size_t len;
    int truncated = 0;
    int capture_error = 0;
    char *output = read_pipe_output(fp, &len, &truncated, &capture_error);
    fclose(fp);

    // Wait for child
    exec_wait_child(pid, NULL);
    free_argv(argv, owned_args, argc);

    if (!output) {
        if (capture_error != 0)
            rt_trap("Exec.CaptureArgs: output capture failed");
        return rt_string_from_bytes("", 0);
    }
    if (truncated) {
        free(output);
        rt_trap("Exec.CaptureArgs: output truncated");
        return rt_string_from_bytes("", 0);
    }

    rt_string result = rt_string_from_bytes(output, len);
    free(output);
    return result;
}

#else // _WIN32

/* Windows argument quoting per CommandLineToArgvW rules (S-22 fix).
   - N backslashes followed by '"': emit 2N backslashes + '\"'
   - N backslashes at end of arg (before closing '"'): emit 2N backslashes
   - N backslashes followed by non-'"': emit N backslashes unchanged      */
static size_t cmdline_quoted_len(const char *s) {
    size_t n = 2; /* outer quotes */
    size_t bs = 0;
    for (; *s; s++) {
        if (*s == '\\') {
            if (bs == SIZE_MAX)
                return SIZE_MAX;
            bs++;
        } else if (*s == '"') {
            if (bs > (SIZE_MAX - n - 2) / 2)
                return SIZE_MAX;
            n += (size_t)bs * 2 + 2; /* 2×bs + '\' + '"' */
            bs = 0;
        } else {
            if (bs > SIZE_MAX - n - 1)
                return SIZE_MAX;
            n += (size_t)bs + 1;
            bs = 0;
        }
    }
    if (bs > (SIZE_MAX - n) / 2)
        return SIZE_MAX;
    n += (size_t)bs * 2; /* trailing backslashes before closing '"' */
    return n;
}

static char *cmdline_append_quoted(char *p, const char *s) {
    *p++ = '"';
    size_t bs = 0;
    for (; *s; s++) {
        if (*s == '\\') {
            bs++;
        } else if (*s == '"') {
            size_t i;
            for (i = 0; i < bs * 2; i++)
                *p++ = '\\';
            *p++ = '\\';
            *p++ = '"';
            bs = 0;
        } else {
            size_t i;
            for (i = 0; i < bs; i++)
                *p++ = '\\';
            *p++ = *s;
            bs = 0;
        }
    }
    size_t i;
    for (i = 0; i < bs * 2; i++)
        *p++ = '\\';
    *p++ = '"';
    return p;
}

/// @brief Build command line string for Windows CreateProcess.
static char *build_cmdline(const char *program, void *args) {
    int64_t nargs = args ? rt_seq_len(args) : 0;

    /* Calculate worst-case length using proper quoting rules */
    size_t len = cmdline_quoted_len(program);
    if (len == SIZE_MAX)
        return NULL;
    for (int64_t i = 0; i < nargs; i++) {
        rt_string arg_str = rt_seq_get_str(args, i);
        size_t quoted_len = cmdline_quoted_len(rt_string_cstr(arg_str));
        rt_str_release_maybe(arg_str);
        if (quoted_len == SIZE_MAX || quoted_len > SIZE_MAX - len - 1)
            return NULL;
        len += 1 + quoted_len; /* space + quoted */
    }
    if (len == SIZE_MAX)
        return NULL;

    char *cmdline = (char *)malloc(len + 1);
    if (!cmdline)
        return NULL;

    char *p = cmdline;
    p = cmdline_append_quoted(p, program);

    for (int64_t i = 0; i < nargs; i++) {
        rt_string arg_str = rt_seq_get_str(args, i);
        *p++ = ' ';
        p = cmdline_append_quoted(p, rt_string_cstr(arg_str));
        rt_str_release_maybe(arg_str);
    }

    *p = '\0';
    return cmdline;
}

/// @brief Manage a Windows STARTUPINFOEX handle allow-list.
/// @details When standard handles must be inherited, CreateProcess still needs
///          `bInheritHandles=TRUE`. The extended attribute list constrains
///          inheritance to the handles explicitly passed here so unrelated
///          inheritable parent handles are not leaked into the child process.
typedef struct exec_win_startup_info {
    STARTUPINFOEXW startup;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs;
} exec_win_startup_info;

/// @brief Initialize STARTUPINFOEXW with a process handle inheritance allow-list.
/// @param info Startup container to initialize.
/// @param handles Handles that the child process is allowed to inherit.
/// @param handle_count Number of entries in @p handles.
/// @return 1 when the allow-list was installed, 0 on allocation/API failure.
static int exec_win_startup_init(exec_win_startup_info *info, HANDLE *handles, DWORD handle_count) {
    SIZE_T attr_size = 0;
    if (!info)
        return 0;
    ZeroMemory(info, sizeof(*info));
    info->startup.StartupInfo.cb = sizeof(info->startup);
    if (!handles || handle_count == 0)
        return 1;
    (void)InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    info->attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
    if (!info->attrs)
        return 0;
    if (!InitializeProcThreadAttributeList(info->attrs, 1, 0, &attr_size)) {
        free(info->attrs);
        info->attrs = NULL;
        return 0;
    }
    info->startup.lpAttributeList = info->attrs;
    if (!UpdateProcThreadAttribute(info->attrs,
                                   0,
                                   PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   handles,
                                   sizeof(HANDLE) * handle_count,
                                   NULL,
                                   NULL)) {
        DeleteProcThreadAttributeList(info->attrs);
        free(info->attrs);
        info->attrs = NULL;
        info->startup.lpAttributeList = NULL;
        return 0;
    }
    return 1;
}

/// @brief Release resources allocated for a Windows extended startup block.
/// @param info Startup container previously initialized by exec_win_startup_init.
static void exec_win_startup_destroy(exec_win_startup_info *info) {
    if (!info || !info->attrs)
        return;
    DeleteProcThreadAttributeList(info->attrs);
    free(info->attrs);
    info->attrs = NULL;
    info->startup.lpAttributeList = NULL;
}

/// @brief Convert a UTF-8 command line and application path to Win32 wide strings.
/// @details CreateProcessW mutates the command-line buffer, so both returned
///          strings are heap-allocated and writable. The caller frees them with
///          free().
/// @param program UTF-8 program path or lookup name.
/// @param cmdline UTF-8 quoted command line.
/// @param out_program Receives allocated wide program string.
/// @param out_cmdline Receives allocated wide command line string.
/// @return 1 on successful conversion, 0 on UTF-8 conversion failure.
static int exec_build_wide_process_strings(const char *program,
                                           const char *cmdline,
                                           wchar_t **out_program,
                                           wchar_t **out_cmdline) {
    if (out_program)
        *out_program = NULL;
    if (out_cmdline)
        *out_cmdline = NULL;
    if (!program || !cmdline || !out_program || !out_cmdline)
        return 0;
    *out_program = rt_file_path_utf8_to_wide(program);
    *out_cmdline = rt_file_path_utf8_to_wide(cmdline);
    if (!*out_program || !*out_cmdline) {
        free(*out_program);
        free(*out_cmdline);
        *out_program = NULL;
        *out_cmdline = NULL;
        return 0;
    }
    return 1;
}

/// @brief Execute program using CreateProcess on Windows.
static int64_t exec_spawn(const char *program, void *args) {
    char *cmdline = build_cmdline(program, args);
    if (!cmdline) {
        rt_trap("Exec: memory allocation failed");
        return -1;
    }

    wchar_t *wprogram = NULL;
    wchar_t *wcmdline = NULL;
    if (!exec_build_wide_process_strings(program, cmdline, &wprogram, &wcmdline)) {
        free(cmdline);
        return -1;
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    BOOL success = CreateProcessW(wprogram, wcmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);

    free(wprogram);
    free(wcmdline);
    free(cmdline);

    if (!success) {
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (int64_t)exit_code;
}

/// @brief Execute program and capture stdout on Windows.
static rt_string exec_capture_spawn(const char *program, void *args) {
    char *cmdline = build_cmdline(program, args);
    if (!cmdline) {
        rt_trap("Exec: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }
    wchar_t *wprogram = NULL;
    wchar_t *wcmdline = NULL;
    if (!exec_build_wide_process_strings(program, cmdline, &wprogram, &wcmdline)) {
        free(cmdline);
        return rt_string_from_bytes("", 0);
    }

    // Create pipe for stdout
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        free(wprogram);
        free(wcmdline);
        free(cmdline);
        return rt_string_from_bytes("", 0);
    }

    // Ensure read handle is not inherited
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE inherited[3];
    DWORD inherited_count = 0;
    inherited[inherited_count++] = hWritePipe;
    HANDLE std_in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE std_err = GetStdHandle(STD_ERROR_HANDLE);
    if (std_in && std_in != INVALID_HANDLE_VALUE)
        inherited[inherited_count++] = std_in;
    if (std_err && std_err != INVALID_HANDLE_VALUE)
        inherited[inherited_count++] = std_err;

    exec_win_startup_info si;
    PROCESS_INFORMATION pi;
    if (!exec_win_startup_init(&si, inherited, inherited_count)) {
        free(wprogram);
        free(wcmdline);
        free(cmdline);
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return rt_string_from_bytes("", 0);
    }
    si.startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.startup.StartupInfo.hStdOutput = hWritePipe;
    si.startup.StartupInfo.hStdError =
        (std_err && std_err != INVALID_HANDLE_VALUE) ? std_err : NULL;
    si.startup.StartupInfo.hStdInput = (std_in && std_in != INVALID_HANDLE_VALUE) ? std_in : NULL;
    ZeroMemory(&pi, sizeof(pi));

    BOOL success = CreateProcessW(wprogram,
                                  wcmdline,
                                  NULL,
                                  NULL,
                                  TRUE,
                                  EXTENDED_STARTUPINFO_PRESENT,
                                  NULL,
                                  NULL,
                                  &si.startup.StartupInfo,
                                  &pi);

    exec_win_startup_destroy(&si);
    free(wprogram);
    free(wcmdline);
    free(cmdline);
    CloseHandle(hWritePipe);

    if (!success) {
        CloseHandle(hReadPipe);
        return rt_string_from_bytes("", 0);
    }

    // Read output
    size_t cap = CAPTURE_INITIAL_SIZE;
    size_t len = 0;
    int truncated = 0;
    char *buf = (char *)malloc(cap);

    if (buf) {
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buf + len, (DWORD)(cap - len), &bytesRead, NULL) &&
               bytesRead > 0) {
            len += bytesRead;
            if (cap - len < 256 && cap < CAPTURE_MAX_SIZE) {
                size_t new_cap = cap * 2;
                if (new_cap > CAPTURE_MAX_SIZE)
                    new_cap = CAPTURE_MAX_SIZE;
                char *new_buf = (char *)realloc(buf, new_cap);
                if (new_buf) {
                    buf = new_buf;
                    cap = new_cap;
                }
            } else if (cap - len < 256 && cap >= CAPTURE_MAX_SIZE) {
                truncated = 1;
                break;
            }
        }
    }

    CloseHandle(hReadPipe);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (!buf) {
        return rt_string_from_bytes("", 0);
    }
    if (truncated) {
        free(buf);
        rt_trap("Exec.CaptureArgs: output truncated");
        return rt_string_from_bytes("", 0);
    }

    rt_string result = rt_string_from_bytes(buf, len);
    free(buf);
    return result;
}

#endif // _WIN32

//=============================================================================
// Public API Implementation
//=============================================================================

/// @brief Execute a program and wait for it to complete.
///
/// Runs the specified program as a child process and waits for it to finish.
/// The program path should be an absolute path or a program in the system PATH.
///
/// **Usage example:**
/// ```
/// ' Run a program and check exit code
/// Dim code = Exec.Run("/usr/bin/date")
/// If code = 0 Then
///     Print "Program succeeded"
/// Else
///     Print "Program failed with code: " & code
/// End If
///
/// ' Run with full path
/// code = Exec.Run("C:\Windows\notepad.exe")
/// ```
///
/// **Exit codes:**
/// - 0: Success (by convention)
/// - Positive: Application-specific error codes
/// - Negative: Signal number (Unix) or system error
/// - -1: Failed to start program
///
/// @param program Path to the program to execute.
///
/// @return Exit code from the program, or -1 if the program could not be started.
///
/// @note Use RunArgs for passing command-line arguments.
/// @note The program runs with inherited environment variables.
/// @note Traps if program is NULL or empty.
///
/// @see rt_exec_run_args For running with arguments
/// @see rt_exec_capture For capturing output
/// @see rt_exec_shell For running shell commands
int64_t rt_exec_run(rt_string program) {
    if (!program) {
        rt_trap("Exec.Run: null program");
        return -1;
    }

    const char *prog_str = NULL;
    size_t prog_len = 0;
    if (!exec_string_cstr_view(program, &prog_str, &prog_len)) {
        rt_trap("Exec.Run: program contains embedded NUL");
        return -1;
    }
    if (prog_len == 0) {
        rt_trap("Exec.Run: empty program");
        return -1;
    }

    return exec_spawn(prog_str, NULL);
}

/// @brief Execute a program and capture its standard output.
///
/// Runs the specified program and captures everything it writes to stdout.
/// The program's stderr is not captured. Use this when you need the output
/// of a command for further processing.
///
/// **Usage example:**
/// ```
/// ' Get current date
/// Dim dateStr = Exec.Capture("/bin/date")
/// Print "Today is: " & dateStr.Trim()
///
/// ' Get system information
/// Dim hostname = Exec.Capture("/bin/hostname")
/// Print "Running on: " & hostname.Trim()
///
/// ' Parse command output
/// Dim users = Exec.Capture("/usr/bin/who")
/// Dim lines = users.Split(Chr(10))
/// Print "Logged in users: " & lines.Len()
/// ```
///
/// **Output handling:**
/// - Maximum capture size: 16 MB
/// - Output includes newlines as written by the program
/// - Binary output is captured as-is (use Trim() to remove trailing newlines)
///
/// @param program Path to the program to execute.
///
/// @return String containing the program's stdout, or empty string on failure.
///
/// @note Does not capture stderr - only stdout.
/// @note Returns empty string if program cannot be started.
/// @note Traps if program is NULL.
///
/// @see rt_exec_capture_args For running with arguments
/// @see rt_exec_shell_capture For shell commands with capture
rt_string rt_exec_capture(rt_string program) {
    if (!program) {
        rt_trap("Exec.Capture: null program");
        return rt_string_from_bytes("", 0);
    }

    const char *prog_str = NULL;
    size_t prog_len = 0;
    if (!exec_string_cstr_view(program, &prog_str, &prog_len)) {
        rt_trap("Exec.Capture: program contains embedded NUL");
        return rt_string_from_bytes("", 0);
    }
    if (prog_len == 0) {
        rt_trap("Exec.Capture: empty program");
        return rt_string_from_bytes("", 0);
    }

    return exec_capture_spawn(prog_str, NULL);
}

/// @brief Execute a program with arguments and wait for completion.
///
/// Runs a program with the specified command-line arguments. This is the
/// preferred method for executing programs with user-provided arguments
/// because arguments are passed directly without shell interpretation.
///
/// **Usage example:**
/// ```
/// ' List directory contents
/// Dim args = Seq.New()
/// args.Push("-l")
/// args.Push("-a")
/// args.Push("/home/user")
/// Dim code = Exec.RunArgs("/bin/ls", args)
///
/// ' Copy a file
/// args = Seq.New()
/// args.Push("source.txt")
/// args.Push("dest.txt")
/// Exec.RunArgs("/bin/cp", args)
///
/// ' Run with user input (safe from shell injection)
/// args = Seq.New()
/// args.Push(userProvidedFilename)  ' Safe even with special chars
/// Exec.RunArgs("/bin/cat", args)
/// ```
///
/// **Arguments:**
/// - Arguments are passed as separate strings (no shell parsing)
/// - Special characters in arguments are preserved literally
/// - No shell expansion (*, ?, ~, etc.)
///
/// @param program Path to the program to execute.
/// @param args Seq of strings containing command-line arguments (may be NULL).
///
/// @return Exit code from the program, or -1 if the program could not be started.
///
/// @note Safer than Shell for user-provided arguments.
/// @note Arguments are NOT shell-expanded.
/// @note Pass NULL for args if no arguments needed.
///
/// @see rt_exec_run For running without arguments
/// @see rt_exec_capture_args For capturing output with arguments
int64_t rt_exec_run_args(rt_string program, void *args) {
    if (!program) {
        rt_trap("Exec.RunArgs: null program");
        return -1;
    }

    const char *prog_str = NULL;
    size_t prog_len = 0;
    if (!exec_string_cstr_view(program, &prog_str, &prog_len)) {
        rt_trap("Exec.RunArgs: program contains embedded NUL");
        return -1;
    }
    if (prog_len == 0) {
        rt_trap("Exec.RunArgs: empty program");
        return -1;
    }
    if (!exec_validate_args_no_nul(args, "Exec.RunArgs: argument contains embedded NUL"))
        return -1;

    return exec_spawn(prog_str, args);
}

/// @brief Execute a program with arguments and capture stdout.
///
/// Runs a program with command-line arguments and captures its standard output.
/// Combines the safety of argument arrays with output capture.
///
/// **Usage example:**
/// ```
/// ' Get details about a file
/// Dim args = Seq.New()
/// args.Push("-l")
/// args.Push(filename)
/// Dim info = Exec.CaptureArgs("/bin/ls", args)
/// Print "File info: " & info.Trim()
///
/// ' Query git for commit info
/// args = Seq.New()
/// args.Push("log")
/// args.Push("--oneline")
/// args.Push("-5")
/// Dim commits = Exec.CaptureArgs("/usr/bin/git", args)
/// ```
///
/// @param program Path to the program to execute.
/// @param args Seq of strings containing command-line arguments (may be NULL).
///
/// @return String containing the program's stdout, or empty string on failure.
///
/// @note Does not capture stderr.
/// @note Arguments are NOT shell-expanded.
/// @note Returns empty string if program cannot be started.
///
/// @see rt_exec_run_args For running without capture
/// @see rt_exec_shell_capture For shell commands with capture
rt_string rt_exec_capture_args(rt_string program, void *args) {
    if (!program) {
        rt_trap("Exec.CaptureArgs: null program");
        return rt_string_from_bytes("", 0);
    }

    const char *prog_str = NULL;
    size_t prog_len = 0;
    if (!exec_string_cstr_view(program, &prog_str, &prog_len)) {
        rt_trap("Exec.CaptureArgs: program contains embedded NUL");
        return rt_string_from_bytes("", 0);
    }
    if (prog_len == 0) {
        rt_trap("Exec.CaptureArgs: empty program");
        return rt_string_from_bytes("", 0);
    }
    if (!exec_validate_args_no_nul(args, "Exec.CaptureArgs: argument contains embedded NUL"))
        return rt_string_from_bytes("", 0);

    return exec_capture_spawn(prog_str, args);
}

/// @brief Execute a command through the system shell.
///
/// Runs a command string through the system shell, enabling shell features
/// like pipes, redirections, variable expansion, and wildcards.
///
/// **Usage example:**
/// ```
/// ' Use shell pipelines
/// Dim code = Exec.Shell("ls -la | grep .txt | wc -l")
///
/// ' Use redirection
/// Exec.Shell("echo hello > output.txt")
///
/// ' Use shell features
/// Exec.Shell("for f in *.txt; do echo $f; done")
/// ```
///
/// **Shell used:**
/// - Unix: `/bin/sh -c "command"`
/// - Windows: `cmd.exe /c "command"`
///
/// **⚠️ SECURITY WARNING:**
/// **NEVER** pass unsanitized user input to this function!
/// ```
/// ' DANGEROUS - shell injection!
/// Exec.Shell("grep " & userInput & " file.txt")
/// ' If userInput = "; rm -rf /", disaster ensues!
///
/// ' Use RunArgs instead for user input
/// Dim args = Seq.New()
/// args.Push(userInput)
/// args.Push("file.txt")
/// Exec.RunArgs("/bin/grep", args)
/// ```
///
/// @param command Shell command string to execute.
///
/// @return Exit code from the shell, or -1 on failure.
///
/// @note Empty command returns 0 (success).
/// @note Uses system shell - behavior may vary across platforms.
/// @note Traps if command is NULL.
///
/// @see rt_exec_run_args For safer execution with user input
/// @see rt_exec_shell_capture For capturing shell output
int64_t rt_exec_shell(rt_string command) {
    if (!command) {
        rt_trap("Exec.Shell: null command");
        return -1;
    }

    const char *cmd_str = NULL;
    size_t cmd_len = 0;
    if (!exec_string_cstr_view(command, &cmd_str, &cmd_len)) {
        rt_trap("Exec.Shell: command contains embedded NUL");
        return -1;
    }

    // Empty command is valid (returns immediately)
    if (cmd_len == 0) {
        return 0;
    }

    // system() uses the platform shell: cmd.exe on Windows and /bin/sh on POSIX.
    int result = system(cmd_str);
    return exec_decode_system_status(result);
}

/// @brief Execute a shell command and capture its output.
///
/// Runs a command through the system shell and captures its standard output.
/// This is useful for running complex shell pipelines and capturing the result.
///
/// **Usage example:**
/// ```
/// ' Count text files
/// Dim count = Exec.ShellCapture("ls *.txt | wc -l")
/// Print "Found " & count.Trim() & " text files"
///
/// ' Get filtered process list
/// Dim procs = Exec.ShellCapture("ps aux | grep python")
/// Print procs
///
/// ' Use shell features
/// Dim result = Exec.ShellCapture("echo $HOME")
/// Print "Home directory: " & result.Trim()
///
/// ' Complex pipeline
/// Dim topFiles = Exec.ShellCapture("du -h * | sort -rh | head -5")
/// ```
///
/// **Output handling:**
/// - Captures stdout only (not stderr)
/// - Maximum capture size: 16 MB
/// - Output includes newlines as written
/// - Use .Trim() to remove trailing newlines
///
/// **⚠️ SECURITY WARNING:**
/// Same warnings apply as for Shell(). **NEVER** pass unsanitized user
/// input to this function - use CaptureArgs() instead.
///
/// @param command Shell command string to execute.
///
/// @return String containing the command's stdout, or empty string on failure.
///
/// @note Uses popen() - both stdout and stderr may be captured on some systems.
/// @note Empty command returns empty string.
/// @note Traps if command is NULL.
///
/// @see rt_exec_capture_args For safer capture with user input
/// @see rt_exec_shell For shell without capture
rt_string rt_exec_shell_capture(rt_string command) {
    if (!command) {
        rt_trap("Exec.ShellCapture: null command");
        return rt_string_from_bytes("", 0);
    }

    const char *cmd_str = NULL;
    size_t cmd_len = 0;
    if (!exec_string_cstr_view(command, &cmd_str, &cmd_len)) {
        rt_trap("Exec.ShellCapture: command contains embedded NUL");
        return rt_string_from_bytes("", 0);
    }

    // Empty command returns empty string
    if (cmd_len == 0) {
        return rt_string_from_bytes("", 0);
    }

    FILE *fp = popen(cmd_str, "r");
    if (!fp) {
        return rt_string_from_bytes("", 0);
    }

    size_t len;
    int truncated = 0;
    int capture_error = 0;
    char *output = read_pipe_output(fp, &len, &truncated, &capture_error);
    int status = pclose(fp);
    exec_set_last_exit_from_status(status);

    if (!output) {
        if (capture_error != 0)
            rt_trap("Exec.ShellCapture: output capture failed");
        return rt_string_from_bytes("", 0);
    }
    if (truncated) {
        free(output);
        rt_trap("Exec.ShellCapture: output truncated");
        return rt_string_from_bytes("", 0);
    }

    rt_string result = rt_string_from_bytes(output, len);
    free(output);
    return result;
}

rt_string rt_exec_shell_full(rt_string command) {
    if (!command) {
        rt_trap("Exec.ShellFull: null command");
        tl_last_exit_code = -1;
        return rt_string_from_bytes("", 0);
    }

    const char *cmd_str = NULL;
    size_t cmd_len = 0;
    if (!exec_string_cstr_view(command, &cmd_str, &cmd_len)) {
        rt_trap("Exec.ShellFull: command contains embedded NUL");
        tl_last_exit_code = -1;
        return rt_string_from_bytes("", 0);
    }
    if (cmd_len == 0) {
        tl_last_exit_code = 0;
        return rt_string_from_bytes("", 0);
    }

    FILE *fp = popen(cmd_str, "r");

    if (!fp) {
        tl_last_exit_code = -1;
        return rt_string_from_bytes("", 0);
    }

    size_t len;
    int truncated = 0;
    int capture_error = 0;
    char *output = read_pipe_output(fp, &len, &truncated, &capture_error);
    int status = pclose(fp);
    exec_set_last_exit_from_status(status);

    if (!output) {
        if (capture_error != 0)
            rt_trap("Exec.ShellFull: output capture failed");
        return rt_string_from_bytes("", 0);
    }
    if (truncated) {
        free(output);
        rt_trap("Exec.ShellFull: output truncated");
        return rt_string_from_bytes("", 0);
    }

    rt_string full_result = rt_string_from_bytes(output, len);
    free(output);
    return full_result;
}

int64_t rt_exec_last_exit_code(void) {
    return tl_last_exit_code;
}
