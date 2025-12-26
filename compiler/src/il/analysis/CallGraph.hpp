//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// A tiny direct-call graph helper for simple inlining heuristics. This is not a
// full-blown analysis; it scans the module and counts direct call sites per
// callee name, and can optionally record caller→callee edges.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Lightweight direct call graph utilities for IL analysis.
/// @details Provides a minimal call graph representation suitable for inlining
///          heuristics. The analysis counts direct call sites and optionally
///          records caller-to-callee edges by name.

#pragma once

#include "il/core/fwd.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace viper::analysis
{

/// @brief Direct-call graph summary for a module.
/// @details Tracks per-callee call counts and a caller→callee adjacency list.
///          The graph only includes direct calls with explicit callee names.
struct CallGraph
{
    /// @brief Total direct call sites per callee name.
    std::unordered_map<std::string, unsigned> callCounts;
    /// @brief Caller-to-callee edges keyed by caller function name.
    std::unordered_map<std::string, std::vector<std::string>> edges;
};

/// @brief Build a direct-call graph for a module.
/// @details Scans all functions and instructions, counting direct call sites
///          and recording caller→callee edges when a call has a named target.
///          Indirect calls are ignored by design.
/// @param module Module to analyze.
/// @return CallGraph summary for the module.
CallGraph buildCallGraph(il::core::Module &module);

} // namespace viper::analysis
