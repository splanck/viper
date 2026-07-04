//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/system/rt_pty.h
// Purpose: Pseudo-terminal-backed child handles for Viper.System.Pty — an
//          interactive terminal surface (merged ANSI output, resize) distinct
//          from the pipe-based Viper.System.Process.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#define RT_PTY_CLASS_ID INT64_C(-0x440202)

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Open a PTY-backed child process.
/// @param program Program path as a runtime string.
/// @param args Seq of argument strings, or NULL for no arguments.
/// @param cwd Working directory, or empty/NULL to inherit.
/// @param env Seq of KEY=value strings, or NULL to inherit the environment.
/// @param cols Initial terminal columns (clamped to [1,4096]; 0 -> 80).
/// @param rows Initial terminal rows (clamped to [1,4096]; 0 -> 24).
/// @return PTY session handle, or NULL when a PTY could not be created
///         (including platforms without PTY support — see rt_pty_is_supported).
void *rt_pty_open(
    rt_string program, void *args, rt_string cwd, void *env, int64_t cols, int64_t rows);

/// @brief Open a PTY-backed child process and return a Viper.Result.
/// @details Returns `Ok(PtySession)` when the child session starts and
///          `Err(message)` when the platform lacks PTY support, startup fails,
///          or validation traps would have been raised by rt_pty_open().
/// @param program Program path as a runtime string.
/// @param args Seq of argument strings, or NULL for no arguments.
/// @param cwd Working directory, or empty/NULL to inherit.
/// @param env Seq of KEY=value strings, or NULL to inherit the environment.
/// @param cols Initial terminal columns (clamped to [1,4096]; 0 -> 80).
/// @param rows Initial terminal rows (clamped to [1,4096]; 0 -> 24).
/// @return Opaque Viper.Result object containing a PTY session or error string.
void *rt_pty_open_result(
    rt_string program, void *args, rt_string cwd, void *env, int64_t cols, int64_t rows);

/// @brief Return TRUE when the runtime can create PTY sessions on this platform.
/// @note Always true on POSIX; on Windows it reflects ConPTY availability
///       (Windows 10 1809+); false on ViperDOS.
int64_t rt_pty_is_supported(void);

/// @brief Return the most recent PTY startup/support error for this process.
/// @details This diagnostic string is intended for UI/reporting after
///          rt_pty_open returns NULL or rt_pty_is_supported returns false.
///          Successful rt_pty_open calls clear it.
/// @return Runtime string containing the last PTY error, or empty when none.
rt_string rt_pty_last_error(void);

/// @brief Return TRUE when @p handle is a live PTY session object.
int64_t rt_pty_is_valid(void *handle);

/// @brief Poll the child and return TRUE while it is still running.
int64_t rt_pty_poll(void *handle);

/// @brief Return TRUE while the child is still running.
int64_t rt_pty_is_running(void *handle);

/// @brief Read currently buffered terminal output plus immediately available bytes.
/// @note The PTY merges stdout and stderr; the result may contain ANSI escapes.
rt_string rt_pty_read(void *handle);

/// @brief Read terminal output as `{ text, truncated }` without trapping on truncation.
void *rt_pty_read_result(void *handle);

/// @brief Write bytes to the terminal input.
/// @return Bytes written, or -1 when the handle is invalid or the write failed.
int64_t rt_pty_write(void *handle, rt_string data);

/// @brief Resize the terminal window (delivers SIGWINCH to the child on POSIX).
/// @return TRUE when the resize request was issued.
int64_t rt_pty_resize(void *handle, int64_t cols, int64_t rows);

/// @brief Return the exit code, or -1 if the child has not exited or is invalid.
int64_t rt_pty_exit_code(void *handle);

/// @brief Request child termination. @return TRUE when a request was sent.
int64_t rt_pty_kill(void *handle);

/// @brief Wait for child exit and return the exit code.
int64_t rt_pty_wait(void *handle);

/// @brief Release OS resources held by a PTY session handle.
void rt_pty_destroy(void *handle);

#ifdef __cplusplus
}
#endif
