// File: src/il/utils/Utils.cpp
// Purpose: Implement small IL helper utilities for blocks and instructions.
// Key invariants: Operates on il_core structures without additional dependencies.
// Ownership/Lifetime: Non-owning views; caller manages lifetimes.
// Links: docs/dev/analysis.md
#include "il/utils/Utils.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"

namespace viper::il
{

/// @brief Determine whether instruction @p I resides in block @p B.
///
/// Performs a linear scan over @p B's `instructions` list from
/// `il::core::BasicBlock` and compares addresses for equality. This does
/// not traverse successor blocks or perform any structural checks beyond
/// membership.
///
/// @param I Instruction to locate (an `il::core::Instr`).
/// @param B Candidate parent block (an `il::core::BasicBlock`).
/// @return True if @p I is contained in @p B.instructions; false otherwise.
/// @invariant The block's `instructions` vector enumerates each instruction
/// once and owns their storage.
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
/// The function examines the final element of @p B.instructions and delegates
/// to ::viper::il::isTerminator to classify it. Blocks are expected to hold
/// any terminator as their last instruction, mirroring the layout of
/// `il::core::BasicBlock`.
///
/// @param B Block to inspect.
/// @return Pointer to the terminator instruction or nullptr if @p B is empty
/// or the last instruction is not a terminator.
/// @invariant If non-null, the returned instruction is exactly the last entry
/// in @p B.instructions.
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
/// Recognizes the `Br`, `CBr`, `Ret`, and `Trap` opcodes from
/// `il::core::Opcode`, which are the only terminators permitted in a
/// `il::core::BasicBlock` per the IL specification.
///
/// @param I Instruction to inspect.
/// @return True if @p I.op is one of the terminator opcodes; false otherwise.
/// @invariant @p I.op must be a valid member of `il::core::Opcode`.
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
