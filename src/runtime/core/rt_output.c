//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_output.c
// Purpose: Implements centralized stdout buffering for the Viper runtime to
//          dramatically reduce syscall overhead during terminal rendering.
//          Enables full buffering (setvbuf _IOFBF) and provides batch mode to
//          defer flushes until natural frame boundaries.
//
// Key invariants:
//   - Initialization (rt_output_init) is idempotent and uses double-checked
//     locking with acquire/release atomics; concurrent callers are safe.
//   - Once initialized, stdout is set to fully-buffered mode with a 16 KB
//     internal buffer; output is flushed only on explicit rt_output_flush or
//     buffer-full conditions.
//   - Batch mode is reference-counted (g_batch_mode_depth); nested begin/end
//     calls work correctly — only the outermost end triggers a flush.
//   - rt_output_str / rt_output_char / rt_output_bytes write to stdout without
//     an implicit flush; callers must call rt_output_flush at frame boundaries.
//
// Ownership/Lifetime:
//   - The internal output buffer (g_output_buffer) is a process-global static
//     array registered with setvbuf; it must remain valid for the process
//     lifetime (guaranteed because it is static).
//   - No heap allocation is performed by this module.
//
// Links: src/runtime/core/rt_output.h (public API),
//        src/runtime/core/rt_term.c (terminal control, uses rt_output),
//        src/runtime/core/rt_io.c (higher-level PRINT/INPUT primitives)
//
//===----------------------------------------------------------------------===//

#include "rt_output.h"

#include "rt_atomic_compat.h"
#include "rt_platform.h"
#include "rt_trap.h"

#if RT_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif !RT_PLATFORM_VIPERDOS
#include <sched.h>
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Size of the stdout buffer.
/// @details 16KB is sufficient for several full screens of output.
///          Larger buffers reduce flush frequency but increase memory usage.
#define RT_OUTPUT_BUFFER_SIZE 16384

/// @brief Internal stdout buffer for full buffering mode.
static char g_output_buffer[RT_OUTPUT_BUFFER_SIZE];

/// @brief Atomic init state: 0=uninit, 1=initializing, 2=done.
/// @details Uses double-checked locking (same pattern as rt_context.c) so that
///          concurrent calls to rt_output_init are safe without a mutex.
static int g_output_init_state = 0;

/// @brief Reference count for nested batch mode calls (atomic).
/// @details Allows nested begin/end batch calls to work correctly across threads.
static int g_batch_mode_depth = 0;

/// @brief Whether an exit-time stdout flush has been registered.
static int g_output_exit_handler_registered = 0;

/// @brief Process-global runtime stdout capture callback.
/// @details The runtime output layer is already process-global, so capture uses
///          the same scope. The REPL installs this only around one VM execution.
static rt_output_capture_hook g_output_capture_hook = {NULL, NULL};

/// @brief Spinlock protecting capture-hook replacement and snapshot reads.
/// @details The callback pointer and opaque context must be observed as one
///          consistent pair.  A small spinlock is sufficient because hook
///          changes are rare and snapshot reads copy only two machine words.
static int g_output_capture_lock;

/// @brief Yield the current thread while waiting for a rare output lock.
static void rt_output_yield_(void) {
#if RT_PLATFORM_WINDOWS
    SwitchToThread();
#elif !RT_PLATFORM_VIPERDOS
    sched_yield();
#endif
}

/// @brief Acquire the output capture spinlock.
static void rt_output_capture_lock_(void) {
    if (__atomic_test_and_set(&g_output_capture_lock, __ATOMIC_ACQUIRE)) {
        do {
            rt_output_yield_();
        } while (__atomic_test_and_set(&g_output_capture_lock, __ATOMIC_ACQUIRE));
    }
}

/// @brief Release the output capture spinlock.
static void rt_output_capture_unlock_(void) {
    __atomic_clear(&g_output_capture_lock, __ATOMIC_RELEASE);
}

/// @brief atexit callback: flush buffered stdout at process exit.
static void rt_output_flush_at_exit_(void) {
    rt_output_flush();
}

#if RT_PLATFORM_LINUX
extern int __cxa_atexit(void (*func)(void *), void *arg, void *dso_handle);

/// @brief __cxa_atexit trampoline wrapping rt_output_flush_at_exit_ to the
///        void(*)(void*) callback signature (Linux only).
static void rt_output_flush_at_exit_adapter_(void *arg) {
    (void)arg;
    rt_output_flush_at_exit_();
}

/// @brief Register the exit-time stdout flush handler.
/// @details Linux routes through libc's __cxa_atexit() (plain atexit() is not
///          reliably available for late-bound native executables); other
///          platforms use atexit(). @return 0 on success.
static int rt_output_register_exit_handler_(void) {
    return __cxa_atexit(rt_output_flush_at_exit_adapter_, NULL, NULL);
}
#else
static int rt_output_register_exit_handler_(void) {
    return atexit(rt_output_flush_at_exit_);
}
#endif

/// @brief Write @p len raw bytes to the platform's standard-output stream.
/// @details Two implementations live behind the `RT_PLATFORM_WINDOWS` macro: Win32 uses
///          `WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ...)` to avoid CRT translation,
///          POSIX uses `fwrite(stdout)`. The Win32 path chunks at `0xFFFFFFFF` because
///          `WriteFile`'s `nNumberOfBytesToWrite` is `DWORD` so a single `size_t` may
///          exceed it on a 64-bit build. Silent no-op on NULL or zero-length input.
#if RT_PLATFORM_WINDOWS
static void rt_output_write_bytes(const char *s, size_t len) {
    if (!s || len == 0)
        return;

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == NULL || out == INVALID_HANDLE_VALUE)
        return;

    while (len > 0) {
        const DWORD chunk = len > 0xFFFFFFFFu ? 0xFFFFFFFFu : (DWORD)len;
        DWORD written = 0;
        if (!WriteFile(out, s, chunk, &written, NULL) || written == 0)
            return;
        s += written;
        len -= written;
    }
}
#endif

/// @brief Install @p hook as the current runtime output capture target.
/// @details Returns the old hook for scoped restoration. This intentionally does
///          not call @ref rt_output_init because capture should also work before
///          stdout buffering is initialized.
rt_output_capture_hook rt_output_set_capture_hook(rt_output_capture_fn fn, void *ctx) {
    rt_output_capture_lock_();
    rt_output_capture_hook oldHook = g_output_capture_hook;
    g_output_capture_hook.fn = fn;
    g_output_capture_hook.ctx = ctx;
    rt_output_capture_unlock_();
    return oldHook;
}

/// @brief Try to deliver bytes to the active capture hook.
/// @details Returns non-zero when a hook consumed the output. A null byte range
///          or zero-length write is treated as consumed because there is nothing
///          left for stdout to do.
static int rt_output_try_capture_(const char *s, size_t len) {
    if (!s || len == 0)
        return 1;
    rt_output_capture_lock_();
    rt_output_capture_hook hook = g_output_capture_hook;
    rt_output_capture_unlock_();
    if (!hook.fn)
        return 0;
    hook.fn(s, len, hook.ctx);
    return 1;
}

/// @brief Initialize stdout buffering (idempotent, thread-safe).
/// @details Switches stdout to full buffering with a 16KB static buffer.
///          Uses double-checked locking so concurrent callers are safe.
void rt_output_init(void) {
    if (__atomic_load_n(&g_output_init_state, __ATOMIC_ACQUIRE) == 2)
        return;

    int expected = 0;
    if (__atomic_compare_exchange_n(
            &g_output_init_state, &expected, 1, /*weak=*/0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
#if RT_PLATFORM_WINDOWS
        // Native-linked Windows executables enter directly through Viper's
        // startup shim, bypassing CRT startup. Avoid configuring CRT stdio in
        // that mode; writes below go straight to the OS handle.
#else
        // Configure stdout for full buffering with our internal buffer.
        // _IOFBF = full buffering: output is written when buffer is full or fflush() is called.
        // This is the key change that reduces system calls.
        setvbuf(stdout, g_output_buffer, _IOFBF, RT_OUTPUT_BUFFER_SIZE);
        if (!g_output_exit_handler_registered && rt_output_register_exit_handler_() == 0)
            g_output_exit_handler_registered = 1;
#endif
        __atomic_store_n(&g_output_init_state, 2, __ATOMIC_RELEASE);
        return;
    }

    // Another thread is initializing; spin until done.
    while (__atomic_load_n(&g_output_init_state, __ATOMIC_ACQUIRE) != 2) {
        rt_output_yield_();
    }
}

/// @brief Write a null-terminated string to stdout without flushing.
void rt_output_str(const char *s) {
    if (!s)
        return;
    if (rt_output_try_capture_(s, strlen(s)))
        return;
    rt_output_init();
#if RT_PLATFORM_WINDOWS
    rt_output_write_bytes(s, strlen(s));
#else
    fputs(s, stdout);
#endif
}

/// @brief Write exactly @p len bytes from @p s to stdout without flushing.
void rt_output_strn(const char *s, size_t len) {
    if (!s || len == 0)
        return;
    if (rt_output_try_capture_(s, len))
        return;
    rt_output_init();
#if RT_PLATFORM_WINDOWS
    rt_output_write_bytes(s, len);
#else
    fwrite(s, 1, len, stdout);
#endif
}

/// @brief Flush the stdout buffer immediately.
void rt_output_flush(void) {
#if !RT_PLATFORM_WINDOWS
    fflush(stdout);
#endif
}

/// @brief Enter batch mode — defer all flushes until the matching end_batch.
/// @details Increments a reference counter. Nested calls are supported.
void rt_output_begin_batch(void) {
    int cur = __atomic_load_n(&g_batch_mode_depth, __ATOMIC_ACQUIRE);
    for (;;) {
        if (cur == INT_MAX) {
            rt_trap("rt_output_begin_batch: batch depth overflow");
            return;
        }
        int next = cur + 1;
        if (__atomic_compare_exchange_n(&g_batch_mode_depth,
                                        &cur,
                                        next,
                                        /*weak=*/0,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE))
            return;
    }
}

/// @brief Exit batch mode — flush stdout when the outermost batch ends.
/// @details Decrements the reference counter. Only the outermost end triggers
///          a flush. Unbalanced end calls (without matching begin) are no-ops.
void rt_output_end_batch(void) {
    int cur = __atomic_load_n(&g_batch_mode_depth, __ATOMIC_ACQUIRE);
    for (;;) {
        if (cur <= 0)
            return;
        int next = cur - 1;
        if (__atomic_compare_exchange_n(&g_batch_mode_depth,
                                        &cur,
                                        next,
                                        /*weak=*/0,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            if (next == 0) {
#if !RT_PLATFORM_WINDOWS
                fflush(stdout);
#endif
            }
            return;
        }
    }
}

/// @brief Return non-zero if batch mode is currently active.
int8_t rt_output_is_batch_mode(void) {
    return __atomic_load_n(&g_batch_mode_depth, __ATOMIC_ACQUIRE) > 0;
}

/// @brief Flush stdout only if not in batch mode.
/// @details Used by PRINT/SAY functions that want immediate output when running
///          interactively but deferred output during canvas rendering loops.
void rt_output_flush_if_not_batch(void) {
    if (__atomic_load_n(&g_batch_mode_depth, __ATOMIC_ACQUIRE) == 0) {
#if !RT_PLATFORM_WINDOWS
        fflush(stdout);
#endif
    }
}
