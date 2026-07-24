//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_pty.c
// Purpose: Implements pseudo-terminal-backed child handles for Zanna.System.Pty.
//          Mirrors rt_process.c (ring buffer, non-blocking drain, reap, GC
//          finalizer) but establishes a controlling terminal so interactive
//          programs see a real TTY, merges output into one ANSI-bearing stream,
//          and supports window resize.
//
// Key invariants:
//   - Open returns NULL when a PTY cannot be created (incl. unsupported targets).
//   - Read is non-blocking and incremental; output is a single merged stream.
//   - Poll/IsRunning reap the child once and preserve the exit code.
//   - Destroy is idempotent and releases all OS resources.
//   - Windows command, cwd, and environment strings cross the Win32 boundary
//     as strict UTF-16; explicit environment blocks are sorted and complete.
//
// Ownership/Lifetime:
//   - Handles are rt_obj_new_i64-allocated and GC-managed; Destroy or the
//     finalizer terminates a still-running child and frees the master fd / PTY.
//
// Platform notes:
//   - POSIX: posix_openpt + fork + setsid + TIOCSCTTY + execvp (the controlling
//     terminal cannot be set up via posix_spawn). Fully exercised on macOS/Linux.
//   - Windows: ConPTY (CreatePseudoConsole/ResizePseudoConsole/ClosePseudoConsole)
//     resolved dynamically and thread-safely (Windows 10 1809+).
//
// Links: src/runtime/system/rt_pty.h, src/runtime/system/rt_process.c
//
//===----------------------------------------------------------------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "rt_pty.h"

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_result.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <wchar.h>
#include <windows.h>
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
typedef HRESULT(WINAPI *pty_create_pseudoconsole_fn)(COORD, HANDLE, HANDLE, DWORD, void **);
typedef HRESULT(WINAPI *pty_resize_pseudoconsole_fn)(void *, COORD);
typedef VOID(WINAPI *pty_close_pseudoconsole_fn)(void *);
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
extern char **environ;
#endif

#define PTY_BUFFER_INITIAL_SIZE 4096
#define PTY_BUFFER_MAX_SIZE (16 * 1024 * 1024)
#define PTY_LAST_ERROR_MAX 256

typedef struct pty_buffer {
    char *data;
    size_t len;
    size_t cap;
    int truncated;
} pty_buffer;

typedef struct pty_string_vector {
    char **values;
    rt_string *owned_strings;
    int64_t owned_count;
} pty_string_vector;

typedef struct rt_pty_impl {
    int8_t started;
    int8_t running;
    int8_t destroyed;
    int64_t exit_code;
    pty_buffer output_buf;

#if defined(_WIN32)
    HANDLE process;
    HANDLE thread;
    HANDLE input_write; // parent -> child
    HANDLE output_read; // child -> parent
    void *hpc;          // HPCON
#else
    pid_t pid;
    int master_fd;
#endif
} rt_pty_impl;

static void pty_finalize(void *obj);
// Thread-local so concurrent Open/OpenResult/IsSupported calls on different
// threads never tear each other's diagnostic or clobber it — the buffer was a
// plain process-global written with snprintf from every path (VDOC-215). Each
// thread reads back the error from the operation it just performed.
static RT_THREAD_LOCAL char pty_last_error[PTY_LAST_ERROR_MAX];

static rt_string empty_string(void) {
    return rt_string_from_bytes("", 0);
}

static rt_pty_impl *pty_checked(void *handle) {
    if (!rt_obj_is_instance(handle, RT_PTY_CLASS_ID, sizeof(rt_pty_impl)))
        return NULL;
    return (rt_pty_impl *)handle;
}

/// @brief Store a bounded user-facing PTY diagnostic string.
/// @details Writes a thread-local buffer, so concurrent PTY calls on different
///          threads keep independent diagnostics and cannot tear or clobber each
///          other (VDOC-215). Read back the error on the same thread that
///          performed the operation.
/// @param message NUL-terminated diagnostic text; NULL clears the buffer.
static void pty_set_last_error(const char *message) {
    if (!message) {
        pty_last_error[0] = '\0';
        return;
    }
    snprintf(pty_last_error, sizeof(pty_last_error), "%s", message);
}

/// @brief Store a POSIX errno-based PTY diagnostic string.
/// @param prefix Context prefix such as "posix_openpt failed".
static void pty_set_last_errno(const char *prefix) {
    snprintf(pty_last_error,
             sizeof(pty_last_error),
             "%s: %s",
             prefix ? prefix : "PTY error",
             strerror(errno));
}

// --- Shared helpers (mirrors of the proven rt_process.c implementations) ----

static int pty_string_cstr_view(rt_string value, const char **out_text, size_t *out_len) {
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

static int pty_validate_string_sequence(void *items,
                                        int require_env_assignment,
                                        const char *trap_msg) {
    int64_t count = items ? rt_seq_len(items) : 0;
    if (count < 0)
        return 1;
    for (int64_t i = 0; i < count; i++) {
        rt_string item = rt_seq_get_str(items, i);
        const char *text = NULL;
        size_t len = 0;
        int ok = pty_string_cstr_view(item, &text, &len);
        if (ok && require_env_assignment) {
            const char *equals = (const char *)memchr(text, '=', len);
            ok = equals && equals != text;
        }
        rt_str_release_maybe(item);
        if (!ok) {
            rt_trap(trap_msg);
            return 0;
        }
    }
    return 1;
}

static void buffer_mark_truncated(pty_buffer *buf) {
    if (buf)
        buf->truncated = 1;
}

static int buffer_append(pty_buffer *buf, const char *data, size_t len) {
    if (!buf || !data || len == 0)
        return 1;
    if (buf->len >= PTY_BUFFER_MAX_SIZE) {
        buffer_mark_truncated(buf);
        return 1;
    }
    if (len > PTY_BUFFER_MAX_SIZE - buf->len) {
        len = PTY_BUFFER_MAX_SIZE - buf->len;
        buffer_mark_truncated(buf);
    }
    if (len == 0)
        return 1;

    size_t needed = buf->len + len;
    if (needed > buf->cap) {
        size_t new_cap = buf->cap ? buf->cap : PTY_BUFFER_INITIAL_SIZE;
        while (new_cap < needed && new_cap < PTY_BUFFER_MAX_SIZE) {
            if (new_cap > PTY_BUFFER_MAX_SIZE / 2) {
                new_cap = PTY_BUFFER_MAX_SIZE;
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

static rt_string buffer_take_string(pty_buffer *buf, int *was_truncated) {
    int truncated = buf ? buf->truncated : 0;
    if (was_truncated)
        *was_truncated = truncated;
    if (!buf) {
        return empty_string();
    }
    buf->truncated = 0;
    if (buf->len == 0)
        return empty_string();
    rt_string out = rt_string_from_bytes(buf->data, buf->len);
    buf->len = 0;
    return out;
}

static rt_string buffer_take(pty_buffer *buf) {
    int truncated = 0;
    rt_string out = buffer_take_string(buf, &truncated);
    if (truncated) {
        rt_trap("Pty: output truncated");
    }
    return out;
}

static void map_set_string_owned(void *map, const char *key, rt_string value) {
    if (!map || !key)
        return;
    rt_map_set_str(map, rt_const_cstr(key), value ? value : rt_const_cstr(""));
}

static void *buffer_take_result(pty_buffer *buf) {
    int truncated = 0;
    rt_string text = buffer_take_string(buf, &truncated);
    void *result = rt_map_new();
    if (!result) {
        rt_str_release_maybe(text);
        return NULL;
    }
    map_set_string_owned(result, "text", text);
    rt_map_set_bool(result, rt_const_cstr("truncated"), truncated ? 1 : 0);
    rt_str_release_maybe(text);
    return result;
}

static void buffer_free(pty_buffer *buf) {
    if (!buf)
        return;
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->truncated = 0;
}

static pty_string_vector build_string_vector(const char *first, void *items, int include_first) {
    pty_string_vector vector;
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

static void free_string_vector(pty_string_vector *vector) {
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

static rt_pty_impl *pty_alloc(void) {
    rt_pty_impl *pty = (rt_pty_impl *)rt_obj_new_i64(RT_PTY_CLASS_ID, (int64_t)sizeof(rt_pty_impl));
    if (!pty) {
        pty_set_last_error("PTY allocation failed");
        rt_trap("Pty.Open: allocation failed");
        return NULL;
    }
    memset(pty, 0, sizeof(*pty));
    pty->exit_code = -1;
#if defined(_WIN32)
    pty->process = NULL;
    pty->thread = NULL;
    pty->input_write = NULL;
    pty->output_read = NULL;
    pty->hpc = NULL;
#else
    pty->pid = -1;
    pty->master_fd = -1;
#endif
    rt_obj_set_finalizer(pty, pty_finalize);
    return pty;
}

static void pty_clamp_size(int64_t *cols, int64_t *rows) {
    if (*cols <= 0)
        *cols = 80;
    if (*rows <= 0)
        *rows = 24;
    if (*cols > 4096)
        *cols = 4096;
    if (*rows > 4096)
        *rows = 4096;
}

// ===========================================================================
#if defined(_WIN32)
// --- Windows ConPTY backend (validate on a Windows host; not built on POSIX) -

static INIT_ONCE pty_conpty_once = INIT_ONCE_STATIC_INIT;
static int pty_conpty_available = 0;
static pty_create_pseudoconsole_fn pty_create_pc = NULL;
static pty_resize_pseudoconsole_fn pty_resize_pc = NULL;
static pty_close_pseudoconsole_fn pty_close_pc = NULL;
typedef int(WINAPI *pty_compare_string_ordinal_fn)(LPCWCH, int, LPCWCH, int, BOOL);
static pty_compare_string_ordinal_fn pty_compare_string_ordinal = NULL;

/// @brief Store a Windows GetLastError-based PTY diagnostic string.
/// @param prefix Context prefix such as "CreateProcessW failed".
static void pty_set_last_win32_error(const char *prefix) {
    snprintf(pty_last_error,
             sizeof(pty_last_error),
             "%s: Windows error %lu",
             prefix ? prefix : "PTY error",
             (unsigned long)GetLastError());
}

/// @brief Store a ConPTY HRESULT without relying on unrelated Win32 last-error state.
static void pty_set_hresult_error(const char *prefix, HRESULT hr) {
    snprintf(pty_last_error,
             sizeof(pty_last_error),
             "%s: HRESULT 0x%08lX",
             prefix ? prefix : "PTY error",
             (unsigned long)hr);
}

/// @brief Resolve the optional ConPTY entry points exactly once per process.
static BOOL CALLBACK pty_load_conpty_once(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once;
    (void)param;
    (void)context;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32)
        return TRUE;
    pty_create_pc = (pty_create_pseudoconsole_fn)(void *)GetProcAddress(k32, "CreatePseudoConsole");
    pty_resize_pc = (pty_resize_pseudoconsole_fn)(void *)GetProcAddress(k32, "ResizePseudoConsole");
    pty_close_pc = (pty_close_pseudoconsole_fn)(void *)GetProcAddress(k32, "ClosePseudoConsole");
    pty_compare_string_ordinal =
        (pty_compare_string_ordinal_fn)(void *)GetProcAddress(k32, "CompareStringOrdinal");
    pty_conpty_available = (pty_create_pc && pty_resize_pc && pty_close_pc) ? 1 : 0;
    return TRUE;
}

static void pty_load_conpty(void) {
    (void)InitOnceExecuteOnce(&pty_conpty_once, pty_load_conpty_once, NULL, NULL);
}

static void close_handle(HANDLE *h) {
    if (h && *h && *h != INVALID_HANDLE_VALUE) {
        CloseHandle(*h);
        *h = NULL;
    }
}

/// @brief Convert a UTF-8 byte run to a freshly allocated wide string.
static wchar_t *pty_widen(const char *text) {
    if (!text)
        text = "";
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (wlen <= 0)
        return NULL;
    wchar_t *wide = (wchar_t *)calloc((size_t)wlen, sizeof(wchar_t));
    if (!wide)
        return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, wlen) <= 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

/// @brief Invoke the dynamically resolved Windows ordinal comparator.
static int pty_compare_ordinal(const wchar_t *left,
                               int left_len,
                               const wchar_t *right,
                               int right_len) {
    pty_load_conpty();
    if (pty_compare_string_ordinal)
        return pty_compare_string_ordinal(left, left_len, right, right_len, TRUE);
    if (left_len < 0 && right_len < 0)
        return wcscmp(left, right) == 0 ? CSTR_EQUAL : 0;
    return wcsncmp(left, right, (size_t)left_len) == 0 ? CSTR_EQUAL : 0;
}

/// @brief Compare complete UTF-16 environment entries using Win32 ordinal rules.
static int pty_compare_env_entry_wide(const void *left, const void *right) {
    const wchar_t *lhs = *(const wchar_t *const *)left;
    const wchar_t *rhs = *(const wchar_t *const *)right;
    int result = pty_compare_ordinal(lhs, -1, rhs, -1);
    if (result == CSTR_LESS_THAN)
        return -1;
    if (result == CSTR_GREATER_THAN)
        return 1;
    return wcscmp(lhs, rhs);
}

/// @brief Test whether two UTF-16 environment entries name the same variable.
static int pty_env_names_equal_wide(const wchar_t *lhs, const wchar_t *rhs) {
    size_t lhs_len = 0;
    size_t rhs_len = 0;
    while (lhs[lhs_len] && lhs[lhs_len] != L'=')
        lhs_len++;
    while (rhs[rhs_len] && rhs[rhs_len] != L'=')
        rhs_len++;
    if (lhs_len == 0 || lhs_len != rhs_len || lhs_len > INT_MAX)
        return 0;
    return pty_compare_ordinal(lhs, (int)lhs_len, rhs, (int)rhs_len) == CSTR_EQUAL;
}

/// @brief Build a strict, sorted, double-NUL-terminated CreateProcessW environment block.
static wchar_t *pty_build_env_block_wide(void *env) {
    int64_t count;
    size_t total_wchars = 1;
    wchar_t **entries = NULL;
    wchar_t *block = NULL;

    if (!env)
        return NULL;
    count = rt_seq_len(env);
    if (count < 0 || (uint64_t)count > SIZE_MAX / sizeof(*entries))
        return NULL;
    if (count > 0) {
        entries = (wchar_t **)calloc((size_t)count, sizeof(*entries));
        if (!entries)
            return NULL;
    }
    for (int64_t i = 0; i < count; i++) {
        rt_string item = rt_seq_get_str(env, i);
        entries[i] = pty_widen(item ? rt_string_cstr(item) : NULL);
        rt_str_release_maybe(item);
        if (!entries[i])
            goto fail;
        size_t length = wcslen(entries[i]);
        if (length == SIZE_MAX || total_wchars > SIZE_MAX - length - 1)
            goto fail;
        total_wchars += length + 1;
    }
    if (count > 1) {
        qsort(entries, (size_t)count, sizeof(*entries), pty_compare_env_entry_wide);
        for (int64_t i = 1; i < count; i++) {
            if (pty_env_names_equal_wide(entries[i - 1], entries[i]))
                goto fail;
        }
    }
    if (total_wchars == SIZE_MAX || total_wchars + 1 > SIZE_MAX / sizeof(*block))
        goto fail;
    block = (wchar_t *)calloc(total_wchars + 1, sizeof(*block));
    if (!block)
        goto fail;
    wchar_t *out = block;
    for (int64_t i = 0; i < count; i++) {
        size_t length = wcslen(entries[i]);
        memcpy(out, entries[i], length * sizeof(*out));
        out += length + 1;
        free(entries[i]);
    }
    free(entries);
    return block;

fail:
    if (entries) {
        for (int64_t i = 0; i < count; i++)
            free(entries[i]);
    }
    free(entries);
    return NULL;
}

/// @brief Append @p arg to a Windows command line with standard quoting.
static int pty_cmdline_append(char **buf, size_t *len, size_t *cap, const char *arg) {
    int need_quote = (arg[0] == '\0');
    for (const char *p = arg; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '"')
            need_quote = 1;
    }
    char stackbuf[1024];
    size_t n = 0;
    char *tmp = stackbuf;
    size_t tmpcap = sizeof(stackbuf);
    size_t arglen = strlen(arg);
    if (arglen * 2 + 4 > tmpcap) {
        tmp = (char *)malloc(arglen * 2 + 4);
        if (!tmp)
            return 0;
    }
    if (*len > 0)
        tmp[n++] = ' ';
    if (need_quote)
        tmp[n++] = '"';
    size_t backslashes = 0;
    for (const char *p = arg; *p; p++) {
        if (*p == '\\') {
            backslashes++;
            tmp[n++] = '\\';
        } else if (*p == '"') {
            for (size_t b = 0; b < backslashes + 1; b++)
                tmp[n++] = '\\';
            tmp[n++] = '"';
            backslashes = 0;
        } else {
            backslashes = 0;
            tmp[n++] = *p;
        }
    }
    if (need_quote) {
        for (size_t b = 0; b < backslashes; b++)
            tmp[n++] = '\\';
        tmp[n++] = '"';
    }
    if (*len + n + 1 > *cap) {
        size_t new_cap = (*cap ? *cap : 256);
        while (new_cap < *len + n + 1)
            new_cap *= 2;
        char *grown = (char *)realloc(*buf, new_cap);
        if (!grown) {
            if (tmp != stackbuf)
                free(tmp);
            return 0;
        }
        *buf = grown;
        *cap = new_cap;
    }
    memcpy(*buf + *len, tmp, n);
    *len += n;
    (*buf)[*len] = '\0';
    if (tmp != stackbuf)
        free(tmp);
    return 1;
}

static rt_pty_impl *pty_open_impl(
    rt_string program, void *args, rt_string cwd, void *env, int64_t cols, int64_t rows) {
    const char *program_text = NULL;
    const char *cwd_text = NULL;
    size_t program_len = 0, cwd_len = 0;
    if (!program) {
        pty_set_last_error("PTY program is empty");
        return NULL;
    }
    if (!pty_string_cstr_view(program, &program_text, &program_len) || program_len == 0) {
        pty_set_last_error("PTY program is invalid");
        rt_trap("Pty.Open: invalid program");
        return NULL;
    }
    if (cwd) {
        if (!pty_string_cstr_view(cwd, &cwd_text, &cwd_len)) {
            pty_set_last_error("PTY working directory is invalid");
            rt_trap("Pty.Open: invalid working directory");
            return NULL;
        }
        if (cwd_len == 0)
            cwd_text = NULL;
    }
    if (!pty_validate_string_sequence(args, 0, "Pty.Open: invalid argument"))
        return NULL;
    if (!pty_validate_string_sequence(env, 1, "Pty.Open: invalid environment entry"))
        return NULL;

    pty_load_conpty();
    if (!pty_conpty_available) {
        pty_set_last_error("Windows ConPTY is unavailable; Windows 10 1809 or newer is required");
        return NULL;
    }

    // Build the command line: program followed by args.
    char *cmdline = NULL;
    size_t cmd_len = 0, cmd_cap = 0;
    if (!pty_cmdline_append(&cmdline, &cmd_len, &cmd_cap, program_text)) {
        pty_set_last_error("PTY command line allocation failed");
        free(cmdline);
        return NULL;
    }
    int64_t arg_count = args ? rt_seq_len(args) : 0;
    for (int64_t i = 0; i < arg_count; i++) {
        rt_string a = rt_seq_get_str(args, i);
        const char *atext = a ? rt_string_cstr(a) : "";
        int ok = pty_cmdline_append(&cmdline, &cmd_len, &cmd_cap, atext);
        rt_str_release_maybe(a);
        if (!ok) {
            pty_set_last_error("PTY command line allocation failed");
            free(cmdline);
            return NULL;
        }
    }

    HANDLE in_read = NULL, in_write = NULL, out_read = NULL, out_write = NULL;
    if (!CreatePipe(&in_read, &in_write, NULL, 0) || !CreatePipe(&out_read, &out_write, NULL, 0)) {
        pty_set_last_win32_error("CreatePipe failed");
        close_handle(&in_read);
        close_handle(&in_write);
        close_handle(&out_read);
        close_handle(&out_write);
        free(cmdline);
        return NULL;
    }

    COORD size;
    size.X = (SHORT)cols;
    size.Y = (SHORT)rows;
    void *hpc = NULL;
    HRESULT hr = pty_create_pc(size, in_read, out_write, 0, &hpc);
    // ConPTY duplicates the handles it needs; close our copies of the child ends.
    close_handle(&in_read);
    close_handle(&out_write);
    if (FAILED(hr) || !hpc) {
        pty_set_hresult_error("CreatePseudoConsole failed", FAILED(hr) ? hr : E_POINTER);
        if (hpc)
            pty_close_pc(hpc);
        close_handle(&in_write);
        close_handle(&out_read);
        free(cmdline);
        return NULL;
    }

    STARTUPINFOEXW si;
    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    SIZE_T attr_size = 0;
    (void)InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    if (attr_size > 0)
        si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
    if (!si.lpAttributeList ||
        !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size)) {
        pty_set_last_win32_error("ConPTY process attribute setup failed");
        free(si.lpAttributeList);
        pty_close_pc(hpc);
        close_handle(&in_write);
        close_handle(&out_read);
        free(cmdline);
        return NULL;
    }
    if (!UpdateProcThreadAttribute(si.lpAttributeList,
                                   0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hpc,
                                   sizeof(hpc),
                                   NULL,
                                   NULL)) {
        pty_set_last_win32_error("ConPTY process attribute setup failed");
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
        pty_close_pc(hpc);
        close_handle(&in_write);
        close_handle(&out_read);
        free(cmdline);
        return NULL;
    }

    wchar_t *wcmd = pty_widen(cmdline);
    wchar_t *wcwd = cwd_text ? pty_widen(cwd_text) : NULL;
    free(cmdline);
    cmdline = NULL;
    wchar_t *env_block = pty_build_env_block_wide(env);
    if (!wcmd || (cwd_text && !wcwd) || (env && !env_block)) {
        pty_set_last_error("ConPTY UTF-8 conversion or environment allocation failed");
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
        free(wcmd);
        free(wcwd);
        free(env_block);
        pty_close_pc(hpc);
        close_handle(&in_write);
        close_handle(&out_read);
        return NULL;
    }

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL ok = CreateProcessW(NULL,
                             wcmd,
                             NULL,
                             NULL,
                             FALSE,
                             EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                             env_block,
                             wcwd,
                             &si.StartupInfo,
                             &pi);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    free(wcmd);
    free(wcwd);
    free(env_block);

    if (!ok) {
        pty_set_last_win32_error("CreateProcessW failed");
        pty_close_pc(hpc);
        close_handle(&in_write);
        close_handle(&out_read);
        return NULL;
    }

    rt_pty_impl *pty = pty_alloc();
    if (!pty) {
        if (TerminateProcess(pi.hProcess, 1))
            (void)WaitForSingleObject(pi.hProcess, INFINITE);
        close_handle(&pi.hThread);
        close_handle(&pi.hProcess);
        pty_close_pc(hpc);
        close_handle(&in_write);
        close_handle(&out_read);
        return NULL;
    }
    pty->started = 1;
    pty->running = 1;
    pty->process = pi.hProcess;
    pty->thread = pi.hThread;
    pty->input_write = in_write;
    pty->output_read = out_read;
    pty->hpc = hpc;
    return pty;
}

static void pty_drain(rt_pty_impl *pty) {
    if (!pty || !pty->output_read)
        return;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(pty->output_read, NULL, 0, NULL, &avail, NULL))
            return;
        if (avail == 0)
            return;
        char chunk[4096];
        DWORD want = avail > sizeof(chunk) ? (DWORD)sizeof(chunk) : avail;
        DWORD got = 0;
        if (!ReadFile(pty->output_read, chunk, want, &got, NULL) || got == 0)
            return;
        if (!buffer_append(&pty->output_buf, chunk, (size_t)got)) {
            rt_trap("Pty: output buffer allocation failed");
            return;
        }
    }
}

static void pty_poll_internal(rt_pty_impl *pty, int wait) {
    if (!pty || pty->destroyed)
        return;
    pty_drain(pty);
    if (!pty->running || !pty->process)
        return;
    DWORD wait_ms = wait ? INFINITE : 0;
    DWORD r = WaitForSingleObject(pty->process, wait_ms);
    if (r == WAIT_OBJECT_0) {
        DWORD code = 0;
        pty->exit_code = GetExitCodeProcess(pty->process, &code) ? (int64_t)code : -1;
        pty->running = 0;
        pty_drain(pty);
    } else if (r == WAIT_FAILED) {
        pty_set_last_win32_error("WaitForSingleObject(ConPTY process) failed");
        pty->exit_code = -1;
        pty->running = 0;
    }
}

static int pty_resize_impl(rt_pty_impl *pty, int64_t cols, int64_t rows) {
    if (!pty || !pty->hpc || !pty_resize_pc)
        return 0;
    COORD size;
    size.X = (SHORT)cols;
    size.Y = (SHORT)rows;
    HRESULT hr = pty_resize_pc(pty->hpc, size);
    if (FAILED(hr)) {
        pty_set_hresult_error("ResizePseudoConsole failed", hr);
        return 0;
    }
    return 1;
}

static int64_t pty_write_impl(rt_pty_impl *pty, const char *bytes, size_t len) {
    if (!pty->input_write)
        return -1;
    size_t off = 0;
    while (off < len) {
        size_t remaining = len - off;
        DWORD chunk = remaining > 0x40000000u ? 0x40000000u : (DWORD)remaining;
        DWORD written = 0;
        if (!WriteFile(pty->input_write, bytes + off, chunk, &written, NULL) || written == 0)
            return off > 0 ? (int64_t)off : -1;
        off += written;
    }
    return (int64_t)off;
}

static void pty_close(rt_pty_impl *pty) {
    if (!pty || pty->destroyed)
        return;
    if (pty->running && pty->process) {
        if (TerminateProcess(pty->process, 1))
            (void)WaitForSingleObject(pty->process, INFINITE);
        pty->running = 0;
        pty->exit_code = 1;
    }
    pty_drain(pty);
    if (pty->hpc && pty_close_pc) {
        pty_close_pc(pty->hpc);
        pty->hpc = NULL;
    }
    close_handle(&pty->input_write);
    close_handle(&pty->output_read);
    close_handle(&pty->thread);
    close_handle(&pty->process);
    buffer_free(&pty->output_buf);
    pty->running = 0;
    pty->started = 0;
    pty->destroyed = 1;
}

static int64_t pty_supported(void) {
    pty_load_conpty();
    return pty_conpty_available ? 1 : 0;
}

// ===========================================================================
#else
// --- POSIX (macOS / Linux): posix_openpt + fork + setsid + execvp ----------

static ssize_t pty_write_no_sigpipe(int fd, const char *bytes, size_t len) {
    sigset_t set, old_set, pending;
    int blocked = 0, had_pending = 0;
    ssize_t n = -1;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    if (sigprocmask(SIG_BLOCK, &set, &old_set) == 0) {
        blocked = 1;
        if (sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1)
            had_pending = 1;
    }
    do {
        n = write(fd, bytes, len);
    } while (n < 0 && errno == EINTR);
    if (blocked && n < 0 && errno == EPIPE && !had_pending) {
        int signo = 0;
        if (sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1)
            (void)sigwait(&set, &signo);
    }
    if (blocked)
        (void)sigprocmask(SIG_SETMASK, &old_set, NULL);
    return n;
}

static void close_fd(int *fd) {
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void pty_child_report_errno(int fd, int error_value) {
    const unsigned char *bytes = (const unsigned char *)&error_value;
    size_t off = 0;
    while (off < sizeof(error_value)) {
        ssize_t count = write(fd, bytes + off, sizeof(error_value) - off);
        if (count > 0) {
            off += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        break;
    }
}

static int pty_signal_session(pid_t pid, int signal_number) {
    if (pid <= 0)
        return 1;
    if (kill(-pid, signal_number) == 0)
        return 1;
    if (errno != ESRCH)
        return 0;
    if (kill(pid, signal_number) == 0 || errno == ESRCH)
        return 1;
    return 0;
}

static int pty_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return 0;
    if ((flags & O_NONBLOCK) != 0)
        return 1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static int pty_set_close_on_exec(int fd) {
#if defined(FD_CLOEXEC)
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0)
        return 0;
    if ((flags & FD_CLOEXEC) != 0)
        return 1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
#else
    (void)fd;
    errno = ENOTSUP;
    return 0;
#endif
}

static void pty_reap_child_blocking(pid_t pid) {
    if (pid <= 0)
        return;
    pid_t result;
    do {
        result = waitpid(pid, NULL, 0);
    } while (result < 0 && errno == EINTR);
}

static void pty_drain(rt_pty_impl *pty) {
    if (!pty || pty->master_fd < 0)
        return;
    for (;;) {
        char chunk[4096];
        ssize_t count = read(pty->master_fd, chunk, sizeof(chunk));
        if (count > 0) {
            if (!buffer_append(&pty->output_buf, chunk, (size_t)count)) {
                rt_trap("Pty: output buffer allocation failed");
                close_fd(&pty->master_fd);
                return;
            }
            continue;
        }
        if (count == 0) {
            // EOF on the master means the child closed the slave (exited).
            close_fd(&pty->master_fd);
            return;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        // A closed slave surfaces as EIO on the master once the child is gone.
        close_fd(&pty->master_fd);
        return;
    }
}

static int64_t decode_exit_status(int status) {
    if (WIFEXITED(status))
        return (int64_t)WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return -(int64_t)WTERMSIG(status);
    return -1;
}

static void pty_poll_internal(rt_pty_impl *pty, int wait) {
    if (!pty || pty->destroyed)
        return;
    pty_drain(pty);
    if (!pty->running || pty->pid <= 0)
        return;
    int status = 0;
    pid_t result;
    for (;;) {
        do {
            result = waitpid(pty->pid, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (!wait || result != 0)
            break;
        pty_drain(pty);
        struct timespec delay = {0, 10000000L};
        (void)nanosleep(&delay, NULL);
    }
    if (result == pty->pid) {
        pty->exit_code = decode_exit_status(status);
        pty->running = 0;
        pty_drain(pty);
    } else if (result < 0 && errno == ECHILD) {
        pty->exit_code = -1;
        pty->running = 0;
        pty_drain(pty);
    } else if (result < 0) {
        pty_set_last_errno("waitpid failed");
    }
}

static void pty_wait_after_sigterm(rt_pty_impl *pty, int grace_ms) {
    if (!pty || grace_ms <= 0)
        return;
    int elapsed = 0;
    while (pty->running && elapsed < grace_ms) {
        pty_poll_internal(pty, 0);
        if (!pty->running)
            break;
        struct timespec delay = {0, 10000000L};
        (void)nanosleep(&delay, NULL);
        elapsed += 10;
    }
}

static int pty_resize_impl(rt_pty_impl *pty, int64_t cols, int64_t rows) {
    if (!pty || pty->master_fd < 0)
        return 0;
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    if (ioctl(pty->master_fd, TIOCSWINSZ, &ws) != 0) {
        pty_set_last_errno("TIOCSWINSZ failed"); // VDOC-214: report backend failure
        return 0;
    }
    return 1;
}

static int64_t pty_write_impl(rt_pty_impl *pty, const char *bytes, size_t len) {
    if (pty->master_fd < 0)
        return -1;
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > (size_t)SSIZE_MAX)
            chunk = (size_t)SSIZE_MAX;
        ssize_t n = pty_write_no_sigpipe(pty->master_fd, bytes + off, chunk);
        if (n <= 0)
            return off > 0 ? (int64_t)off : -1;
        off += (size_t)n;
    }
    return (int64_t)off;
}

static rt_pty_impl *pty_open_impl(
    rt_string program, void *args, rt_string cwd, void *env, int64_t cols, int64_t rows) {
    const char *program_text = NULL;
    const char *cwd_text = NULL;
    size_t program_len = 0, cwd_len = 0;
    if (!program) {
        pty_set_last_error("PTY program is empty");
        return NULL;
    }
    if (!pty_string_cstr_view(program, &program_text, &program_len) || program_len == 0) {
        pty_set_last_error("PTY program is invalid");
        rt_trap("Pty.Open: invalid program");
        return NULL;
    }
    if (cwd) {
        if (!pty_string_cstr_view(cwd, &cwd_text, &cwd_len)) {
            pty_set_last_error("PTY working directory is invalid");
            rt_trap("Pty.Open: invalid working directory");
            return NULL;
        }
        if (cwd_len == 0)
            cwd_text = NULL;
    }
    if (!pty_validate_string_sequence(args, 0, "Pty.Open: invalid argument"))
        return NULL;
    if (!pty_validate_string_sequence(env, 1, "Pty.Open: invalid environment entry"))
        return NULL;

    pty_string_vector argv = build_string_vector(program_text, args, 1);
    if (!argv.values) {
        pty_set_last_error("PTY argv allocation failed");
        rt_trap("Pty.Open: argv allocation failed");
        return NULL;
    }
    pty_string_vector envp;
    memset(&envp, 0, sizeof(envp));
    if (env)
        envp = build_string_vector(NULL, env, 0);
    if (env && !envp.values) {
        free_string_vector(&argv);
        pty_set_last_error("PTY environment allocation failed");
        rt_trap("Pty.Open: environment allocation failed");
        return NULL;
    }

    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
        pty_set_last_errno("posix_openpt/grantpt/unlockpt failed");
        if (master >= 0)
            close(master);
        free_string_vector(&envp);
        free_string_vector(&argv);
        return NULL;
    }

    char slave_name[256];
#if RT_PLATFORM_LINUX
    int pts_rc = ptsname_r(master, slave_name, sizeof(slave_name));
    if (pts_rc != 0) {
        errno = pts_rc;
        pty_set_last_errno("ptsname_r failed");
        close(master);
        free_string_vector(&envp);
        free_string_vector(&argv);
        return NULL;
    }
#else
    {
        const char *sn = ptsname(master);
        if (!sn) {
            pty_set_last_errno("ptsname failed");
            close(master);
            free_string_vector(&envp);
            free_string_vector(&argv);
            return NULL;
        }
        strncpy(slave_name, sn, sizeof(slave_name) - 1);
        slave_name[sizeof(slave_name) - 1] = '\0';
    }
#endif

    // Exec-status pipe: the write end is close-on-exec, so a SUCCESSFUL exec
    // closes it and the parent's read sees EOF. On any pre-exec failure the child
    // writes its errno and _exit(127)s, so the parent can surface the startup
    // failure by returning NULL instead of an apparently valid session that only
    // fails later (VDOC-213).
    int exec_pipe[2] = {-1, -1};
#if RT_PLATFORM_LINUX && defined(O_CLOEXEC)
    int pipe_rc = pipe2(exec_pipe, O_CLOEXEC);
#else
    int pipe_rc = pipe(exec_pipe);
#endif
    if (pipe_rc != 0) {
        pty_set_last_errno("pipe failed");
        close(master);
        free_string_vector(&envp);
        free_string_vector(&argv);
        return NULL;
    }
#if defined(FD_CLOEXEC) && !(RT_PLATFORM_LINUX && defined(O_CLOEXEC))
    {
        int f = fcntl(exec_pipe[1], F_GETFD, 0);
        if (f < 0 || fcntl(exec_pipe[1], F_SETFD, f | FD_CLOEXEC) != 0) {
            pty_set_last_errno("setting exec-status close-on-exec failed");
            close(exec_pipe[0]);
            close(exec_pipe[1]);
            close(master);
            free_string_vector(&envp);
            free_string_vector(&argv);
            return NULL;
        }
    }
#endif

    pid_t pid = fork();
    if (pid < 0) {
        pty_set_last_errno("fork failed");
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        close(master);
        free_string_vector(&envp);
        free_string_vector(&argv);
        return NULL;
    }

    if (pid == 0) {
        // CHILD — only async-signal-safe calls until exec.
        close(exec_pipe[0]);
        if (setsid() < 0) {
            pty_child_report_errno(exec_pipe[1], errno);
            _exit(127);
        }
        int slave = open(slave_name, O_RDWR);
        if (slave < 0) {
            int e = errno;
            pty_child_report_errno(exec_pipe[1], e);
            _exit(127);
        }
#if defined(TIOCSCTTY)
        if (ioctl(slave, TIOCSCTTY, 0) != 0) {
            int e = errno;
            pty_child_report_errno(exec_pipe[1], e);
            _exit(127);
        }
#endif
        struct winsize ws;
        memset(&ws, 0, sizeof(ws));
        ws.ws_col = (unsigned short)cols;
        ws.ws_row = (unsigned short)rows;
        if (ioctl(slave, TIOCSWINSZ, &ws) != 0 || dup2(slave, STDIN_FILENO) < 0 ||
            dup2(slave, STDOUT_FILENO) < 0 || dup2(slave, STDERR_FILENO) < 0) {
            int e = errno;
            pty_child_report_errno(exec_pipe[1], e);
            _exit(127);
        }
        if (slave > STDERR_FILENO)
            close(slave);
        close(master);
        if (cwd_text && chdir(cwd_text) != 0) {
            int e = errno;
            pty_child_report_errno(exec_pipe[1], e);
            _exit(127);
        }
        // Always PATH-search bare program names. For an explicit environment,
        // adopt it as `environ` first so execvp searches ITS PATH and passes it
        // to the child — previously the explicit-env branch used execve, which
        // does no PATH search, so adding an environment turned a searchable bare
        // name into exit code 127 (VDOC-213). This matches Process.Start*.
        if (envp.values)
            environ = envp.values;
        execvp(program_text, argv.values);
        // exec failed: report errno to the parent (the CLOEXEC write end is
        // still open because exec did not run).
        {
            int e = errno;
            pty_child_report_errno(exec_pipe[1], e);
        }
        _exit(127);
    }

    // PARENT
    close(exec_pipe[1]);
    // Block until the child either reports a startup error or successfully
    // exec's (closing the CLOEXEC write end -> EOF). Retry on EINTR.
    int child_errno = 0;
    size_t status_bytes = 0;
    while (status_bytes < sizeof(child_errno)) {
        ssize_t count = read(exec_pipe[0],
                             (unsigned char *)&child_errno + status_bytes,
                             sizeof(child_errno) - status_bytes);
        if (count > 0) {
            status_bytes += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            status_bytes = sizeof(child_errno) + 1u;
        break;
    }
    close(exec_pipe[0]);
    if (status_bytes != 0) {
        // The child failed before/at exec (e.g. program not found on PATH).
        pty_reap_child_blocking(pid);
        close(master);
        free_string_vector(&envp);
        free_string_vector(&argv);
        if (status_bytes == sizeof(child_errno)) {
            errno = child_errno;
            pty_set_last_errno("child exec failed");
        } else {
            pty_set_last_error("child exec status protocol failed");
        }
        return NULL;
    }

    if (!pty_set_nonblocking(master) || !pty_set_close_on_exec(master)) {
        int setup_errno = errno;
        (void)pty_signal_session(pid, SIGTERM);
        pty_reap_child_blocking(pid);
        close(master);
        free_string_vector(&envp);
        free_string_vector(&argv);
        errno = setup_errno;
        pty_set_last_errno("PTY master descriptor setup failed");
        return NULL;
    }
    free_string_vector(&envp);
    free_string_vector(&argv);

    rt_pty_impl *pty = pty_alloc();
    if (!pty) {
        (void)pty_signal_session(pid, SIGTERM);
        pty_reap_child_blocking(pid);
        close(master);
        return NULL;
    }
    pty->started = 1;
    pty->running = 1;
    pty->pid = pid;
    pty->master_fd = master;
    return pty;
}

static void pty_close(rt_pty_impl *pty) {
    if (!pty || pty->destroyed)
        return;
    if (pty->running && pty->pid > 0) {
        if (!pty_signal_session(pty->pid, SIGTERM))
            pty_set_last_errno("PTY SIGTERM failed");
        pty_wait_after_sigterm(pty, 500);
        if (pty->running) {
            if (!pty_signal_session(pty->pid, SIGKILL))
                pty_set_last_errno("PTY SIGKILL failed");
            pty_poll_internal(pty, 1);
        }
    } else {
        pty_poll_internal(pty, 0);
    }
    pty_drain(pty);
    close_fd(&pty->master_fd);
    buffer_free(&pty->output_buf);
    pty->running = 0;
    pty->started = 0;
    pty->destroyed = 1;
}

static int64_t pty_supported(void) {
    return 1;
}

#endif
// ===========================================================================

static void pty_finalize(void *obj) {
    pty_close((rt_pty_impl *)obj);
}

void *rt_pty_open(
    rt_string program, void *args, rt_string cwd, void *env, int64_t cols, int64_t rows) {
    pty_set_last_error(NULL);
    pty_clamp_size(&cols, &rows);
    void *handle = pty_open_impl(program, args, cwd, env, cols, rows);
    if (handle)
        pty_set_last_error(NULL);
    return handle;
}

/// @brief Wrap a PTY open attempt in a Result object.
/// @details Validation traps from rt_pty_open() are converted into Err strings
///          so callers can handle startup failures without consulting the
///          process-global LastError side channel.
/// @param program Program path.
/// @param args Argument sequence, or NULL.
/// @param cwd Working directory, or NULL/empty.
/// @param env Environment sequence, or NULL.
/// @param cols Initial terminal columns.
/// @param rows Initial terminal rows.
/// @return Owned `Zanna.Result` carrying a PtySession or an error string.
void *rt_pty_open_result(
    rt_string program, void *args, rt_string cwd, void *env, int64_t cols, int64_t rows) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        const char *err = rt_trap_get_error();
        rt_trap_clear_recovery();
        return rt_result_err_str(rt_const_cstr(err && err[0] ? err : "Pty.Open failed"));
    }

    void *handle = rt_pty_open(program, args, cwd, env, cols, rows);
    rt_trap_clear_recovery();
    if (!handle) {
        rt_string err = rt_pty_last_error();
        if (!err || rt_str_len(err) == 0) {
            rt_str_release_maybe(err);
            return rt_result_err_str(rt_const_cstr("Pty.Open failed"));
        }
        void *result = rt_result_err_str(err);
        rt_str_release_maybe(err);
        return result;
    }

    void *result = rt_result_ok(handle);
    if (rt_obj_release_check0(handle))
        rt_obj_free(handle);
    return result;
}

int64_t rt_pty_is_supported(void) {
    int64_t supported = pty_supported();
    if (!supported && pty_last_error[0] == '\0')
        pty_set_last_error("PTY support is not available on this platform");
    return supported;
}

rt_string rt_pty_last_error(void) {
    if (pty_last_error[0] == '\0')
        return empty_string();
    return rt_string_from_bytes(pty_last_error, strlen(pty_last_error));
}

int64_t rt_pty_is_valid(void *handle) {
    rt_pty_impl *pty = pty_checked(handle);
    return pty && pty->started && !pty->destroyed ? 1 : 0;
}

int64_t rt_pty_poll(void *handle) {
    rt_pty_impl *pty = pty_checked(handle);
    if (!pty || !pty->started || pty->destroyed)
        return 0;
    pty_poll_internal(pty, 0);
    return pty->running ? 1 : 0;
}

int64_t rt_pty_is_running(void *handle) {
    return rt_pty_poll(handle);
}

rt_string rt_pty_read(void *handle) {
    rt_pty_impl *pty = pty_checked(handle);
    if (!pty || !pty->started || pty->destroyed)
        return empty_string();
    pty_drain(pty);
    return buffer_take(&pty->output_buf);
}

void *rt_pty_read_result(void *handle) {
    rt_pty_impl *pty = pty_checked(handle);
    if (!pty || !pty->started || pty->destroyed)
        return buffer_take_result(NULL);
    pty_drain(pty);
    return buffer_take_result(&pty->output_buf);
}

int64_t rt_pty_write(void *handle, rt_string data) {
    rt_pty_impl *pty = pty_checked(handle);
    if (!pty || !pty->started || pty->destroyed)
        return -1;
    const char *bytes = data ? rt_string_cstr(data) : "";
    size_t len = data ? (size_t)rt_str_len(data) : 0;
    if (len == 0)
        return 0;
    return pty_write_impl(pty, bytes, len);
}

int64_t rt_pty_resize(void *handle, int64_t cols, int64_t rows) {
    rt_pty_impl *pty = pty_checked(handle);
    if (!pty || !pty->started || pty->destroyed)
        return 0;
    pty_clamp_size(&cols, &rows);
    // Propagate the backend result: TRUE only when the OS actually applied the
    // new size (TIOCSWINSZ / ResizePseudoConsole succeeded). Failures set the
    // PTY LastError (VDOC-214).
    return pty_resize_impl(pty, cols, rows) ? 1 : 0;
}

int64_t rt_pty_exit_code(void *handle) {
    rt_pty_impl *pty = pty_checked(handle);
    if (!pty || !pty->started || pty->destroyed)
        return -1;
    pty_poll_internal(pty, 0);
    return pty->running ? -1 : pty->exit_code;
}

int64_t rt_pty_kill(void *handle) {
    rt_pty_impl *pty = pty_checked(handle);
    if (!pty || !pty->started || pty->destroyed || !pty->running)
        return 0;
#if defined(_WIN32)
    if (!pty->process)
        return 0;
    return TerminateProcess(pty->process, 1) ? 1 : 0;
#else
    if (pty->pid <= 0)
        return 0;
    if (kill(-pty->pid, SIGTERM) == 0)
        return 1;
    if (errno == ESRCH)
        return kill(pty->pid, SIGTERM) == 0 ? 1 : 0;
    return 0;
#endif
}

int64_t rt_pty_wait(void *handle) {
    rt_pty_impl *pty = pty_checked(handle);
    if (!pty || !pty->started || pty->destroyed)
        return -1;
    pty_poll_internal(pty, 1);
    return pty->exit_code;
}

void rt_pty_destroy(void *handle) {
    rt_pty_impl *pty = pty_checked(handle);
    pty_close(pty);
}
