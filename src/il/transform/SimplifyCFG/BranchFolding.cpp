//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/SimplifyCFG/BranchFolding.cpp
// Purpose: Collapse redundant branch terminators into simpler forms inside the
//          SimplifyCFG pass.
// Key invariants: Terminators rewritten to unconditional branches preserve the
//                 chosen successor label/arguments; EH-sensitive blocks are
//                 skipped; statistics/logging remain consistent with pass
//                 expectations.
// Ownership/Lifetime: Mutates pass-owned il::core::Instr instances in place and
//                     does not allocate persistent storage.
// Perf/Threading notes: Linear scan over each function; negligible additional
//                       allocations besides temporary strings; intended for
//                       single-threaded pass execution.
// Links: docs/il-passes.md#simplifycfg, docs/codemap.md#passes
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Branch folding utilities for SimplifyCFG.
/// @details Contains helpers that rewrite trivial switches and conditionals into
///          unconditional jumps when their successor sets or branch arguments are
///          equivalent.  This keeps the control-flow graph minimal ahead of more
///          aggressive optimisations.

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

/// @brief Convert a conditional/switch terminator into an unconditional branch.
///
/// @details The rewrite performs the following steps:
///          1. Assert the chosen successor is in range and capture its label.
///          2. Replace the opcode with @ref il::core::Opcode::Br and clear all
///             SSA operands because unconditional branches carry no condition.
///          3. Shrink the label list to the single surviving successor.
///          4. If branch arguments were present, retain only the argument list
///             attached to the surviving edge so phi nodes in the successor
///             still receive the correct incoming values.
///
/// @param instr Instruction to rewrite.
/// @param successorIndex Index of the successor to retain.
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

/// @brief Fold switches that devolve into a single unconditional branch.
///
/// @details Walks every block/instruction, skipping EH-sensitive blocks, and
///          considers each @c SwitchI32 terminator.  Switches are simplified in
///          two scenarios:
///          - No explicit cases: the switch jumps directly to the default
///            successor and the opcode is rewritten to @c br.
///          - Exactly one case whose label/branch arguments match the default:
///            both edges become indistinguishable, so the switch is folded into
///            a branch targeting the first successor.
///          When a simplification occurs the helper updates statistics and emits
///          optional debug logging for visibility.
///
/// @param ctx SimplifyCFG context providing EH sensitivity checks and logging.
/// @return True when any switch was simplified.
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

/// @brief Fold conditional branches when both arms lead to the same successor.
///
/// @details Evaluates each @c cbr terminator and attempts two simplifications:
///          1. If the condition operand is a compile-time boolean constant, pick
///             the known successor and rewrite the terminator to @c br.
///          2. When both successors share the same label, confirm that their
///             branch argument lists are equivalent (either both absent or
///             element-wise identical) and collapse to a single @c br.
///          Successful rewrites increment statistics and optionally log the
///          transformed block label.
///
/// @param ctx SimplifyCFG context providing EH sensitivity checks and logging.
/// @return True when any conditional branch was simplified.
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
                    std::string message = "folded redundant cbr in block '" + block.label + "'";
                    ctx.logDebug(message);
                }
            }
        }
    }

    return changed;
}

} // namespace il::transform::simplify_cfg
