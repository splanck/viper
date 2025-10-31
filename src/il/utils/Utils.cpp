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
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"

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

} // namespace viper::il
