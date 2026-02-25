//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the SimplifyCFG logic that recognises and removes empty forwarding
// blocks.  These blocks contain only a trivial branch to a successor and exist
// solely to forward arguments.  The routines below redirect predecessor edges
// to bypass the forwarding block, substitute argument values directly, and
// finally erase the redundant block once all edges have been retargeted.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Forwarding-block elimination helpers for SimplifyCFG.
/// @details Provides predicates for identifying empty forwarders, routines for
///          updating predecessor terminators, and the pass entry point that
///          collects statistics and debug output.

#include "il/transform/SimplifyCFG/ForwardingElimination.hpp"

#include "il/transform/SimplifyCFG/Utils.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::transform::simplify_cfg
{
namespace
{

/// @brief Check whether a block is an EH-safe forwarding trampoline.
///
/// @details Valid forwarding blocks start with optional non-side-effecting
///          instructions, end with a single @c br terminator, and merely pass
///          through branch arguments to a unique successor.  The predicate
///          verifies exception-handling constraints, ensures no side effects
///          occur before the terminator, and rejects blocks whose terminator
///          reuses locally defined temporaries in its arguments.
///
/// @param ctx   SimplifyCFG context providing EH sensitivity checks.
/// @param block Candidate block to inspect.
/// @returns True when the block can be safely removed.
bool isEmptyForwardingBlock(SimplifyCFG::SimplifyCFGPassContext &ctx,
                            const il::core::BasicBlock &block)
{
    if (isEntryLabel(block.label))
        return false;

    if (ctx.isEHSensitive(block))
        return false;

    if (block.instructions.empty())
        return false;

    const il::core::Instr *terminator = findTerminator(block);
    if (!terminator)
        return false;

    if (terminator->op != il::core::Opcode::Br)
        return false;

    if (terminator->labels.size() != 1)
        return false;

    if (&block.instructions.back() != terminator)
        return false;

    std::unordered_set<unsigned> definedTemps;
    for (const auto &instr : block.instructions)
    {
        if (instr.result)
            definedTemps.insert(*instr.result);
    }

    for (const auto &instr : block.instructions)
    {
        if (&instr == terminator)
            break;

        if (hasSideEffects(instr))
            return false;
    }

    if (!terminator->brArgs.empty())
    {
        if (terminator->brArgs.size() != 1)
            return false;

        for (const auto &value : terminator->brArgs.front())
        {
            if (value.kind == il::core::Value::Kind::Temp && definedTemps.contains(value.id))
                return false;
        }
    }

    // Reject blocks whose params are referenced from other blocks.
    // After inlining, cross-block references to block params may exist.
    // Removing such a forwarder without global substitution would leave
    // dangling references.  Let mergeSinglePredBlocks handle these instead.
    if (!block.params.empty())
    {
        std::unordered_set<unsigned> paramIds;
        for (const auto &p : block.params)
            paramIds.insert(p.id);

        const il::core::Function &F = ctx.function;
        for (const auto &bb : F.blocks)
        {
            if (&bb == &block)
                continue;
            for (const auto &instr : bb.instructions)
            {
                for (const auto &op : instr.operands)
                {
                    if (op.kind == il::core::Value::Kind::Temp && paramIds.count(op.id))
                        return false;
                }
                for (const auto &argList : instr.brArgs)
                {
                    for (const auto &v : argList)
                    {
                        if (v.kind == il::core::Value::Kind::Temp && paramIds.count(v.id))
                            return false;
                    }
                }
            }
        }
    }

    return true;
}

/// @brief Retarget a predecessor's terminator to bypass a forwarding block.
///
/// @details Locates occurrences of @p dead in the predecessor's terminator,
///          substitutes the forwarding block's parameters with their incoming
///          arguments, and rebuilds the argument list to match the successor's
///          expectations.  When the forwarding block forwarded arguments, the
///          helper materialises the substituted values so that the successor sees
///          the same inputs it would have received prior to removal.
///
/// @param pred Predecessor block whose terminator will be rewritten.
/// @param dead Forwarding block slated for removal.
/// @param succ Successor originally targeted by @p dead.
void redirectPredecessor(il::core::BasicBlock &pred,
                         il::core::BasicBlock &dead,
                         il::core::BasicBlock &succ)
{
    il::core::Instr *predTerm = findTerminator(pred);
    if (!predTerm)
        return;

    bool referencesDead = false;
    for (const auto &label : predTerm->labels)
    {
        if (label == dead.label)
        {
            referencesDead = true;
            break;
        }
    }

    if (!referencesDead)
        return;

    il::core::Instr *deadTerm = findTerminator(dead);
    assert(deadTerm && deadTerm->op == il::core::Opcode::Br);
    assert(deadTerm->labels.size() == 1);

    const std::vector<il::core::Value> *deadArgs = nullptr;
    if (!deadTerm->brArgs.empty())
    {
        assert(deadTerm->brArgs.size() == 1);
        deadArgs = &deadTerm->brArgs.front();
    }

    std::unordered_map<unsigned, il::core::Value> substitution;
    substitution.reserve(dead.params.size());

    for (size_t idx = 0; idx < predTerm->labels.size(); ++idx)
    {
        if (predTerm->labels[idx] != dead.label)
            continue;

        std::vector<il::core::Value> incomingArgs;
        if (idx < predTerm->brArgs.size())
            incomingArgs = predTerm->brArgs[idx];

        assert(incomingArgs.size() == dead.params.size());

        substitution.clear();
        for (size_t paramIdx = 0; paramIdx < dead.params.size(); ++paramIdx)
            substitution.emplace(dead.params[paramIdx].id, incomingArgs[paramIdx]);

        std::vector<il::core::Value> newArgs;
        if (deadArgs)
        {
            newArgs.reserve(deadArgs->size());
            for (const auto &value : *deadArgs)
                newArgs.push_back(substituteValue(value, substitution));
        }

        predTerm->labels[idx] = succ.label;
        if (predTerm->brArgs.size() <= idx)
            predTerm->brArgs.resize(idx + 1);
        predTerm->brArgs[idx] = std::move(newArgs);
    }
}

} // namespace

/// @brief Remove every empty forwarding block within a function.
///
/// @details Collects the labels of eligible forwarders, redirects all
///          predecessors around each block, and erases the block once no edges
///          refer to it.  Statistics and optional debug logs track how many
///          predecessors were retargeted and how many blocks were deleted.  The
///          function returns whether the control-flow graph changed so callers
///          can trigger follow-up cleanups.
///
/// @param ctx SimplifyCFG context owning the function under transformation.
/// @returns True if any blocks were rewritten or removed.
bool removeEmptyForwarders(SimplifyCFG::SimplifyCFGPassContext &ctx)
{
    il::core::Function &F = ctx.function;
    bool changed = false;

    std::vector<std::string> forwardingBlocks;
    forwardingBlocks.reserve(F.blocks.size());
    for (const auto &block : F.blocks)
    {
        if (isEmptyForwardingBlock(ctx, block))
            forwardingBlocks.push_back(block.label);
    }

    size_t removedBlocks = 0;

    for (const auto &deadLabel : forwardingBlocks)
    {
        auto deadIt = std::find_if(F.blocks.begin(),
                                   F.blocks.end(),
                                   [&](const il::core::BasicBlock &block)
                                   { return block.label == deadLabel; });
        if (deadIt == F.blocks.end())
            continue;

        il::core::BasicBlock &dead = *deadIt;
        il::core::Instr *deadTerm = findTerminator(dead);
        if (!deadTerm || deadTerm->labels.size() != 1)
            continue;

        const std::string &succLabel = deadTerm->labels.front();
        if (succLabel == dead.label)
            continue;

        auto succIt = std::find_if(F.blocks.begin(),
                                   F.blocks.end(),
                                   [&](const il::core::BasicBlock &block)
                                   { return block.label == succLabel; });
        if (succIt == F.blocks.end())
            continue;

        il::core::BasicBlock &succ = *succIt;

        size_t redirected = 0;
        for (auto &pred : F.blocks)
        {
            il::core::Instr *predTerm = findTerminator(pred);
            if (!predTerm)
                continue;

            bool touchesDead = false;
            for (const auto &label : predTerm->labels)
            {
                if (label == dead.label)
                {
                    touchesDead = true;
                    break;
                }
            }

            if (!touchesDead)
                continue;

            redirectPredecessor(pred, dead, succ);
            ++redirected;
        }

        if (redirected > 0)
        {
            changed = true;
            ctx.stats.predsMerged += redirected;
            if (ctx.isDebugLoggingEnabled())
            {
                std::string message = "redirected " + std::to_string(redirected) +
                                      " predecessor edges around block '" + dead.label + "'";
                ctx.logDebug(message);
            }
        }

        bool hasPreds = false;
        for (const auto &pred : F.blocks)
        {
            const il::core::Instr *predTerm = findTerminator(pred);
            if (!predTerm)
                continue;

            for (const auto &label : predTerm->labels)
            {
                if (label == dead.label)
                {
                    hasPreds = true;
                    break;
                }
            }

            if (hasPreds)
                break;
        }

        if (hasPreds)
            continue;

        F.blocks.erase(F.blocks.begin() + std::distance(F.blocks.begin(), deadIt));
        ++removedBlocks;
    }

    if (removedBlocks > 0)
    {
        changed = true;
        ctx.stats.emptyBlocksRemoved += removedBlocks;
        if (ctx.isDebugLoggingEnabled())
        {
            std::string message = "removed " + std::to_string(removedBlocks) +
                                  " empty forwarding block" + (removedBlocks == 1 ? "" : "s");
            ctx.logDebug(message);
        }
    }

    return changed;
}

} // namespace il::transform::simplify_cfg
