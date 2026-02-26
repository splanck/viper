//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_exec.c
// Purpose: Implements external command execution for the Viper.Exec class.
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
#include "rt_seq.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define popen _popen
#define pclose _pclose
#elif defined(__viperdos__)
// ViperDOS provides POSIX-compatible process APIs via libc.
#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#else
#include <errno.h>
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

/// @brief Read all output from a pipe into a dynamically allocated buffer.
static char *read_pipe_output(FILE *fp, size_t *out_len)
{
    size_t cap = CAPTURE_INITIAL_SIZE;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf)
    {
        *out_len = 0;
        return NULL;
    }

    while (!feof(fp))
    {
        size_t space = cap - len;
        if (space < 256)
        {
            if (cap >= CAPTURE_MAX_SIZE)
                break;
            size_t new_cap = cap * 2;
            if (new_cap > CAPTURE_MAX_SIZE)
                new_cap = CAPTURE_MAX_SIZE;
            char *new_buf = (char *)realloc(buf, new_cap);
            if (!new_buf)
                break;
            buf = new_buf;
            cap = new_cap;
            space = cap - len;
        }

        size_t n = fread(buf + len, 1, space, fp);
        if (n == 0)
            break;
        len += n;
    }

    *out_len = len;
    return buf;
}

#if !defined(_WIN32)

/// @brief Build argv array from program and Seq of arguments.
/// Caller must free the returned array (but not individual strings).
static char **build_argv(const char *program, void *args, int64_t *out_argc)
{
    int64_t nargs = args ? rt_seq_len(args) : 0;
    int64_t total = 1 + nargs + 1; // program + args + NULL terminator

    char **argv = (char **)malloc((size_t)total * sizeof(char *));
    if (!argv)
    {
        *out_argc = 0;
        return NULL;
    }

    argv[0] = (char *)(uintptr_t)program;
    for (int64_t i = 0; i < nargs; i++)
    {
        rt_string arg_str = (rt_string)rt_seq_get(args, i);
        argv[1 + i] = (char *)(uintptr_t)rt_string_cstr(arg_str);
    }
    argv[total - 1] = NULL;

    *out_argc = total - 1;
    return argv;
}

/// @brief Execute program with arguments using posix_spawn.
static int64_t exec_spawn(const char *program, void *args)
{
    int64_t argc;
    char **argv = build_argv(program, args, &argc);
    if (!argv)
    {
        rt_trap("Exec: memory allocation failed");
        return -1;
    }

    pid_t pid;
    int status = posix_spawn(&pid, program, NULL, NULL, argv, environ);

    if (status != 0)
    {
        free(argv);
        // Return -1 if spawn failed (program not found, etc.)
        return -1;
    }

    // Wait for child to finish
    int exit_status;
    if (waitpid(pid, &exit_status, 0) == -1)
    {
        free(argv);
        return -1;
    }

    free(argv);

    if (WIFEXITED(exit_status))
    {
        return WEXITSTATUS(exit_status);
    }
    else if (WIFSIGNALED(exit_status))
    {
        return -WTERMSIG(exit_status);
    }
    return -1;
}

/// @brief Execute program with arguments and capture stdout using fork/exec.
static rt_string exec_capture_spawn(const char *program, void *args)
{
    int64_t argc;
    char **argv = build_argv(program, args, &argc);
    if (!argv)
    {
        rt_trap("Exec: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    // Create pipe for stdout
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        free(argv);
        rt_trap("Exec: pipe creation failed");
        return rt_string_from_bytes("", 0);
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);               // Close read end in child
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
    posix_spawn_file_actions_addclose(&actions, pipefd[1]); // Close original write end

    pid_t pid;
    int status = posix_spawn(&pid, program, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);

    if (status != 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        free(argv);
        return rt_string_from_bytes("", 0);
    }

    // Close write end in parent
    close(pipefd[1]);

    // Read all output
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp)
    {
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        free(argv);
        return rt_string_from_bytes("", 0);
    }

    size_t len;
    char *output = read_pipe_output(fp, &len);
    fclose(fp);

    // Wait for child
    waitpid(pid, NULL, 0);
    free(argv);

    if (!output)
    {
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
static size_t cmdline_quoted_len(const char *s)
{
    size_t n = 2; /* outer quotes */
    int bs = 0;
    for (; *s; s++)
    {
        if (*s == '\\')
        {
            bs++;
        }
        else if (*s == '"')
        {
            n += (size_t)bs * 2 + 2; /* 2×bs + '\' + '"' */
            bs = 0;
        }
        else
        {
            n += (size_t)bs + 1;
            bs = 0;
        }
    }
    n += (size_t)bs * 2; /* trailing backslashes before closing '"' */
    return n;
}

static char *cmdline_append_quoted(char *p, const char *s)
{
    *p++ = '"';
    int bs = 0;
    for (; *s; s++)
    {
        if (*s == '\\')
        {
            bs++;
        }
        else if (*s == '"')
        {
            int i;
            for (i = 0; i < bs * 2; i++)
                *p++ = '\\';
            *p++ = '\\';
            *p++ = '"';
            bs = 0;
        }
        else
        {
            int i;
            for (i = 0; i < bs; i++)
                *p++ = '\\';
            *p++ = *s;
            bs = 0;
        }
    }
    int i;
    for (i = 0; i < bs * 2; i++)
        *p++ = '\\';
    *p++ = '"';
    return p;
}

/// @brief Build command line string for Windows CreateProcess.
static char *build_cmdline(const char *program, void *args)
{
    int64_t nargs = args ? rt_seq_len(args) : 0;

    /* Calculate worst-case length using proper quoting rules */
    size_t len = cmdline_quoted_len(program);
    for (int64_t i = 0; i < nargs; i++)
    {
        rt_string arg_str = (rt_string)rt_seq_get(args, i);
        len += 1 + cmdline_quoted_len(rt_string_cstr(arg_str)); /* space + quoted */
    }

    char *cmdline = (char *)malloc(len + 1);
    if (!cmdline)
        return NULL;

    char *p = cmdline;
    p = cmdline_append_quoted(p, program);

    for (int64_t i = 0; i < nargs; i++)
    {
        rt_string arg_str = (rt_string)rt_seq_get(args, i);
        *p++ = ' ';
        p = cmdline_append_quoted(p, rt_string_cstr(arg_str));
    }

    *p = '\0';
    return cmdline;
}

/// @brief Execute program using CreateProcess on Windows.
static int64_t exec_spawn(const char *program, void *args)
{
    char *cmdline = build_cmdline(program, args);
    if (!cmdline)
    {
        rt_trap("Exec: memory allocation failed");
        return -1;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    BOOL success = CreateProcessA(program, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);

    free(cmdline);

    if (!success)
    {
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
static rt_string exec_capture_spawn(const char *program, void *args)
{
    char *cmdline = build_cmdline(program, args);
    if (!cmdline)
    {
        rt_trap("Exec: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    // Create pipe for stdout
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
    {
        free(cmdline);
        return rt_string_from_bytes("", 0);
    }

    // Ensure read handle is not inherited
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.hStdInput = NULL;
    ZeroMemory(&pi, sizeof(pi));

    BOOL success = CreateProcessA(program,
                                  cmdline,
                                  NULL,
                                  NULL,
                                  TRUE, // Inherit handles
                                  0,
                                  NULL,
                                  NULL,
                                  &si,
                                  &pi);

    free(cmdline);
    CloseHandle(hWritePipe);

    if (!success)
    {
        CloseHandle(hReadPipe);
        return rt_string_from_bytes("", 0);
    }

    // Read output
    size_t cap = CAPTURE_INITIAL_SIZE;
    size_t len = 0;
    char *buf = (char *)malloc(cap);

    if (buf)
    {
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buf + len, (DWORD)(cap - len), &bytesRead, NULL) &&
               bytesRead > 0)
        {
            len += bytesRead;
            if (cap - len < 256 && cap < CAPTURE_MAX_SIZE)
            {
                size_t new_cap = cap * 2;
                if (new_cap > CAPTURE_MAX_SIZE)
                    new_cap = CAPTURE_MAX_SIZE;
                char *new_buf = (char *)realloc(buf, new_cap);
                if (new_buf)
                {
                    buf = new_buf;
                    cap = new_cap;
                }
            }
        }
    }

    CloseHandle(hReadPipe);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (!buf)
    {
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
int64_t rt_exec_run(rt_string program)
{
    if (!program)
    {
        rt_trap("Exec.Run: null program");
        return -1;
    }

    const char *prog_str = rt_string_cstr(program);
    if (!prog_str || rt_str_len(program) == 0)
    {
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
rt_string rt_exec_capture(rt_string program)
{
    if (!program)
    {
        rt_trap("Exec.Capture: null program");
        return rt_string_from_bytes("", 0);
    }

    const char *prog_str = rt_string_cstr(program);
    if (!prog_str || rt_str_len(program) == 0)
    {
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
int64_t rt_exec_run_args(rt_string program, void *args)
{
    if (!program)
    {
        rt_trap("Exec.RunArgs: null program");
        return -1;
    }

    const char *prog_str = rt_string_cstr(program);
    if (!prog_str || rt_str_len(program) == 0)
    {
        rt_trap("Exec.RunArgs: empty program");
        return -1;
    }

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
rt_string rt_exec_capture_args(rt_string program, void *args)
{
    if (!program)
    {
        rt_trap("Exec.CaptureArgs: null program");
        return rt_string_from_bytes("", 0);
    }

    const char *prog_str = rt_string_cstr(program);
    if (!prog_str || rt_str_len(program) == 0)
    {
        rt_trap("Exec.CaptureArgs: empty program");
        return rt_string_from_bytes("", 0);
    }

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
int64_t rt_exec_shell(rt_string command)
{
    if (!command)
    {
        rt_trap("Exec.Shell: null command");
        return -1;
    }

    const char *cmd_str = rt_string_cstr(command);
    if (!cmd_str)
    {
        return -1;
    }

    // Empty command is valid (returns immediately)
    if (rt_str_len(command) == 0)
    {
        return 0;
    }

#ifdef _WIN32
    // On Windows, system() uses cmd.exe
    int result = system(cmd_str);
    return (int64_t)result;
#else
    // On POSIX/ViperDOS, system() uses the shell
    int result = system(cmd_str);
    if (result == -1)
    {
        return -1;
    }
    if (WIFEXITED(result))
    {
        return WEXITSTATUS(result);
    }
    return -1;
#endif
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
rt_string rt_exec_shell_capture(rt_string command)
{
    if (!command)
    {
        rt_trap("Exec.ShellCapture: null command");
        return rt_string_from_bytes("", 0);
    }

    const char *cmd_str = rt_string_cstr(command);
    if (!cmd_str)
    {
        return rt_string_from_bytes("", 0);
    }

    // Empty command returns empty string
    if (rt_str_len(command) == 0)
    {
        return rt_string_from_bytes("", 0);
    }

    FILE *fp = popen(cmd_str, "r");
    if (!fp)
    {
        return rt_string_from_bytes("", 0);
    }

    size_t len;
    char *output = read_pipe_output(fp, &len);
    pclose(fp);

    if (!output)
    {
        return rt_string_from_bytes("", 0);
    }

    rt_string result = rt_string_from_bytes(output, len);
    free(output);
    return result;
}

rt_string rt_exec_shell_full(rt_string command)
{
    if (!command)
    {
        rt_trap("Exec.ShellFull: null command");
        tl_last_exit_code = -1;
        return rt_string_from_bytes("", 0);
    }

    const char *cmd_str = rt_string_cstr(command);
    if (!cmd_str || rt_str_len(command) == 0)
    {
        tl_last_exit_code = 0;
        return rt_string_from_bytes("", 0);
    }

    FILE *fp = popen(cmd_str, "r");

    if (!fp)
    {
        tl_last_exit_code = -1;
        return rt_string_from_bytes("", 0);
    }

    size_t len;
    char *output = read_pipe_output(fp, &len);
    int status = pclose(fp);

#ifdef _WIN32
    tl_last_exit_code = (int64_t)status;
#else
    tl_last_exit_code = WIFEXITED(status) ? (int64_t)WEXITSTATUS(status) : (int64_t)-1;
#endif

    if (!output)
    {
        return rt_string_from_bytes("", 0);
    }

    rt_string full_result = rt_string_from_bytes(output, len);
    free(output);
    return full_result;
}

int64_t rt_exec_last_exit_code(void)
{
    return tl_last_exit_code;
}
