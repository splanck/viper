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

#include <optional>
#include <string>
#include <utility>
#include <vector>

/// @brief Result of launching a subprocess.
struct RunResult
{
    int exit_code;   ///< Normalised process exit code (or -1 on launch failure).
    std::string out; ///< Captured standard output text.
    std::string err; ///< Captured standard error text (may be merged with stdout).
};

/// @brief Spawn a subprocess using the provided argument vector.
/// @param argv Command-line arguments including the executable at index zero.
/// @param cwd Optional working directory to set before launching the process.
/// @param env Environment variable overrides expressed as key/value pairs.
/// @return Captured process result including exit code and output streams.
RunResult run_process(const std::vector<std::string> &argv,
                      std::optional<std::string> cwd = std::nullopt,
                      const std::vector<std::pair<std::string, std::string>> &env = {});
