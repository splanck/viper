// File: src/tools/ilc/cli.hpp
// Purpose: Declarations for ilc subcommand handlers and usage helper.
// Key invariants: None.
// Ownership/Lifetime: N/A.
// Links: docs/codemap.md

#pragma once

#include "vm/Trace.hpp"
#include <cstdint>
#include <string>

namespace il::support
{
class SourceManager;
}

namespace ilc
{

/// @brief Shared configuration for ilc subcommands that execute IL.
struct SharedCliOptions
{
    /// @brief Trace settings requested via --trace flags.
    il::vm::TraceConfig trace{};

    /// @brief Optional replacement for standard input.
    std::string stdinPath{};

    /// @brief Maximum number of interpreter steps (0 means unlimited).
    std::uint64_t maxSteps = 0;

    /// @brief Whether bounds checks should be enabled during lowering.
    bool boundsChecks = false;

    /// @brief Request formatted trap diagnostics on unhandled errors.
    bool dumpTrap = false;
};

/// @brief Result of attempting to parse a shared CLI option.
enum class SharedOptionParseResult
{
    NotMatched, ///< Argument does not correspond to a shared option.
    Parsed,     ///< Argument consumed and reflected in the configuration.
    Error       ///< Argument looked like a shared option but was malformed.
};

/// @brief Parse an ilc option common to multiple subcommands.
///
/// @param index Index of the current argument; advanced when parameters are consumed.
/// @param argc Total number of arguments available.
/// @param argv Argument vector.
/// @param opts Accumulator receiving parsed option values.
/// @return Parsing outcome describing whether the argument was handled.
SharedOptionParseResult parseSharedOption(int &index,
                                          int argc,
                                          char **argv,
                                          SharedCliOptions &opts);

} // namespace ilc

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

/// @brief Handle `ilc -run` with an externally managed source manager.
///
/// Allows tests to preconfigure the @ref il::support::SourceManager used by the
/// run command, enabling overflow scenarios to be triggered deterministically.
///
/// @param argc Number of arguments following `-run`.
/// @param argv Argument vector beginning with the IL file path.
/// @param sm Source manager instance supplied by the caller.
/// @return Exit status of the run command; non-zero on failure.
int cmdRunILWithSourceManager(int argc, char **argv, il::support::SourceManager &sm);

/// @brief Handle `ilc -run`.
///
/// Triggered when `ilc` receives the `-run` option. `argv[0]` should hold the
/// path to an IL file and the remaining elements provide optional flags such as
/// tracing or debugger control.
///
/// @param argc Number of arguments following `-run`.
/// @param argv Argument vector beginning with the IL file path.
/// @return Exit code of the executed program or `1` when validation fails or the
///         module lacks `func @main()` (emitting "missing main").
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
