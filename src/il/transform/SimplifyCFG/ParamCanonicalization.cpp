//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the SimplifyCFG parameter canonicalisation routines.  The helpers
// tighten block parameter lists by removing unused entries and by eliminating
// parameters that receive the same value from every predecessor.  They also
// adjust predecessor branch arguments so control-flow edges remain arity
// compatible.  The transformations operate in place on a function and preserve
// the module's semantics.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Utilities that keep block parameters and branch arguments aligned.
/// @details Provides the core algorithms used by the SimplifyCFG pass to drop
///          redundant block parameters, update predecessor edges, and maintain
///          argument ordering without rebuilding surrounding data structures.

#include "il/transform/SimplifyCFG/ParamCanonicalization.hpp"

#include "il/transform/SimplifyCFG/Utils.hpp"

#include "il/core/BasicBlock.hpp"

#include <cassert>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::transform::simplify_cfg
{
namespace
{

/// @brief Synchronise predecessor branch arguments with an updated block signature.
///
/// @details Iterates over every predecessor terminator that targets @p block and
///          ensures its branch argument list mirrors the block's current
///          parameter layout.  Arguments are truncated when the block dropped
///          parameters, cleared when the block takes no parameters, and verified
///          to remain in lock-step to avoid mismatched arities after other
///          canonicalisation steps.
///
/// @param ctx   SimplifyCFG context providing access to the parent function.
/// @param block Block whose incoming arguments must be realigned.
void realignBranchArgs(SimplifyCFG::SimplifyCFGPassContext &ctx, il::core::BasicBlock &block)
{
    for (auto &pred : ctx.function.blocks)
    {
        il::core::Instr *term = findTerminator(pred);
        if (!term)
            continue;

        for (size_t edgeIdx = 0; edgeIdx < term->labels.size(); ++edgeIdx)
        {
            if (term->labels[edgeIdx] != block.label)
                continue;

            if (term->brArgs.size() <= edgeIdx)
            {
                assert(block.params.empty() && "missing branch args for block parameters");
                continue;
            }

            auto &args = term->brArgs[edgeIdx];

            if (block.params.empty())
            {
                args.clear();
                continue;
            }

            if (args.size() > block.params.size())
                args.resize(block.params.size());

            assert(args.size() == block.params.size() &&
                   "mismatched branch argument count after parameter update");
        }
    }
}

/// @brief Remove parameters that receive the same value from every predecessor.
///
/// @details Walks all incoming edges to @p block and checks whether each block
///          parameter is always passed the same SSA value.  When a unanimous
///          value is found, the helper substitutes that value directly inside
///          the block and erases the parameter alongside the corresponding
///          branch arguments.  The scan repeats until no more parameters can be
///          eliminated, guaranteeing a fixed point even when substitutions
///          expose additional redundancies.
///
/// @param ctx   SimplifyCFG context exposing the current function and logging.
/// @param block Block under inspection.
/// @returns True if any parameters were removed.
bool shrinkParamsEqualAcrossPreds(SimplifyCFG::SimplifyCFGPassContext &ctx,
                                  il::core::BasicBlock &block)
{
    bool removedAny = false;

    while (true)
    {
        bool removedThisIteration = false;

        for (size_t paramIdx = 0; paramIdx < block.params.size();)
        {
            const unsigned paramId = block.params[paramIdx].id;
            il::core::Value commonValue{};
            bool hasCommonValue = false;
            bool mismatch = false;

            for (auto &pred : ctx.function.blocks)
            {
                il::core::Instr *term = findTerminator(pred);
                if (!term)
                    continue;

                for (size_t edgeIdx = 0; edgeIdx < term->labels.size(); ++edgeIdx)
                {
                    if (term->labels[edgeIdx] != block.label)
                        continue;

                    if (term->brArgs.size() <= edgeIdx)
                    {
                        mismatch = true;
                        break;
                    }

                    const auto &args = term->brArgs[edgeIdx];
                    if (args.size() != block.params.size())
                    {
                        mismatch = true;
                        break;
                    }

                    const il::core::Value &incoming = args[paramIdx];
                    if (!hasCommonValue)
                    {
                        commonValue = incoming;
                        hasCommonValue = true;
                    }
                    else if (!valuesEqual(incoming, commonValue))
                    {
                        mismatch = true;
                        break;
                    }
                }

                if (mismatch)
                    break;
            }

            if (!hasCommonValue || mismatch)
            {
                ++paramIdx;
                continue;
            }

            auto replaceUses = [&](il::core::Value &value)
            {
                if (value.kind == il::core::Value::Kind::Temp && value.id == paramId)
                    value = commonValue;
            };

            for (auto &instr : block.instructions)
            {
                for (auto &operand : instr.operands)
                    replaceUses(operand);

                for (auto &argList : instr.brArgs)
                {
                    for (auto &val : argList)
                        replaceUses(val);
                }
            }

            for (auto &pred : ctx.function.blocks)
            {
                il::core::Instr *term = findTerminator(pred);
                if (!term)
                    continue;

                for (size_t edgeIdx = 0; edgeIdx < term->labels.size(); ++edgeIdx)
                {
                    if (term->labels[edgeIdx] != block.label)
                        continue;

                    if (term->brArgs.size() <= edgeIdx)
                        continue;

                    auto &args = term->brArgs[edgeIdx];
                    if (paramIdx < args.size())
                    {
                        args.erase(args.begin() + static_cast<std::ptrdiff_t>(paramIdx));
                    }
                }
            }

            block.params.erase(block.params.begin() + static_cast<std::ptrdiff_t>(paramIdx));
            removedThisIteration = true;
            removedAny = true;
        }

        if (!removedThisIteration)
            break;
    }

    if (removedAny)
        realignBranchArgs(ctx, block);

    return removedAny;
}

/// @brief Drop block parameters whose SSA value is never referenced.
///
/// @details Scans the block's instructions and branch arguments to determine
///          whether each parameter identifier is used.  When a parameter is
///          dead, the helper erases it and prunes the matching argument from
///          every predecessor edge before finally realigning the remaining
///          arguments.  The process repeats until all unused parameters are
///          removed so later passes operate on a minimal signature.
///
/// @param ctx   SimplifyCFG context with access to the function being mutated.
/// @param block Block whose parameters are assessed.
/// @returns True if any parameters were eliminated.
bool dropUnusedParams(SimplifyCFG::SimplifyCFGPassContext &ctx, il::core::BasicBlock &block)
{
    bool removedAny = false;

    for (size_t paramIdx = 0; paramIdx < block.params.size();)
    {
        const unsigned paramId = block.params[paramIdx].id;
        bool used = false;

        for (const auto &instr : block.instructions)
        {
            auto checkValue = [&](const il::core::Value &value)
            { return value.kind == il::core::Value::Kind::Temp && value.id == paramId; };

            for (const auto &operand : instr.operands)
            {
                if (checkValue(operand))
                {
                    used = true;
                    break;
                }
            }

            if (used)
                break;

            for (const auto &argList : instr.brArgs)
            {
                for (const auto &value : argList)
                {
                    if (checkValue(value))
                    {
                        used = true;
                        break;
                    }
                }

                if (used)
                    break;
            }

            if (used)
                break;
        }

        if (used)
        {
            ++paramIdx;
            continue;
        }

        for (auto &pred : ctx.function.blocks)
        {
            il::core::Instr *term = findTerminator(pred);
            if (!term)
                continue;

            for (size_t edgeIdx = 0; edgeIdx < term->labels.size(); ++edgeIdx)
            {
                if (term->labels[edgeIdx] != block.label)
                    continue;

                if (term->brArgs.size() <= edgeIdx)
                    continue;

                auto &args = term->brArgs[edgeIdx];
                if (paramIdx < args.size())
                {
                    args.erase(args.begin() + static_cast<std::ptrdiff_t>(paramIdx));
                }
            }
        }

        block.params.erase(block.params.begin() + static_cast<std::ptrdiff_t>(paramIdx));
        removedAny = true;
    }

    if (removedAny)
        realignBranchArgs(ctx, block);

    return removedAny;
}

} // namespace

/// @brief Entry point that canonicalises parameters and branch arguments.
///
/// @details Iterates the function's blocks, skipping exception-handling regions
///          where parameter manipulation is unsafe, and applies both redundancy
///          elimination helpers.  The routine aggregates statistics, emits
///          optional debug logs, and returns whether the function changed so the
///          surrounding pass manager can schedule follow-up work when needed.
///
/// @param ctx SimplifyCFG context bound to the function under transformation.
/// @returns True if any block parameters or arguments were simplified.
bool canonicalizeParamsAndArgs(SimplifyCFG::SimplifyCFGPassContext &ctx)
{
    il::core::Function &F = ctx.function;

    bool changed = false;

    for (auto &block : F.blocks)
    {
        if (ctx.isEHSensitive(block))
            continue;

        if (block.params.empty())
            continue;

        const size_t beforeShrink = block.params.size();
        if (shrinkParamsEqualAcrossPreds(ctx, block))
        {
            const size_t removed = beforeShrink - block.params.size();
            if (removed > 0)
            {
                changed = true;
                ctx.stats.paramsShrunk += removed;
                if (ctx.isDebugLoggingEnabled())
                {
                    std::string message = "replaced duplicated params in block '" + block.label +
                                          "', removed " + std::to_string(removed);
                    ctx.logDebug(message);
                }
            }
        }

        if (block.params.empty())
            continue;

        const size_t beforeDrop = block.params.size();
        if (dropUnusedParams(ctx, block))
        {
            const size_t removed = beforeDrop - block.params.size();
            if (removed > 0)
            {
                changed = true;
                ctx.stats.paramsShrunk += removed;
                if (ctx.isDebugLoggingEnabled())
                {
                    std::string message = "dropped unused params in block '" + block.label +
                                          "', removed " + std::to_string(removed);
                    ctx.logDebug(message);
                }
            }
        }
    }

    return changed;
}

} // namespace il::transform::simplify_cfg
