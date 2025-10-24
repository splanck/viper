// src/codegen/x86_64/LowerDiv.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Lower signed 64-bit division and remainder pseudos into concrete
//          x86-64 Machine IR sequences that guard against division by zero and
//          call the runtime trap when necessary.
// Invariants: Only integer GPR virtual registers are expected as operands.
//             Lowering materialises a shared trap block per function and
//             preserves the order of subsequent instructions via explicit
//             continuation blocks.
// Ownership: Functions operate on Machine IR structures passed by reference
//            without taking ownership.
// Notes: Phase A helper intended to run after IL→MIR lowering and before
//        register allocation.

#include "MachineIR.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace viper::codegen::x64
{

namespace
{
constexpr std::string_view kTrapLabel{".Ltrap_div0"};

[[nodiscard]] Operand cloneOperand(const Operand &operand)
{
    return operand;
}

[[nodiscard]] std::optional<std::size_t> findBlockIndex(const MFunction &fn, std::string_view label)
{
    for (std::size_t idx = 0; idx < fn.blocks.size(); ++idx)
    {
        if (fn.blocks[idx].label == label)
        {
            return idx;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string makeContinuationLabel(const MFunction &fn,
                                                const MBasicBlock &block,
                                                unsigned sequence)
{
    std::string base;
    if (!block.label.empty())
    {
        base = block.label;
    }
    else if (!fn.name.empty())
    {
        base = fn.name;
    }
    else
    {
        base = ".Ldiv";
    }
    base += ".div.";
    base += std::to_string(sequence);
    base += ".after";
    return base;
}

[[nodiscard]] Operand makePhysRegOperand(PhysReg reg)
{
    return x64::makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(reg));
}
} // namespace

/// \brief Lower signed division and remainder pseudos into guarded IDIV sequences.
/// \details Rewrites DIVS64rr/REMS64rr pseudos into a guard that tests the divisor
///          for zero, branches to a shared trap block on zero, and otherwise
///          performs the CQO/IDIV sequence using the SysV ABI registers.
void lowerSignedDivRem(MFunction &fn)
{
    const std::string trapLabel{std::string{kTrapLabel}};
    std::optional<std::size_t> trapIndex{};
    unsigned sequenceId{0U};

    auto ensureTrapBlock = [&]() -> std::size_t
    {
        if (trapIndex)
        {
            return *trapIndex;
        }

        if (auto existing = findBlockIndex(fn, trapLabel))
        {
            trapIndex = *existing;
            auto &trapBlock = fn.blocks[*trapIndex];
            const bool hasCall =
                std::any_of(trapBlock.instructions.begin(),
                            trapBlock.instructions.end(),
                            [](const MInstr &instr) { return instr.opcode == MOpcode::CALL; });
            if (!hasCall)
            {
                trapBlock.append(MInstr::make(
                    MOpcode::CALL, std::vector<Operand>{makeLabelOperand("rt_trap_div0")}));
            }
            return *trapIndex;
        }

        MBasicBlock trapBlock{};
        trapBlock.label = trapLabel;
        trapBlock.append(
            MInstr::make(MOpcode::CALL, std::vector<Operand>{makeLabelOperand("rt_trap_div0")}));
        fn.blocks.push_back(std::move(trapBlock));
        trapIndex = fn.blocks.size() - 1U;
        return *trapIndex;
    };

    for (std::size_t blockIdx = 0; blockIdx < fn.blocks.size(); ++blockIdx)
    {
        auto &block = fn.blocks[blockIdx];
        for (std::size_t instrIdx = 0; instrIdx < block.instructions.size(); ++instrIdx)
        {
            const MInstr &candidate = block.instructions[instrIdx];
            if (candidate.opcode != MOpcode::DIVS64rr && candidate.opcode != MOpcode::REMS64rr)
            {
                continue;
            }

            if (candidate.operands.size() < 3U)
            {
                continue; // Phase A expectation: dest, dividend, divisor.
            }

            if (!std::holds_alternative<OpReg>(candidate.operands[0]))
            {
                continue; // Destination must be a virtual register.
            }

            const Operand &dividendOp = candidate.operands[1];
            const Operand &divisorOp = candidate.operands[2];

            const bool dividendSupported = std::holds_alternative<OpReg>(dividendOp) ||
                                           std::holds_alternative<OpImm>(dividendOp);
            if (!dividendSupported)
            {
                continue; // Phase A: expect register or immediate dividend.
            }

            if (!std::holds_alternative<OpReg>(divisorOp))
            {
                continue; // Phase A: divisor must be a register operand.
            }

            MInstr pseudo = std::move(block.instructions[instrIdx]);
            const bool isDiv = pseudo.opcode == MOpcode::DIVS64rr;

            MBasicBlock afterBlock{};
            afterBlock.label = makeContinuationLabel(fn, block, sequenceId++);

            const auto tailBegin =
                block.instructions.begin() + static_cast<std::ptrdiff_t>(instrIdx + 1U);
            afterBlock.instructions.assign(std::make_move_iterator(tailBegin),
                                           std::make_move_iterator(block.instructions.end()));
            block.instructions.erase(tailBegin, block.instructions.end());
            block.instructions.erase(block.instructions.begin() +
                                     static_cast<std::ptrdiff_t>(instrIdx));

            ensureTrapBlock();

            const Operand destOp = cloneOperand(pseudo.operands[0]);
            const Operand dividendClone = cloneOperand(dividendOp);
            const Operand divisorClone = cloneOperand(divisorOp);

            block.append(MInstr::make(
                MOpcode::TESTrr,
                std::vector<Operand>{cloneOperand(divisorClone), cloneOperand(divisorClone)}));
            block.append(
                MInstr::make(MOpcode::JCC,
                             std::vector<Operand>{makeImmOperand(0), makeLabelOperand(trapLabel)}));

            const Operand raxOp = makePhysRegOperand(PhysReg::RAX);
            const Operand rdxOp = makePhysRegOperand(PhysReg::RDX);

            if (std::holds_alternative<OpImm>(dividendClone))
            {
                block.append(MInstr::make(
                    MOpcode::MOVri,
                    std::vector<Operand>{cloneOperand(raxOp), cloneOperand(dividendClone)}));
            }
            else
            {
                block.append(MInstr::make(
                    MOpcode::MOVrr,
                    std::vector<Operand>{cloneOperand(raxOp), cloneOperand(dividendClone)}));
            }

            block.append(MInstr::make(MOpcode::CQO, {}));
            block.append(
                MInstr::make(MOpcode::IDIVrm, std::vector<Operand>{cloneOperand(divisorClone)}));

            const Operand resultPhys = isDiv ? raxOp : rdxOp;
            block.append(
                MInstr::make(MOpcode::MOVrr,
                             std::vector<Operand>{cloneOperand(destOp), cloneOperand(resultPhys)}));

            block.append(MInstr::make(MOpcode::JMP,
                                      std::vector<Operand>{makeLabelOperand(afterBlock.label)}));

            fn.blocks.push_back(std::move(afterBlock));

            instrIdx = block.instructions.size();
        }
    }
}

} // namespace viper::codegen::x64
