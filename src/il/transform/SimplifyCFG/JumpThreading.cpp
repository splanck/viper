//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements jump threading transformation for SimplifyCFG.
// Jump threading optimizes control flow by redirecting predecessors that
// pass known values for branch conditions directly to the target block,
// bypassing the intermediate conditional branch.
//
//===----------------------------------------------------------------------===//

#include "il/transform/SimplifyCFG/JumpThreading.hpp"

#include "il/transform/SimplifyCFG/Utils.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::transform::simplify_cfg
{

namespace
{

/// @brief Find a basic block by label.
il::core::BasicBlock *findBlock(il::core::Function &F, const std::string &label)
{
    for (auto &block : F.blocks)
    {
        if (block.label == label)
            return &block;
    }
    return nullptr;
}

/// @brief Build a map of all predecessors for each block.
std::unordered_map<std::string, std::vector<il::core::BasicBlock *>> buildPredecessorMap(
    il::core::Function &F)
{
    std::unordered_map<std::string, std::vector<il::core::BasicBlock *>> preds;

    for (auto &block : F.blocks)
    {
        const il::core::Instr *term = findTerminator(block);
        if (!term)
            continue;

        for (const auto &label : term->labels)
        {
            preds[label].push_back(&block);
        }
    }

    return preds;
}

/// @brief Get the index of a label in a terminator's labels vector.
std::optional<size_t> labelIndex(const il::core::Instr &term, const std::string &target)
{
    for (size_t i = 0; i < term.labels.size(); ++i)
    {
        if (term.labels[i] == target)
            return i;
    }
    return std::nullopt;
}

/// @brief Determine what constant value (if any) flows to a block parameter.
std::optional<il::core::Value> getConstantArgForParam(const il::core::BasicBlock &pred,
                                                      const il::core::BasicBlock &target,
                                                      size_t paramIndex)
{
    const il::core::Instr *term = findTerminator(pred);
    if (!term)
        return std::nullopt;

    auto targetIdx = labelIndex(*term, target.label);
    if (!targetIdx)
        return std::nullopt;

    if (*targetIdx >= term->brArgs.size())
        return std::nullopt;

    const auto &args = term->brArgs[*targetIdx];
    if (paramIndex >= args.size())
        return std::nullopt;

    const il::core::Value &arg = args[paramIndex];
    if (arg.kind == il::core::Value::Kind::ConstInt ||
        arg.kind == il::core::Value::Kind::ConstFloat || arg.kind == il::core::Value::Kind::NullPtr)
    {
        return arg;
    }

    return std::nullopt;
}

/// @brief Check if a block is a simple conditional branch with condition from params.
/// Returns the param index of the condition if found.
std::optional<size_t> findConditionParamIndex(const il::core::BasicBlock &block)
{
    if (block.instructions.empty())
        return std::nullopt;

    const il::core::Instr &term = block.instructions.back();
    if (term.op != il::core::Opcode::CBr)
        return std::nullopt;

    if (term.operands.empty())
        return std::nullopt;

    const il::core::Value &cond = term.operands[0];
    if (cond.kind != il::core::Value::Kind::Temp)
        return std::nullopt;

    // Check if the condition is a block parameter
    for (size_t i = 0; i < block.params.size(); ++i)
    {
        if (block.params[i].id == cond.id)
            return i;
    }

    return std::nullopt;
}

/// @brief Check if a block has only a conditional branch (no other instructions).
bool isSimpleCbrBlock(const il::core::BasicBlock &block)
{
    // Allow blocks with only a cbr terminator, or with simple non-side-effect
    // instructions that can be duplicated
    if (block.instructions.empty())
        return false;

    const il::core::Instr &term = block.instructions.back();
    if (term.op != il::core::Opcode::CBr)
        return false;

    // For now, only thread if the block has just the cbr
    // More aggressive threading could duplicate small instruction sequences
    return block.instructions.size() == 1;
}

/// @brief Compute the arguments to pass to the threaded target.
std::vector<il::core::Value> computeThreadedArgs(const il::core::BasicBlock &pred,
                                                 const il::core::BasicBlock &intermediate,
                                                 const il::core::BasicBlock &target,
                                                 size_t targetBranchIdx)
{
    const il::core::Instr *predTerm = findTerminator(pred);
    const il::core::Instr *intTerm = findTerminator(intermediate);
    if (!predTerm || !intTerm)
        return {};

    // Get args that pred passes to intermediate
    auto toIntIdx = labelIndex(*predTerm, intermediate.label);
    if (!toIntIdx || *toIntIdx >= predTerm->brArgs.size())
        return {};

    const auto &predToIntArgs = predTerm->brArgs[*toIntIdx];

    // Build mapping: intermediate param id -> value from pred
    std::unordered_map<unsigned, il::core::Value> mapping;
    for (size_t i = 0; i < intermediate.params.size() && i < predToIntArgs.size(); ++i)
    {
        mapping[intermediate.params[i].id] = predToIntArgs[i];
    }

    // Get args that intermediate would pass to target
    if (targetBranchIdx >= intTerm->brArgs.size())
        return {};

    const auto &intToTargetArgs = intTerm->brArgs[targetBranchIdx];

    // Substitute values through the mapping
    std::vector<il::core::Value> result;
    result.reserve(intToTargetArgs.size());
    for (const auto &arg : intToTargetArgs)
    {
        result.push_back(substituteValue(arg, mapping));
    }

    return result;
}

} // namespace

bool threadJumps(SimplifyCFG::SimplifyCFGPassContext &ctx)
{
    il::core::Function &F = ctx.function;
    bool changed = false;

    // Build predecessor map
    auto predecessors = buildPredecessorMap(F);

    // Collect blocks to thread (don't modify while iterating)
    struct ThreadingCandidate
    {
        il::core::BasicBlock *pred;
        il::core::BasicBlock *intermediate;
        std::string newTarget;
        std::vector<il::core::Value> newArgs;
        size_t predBranchIdx;
    };

    std::vector<ThreadingCandidate> candidates;

    for (auto &block : F.blocks)
    {
        // Skip EH-sensitive blocks
        if (ctx.isEHSensitive(block))
            continue;

        // Check if this is a simple cbr block
        if (!isSimpleCbrBlock(block))
            continue;

        // Find the condition parameter index
        auto condParamIdx = findConditionParamIndex(block);
        if (!condParamIdx)
            continue;

        const il::core::Instr &term = block.instructions.back();
        if (term.labels.size() != 2)
            continue;

        // Check each predecessor
        auto predIt = predecessors.find(block.label);
        if (predIt == predecessors.end())
            continue;

        for (il::core::BasicBlock *pred : predIt->second)
        {
            // Skip self-loops
            if (pred == &block)
                continue;

            // Skip EH-sensitive predecessors
            if (ctx.isEHSensitive(*pred))
                continue;

            // Check if pred passes a constant for the condition
            auto constArg = getConstantArgForParam(*pred, block, *condParamIdx);
            if (!constArg)
                continue;

            // Determine which branch to take based on the constant
            bool condValue = false;
            if (constArg->kind == il::core::Value::Kind::ConstInt)
            {
                condValue = (constArg->i64 != 0);
            }
            else
            {
                continue; // Only handle integer constants for now
            }

            // CBr: true branch is index 0, false branch is index 1
            size_t targetBranchIdx = condValue ? 0 : 1;
            const std::string &newTarget = term.labels[targetBranchIdx];

            // Compute the arguments for the threaded jump
            auto *targetBlock = findBlock(F, newTarget);
            if (!targetBlock)
                continue;
            auto newArgs = computeThreadedArgs(*pred, block, *targetBlock, targetBranchIdx);

            // Find which branch index in pred goes to this block
            il::core::Instr *predTerm = findTerminator(*pred);
            if (!predTerm)
                continue;

            auto predBranchIdx = labelIndex(*predTerm, block.label);
            if (!predBranchIdx)
                continue;

            candidates.push_back({pred, &block, newTarget, newArgs, *predBranchIdx});
        }
    }

    // Apply threading transformations
    for (const auto &candidate : candidates)
    {
        il::core::Instr *predTerm = findTerminator(*candidate.pred);
        if (!predTerm)
            continue;

        // Update the predecessor's terminator
        if (candidate.predBranchIdx < predTerm->labels.size())
        {
            predTerm->labels[candidate.predBranchIdx] = candidate.newTarget;
        }

        if (candidate.predBranchIdx < predTerm->brArgs.size())
        {
            predTerm->brArgs[candidate.predBranchIdx] = candidate.newArgs;
        }

        changed = true;

        if (ctx.isDebugLoggingEnabled())
        {
            std::string message = "threaded jump from '" + candidate.pred->label + "' through '" +
                                  candidate.intermediate->label + "' to '" + candidate.newTarget +
                                  "'";
            ctx.logDebug(message);
        }
    }

    return changed;
}

} // namespace il::transform::simplify_cfg
