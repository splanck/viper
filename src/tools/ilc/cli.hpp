// File: src/tools/ilc/cli.hpp
// Purpose: Declarations for ilc subcommand handlers and usage helper.
// Key invariants: None.
// Ownership/Lifetime: N/A.
// Links: docs/class-catalog.md

#pragma once

/// @brief Handle `ilc front basic` subcommands.
///
/// Invoked when the command line begins with `ilc front basic`. After the
/// subcommand tokens are consumed, `argc` and `argv` contain the remaining
/// arguments specific to the BASIC front end.
///
/// @param argc Number of arguments following `front basic`.
/// @param argv Array of argument strings.
/// @return `0` on successful compilation or execution, non‑zero on errors such
///         as parse failures or runtime traps.
///
/// @note May emit IL to `stdout` or run the resulting module via the VM and can
/// redirect `stdin` when `--stdin-from` is provided.
int cmdFrontBasic(int argc, char **argv);

/// @brief Handle `ilc -run`.
///
/// Triggered when `ilc` receives the `-run` option. `argv[0]` should hold the
/// path to an IL file and the remaining elements provide optional flags such as
/// tracing or debugger control.
///
/// @param argc Number of arguments following `-run`.
/// @param argv Argument vector beginning with the IL file path.
/// @return Exit code of the executed program or `1` when validation fails.
///
/// @note May modify `stdin`, emit trace/debug information to `stderr`, and
/// collect execution statistics.
int cmdRunIL(int argc, char **argv);

/// @brief Handle `ilc il-opt`.
///
/// Invoked for the `ilc il-opt` subcommand. `argv[0]` supplies the input IL
/// file while subsequent arguments select passes and the output file.
///
/// @param argc Number of arguments following `ilc il-opt`.
/// @param argv Argument vector beginning with the input IL file.
/// @return `0` on success or `1` if arguments are malformed or file operations
///         fail.
///
/// @note Writes the optimized module to disk and may print pass statistics to
/// `stdout`.
int cmdILOpt(int argc, char **argv);

/// @brief Print usage information for ilc.
///
/// Called when no or invalid arguments are supplied to `ilc` or when a handler
/// needs to display help text.
///
/// @return void
/// @note Writes a synopsis and option hints to `stderr` and has no other side
/// effects.
void usage();
