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

#pragma once

#include "il/core/fwd.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace il::analysis
{

struct CallGraph
{
    std::unordered_map<std::string, unsigned> callCounts;
    std::unordered_map<std::string, std::vector<std::string>> edges;
};

CallGraph buildCallGraph(core::Module &module);

} // namespace il::analysis
