//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_process.c
// Purpose: Implements streaming, cancellable child-process handles for
//          Viper.System.Process.
//
// Key invariants:
//   - Start returns NULL when the process cannot be spawned.
//   - Output reads are non-blocking and incremental.
//   - Poll/IsRunning reap the process once and preserve the exit code.
//   - Destroy is idempotent and closes all OS handles.
//
// Ownership/Lifetime:
//   - Handles are rt_obj_new_i64-allocated and GC-managed.
//   - Destroy or the GC finalizer terminates a still-running child and releases
//     pipe buffers and OS resources.
//
// Links: src/runtime/system/rt_process.h
//
//===----------------------------------------------------------------------===//

#include "rt_process.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__viperdos__)
// ViperDOS process handles are intentionally unavailable until async process
// primitives exist in the target runtime.
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#define PROCESS_BUFFER_INITIAL_SIZE 4096
#define PROCESS_BUFFER_MAX_SIZE (16 * 1024 * 1024)

typedef struct process_buffer {
    char *data;
    size_t len;
    size_t cap;
} process_buffer;

typedef struct process_string_vector {
    char **values;
    rt_string *owned_strings;
    int64_t owned_count;
} process_string_vector;

typedef struct rt_process_impl {
    int8_t started;
    int8_t running;
    int8_t destroyed;
    int64_t exit_code;
    process_buffer stdout_buf;
    process_buffer stderr_buf;

#if defined(_WIN32)
    HANDLE process;
    HANDLE thread;
    HANDLE stdout_read;
    HANDLE stderr_read;
#elif defined(__viperdos__)
    int reserved;
#else
    pid_t pid;
    int stdout_fd;
    int stderr_fd;
#endif
} rt_process_impl;

static void process_finalize(void *obj);

static rt_string empty_string(void) {
    return rt_string_from_bytes("", 0);
}

static rt_process_impl *process_checked(void *handle) {
    if (!rt_obj_is_instance(handle, RT_PROCESS_CLASS_ID, sizeof(rt_process_impl)))
        return NULL;
    return (rt_process_impl *)handle;
}

static int buffer_append(process_buffer *buf, const char *data, size_t len) {
    if (!buf || !data || len == 0)
        return 1;
    if (buf->len >= PROCESS_BUFFER_MAX_SIZE)
        return 1;
    if (len > PROCESS_BUFFER_MAX_SIZE - buf->len)
        len = PROCESS_BUFFER_MAX_SIZE - buf->len;
    if (len == 0)
        return 1;

    size_t needed = buf->len + len;
    if (needed > buf->cap) {
        size_t new_cap = buf->cap ? buf->cap : PROCESS_BUFFER_INITIAL_SIZE;
        while (new_cap < needed && new_cap < PROCESS_BUFFER_MAX_SIZE) {
            if (new_cap > PROCESS_BUFFER_MAX_SIZE / 2) {
                new_cap = PROCESS_BUFFER_MAX_SIZE;
                break;
            }
            new_cap *= 2;
        }
        if (new_cap < needed)
            new_cap = needed;
        char *new_data = (char *)realloc(buf->data, new_cap);
        if (!new_data)
            return 0;
        buf->data = new_data;
        buf->cap = new_cap;
    }

    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return 1;
}

static rt_string buffer_take(process_buffer *buf) {
    if (!buf || buf->len == 0)
        return empty_string();
    rt_string out = rt_string_from_bytes(buf->data, buf->len);
    buf->len = 0;
    return out;
}

static void buffer_free(process_buffer *buf) {
    if (!buf)
        return;
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static process_string_vector build_string_vector(const char *first,
                                                 void *items,
                                                 int include_first) {
    process_string_vector vector;
    memset(&vector, 0, sizeof(vector));

    int64_t item_count = items ? rt_seq_len(items) : 0;
    int64_t total = item_count + (include_first ? 1 : 0) + 1;
    if (total <= 0 || total > INT_MAX)
        return vector;

    vector.values = (char **)calloc((size_t)total, sizeof(char *));
    if (!vector.values)
        return vector;

    if (item_count > 0) {
        vector.owned_strings = (rt_string *)calloc((size_t)item_count, sizeof(rt_string));
        if (!vector.owned_strings) {
            free(vector.values);
            vector.values = NULL;
            return vector;
        }
        vector.owned_count = item_count;
    }

    int64_t at = 0;
    if (include_first)
        vector.values[at++] = (char *)(uintptr_t)first;
    for (int64_t i = 0; i < item_count; i++) {
        rt_string item = rt_seq_get_str(items, i);
        vector.owned_strings[i] = item;
        vector.values[at++] = (char *)(uintptr_t)(item ? rt_string_cstr(item) : "");
    }
    vector.values[at] = NULL;
    return vector;
}

static void free_string_vector(process_string_vector *vector) {
    if (!vector)
        return;
    if (vector->owned_strings) {
        for (int64_t i = 0; i < vector->owned_count; i++)
            rt_str_release_maybe(vector->owned_strings[i]);
        free(vector->owned_strings);
    }
    free(vector->values);
    memset(vector, 0, sizeof(*vector));
}

static rt_process_impl *process_alloc(void) {
    rt_process_impl *proc =
        (rt_process_impl *)rt_obj_new_i64(RT_PROCESS_CLASS_ID, (int64_t)sizeof(rt_process_impl));
    if (!proc) {
        rt_trap("Process.Start: allocation failed");
        return NULL;
    }
    memset(proc, 0, sizeof(*proc));
    proc->exit_code = -1;
#if defined(_WIN32)
    proc->process = NULL;
    proc->thread = NULL;
    proc->stdout_read = NULL;
    proc->stderr_read = NULL;
#elif !defined(__viperdos__)
    proc->pid = -1;
    proc->stdout_fd = -1;
    proc->stderr_fd = -1;
#endif
    rt_obj_set_finalizer(proc, process_finalize);
    return proc;
}

#if defined(_WIN32)

static size_t cmdline_quoted_len(const char *s) {
    size_t len = 2;
    int backslashes = 0;
    for (; *s; s++) {
        if (*s == '\\') {
            backslashes++;
        } else if (*s == '"') {
            len += (size_t)backslashes * 2 + 2;
            backslashes = 0;
        } else {
            len += (size_t)backslashes + 1;
            backslashes = 0;
        }
    }
    len += (size_t)backslashes * 2;
    return len;
}

static char *cmdline_append_quoted(char *out, const char *s) {
    *out++ = '"';
    int backslashes = 0;
    for (; *s; s++) {
        if (*s == '\\') {
            backslashes++;
        } else if (*s == '"') {
            for (int i = 0; i < backslashes * 2; i++)
                *out++ = '\\';
            *out++ = '\\';
            *out++ = '"';
            backslashes = 0;
        } else {
            for (int i = 0; i < backslashes; i++)
                *out++ = '\\';
            *out++ = *s;
            backslashes = 0;
        }
    }
    for (int i = 0; i < backslashes * 2; i++)
        *out++ = '\\';
    *out++ = '"';
    return out;
}

static char *build_cmdline(const char *program, void *args) {
    int64_t nargs = args ? rt_seq_len(args) : 0;
    size_t len = cmdline_quoted_len(program);
    for (int64_t i = 0; i < nargs; i++) {
        rt_string arg = rt_seq_get_str(args, i);
        len += 1 + cmdline_quoted_len(arg ? rt_string_cstr(arg) : "");
        rt_str_release_maybe(arg);
    }

    char *cmdline = (char *)malloc(len + 1);
    if (!cmdline)
        return NULL;

    char *out = cmdline;
    out = cmdline_append_quoted(out, program);
    for (int64_t i = 0; i < nargs; i++) {
        rt_string arg = rt_seq_get_str(args, i);
        *out++ = ' ';
        out = cmdline_append_quoted(out, arg ? rt_string_cstr(arg) : "");
        rt_str_release_maybe(arg);
    }
    *out = '\0';
    return cmdline;
}

static char *build_env_block(void *env) {
    if (!env)
        return NULL;

    int64_t count = rt_seq_len(env);
    size_t len = 2;
    for (int64_t i = 0; i < count; i++) {
        rt_string item = rt_seq_get_str(env, i);
        len += strlen(item ? rt_string_cstr(item) : "") + 1;
        rt_str_release_maybe(item);
    }

    char *block = (char *)calloc(len, 1);
    if (!block)
        return NULL;

    char *out = block;
    for (int64_t i = 0; i < count; i++) {
        rt_string item = rt_seq_get_str(env, i);
        const char *text = item ? rt_string_cstr(item) : "";
        size_t item_len = strlen(text);
        memcpy(out, text, item_len);
        out += item_len + 1;
        rt_str_release_maybe(item);
    }
    *out = '\0';
    return block;
}

static int create_child_pipe(HANDLE *read_pipe, HANDLE *write_pipe) {
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(read_pipe, write_pipe, &sa, 0))
        return 0;
    if (!SetHandleInformation(*read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(*read_pipe);
        CloseHandle(*write_pipe);
        *read_pipe = NULL;
        *write_pipe = NULL;
        return 0;
    }
    return 1;
}

static void close_handle(HANDLE *handle) {
    if (handle && *handle) {
        CloseHandle(*handle);
        *handle = NULL;
    }
}

static void drain_pipe(HANDLE *read_pipe, process_buffer *buf) {
    if (!read_pipe || !*read_pipe)
        return;

    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(*read_pipe, NULL, 0, NULL, &available, NULL)) {
            close_handle(read_pipe);
            return;
        }
        if (available == 0)
            return;

        char chunk[4096];
        DWORD to_read = available < sizeof(chunk) ? available : (DWORD)sizeof(chunk);
        DWORD read_count = 0;
        if (!ReadFile(*read_pipe, chunk, to_read, &read_count, NULL) || read_count == 0) {
            close_handle(read_pipe);
            return;
        }
        if (!buffer_append(buf, chunk, read_count))
            rt_trap("Process: output buffer allocation failed");
    }
}

static void process_drain(rt_process_impl *proc) {
    if (!proc)
        return;
    drain_pipe(&proc->stdout_read, &proc->stdout_buf);
    drain_pipe(&proc->stderr_read, &proc->stderr_buf);
}

static void process_poll_internal(rt_process_impl *proc, int wait) {
    if (!proc || proc->destroyed)
        return;

    process_drain(proc);
    if (!proc->running || !proc->process)
        return;

    DWORD wait_result = WaitForSingleObject(proc->process, wait ? INFINITE : 0);
    if (wait_result == WAIT_OBJECT_0) {
        DWORD exit_code = 0;
        if (GetExitCodeProcess(proc->process, &exit_code)) {
            proc->exit_code = (int64_t)exit_code;
        } else {
            proc->exit_code = -1;
        }
        proc->running = 0;
        process_drain(proc);
    }
}

static rt_process_impl *process_start_impl(rt_string program,
                                           void *args,
                                           rt_string cwd,
                                           void *env) {
    if (!program || rt_str_len(program) == 0)
        return NULL;

    const char *program_text = rt_string_cstr(program);
    const char *cwd_text = cwd && rt_str_len(cwd) > 0 ? rt_string_cstr(cwd) : NULL;
    char *cmdline = build_cmdline(program_text, args);
    if (!cmdline) {
        rt_trap("Process.Start: command line allocation failed");
        return NULL;
    }

    char *env_block = build_env_block(env);
    if (env && !env_block) {
        free(cmdline);
        rt_trap("Process.Start: environment allocation failed");
        return NULL;
    }

    HANDLE stdout_read = NULL;
    HANDLE stdout_write = NULL;
    HANDLE stderr_read = NULL;
    HANDLE stderr_write = NULL;
    if (!create_child_pipe(&stdout_read, &stdout_write) ||
        !create_child_pipe(&stderr_read, &stderr_write)) {
        close_handle(&stdout_read);
        close_handle(&stdout_write);
        close_handle(&stderr_read);
        close_handle(&stderr_write);
        free(env_block);
        free(cmdline);
        return NULL;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = stdout_write;
    si.hStdError = stderr_write;

    BOOL ok = CreateProcessA(
        program_text, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, env_block, cwd_text, &si, &pi);

    close_handle(&stdout_write);
    close_handle(&stderr_write);
    free(env_block);
    free(cmdline);

    if (!ok) {
        close_handle(&stdout_read);
        close_handle(&stderr_read);
        return NULL;
    }

    rt_process_impl *proc = process_alloc();
    if (!proc) {
        close_handle(&stdout_read);
        close_handle(&stderr_read);
        close_handle(&pi.hProcess);
        close_handle(&pi.hThread);
        return NULL;
    }
    proc->started = 1;
    proc->running = 1;
    proc->process = pi.hProcess;
    proc->thread = pi.hThread;
    proc->stdout_read = stdout_read;
    proc->stderr_read = stderr_read;
    return proc;
}

#elif defined(__viperdos__)

static void process_drain(rt_process_impl *proc) {
    (void)proc;
}

static void process_poll_internal(rt_process_impl *proc, int wait) {
    (void)proc;
    (void)wait;
}

static rt_process_impl *process_start_impl(rt_string program,
                                           void *args,
                                           rt_string cwd,
                                           void *env) {
    (void)program;
    (void)args;
    (void)cwd;
    (void)env;
    return NULL;
}

#else

static void close_fd(int *fd) {
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void drain_fd(int *fd, process_buffer *buf) {
    if (!fd || *fd < 0)
        return;

    for (;;) {
        char chunk[4096];
        ssize_t count = read(*fd, chunk, sizeof(chunk));
        if (count > 0) {
            if (!buffer_append(buf, chunk, (size_t)count))
                rt_trap("Process: output buffer allocation failed");
            continue;
        }
        if (count == 0) {
            close_fd(fd);
            return;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        close_fd(fd);
        return;
    }
}

static void process_drain(rt_process_impl *proc) {
    if (!proc)
        return;
    drain_fd(&proc->stdout_fd, &proc->stdout_buf);
    drain_fd(&proc->stderr_fd, &proc->stderr_buf);
}

static int64_t decode_exit_status(int status) {
    if (WIFEXITED(status))
        return (int64_t)WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return -(int64_t)WTERMSIG(status);
    return -1;
}

static void process_poll_internal(rt_process_impl *proc, int wait) {
    if (!proc || proc->destroyed)
        return;

    process_drain(proc);
    if (!proc->running || proc->pid <= 0)
        return;

    int status = 0;
    pid_t result;
    do {
        result = waitpid(proc->pid, &status, wait ? 0 : WNOHANG);
    } while (result < 0 && errno == EINTR);

    if (result == proc->pid) {
        proc->exit_code = decode_exit_status(status);
        proc->running = 0;
        process_drain(proc);
    } else if (result < 0 && errno == ECHILD) {
        proc->exit_code = -1;
        proc->running = 0;
        process_drain(proc);
    }
}

static rt_process_impl *process_start_impl(rt_string program,
                                           void *args,
                                           rt_string cwd,
                                           void *env) {
    if (!program || rt_str_len(program) == 0)
        return NULL;

    const char *program_text = rt_string_cstr(program);
    const char *cwd_text = cwd && rt_str_len(cwd) > 0 ? rt_string_cstr(cwd) : NULL;
    process_string_vector argv = build_string_vector(program_text, args, 1);
    if (!argv.values) {
        rt_trap("Process.Start: argv allocation failed");
        return NULL;
    }

    process_string_vector envp;
    memset(&envp, 0, sizeof(envp));
    if (env)
        envp = build_string_vector(NULL, env, 0);
    if (env && !envp.values) {
        free_string_vector(&argv);
        rt_trap("Process.Start: environment allocation failed");
        return NULL;
    }

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        close_fd(&stdout_pipe[0]);
        close_fd(&stdout_pipe[1]);
        close_fd(&stderr_pipe[0]);
        close_fd(&stderr_pipe[1]);
        free_string_vector(&envp);
        free_string_vector(&argv);
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close_fd(&stdout_pipe[0]);
        close_fd(&stdout_pipe[1]);
        close_fd(&stderr_pipe[0]);
        close_fd(&stderr_pipe[1]);
        free_string_vector(&envp);
        free_string_vector(&argv);
        return NULL;
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0)
            _exit(127);
        if (dup2(stderr_pipe[1], STDERR_FILENO) < 0)
            _exit(127);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        if (cwd_text) {
            if (chdir(cwd_text) != 0)
                _exit(127);
        }

        execve(program_text, argv.values, envp.values ? envp.values : environ);
        _exit(errno == ENOENT ? 127 : 126);
    }

    close_fd(&stdout_pipe[1]);
    close_fd(&stderr_pipe[1]);
    set_nonblocking(stdout_pipe[0]);
    set_nonblocking(stderr_pipe[0]);
    free_string_vector(&envp);
    free_string_vector(&argv);

    rt_process_impl *proc = process_alloc();
    if (!proc) {
        close_fd(&stdout_pipe[0]);
        close_fd(&stderr_pipe[0]);
        return NULL;
    }
    proc->started = 1;
    proc->running = 1;
    proc->pid = pid;
    proc->stdout_fd = stdout_pipe[0];
    proc->stderr_fd = stderr_pipe[0];
    return proc;
}

#endif

static void process_close(rt_process_impl *proc) {
    if (!proc || proc->destroyed)
        return;

#if defined(_WIN32)
    if (proc->running && proc->process) {
        (void)TerminateProcess(proc->process, 1);
        (void)WaitForSingleObject(proc->process, INFINITE);
        proc->running = 0;
        proc->exit_code = 1;
    }
    process_drain(proc);
    close_handle(&proc->stdout_read);
    close_handle(&proc->stderr_read);
    close_handle(&proc->thread);
    close_handle(&proc->process);
#elif defined(__viperdos__)
    process_drain(proc);
#else
    if (proc->running && proc->pid > 0) {
        (void)kill(proc->pid, SIGTERM);
        process_poll_internal(proc, 0);
        if (proc->running) {
            (void)kill(proc->pid, SIGKILL);
            process_poll_internal(proc, 1);
        }
    } else {
        process_poll_internal(proc, 0);
    }
    process_drain(proc);
    close_fd(&proc->stdout_fd);
    close_fd(&proc->stderr_fd);
#endif

    buffer_free(&proc->stdout_buf);
    buffer_free(&proc->stderr_buf);
    proc->running = 0;
    proc->started = 0;
    proc->destroyed = 1;
}

static void process_finalize(void *obj) {
    process_close((rt_process_impl *)obj);
}

void *rt_process_start(rt_string program, void *args) {
    return rt_process_start_with_env(program, args, NULL, NULL);
}

void *rt_process_start_in(rt_string program, void *args, rt_string cwd) {
    return rt_process_start_with_env(program, args, cwd, NULL);
}

void *rt_process_start_with_env(rt_string program, void *args, rt_string cwd, void *env) {
    return process_start_impl(program, args, cwd, env);
}

int64_t rt_process_is_valid(void *handle) {
    rt_process_impl *proc = process_checked(handle);
    return proc && proc->started && !proc->destroyed ? 1 : 0;
}

int64_t rt_process_poll(void *handle) {
    rt_process_impl *proc = process_checked(handle);
    if (!proc || !proc->started || proc->destroyed)
        return 0;
    process_poll_internal(proc, 0);
    return proc->running ? 1 : 0;
}

int64_t rt_process_is_running(void *handle) {
    return rt_process_poll(handle);
}

rt_string rt_process_read_stdout(void *handle) {
    rt_process_impl *proc = process_checked(handle);
    if (!proc || !proc->started || proc->destroyed)
        return empty_string();
    process_drain(proc);
    return buffer_take(&proc->stdout_buf);
}

rt_string rt_process_read_stderr(void *handle) {
    rt_process_impl *proc = process_checked(handle);
    if (!proc || !proc->started || proc->destroyed)
        return empty_string();
    process_drain(proc);
    return buffer_take(&proc->stderr_buf);
}

int64_t rt_process_exit_code(void *handle) {
    rt_process_impl *proc = process_checked(handle);
    if (!proc || !proc->started || proc->destroyed)
        return -1;
    process_poll_internal(proc, 0);
    return proc->running ? -1 : proc->exit_code;
}

int64_t rt_process_kill(void *handle) {
    rt_process_impl *proc = process_checked(handle);
    if (!proc || !proc->started || proc->destroyed || !proc->running)
        return 0;

#if defined(_WIN32)
    if (!proc->process)
        return 0;
    return TerminateProcess(proc->process, 1) ? 1 : 0;
#elif defined(__viperdos__)
    return 0;
#else
    if (proc->pid <= 0)
        return 0;
    return kill(proc->pid, SIGTERM) == 0 ? 1 : 0;
#endif
}

int64_t rt_process_wait(void *handle) {
    rt_process_impl *proc = process_checked(handle);
    if (!proc || !proc->started || proc->destroyed)
        return -1;
    process_poll_internal(proc, 1);
    return proc->exit_code;
}

void rt_process_destroy(void *handle) {
    rt_process_impl *proc = process_checked(handle);
    process_close(proc);
}
