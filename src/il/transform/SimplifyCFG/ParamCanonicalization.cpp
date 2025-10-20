// File: src/il/transform/SimplifyCFG/ParamCanonicalization.cpp
// License: MIT (see LICENSE for details).
// Purpose: Implements parameter and argument canonicalisation for SimplifyCFG.
// Key invariants: Keeps branch arguments aligned with block parameter layout.
// Ownership/Lifetime: Mutates block parameter lists and predecessor argument lists.
// Links: docs/codemap.md

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
                std::string message = "realignBranchArgs: predecessor '" + pred.label +
                                      "' is missing branch arguments for edge to block '" +
                                      block.label + "'";
                ctx.logDebug(message);
                term->brArgs.resize(edgeIdx + 1);
            }

            auto &args = term->brArgs[edgeIdx];

            if (block.params.empty())
            {
                args.clear();
                continue;
            }

            if (args.size() > block.params.size())
                args.resize(block.params.size());

            if (args.size() != block.params.size())
            {
                std::string message = "realignBranchArgs: predecessor '" + pred.label +
                                      "' has " + std::to_string(args.size()) +
                                      " argument(s) for block '" + block.label + "' expecting " +
                                      std::to_string(block.params.size()) +
                                      "; skipping argument realignment";
                ctx.logDebug(message);
                continue;
            }
        }
    }
}

bool shrinkParamsEqualAcrossPreds(SimplifyCFG::SimplifyCFGPassContext &ctx, il::core::BasicBlock &block)
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

            auto replaceUses = [&](il::core::Value &value) {
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

bool dropUnusedParams(SimplifyCFG::SimplifyCFGPassContext &ctx, il::core::BasicBlock &block)
{
    bool removedAny = false;

    for (size_t paramIdx = 0; paramIdx < block.params.size();)
    {
        const unsigned paramId = block.params[paramIdx].id;
        bool used = false;

        for (const auto &instr : block.instructions)
        {
            auto checkValue = [&](const il::core::Value &value) {
                return value.kind == il::core::Value::Kind::Temp && value.id == paramId;
            };

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

        realignBranchArgs(ctx, block);

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

        realignBranchArgs(ctx, block);
    }

    return changed;
}

} // namespace il::transform::simplify_cfg

