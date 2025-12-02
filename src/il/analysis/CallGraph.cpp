//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a tiny direct-call graph helper for inlining heuristics.
//
//===----------------------------------------------------------------------===//

#include "il/analysis/CallGraph.hpp"

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"

namespace viper::analysis
{

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
