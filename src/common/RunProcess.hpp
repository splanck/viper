//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: common/RunProcess.hpp
// Purpose: Declare cross-platform process execution helpers for CLI utilities.
// Key invariants: RunResult captures exit codes and aggregated stdout/stderr text.
// Ownership/Lifetime: Callers own argument buffers; helper copies command text as needed.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/// @brief Result of launching a subprocess.
/// @details The legacy @ref exit_code field remains available for existing
///          call sites.  Launch/setup failures are reported with
///          @ref launch_failed set to true and @ref exit_code set to -1.
///          Normal process termination sets @ref launched to true and stores
///          the exact native process status in @ref native_exit_code when the
///          host API provides one.  On Windows, native exit codes larger than
///          @c INT_MAX are saturated in @ref exit_code so they cannot collide
///          with the -1 launch-failure sentinel; callers that need the exact
///          value should read @ref native_exit_code.
struct RunResult {
    int exit_code{0};               ///< Normalised process exit code (or -1 on launch failure).
    std::string out;                ///< Captured standard output text.
    std::string err;                ///< Captured standard error text (may be merged with stdout).
    bool launched{false};           ///< True once the host successfully starts a child process.
    bool launch_failed{false};      ///< True when setup or process creation failed.
    std::uint32_t native_exit_code{0}; ///< Exact host exit code when available.
};

/// @brief Spawn a subprocess using the provided argument vector.
/// @param argv Command-line arguments including the executable at index zero.
/// @param cwd Optional working directory to set before launching the process.
/// @param env Environment variable overrides expressed as key/value pairs.
/// @return Captured process result including exit code and output streams.
RunResult run_process(const std::vector<std::string> &argv,
                      std::optional<std::string> cwd = std::nullopt,
                      const std::vector<std::pair<std::string, std::string>> &env = {});

/// @brief Spawn a subprocess through the host shell explicitly.
/// @param command Shell command text.
/// @param cwd Optional working directory to set before launching the process.
/// @param env Environment variable overrides expressed as key/value pairs.
/// @return Captured process result including exit code and output streams.
RunResult run_shell_command(const std::string &command,
                            std::optional<std::string> cwd = std::nullopt,
                            const std::vector<std::pair<std::string, std::string>> &env = {});
