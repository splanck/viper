//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/system/rt_process.h
// Purpose: Streaming child-process handles for Viper.System.Process.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#define RT_PROCESS_CLASS_ID INT64_C(-0x440201)

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Start a child process with inherited cwd and environment.
/// @param program Program path as a runtime string.
/// @param args Seq of argument strings, or NULL for no arguments.
/// @return Process handle, or NULL when the process could not be started.
void *rt_process_start(rt_string program, void *args);

/// @brief Start a child process in a working directory with inherited environment.
/// @param program Program path as a runtime string.
/// @param args Seq of argument strings, or NULL for no arguments.
/// @param cwd Working directory, or empty/NULL to inherit.
/// @return Process handle, or NULL when the process could not be started.
void *rt_process_start_in(rt_string program, void *args, rt_string cwd);

/// @brief Start a child process with an explicit cwd and environment.
/// @param program Program path as a runtime string.
/// @param args Seq of argument strings, or NULL for no arguments.
/// @param cwd Working directory, or empty/NULL to inherit.
/// @param env Seq of KEY=value strings, or NULL to inherit.
/// @return Process handle, or NULL when the process could not be started.
void *rt_process_start_with_env(rt_string program, void *args, rt_string cwd, void *env);

/// @brief Return TRUE when @p handle is a live process handle object.
int64_t rt_process_is_valid(void *handle);

/// @brief Poll the process and return TRUE while it is still running.
int64_t rt_process_poll(void *handle);

/// @brief Return TRUE while the process is still running.
int64_t rt_process_is_running(void *handle);

/// @brief Read currently buffered stdout plus any immediately available bytes.
rt_string rt_process_read_stdout(void *handle);

/// @brief Read currently buffered stderr plus any immediately available bytes.
rt_string rt_process_read_stderr(void *handle);

/// @brief Write bytes to the child's stdin pipe.
/// @param handle Process handle.
/// @param data Bytes to write (runtime string; may contain newlines).
/// @return Number of bytes written, or -1 when the handle is invalid, the child
///         has no stdin pipe, or the write failed (e.g. the child closed stdin).
int64_t rt_process_write_stdin(void *handle, rt_string data);

/// @brief Return the exit code, or -1 if the process has not exited or the handle is invalid.
int64_t rt_process_exit_code(void *handle);

/// @brief Request process termination.
/// @return TRUE when a termination request was sent.
int64_t rt_process_kill(void *handle);

/// @brief Wait for process exit and return the exit code.
int64_t rt_process_wait(void *handle);

/// @brief Release OS resources held by a process handle.
void rt_process_destroy(void *handle);

#ifdef __cplusplus
}
#endif
