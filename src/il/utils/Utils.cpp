//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/utils/Utils.cpp
// Purpose: Provide shared helper utilities for inspecting IL blocks and
//          instructions without pulling in heavy analysis headers.
// Links: docs/codemap.md#il-utils
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements lightweight inspection helpers for IL blocks.
/// @details Centralising the helpers here keeps cross-cutting utilities such as
///          `belongsToBlock` and `isTerminator` available without bloating the
///          headers included throughout the compiler pipeline.
#include "il/utils/Utils.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"

#include <algorithm>

namespace viper::il
{

/// @brief Determine whether instruction @p I resides in block @p B.
/// @details Performs a linear scan over the block's instruction list and
///          compares addresses for equality.  The helper intentionally avoids
///          more expensive IR queries because callers typically use it in
///          assertion checks or debug-only validation contexts.
/// @param I Instruction to locate.
/// @param B Candidate parent block.
/// @return True when @p I is one of @p B's instructions; false otherwise.
bool belongsToBlock(const Instruction &I, const Block &B)
{
    for (const auto &inst : B.instructions)
    {
        if (&inst == &I)
        {
            return true;
        }
    }
    return false;
}

/// @brief Fetch the terminating instruction of @p B, if any.
/// @details Blocks store their terminator, when present, as the final
///          instruction.  The helper checks the last instruction and returns it
///          when @ref isTerminator confirms the opcode category.  Returning
///          `nullptr` when the block lacks a terminator allows callers to gate
///          validation logic without exceptions.
/// @param B Block to inspect.
/// @return Pointer to the terminator instruction or nullptr if the block is
///         empty or the last instruction is non-terminating.
Instruction *terminator(Block &B)
{
    if (B.instructions.empty())
    {
        return nullptr;
    }
    Instruction &last = B.instructions.back();
    return isTerminator(last) ? &last : nullptr;
}

/// @brief Classify whether instruction @p I terminates control flow.
/// @details Recognises the opcodes that end a basic block according to the IL
///          specification (branches, returns, traps, and EH resumes).  Anything
///          outside that set is treated as a non-terminator.  Keeping the logic
///          centralised ensures passes agree on which instructions must end
///          basic blocks.
/// @param I Instruction to inspect.
/// @return True when @p I.op is a terminating opcode; false otherwise.
bool isTerminator(const Instruction &I)
{
    using ::il::core::Opcode;
    switch (I.op)
    {
        case Opcode::Br:
        case Opcode::CBr:
        case Opcode::SwitchI32:
        case Opcode::Ret:
        case Opcode::Trap:
        case Opcode::TrapFromErr:
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
            return true;
        default:
            return false;
    }
}

/// @brief Replace all uses of temporary @p tempId with @p replacement in @p F.
/// @details Iterates every instruction and branch argument list, swapping
///          occurrences of temporary @p tempId with the replacement value.
///          This helper is used when optimization passes eliminate instructions
///          and need to redirect all consumers to a new SSA value.
/// @param F Function whose instructions are rewritten.
/// @param tempId Temporary identifier to replace.
/// @param replacement Replacement value assigned to each occurrence.
/// @sideeffect Mutates operands and branch arguments in place.
void replaceAllUses(::il::core::Function &F, unsigned tempId, const ::il::core::Value &replacement)
{
    using ::il::core::Value;

    for (auto &B : F.blocks)
    {
        for (auto &I : B.instructions)
        {
            // Replace in instruction operands
            for (auto &Op : I.operands)
            {
                if (Op.kind == Value::Kind::Temp && Op.id == tempId)
                {
                    Op = replacement;
                }
            }

            // Replace in branch arguments
            for (auto &argList : I.brArgs)
            {
                for (auto &Arg : argList)
                {
                    if (Arg.kind == Value::Kind::Temp && Arg.id == tempId)
                    {
                        Arg = replacement;
                    }
                }
            }
        }
    }
}

/// @brief Compute the next unused temporary identifier in function @p F.
/// @details Inspects procedure parameters, block parameters, instruction
///          results, operands, and branch arguments to find the highest-numbered
///          temporary currently in use.  The returned value is one greater than
///          that maximum and therefore safe to assign to newly introduced SSA
///          values.  Used by transformation passes that need to allocate fresh
///          temporaries without causing identifier collisions.
/// @param F Function to inspect.
/// @return First unused temporary identifier (max + 1).
unsigned nextTempId(const ::il::core::Function &F)
{
    using ::il::core::Value;

    unsigned next = 0;
    auto update = [&](unsigned v) { next = std::max(next, v + 1); };

    // Scan function parameters
    for (const auto &p : F.params)
    {
        update(p.id);
    }

    // Scan blocks
    for (const auto &B : F.blocks)
    {
        // Scan block parameters
        for (const auto &p : B.params)
        {
            update(p.id);
        }

        // Scan instructions
        for (const auto &I : B.instructions)
        {
            // Check instruction result
            if (I.result)
            {
                update(*I.result);
            }

            // Check operands
            for (const auto &Op : I.operands)
            {
                if (Op.kind == Value::Kind::Temp)
                {
                    update(Op.id);
                }
            }

            // Check branch arguments
            for (const auto &argList : I.brArgs)
            {
                for (const auto &Arg : argList)
                {
                    if (Arg.kind == Value::Kind::Temp)
                    {
                        update(Arg.id);
                    }
                }
            }
        }
    }

    return next;
}

} // namespace viper::il
