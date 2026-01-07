//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements direct-call graph construction for inlining heuristics.
/// @details Scans each function's blocks and instructions, records edges for
///          direct `call` instructions with resolved callee names, and
///          accumulates per-callee call counts. The graph is intentionally
///          lightweight: it does not model indirect calls, call-site metadata,
///          or recursion analysis, and it preserves duplicates in the edge list
///          so callers can reason about call-site multiplicity.
//
//===----------------------------------------------------------------------===//

#include "il/analysis/CallGraph.hpp"

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"

namespace viper::analysis
{

/// @brief Build a direct-call graph for an IL module.
/// @details Walks every block and instruction in each function; when it finds a
///          direct call (`Opcode::Call` with a non-empty callee name) it appends
///          the callee to the caller's edge list and increments the callee's
///          call count. Indirect calls or unresolved callees are skipped, and
///          repeated call sites are kept as duplicate entries in the edge list.
/// @param module Module to scan; the IL is not modified.
/// @return Call graph populated with edges and call counts for direct calls only.
CallGraph buildCallGraph(il::core::Module &module)
{
    CallGraph cg;
    for (auto &fn : module.functions)
    {
        for (auto &B : fn.blocks)
        {
            for (auto &I : B.instructions)
            {
                if (I.op == il::core::Opcode::Call && !I.callee.empty())
                {
                    ++cg.callCounts[I.callee];
                    cg.edges[fn.name].push_back(I.callee);
                }
            }
        }
    }
    return cg;
}

} // namespace viper::analysis
