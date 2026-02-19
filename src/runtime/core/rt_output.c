//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: runtime/rt_output.c
// Purpose: Implementation of centralized output buffering.
//
// This module dramatically improves terminal rendering performance by:
// 1. Enabling full buffering on stdout (reduces syscalls per write)
// 2. Providing batch mode to group multiple operations
// 3. Deferring flushes until natural boundaries (frame end, input, etc.)
//
// The key insight is that terminal rendering in games typically does:
//   LOCATE y, x → fputs + fflush (2 syscalls)
//   COLOR fg, bg → fputs + fflush (2 syscalls)
//   PRINT char   → fwrite (1 syscall)
// Per cell: 5 syscalls. For 60x20 = 1200 cells = 6000 syscalls/frame
//
// With buffering:
//   All operations → buffer accumulation
//   End of frame → single fflush (1 syscall)
// Result: 6000x reduction in syscalls, no visible flashing.
//
//===----------------------------------------------------------------------===//

#include "rt_output.h"

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
