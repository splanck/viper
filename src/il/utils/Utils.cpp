//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements lightweight inspection helpers used across IL analysis and
// transformation passes. The functions operate on `BasicBlock` and `Instr`
// structures without introducing additional dependencies, making them safe to
// include from most layers of the compiler.
//
//===----------------------------------------------------------------------===//
#include "il/utils/Utils.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"

namespace viper::il
{

/// @brief Determine whether instruction @p I resides in block @p B.
///
/// Performs a linear scan over the block's instruction list and compares raw
/// addresses for equality. The helper intentionally avoids more expensive IR
/// queries because callers typically use it in assertion checks or debug-only
/// validation.
///
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
///
/// Blocks store their terminator, when present, as the final instruction. This
/// helper checks the last instruction and returns it when `isTerminator`
/// confirms the opcode category.
///
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
///
/// Recognises the opcodes that end a basic block according to the IL
/// specification (branches, returns, traps, and EH resumes). Anything outside
/// that set is treated as a non-terminator.
///
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
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
            return true;
        default:
            return false;
    }
}

} // namespace viper::il
