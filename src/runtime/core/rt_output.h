//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_output.h
// Purpose: Centralized output buffering layer for improved terminal rendering performance, accumulating stdout writes and flushing at strategic points to minimize system calls.
//
// Key invariants:
//   - rt_output_init is idempotent; safe to call multiple times.
//   - Batch mode accumulates output; rt_output_flush_batch sends all buffered data.
//   - Auto-flush on newline is configurable; default matches BASIC's PRINT semantics.
//   - Thread safety for batch mode control is provided by internal locking.
//
// Ownership/Lifetime:
//   - No ownership transfer; functions accept null-terminated C string pointers.
//   - The internal buffer is owned by the module; callers must not free it.
//
// Links: src/runtime/core/rt_output.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Initialize output buffering for stdout.
    /// @details Configures stdout with full buffering using an internal buffer.
    ///          Should be called once at program startup. Safe to call multiple
    ///          times; subsequent calls are no-ops.
    void rt_output_init(void);

    /// @brief Write a string to the output buffer.
    /// @param s Null-terminated string to write.
    /// @details Writes to stdout without flushing unless auto-flush is enabled
    ///          and a newline is encountered.
    void rt_output_str(const char *s);

    /// @brief Write a string with explicit length to the output buffer.
    /// @param s String data (not necessarily null-terminated).
    /// @param len Number of bytes to write.
    void rt_output_strn(const char *s, size_t len);

    /// @brief Flush any buffered output to the terminal.
    /// @details Forces all pending output to be written. Call this before
    ///          operations that need immediate visibility (e.g., before INPUT).
    void rt_output_flush(void);

    /// @brief Begin batch mode for output operations.
    /// @details While in batch mode, terminal control sequences (COLOR, LOCATE,
    ///          etc.) do not trigger individual flushes. Call rt_output_end_batch()
    ///          or rt_output_flush() to flush accumulated output.
    ///          Batch mode is reference-counted, so nested begin/end pairs work correctly.
    void rt_output_begin_batch(void);

    /// @brief End batch mode and optionally flush.
    /// @details Decrements the batch mode reference count. When the count reaches
    ///          zero, flushes all accumulated output.
    void rt_output_end_batch(void);

    /// @brief Check if batch mode is currently active.
    /// @return Non-zero if batch mode is active, zero otherwise.
    int rt_output_is_batch_mode(void);

    /// @brief Flush output only if not in batch mode.
    /// @details Used by terminal control functions to conditionally flush.
    ///          In batch mode, this is a no-op; otherwise it flushes.
    void rt_output_flush_if_not_batch(void);

#ifdef __cplusplus
}
#endif
