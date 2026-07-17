//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/system/rt_exec.h
// Purpose: External command execution for Zanna.System.Exec, providing Run, Capture, and Shell
// variants with argument arrays and exit code capture.
//
// Key invariants:
//   - Run/RunArgs wait for process completion and return the exit code.
//   - Capture/CaptureArgs capture stdout as a string; stderr is discarded.
//   - Shell/ShellCapture pass the command string directly to the platform shell.
//   - SECURITY: Never pass unsanitized user input to Shell functions; shell injection risk.
//
// Ownership/Lifetime:
//   - Returned strings from Capture functions are newly allocated; caller must release.
//   - No heap allocation for Run/RunArgs; exit code returned directly.
//
// Links: src/runtime/system/rt_exec.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#define RT_EXEC_COMMAND_RESULT_CLASS_ID INT64_C(-0x440203)

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Execute a program and wait for completion.
/// @param program Program path as runtime string.
/// @return Exit code of the program.
int64_t rt_exec_run(rt_string program);

/// @brief Execute a program and capture stdout.
/// @param program Program path as runtime string.
/// @return Captured stdout as string.
rt_string rt_exec_capture(rt_string program);

/// @brief Execute a program with arguments and wait.
/// @param program Program path as runtime string.
/// @param args Seq of argument strings.
/// @return Exit code of the program.
int64_t rt_exec_run_args(rt_string program, void *args);

/// @brief Execute a program with arguments and capture stdout.
/// @param program Program path as runtime string.
/// @param args Seq of argument strings.
/// @return Captured stdout as string.
rt_string rt_exec_capture_args(rt_string program, void *args);

/// @brief Execute a command through the system shell.
/// @param command Shell command string.
/// @return Exit code from the shell.
/// @warning Do not pass unsanitized user input - shell injection risk.
int64_t rt_exec_shell(rt_string command);

/// @brief Execute a shell command and capture stdout.
/// @param command Shell command string.
/// @return Captured stdout as string.
/// @warning Do not pass unsanitized user input - shell injection risk.
rt_string rt_exec_shell_capture(rt_string command);

/// @brief Execute a shell command, capture stdout, and record the exit code
///        for retrieval via rt_exec_last_exit_code().
/// @param command Shell command string (passed to /bin/sh -c).
/// @return Captured stdout as string. To include stderr, append "2>&1" to
///         the command (the caller controls stream merging, same as ShellCapture).
/// @note The exit code is stored per-thread; call rt_exec_last_exit_code()
///       immediately after to retrieve it before the next exec call.
/// @warning Do not pass unsanitized user input - shell injection risk.
rt_string rt_exec_shell_full(rt_string command);

/// @brief Execute a shell command and return output plus exit code together.
/// @details This is the side-channel-free replacement for calling
///          rt_exec_shell_full() followed by rt_exec_last_exit_code().
/// @param command Shell command string (passed to /bin/sh -c or cmd /c).
/// @return Opaque Zanna.System.CommandResult object containing stdout and exit code.
/// @warning Do not pass unsanitized user input - shell injection risk.
void *rt_exec_shell_result(rt_string command);

/// @brief Return the exit code from the most recent rt_exec_shell_full() call.
/// @return Exit code (0 = success), or -1 if rt_exec_shell_full was never called.
int64_t rt_exec_last_exit_code(void);

/// @brief Return the captured stdout from a CommandResult.
/// @param result Opaque Zanna.System.CommandResult object.
/// @return Retained runtime string containing captured stdout.
rt_string rt_exec_command_result_output(void *result);

/// @brief Return the exit code from a CommandResult.
/// @param result Opaque Zanna.System.CommandResult object.
/// @return Process exit code, or -1 for invalid input.
int64_t rt_exec_command_result_exit_code(void *result);

/// @brief Return whether a CommandResult exited successfully.
/// @param result Opaque Zanna.System.CommandResult object.
/// @return 1 when ExitCode is 0, otherwise 0.
int8_t rt_exec_command_result_succeeded(void *result);

#ifdef __cplusplus
}
#endif
