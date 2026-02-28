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

#include <stdio.h>
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

void rt_output_init(void)
{
    if (__atomic_load_n(&g_output_init_state, __ATOMIC_ACQUIRE) == 2)
        return;

    int expected = 0;
    if (__atomic_compare_exchange_n(
            &g_output_init_state, &expected, 1, /*weak=*/0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        // Configure stdout for full buffering with our internal buffer.
        // _IOFBF = full buffering: output is written when buffer is full or fflush() is called.
        // This is the key change that reduces system calls.
        setvbuf(stdout, g_output_buffer, _IOFBF, RT_OUTPUT_BUFFER_SIZE);
        __atomic_store_n(&g_output_init_state, 2, __ATOMIC_RELEASE);
        return;
    }

    // Another thread is initializing; spin until done.
    while (__atomic_load_n(&g_output_init_state, __ATOMIC_ACQUIRE) != 2)
    {
        // spin
    }
}

void rt_output_str(const char *s)
{
    if (!s)
        return;
    fputs(s, stdout);
}

void rt_output_strn(const char *s, size_t len)
{
    if (!s || len == 0)
        return;
    fwrite(s, 1, len, stdout);
}

void rt_output_flush(void)
{
    fflush(stdout);
}

void rt_output_begin_batch(void)
{
    __atomic_fetch_add(&g_batch_mode_depth, 1, __ATOMIC_ACQ_REL);
}

void rt_output_end_batch(void)
{
    int prev = __atomic_fetch_sub(&g_batch_mode_depth, 1, __ATOMIC_ACQ_REL);
    if (prev <= 1)
    {
        // Exiting outermost batch mode: flush accumulated output
        fflush(stdout);
    }
}

int rt_output_is_batch_mode(void)
{
    return __atomic_load_n(&g_batch_mode_depth, __ATOMIC_ACQUIRE) > 0;
}

void rt_output_flush_if_not_batch(void)
{
    if (__atomic_load_n(&g_batch_mode_depth, __ATOMIC_ACQUIRE) == 0)
    {
        fflush(stdout);
    }
}
