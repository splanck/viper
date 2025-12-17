//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_exec.h
// Purpose: External command execution for Viper.Exec.
//
// Exec provides functions to run external programs:
// - Run/RunArgs: Execute program and wait, return exit code
// - Capture/CaptureArgs: Execute and capture stdout
// - Shell/ShellCapture: Run through system shell
//
// SECURITY NOTE: Shell functions pass commands directly to the shell.
// Never pass unsanitized user input to Shell/ShellCapture as this
// creates shell injection vulnerabilities.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
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

#ifdef __cplusplus
}
#endif
