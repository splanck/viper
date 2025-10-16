// File: src/il/transform/SimplifyCFG/BranchFolding.cpp
// License: MIT (see LICENSE for details).
// Purpose: Implements folding of trivial conditional and switch branches.
// Key invariants: Rewrites branches only when successor/argument equivalence holds.
// Ownership/Lifetime: Mutates IL instructions in place without reallocating blocks.
// Links: docs/codemap.md

#include "il/transform/SimplifyCFG/BranchFolding.hpp"

#include "il/transform/SimplifyCFG/Utils.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"

#include <cassert>
#include <string>
#include <vector>

namespace il::transform::simplify_cfg
{
namespace
{

void rewriteToUnconditionalBranch(il::core::Instr &instr, size_t successorIndex)
{
    assert(successorIndex < instr.labels.size());
    const std::string target = instr.labels[successorIndex];
    instr.op = il::core::Opcode::Br;
    instr.operands.clear();
    instr.labels.clear();
    instr.labels.push_back(target);

    if (instr.brArgs.empty())
        return;

    std::vector<std::vector<il::core::Value>> newArgs;
    if (successorIndex < instr.brArgs.size())
        newArgs.push_back(instr.brArgs[successorIndex]);
    instr.brArgs = std::move(newArgs);
}

} // namespace

bool foldTrivialSwitches(SimplifyCFG::SimplifyCFGPassContext &ctx)
{
    il::core::Function &F = ctx.function;
    bool changed = false;

    for (auto &block : F.blocks)
    {
        if (ctx.isEHSensitive(block))
            continue;

        for (auto &instr : block.instructions)
        {
            if (instr.op != il::core::Opcode::SwitchI32)
                continue;

            bool simplified = false;
            const size_t caseCount = il::core::switchCaseCount(instr);

            if (caseCount == 0)
            {
                rewriteToUnconditionalBranch(instr, 0);
                simplified = true;
            }
            else if (caseCount == 1)
            {
                const std::string &defaultLabel = il::core::switchDefaultLabel(instr);
                const std::string &caseLabel = il::core::switchCaseLabel(instr, 0);
                if (defaultLabel == caseLabel)
                {
                    const auto &defaultArgs = il::core::switchDefaultArgs(instr);
                    const auto &caseArgs = il::core::switchCaseArgs(instr, 0);
                    if (valueVectorsEqual(defaultArgs, caseArgs))
                    {
                        rewriteToUnconditionalBranch(instr, 0);
                        simplified = true;
                    }
                }
            }

            if (simplified)
            {
                changed = true;
                ++ctx.stats.switchToBr;
                if (ctx.isDebugLoggingEnabled())
                {
                    std::string message = "folded trivial switch in block '" + block.label + "'";
                    ctx.logDebug(message);
                }
            }
        }
    }

    return changed;
}

bool foldTrivialConditionalBranches(SimplifyCFG::SimplifyCFGPassContext &ctx)
{
    il::core::Function &F = ctx.function;
    bool changed = false;

    for (auto &block : F.blocks)
    {
        if (ctx.isEHSensitive(block))
            continue;

        for (auto &instr : block.instructions)
        {
            if (instr.op != il::core::Opcode::CBr)
                continue;

            bool simplified = false;

            if (!instr.operands.empty())
            {
                const il::core::Value &cond = instr.operands.front();
                if (cond.kind == il::core::Value::Kind::ConstInt && cond.isBool)
                {
                    const bool takeTrue = cond.i64 != 0;
                    const size_t successorIndex = takeTrue ? 0 : 1;
                    if (successorIndex < instr.labels.size())
                    {
                        rewriteToUnconditionalBranch(instr, successorIndex);
                        simplified = true;
                    }
                }
            }

            if (!simplified && instr.labels.size() >= 2 && instr.labels[0] == instr.labels[1])
            {
                const std::vector<il::core::Value> *trueArgs =
                    instr.brArgs.size() > 0 ? &instr.brArgs[0] : nullptr;
                const std::vector<il::core::Value> *falseArgs =
                    instr.brArgs.size() > 1 ? &instr.brArgs[1] : nullptr;
                bool argsMatch = false;
                if (!trueArgs && !falseArgs)
                {
                    argsMatch = true;
                }
                else if (trueArgs && falseArgs)
                {
                    argsMatch = valueVectorsEqual(*trueArgs, *falseArgs);
                }

                if (argsMatch)
                {
                    rewriteToUnconditionalBranch(instr, 0);
                    simplified = true;
                }
            }

            if (simplified)
            {
                changed = true;
                ++ctx.stats.cbrToBr;
                if (ctx.isDebugLoggingEnabled())
                {
                    std::string message =
                        "folded redundant cbr in block '" + block.label + "'";
                    ctx.logDebug(message);
                }
            }
        }
    }

    return changed;
}

} // namespace il::transform::simplify_cfg

