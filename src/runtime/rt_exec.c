//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_exec.c
// Purpose: Implement external command execution for Viper.Exec.
//
// SECURITY NOTE: Shell functions (Shell, ShellCapture) pass commands directly
// to the system shell. Never pass unsanitized user input to these functions
// as this creates shell injection vulnerabilities.
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

#ifndef _WIN32

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

    argv[0] = (char *)program;
    for (int64_t i = 0; i < nargs; i++)
    {
        rt_string arg_str = (rt_string)rt_seq_get(args, i);
        argv[1 + i] = (char *)rt_string_cstr(arg_str);
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
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);              // Close read end in child
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);              // Close original write end

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

/// @brief Build command line string for Windows CreateProcess.
static char *build_cmdline(const char *program, void *args)
{
    int64_t nargs = args ? rt_seq_len(args) : 0;

    // Calculate required length
    size_t len = strlen(program) + 3; // quotes + space
    for (int64_t i = 0; i < nargs; i++)
    {
        rt_string arg_str = (rt_string)rt_seq_get(args, i);
        len += rt_len(arg_str) + 3; // quotes + space
    }

    char *cmdline = (char *)malloc(len + 1);
    if (!cmdline)
        return NULL;

    char *p = cmdline;

    // Add program (quoted)
    *p++ = '"';
    size_t plen = strlen(program);
    memcpy(p, program, plen);
    p += plen;
    *p++ = '"';

    // Add arguments
    for (int64_t i = 0; i < nargs; i++)
    {
        rt_string arg_str = (rt_string)rt_seq_get(args, i);
        const char *arg = rt_string_cstr(arg_str);
        int64_t alen = rt_len(arg_str);

        *p++ = ' ';
        *p++ = '"';
        memcpy(p, arg, (size_t)alen);
        p += alen;
        *p++ = '"';
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

    BOOL success = CreateProcessA(
        program,
        cmdline,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi);

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

    BOOL success = CreateProcessA(
        program,
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
        while (ReadFile(hReadPipe, buf + len, (DWORD)(cap - len), &bytesRead, NULL) && bytesRead > 0)
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

// ============================================================================
// Public API Implementation
// ============================================================================

int64_t rt_exec_run(rt_string program)
{
    if (!program)
    {
        rt_trap("Exec.Run: null program");
        return -1;
    }

    const char *prog_str = rt_string_cstr(program);
    if (!prog_str || rt_len(program) == 0)
    {
        rt_trap("Exec.Run: empty program");
        return -1;
    }

    return exec_spawn(prog_str, NULL);
}

rt_string rt_exec_capture(rt_string program)
{
    if (!program)
    {
        rt_trap("Exec.Capture: null program");
        return rt_string_from_bytes("", 0);
    }

    const char *prog_str = rt_string_cstr(program);
    if (!prog_str || rt_len(program) == 0)
    {
        rt_trap("Exec.Capture: empty program");
        return rt_string_from_bytes("", 0);
    }

    return exec_capture_spawn(prog_str, NULL);
}

int64_t rt_exec_run_args(rt_string program, void *args)
{
    if (!program)
    {
        rt_trap("Exec.RunArgs: null program");
        return -1;
    }

    const char *prog_str = rt_string_cstr(program);
    if (!prog_str || rt_len(program) == 0)
    {
        rt_trap("Exec.RunArgs: empty program");
        return -1;
    }

    return exec_spawn(prog_str, args);
}

rt_string rt_exec_capture_args(rt_string program, void *args)
{
    if (!program)
    {
        rt_trap("Exec.CaptureArgs: null program");
        return rt_string_from_bytes("", 0);
    }

    const char *prog_str = rt_string_cstr(program);
    if (!prog_str || rt_len(program) == 0)
    {
        rt_trap("Exec.CaptureArgs: empty program");
        return rt_string_from_bytes("", 0);
    }

    return exec_capture_spawn(prog_str, args);
}

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
    if (rt_len(command) == 0)
    {
        return 0;
    }

#ifdef _WIN32
    // On Windows, system() uses cmd.exe
    int result = system(cmd_str);
    return (int64_t)result;
#else
    // On POSIX, system() uses /bin/sh -c
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
    if (rt_len(command) == 0)
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
