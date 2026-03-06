//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/LoopRotate.cpp
// Purpose: Implements loop rotation (while → do-while transformation).
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Converts while-style loops into do-while form.
/// @details A while-loop has its exit test in the header block:
///
///   ^header(params):
///       %cond = ...
///       cbr %cond, ^body, ^exit
///
/// After rotation, the header becomes a guard that runs once, the body block
/// absorbs the header's parameters, and a copy of the header's instructions
/// (the condition test) is appended to the latch:
///
///   ^guard:
///       [header instrs with initial args]
///       cbr %cond, ^body(initial_args), ^exit
///
///   ^body(params):
///       [original body]
///       [header instrs copy with latch args]
///       cbr %cond', ^body(next_args), ^exit
///
/// This eliminates one branch per iteration and creates a single-entry
/// loop body amenable to LICM and unrolling.

#include "il/transform/LoopRotate.hpp"

#include "il/transform/AnalysisIDs.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/SimplifyCFG/Utils.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/analysis/Dominators.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/utils/Utils.hpp"

#include <string>
#include <unordered_map>
#include <vector>

using namespace il::core;

namespace il::transform
{
namespace
{

size_t findBlockIndex(const Function &function, const std::string &label)
{
    for (size_t i = 0; i < function.blocks.size(); ++i)
    {
        if (function.blocks[i].label == label)
            return i;
    }
    return SIZE_MAX;
}

std::string makeUniqueLabel(const Function &function, const std::string &base)
{
    std::string candidate = base;
    unsigned suffix = 0;
    auto labelExists = [&](const std::string &label)
    {
        for (const auto &block : function.blocks)
        {
            if (block.label == label)
                return true;
        }
        return false;
    };

    while (labelExists(candidate))
        candidate = base + "." + std::to_string(++suffix);
    return candidate;
}

/// @brief Check if header is a simple conditional branch block suitable for rotation.
/// @details The header must contain only non-side-effecting instructions followed
///          by a cbr terminator. All instructions before the cbr must be pure
///          (comparisons, arithmetic, casts) so they can be safely duplicated
///          into the latch block.
bool isRotatableHeader(const BasicBlock &header, const Loop &loop)
{
    if (header.instructions.empty())
        return false;

    const Instr &term = header.instructions.back();
    if (term.op != Opcode::CBr)
        return false;

    // Must have exactly two successors: one inside loop (body), one outside (exit)
    if (term.labels.size() != 2)
        return false;

    bool hasInside = false;
    bool hasOutside = false;
    for (const auto &label : term.labels)
    {
        if (loop.contains(label))
            hasInside = true;
        else
            hasOutside = true;
    }
    if (!hasInside || !hasOutside)
        return false;

    // All non-terminator instructions must be safe to duplicate:
    // pure value-producing instructions with no side effects.
    for (size_t i = 0; i + 1 < header.instructions.size(); ++i)
    {
        const auto &instr = header.instructions[i];
        if (!instr.result.has_value())
            return false;

        const auto &info = getOpcodeInfo(instr.op);
        // Reject instructions with side effects or that are terminators
        if (info.hasSideEffects || info.isTerminator)
            return false;
        // Reject memory operations (loads/stores/allocas) — not safe to duplicate
        if (hasMemoryRead(instr.op) || hasMemoryWrite(instr.op))
            return false;
        // Reject calls
        if (instr.op == Opcode::Call || instr.op == Opcode::CallIndirect)
            return false;
    }

    return true;
}

/// @brief Clone an instruction, remapping temporary IDs according to a mapping.
/// @param src Source instruction to clone.
/// @param remap Map from old temp IDs to new temp IDs.
/// @param nextId Counter for allocating new IDs.
/// @return Cloned instruction with remapped operands and result.
Instr cloneInstr(const Instr &src,
                 const std::unordered_map<unsigned, unsigned> &remap,
                 unsigned &nextId)
{
    Instr clone = src;

    // Remap result
    if (clone.result.has_value())
    {
        unsigned newId = nextId++;
        clone.result = newId;
    }

    // Remap operands
    for (auto &op : clone.operands)
    {
        if (op.kind == Value::Kind::Temp)
        {
            auto it = remap.find(op.id);
            if (it != remap.end())
                op.id = it->second;
        }
    }

    return clone;
}

/// @brief Attempt to rotate a single loop.
/// @return True if the loop was rotated.
bool rotateLoop(Function &function, const Loop &loop)
{
    // Require single latch and single exit for safety
    if (loop.latchLabels.size() != 1)
        return false;
    if (loop.exits.size() != 1)
        return false;

    size_t headerIdx = findBlockIndex(function, loop.headerLabel);
    if (headerIdx == SIZE_MAX)
        return false;

    // Check if the header is a simple rotatable conditional branch
    if (!isRotatableHeader(function.blocks[headerIdx], loop))
        return false;

    // Capture header state before modifications
    const std::string headerLabel = function.blocks[headerIdx].label;
    const std::vector<Param> headerParams = function.blocks[headerIdx].params;
    const std::vector<Instr> headerInstrs = function.blocks[headerIdx].instructions;
    const Instr &headerTerm = headerInstrs.back();

    // Identify the body successor (inside loop) and exit successor (outside loop)
    std::string bodySuccLabel;
    std::string exitLabel;
    size_t bodyBrArgIdx = SIZE_MAX;
    size_t exitBrArgIdx = SIZE_MAX;

    for (size_t i = 0; i < headerTerm.labels.size(); ++i)
    {
        if (loop.contains(headerTerm.labels[i]))
        {
            bodySuccLabel = headerTerm.labels[i];
            bodyBrArgIdx = i;
        }
        else
        {
            exitLabel = headerTerm.labels[i];
            exitBrArgIdx = i;
        }
    }

    if (bodySuccLabel.empty() || exitLabel.empty())
        return false;

    // Don't rotate if the body successor is the header itself (while(true) {})
    if (bodySuccLabel == headerLabel)
        return false;

    // Find the latch block
    size_t latchIdx = findBlockIndex(function, loop.latchLabels[0]);
    if (latchIdx == SIZE_MAX)
        return false;

    // The latch must end with br ^header(args)
    if (function.blocks[latchIdx].instructions.empty())
        return false;
    const Instr &latchTerm = function.blocks[latchIdx].instructions.back();
    if (latchTerm.op != Opcode::Br)
        return false;
    if (latchTerm.labels.size() != 1 || latchTerm.labels[0] != headerLabel)
        return false;

    // Collect the latch's branch arguments to the header
    std::vector<Value> latchArgs;
    if (!latchTerm.brArgs.empty())
        latchArgs = latchTerm.brArgs[0];

    // Collect outside-loop predecessors of the header (entry edges)
    struct EntryEdge
    {
        size_t blockIdx;
        size_t labelIdx;
        std::vector<Value> args;
    };
    std::vector<EntryEdge> entryEdges;

    for (size_t bi = 0; bi < function.blocks.size(); ++bi)
    {
        if (loop.contains(function.blocks[bi].label))
            continue;
        for (auto &instr : function.blocks[bi].instructions)
        {
            if (!viper::il::isTerminator(instr))
                continue;
            for (size_t li = 0; li < instr.labels.size(); ++li)
            {
                if (instr.labels[li] == headerLabel)
                {
                    std::vector<Value> args;
                    if (li < instr.brArgs.size())
                        args = instr.brArgs[li];
                    entryEdges.push_back({bi, li, args});
                }
            }
            break;
        }
    }

    if (entryEdges.empty())
        return false;

    unsigned nextId = viper::il::nextTempId(function);

    // === Step 1: Turn the header into a guard block ===
    // The guard block contains the header's instructions with the original entry
    // arguments, branching to the body on true or the exit on false.
    // We keep the header block as-is but redirect its body-successor to point
    // to a new rotated body block.

    // Create new guard label
    std::string guardLabel = makeUniqueLabel(function, headerLabel + ".guard");

    // Build guard block: clone of header instructions
    BasicBlock guard;
    guard.label = guardLabel;
    guard.params = headerParams; // Same params as original header

    // Re-ID the guard's params
    std::unordered_map<unsigned, unsigned> guardRemap;
    for (auto &param : guard.params)
    {
        unsigned newId = nextId++;
        guardRemap[param.id] = newId;
        param.id = newId;
        if (function.valueNames.size() <= param.id)
            function.valueNames.resize(param.id + 1);
        function.valueNames[param.id] = param.name;
    }

    // Clone header instructions into guard, remapping temp references
    for (const auto &instr : headerInstrs)
    {
        Instr clone = cloneInstr(instr, guardRemap, nextId);

        // Update remap with new result ID
        if (instr.result.has_value() && clone.result.has_value())
            guardRemap[*instr.result] = *clone.result;

        // For the cbr terminator, remap its body-successor branch args
        if (clone.op == Opcode::CBr)
        {
            // Remap branch args to use guard's remapped values
            for (auto &bundle : clone.brArgs)
            {
                for (auto &arg : bundle)
                {
                    if (arg.kind == Value::Kind::Temp)
                    {
                        auto it = guardRemap.find(arg.id);
                        if (it != guardRemap.end())
                            arg.id = it->second;
                    }
                }
            }
        }

        guard.instructions.push_back(std::move(clone));
    }
    guard.terminated = true;

    // === Step 2: Modify the latch to include header condition ===
    // Remove the latch's unconditional br ^header and replace with:
    //   [cloned header instructions using latch args]
    //   cbr %cond, ^bodySucc(next_args), ^exit(exit_args)

    // Build remap from header params to latch args
    std::unordered_map<unsigned, unsigned> latchRemap;
    for (size_t pi = 0; pi < headerParams.size() && pi < latchArgs.size(); ++pi)
    {
        if (latchArgs[pi].kind == Value::Kind::Temp)
            latchRemap[headerParams[pi].id] = latchArgs[pi].id;
    }

    // Remove the old latch terminator
    auto &latchInstrs = function.blocks[latchIdx].instructions;
    if (!latchInstrs.empty())
        latchInstrs.pop_back();

    // Clone header instructions into latch
    for (const auto &instr : headerInstrs)
    {
        Instr clone = cloneInstr(instr, latchRemap, nextId);

        if (instr.result.has_value() && clone.result.has_value())
            latchRemap[*instr.result] = *clone.result;

        if (clone.op == Opcode::CBr)
        {
            // Remap cbr operands
            for (auto &op : clone.operands)
            {
                if (op.kind == Value::Kind::Temp)
                {
                    auto it = latchRemap.find(op.id);
                    if (it != latchRemap.end())
                        op.id = it->second;
                }
            }

            // Redirect: body successor → bodySuccLabel, exit → exitLabel
            // The cbr's body-side should branch to the body successor with latch args
            // The exit-side keeps its target
            for (size_t li = 0; li < clone.labels.size(); ++li)
            {
                if (clone.labels[li] == bodySuccLabel)
                {
                    // Remap body branch args
                    if (li < clone.brArgs.size())
                    {
                        for (auto &arg : clone.brArgs[li])
                        {
                            if (arg.kind == Value::Kind::Temp)
                            {
                                auto it = latchRemap.find(arg.id);
                                if (it != latchRemap.end())
                                    arg.id = it->second;
                            }
                        }
                    }
                }
                else
                {
                    // Exit branch args
                    if (li < clone.brArgs.size())
                    {
                        for (auto &arg : clone.brArgs[li])
                        {
                            if (arg.kind == Value::Kind::Temp)
                            {
                                auto it = latchRemap.find(arg.id);
                                if (it != latchRemap.end())
                                    arg.id = it->second;
                            }
                        }
                    }
                }
            }
        }

        latchInstrs.push_back(std::move(clone));
    }
    function.blocks[latchIdx].terminated = true;

    // === Step 3: Redirect entry edges to the guard block ===
    for (const auto &edge : entryEdges)
    {
        auto &instr = function.blocks[edge.blockIdx].instructions.back();
        instr.labels[edge.labelIdx] = guardLabel;
    }

    // === Step 4: Remove the original header (it's now dead) ===
    // Don't actually remove it — let DCE/SimplifyCFG clean it up.
    // But we need to make it unreachable by clearing its instructions.
    // Actually, the latch no longer branches to it, and entry edges go to guard.
    // The header might still be referenced by the body successor if it was branching
    // back. But we redirected the latch. The header is now dead code.

    // Add the guard block to the function
    function.blocks.push_back(std::move(guard));

    return true;
}

} // namespace

std::string_view LoopRotate::id() const
{
    return "loop-rotate";
}

PreservedAnalyses LoopRotate::run(Function &function, AnalysisManager &analysis)
{
    auto &loopInfo = analysis.getFunctionResult<LoopInfo>(kAnalysisLoopInfo, function);

    bool changed = false;
    for (const Loop &loop : loopInfo.loops())
    {
        // Skip nested loops — only rotate outermost
        if (!loop.parentHeader.empty())
            continue;
        changed |= rotateLoop(function, loop);
    }

    if (!changed)
        return PreservedAnalyses::all();

    PreservedAnalyses preserved;
    preserved.preserveAllModules();
    return preserved;
}

} // namespace il::transform
