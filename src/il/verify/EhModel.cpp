//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

const BasicBlock *EhModel::findBlock(const std::string &label) const
{
    auto it = blocks.find(label);
    if (it == blocks.end())
        return nullptr;
    return it->second;
}

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

