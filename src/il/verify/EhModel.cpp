//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/il/verify/EhModel.cpp
//
// Purpose:
//   Provide the concrete implementation of the EhModel helper which captures
//   a function's exception-handling structure for downstream verification
//   passes.
//
// Key invariants:
//   * The model does not mutate the underlying function.
//   * Successor queries rely on the label map populated during construction.
//
// Ownership/Lifetime:
//   The EhModel stores raw pointers to IR nodes owned by the caller and expects
//   them to remain valid for the model's lifetime.
//
// Links:
//   docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/verify/EhModel.hpp"

#include "il/verify/ControlFlowChecker.hpp"

#include <algorithm>

using namespace il::core;

namespace il::verify
{

/// @brief Capture exception-handling structure for @p function.
/// @details Builds label lookups for all basic blocks and records the entry
///          block so later analyses can answer reachability queries without
///          recomputing metadata.  The model stores raw pointers into the
///          original function and therefore must not outlive it.
/// @param function Function whose EH layout should be modelled.
EhModel::EhModel(const Function &function) : fn(&function)
{
    if (!function.blocks.empty())
        entryBlock = &function.blocks.front();

    blocks.reserve(function.blocks.size());
    for (const auto &block : function.blocks)
    {
        blocks[block.label] = &block;
        if (hasEh)
            continue;

        for (const auto &instr : block.instructions)
        {
            switch (instr.op)
            {
                case Opcode::EhPush:
                case Opcode::EhPop:
                case Opcode::EhEntry:
                case Opcode::ResumeSame:
                case Opcode::ResumeNext:
                case Opcode::ResumeLabel:
                case Opcode::Trap:
                case Opcode::TrapFromErr:
                    hasEh = true;
                    break;
                default:
                    break;
            }
            if (hasEh)
                break;
        }
    }
}

/// @brief Locate a basic block by its label.
/// @details Consults the pre-built label map and returns the corresponding
///          basic-block pointer when it exists.  Missing labels yield @c nullptr
///          so callers can report diagnostics without dereferencing invalid
///          pointers.
/// @param label Name of the basic block to retrieve.
/// @return Pointer to the block when present, otherwise nullptr.
const BasicBlock *EhModel::findBlock(const std::string &label) const
{
    auto it = blocks.find(label);
    if (it == blocks.end())
        return nullptr;
    return it->second;
}

/// @brief Enumerate successor blocks referenced by a terminator instruction.
/// @details Handles the various terminator flavours used by the IL (branch,
///          conditional branch, switch, resume variants, and trap).  Labels are
///          resolved through @ref findBlock so downstream checks receive direct
///          block pointers.  Missing labels are ignored to keep verification
///          resilient to malformed modules.
/// @param terminator Terminator instruction whose outgoing edges are requested.
/// @return Vector containing zero or more successor block pointers.
std::vector<const BasicBlock *> EhModel::gatherSuccessors(const Instr &terminator) const
{
    std::vector<const BasicBlock *> successors;
    switch (terminator.op)
    {
        case Opcode::Br:
            if (!terminator.labels.empty())
            {
                if (const BasicBlock *target = findBlock(terminator.labels[0]))
                    successors.push_back(target);
            }
            break;
        case Opcode::CBr:
        case Opcode::SwitchI32:
            for (const std::string &label : terminator.labels)
            {
                if (const BasicBlock *target = findBlock(label))
                    successors.push_back(target);
            }
            break;
        case Opcode::ResumeLabel:
            if (!terminator.labels.empty())
            {
                if (const BasicBlock *target = findBlock(terminator.labels[0]))
                    successors.push_back(target);
            }
            break;
        default:
            break;
    }
    return successors;
}

/// @brief Retrieve the terminator instruction for @p block.
/// @details Scans the block's instruction list for the last element and checks
///          whether it is a terminator.  Non-terminating blocks yield
///          @c nullptr, allowing callers to differentiate between fallthrough
///          and explicit control transfers.
/// @param block Basic block whose terminator is requested.
/// @return Pointer to the terminator instruction, or nullptr when absent.
const Instr *EhModel::findTerminator(const BasicBlock &block) const
{
    for (const auto &instr : block.instructions)
    {
        if (isTerminator(instr.op))
            return &instr;
    }
    return nullptr;
}

} // namespace il::verify
