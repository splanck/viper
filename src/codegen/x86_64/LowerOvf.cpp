//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the lowering pass that expands overflow-checked arithmetic pseudos
// (ADDOvfrr, SUBOvfrr, IMULOvfrr) into their real arithmetic instructions
// followed by a conditional branch to a trap block on signed overflow.
//
// The pass executes between IL->MIR lowering and register allocation, alongside
// the division lowering pass. It keeps operand usage confined to general-purpose
// registers and reuses a single trap block per function to minimise code growth.
//
// Pattern generated for each overflow-checked op:
//   ADDrr / SUBrr / IMULrr  dest, lhs, rhs
//   JO  .Ltrap_ovf_<funcname>
//
// The trap block calls rt_trap to abort execution.
//
//===----------------------------------------------------------------------===//

#include "MachineIR.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace viper::codegen::x64
{

namespace
{

/// @brief Locate a basic block index using its label, if present.
[[nodiscard]] std::optional<std::size_t> findBlock(const MFunction &fn, const std::string &label)
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

/// @brief Produce a shallow copy of a Machine IR operand.
[[nodiscard]] Operand cloneOp(const Operand &operand)
{
    return operand;
}

} // namespace

/// @brief Rewrite overflow-checked arithmetic pseudos into guarded sequences.
///
/// @details Walks each machine basic block looking for ADDOvfrr, SUBOvfrr, and
///          IMULOvfrr pseudo-ops. Each is replaced with the real arithmetic
///          instruction (ADDrr, SUBrr, IMULrr) followed by a JCC with overflow
///          condition to a shared trap block. The trap block calls rt_trap.
///
/// @param fn Machine IR function being rewritten in place.
void lowerOverflowOps(MFunction &fn)
{
    const std::string trapLabel = ".Ltrap_ovf_" + fn.name;
    std::optional<std::size_t> trapIndex{};

    auto ensureTrapBlock = [&]() -> std::size_t
    {
        if (trapIndex)
        {
            return *trapIndex;
        }

        if (auto existing = findBlock(fn, trapLabel))
        {
            trapIndex = *existing;
            return *trapIndex;
        }

        MBasicBlock trapBlock{};
        trapBlock.label = trapLabel;
        trapBlock.append(
            MInstr::make(MOpcode::CALL, std::vector<Operand>{makeLabelOperand("rt_trap")}));
        fn.blocks.push_back(std::move(trapBlock));
        trapIndex = fn.blocks.size() - 1U;
        return *trapIndex;
    };

    // Condition code 12 = "o" (overflow)
    constexpr int64_t kCondOverflow = 12;

    // Pre-scan: if any overflow pseudo exists, create the trap block up front
    // so that fn.blocks is not reallocated while we hold references into it.
    for (const auto &block : fn.blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.opcode == MOpcode::ADDOvfrr || instr.opcode == MOpcode::SUBOvfrr ||
                instr.opcode == MOpcode::IMULOvfrr)
            {
                ensureTrapBlock();
                goto rewrite; // trap block created, proceed to rewriting
            }
        }
    }
    return; // no overflow pseudos found

rewrite:
    // The trap block index is now stable — fn.blocks will not reallocate.
    const std::size_t blockCount = fn.blocks.size() - 1U; // exclude the trap block
    for (std::size_t blockIdx = 0; blockIdx < blockCount; ++blockIdx)
    {
        auto &block = fn.blocks[blockIdx];
        for (std::size_t i = 0; i < block.instructions.size(); ++i)
        {
            const MInstr &instr = block.instructions[i];
            MOpcode realOpc;
            switch (instr.opcode)
            {
                case MOpcode::ADDOvfrr:
                    realOpc = MOpcode::ADDrr;
                    break;
                case MOpcode::SUBOvfrr:
                    realOpc = MOpcode::SUBrr;
                    break;
                case MOpcode::IMULOvfrr:
                    realOpc = MOpcode::IMULrr;
                    break;
                default:
                    continue;
            }

            if (instr.operands.size() < 2U)
            {
                continue;
            }

            // Clone operands from the pseudo instruction
            std::vector<Operand> realOps;
            realOps.reserve(instr.operands.size());
            for (const auto &op : instr.operands)
            {
                realOps.push_back(cloneOp(op));
            }

            // Replace pseudo with real arithmetic instruction
            block.instructions[i] = MInstr::make(realOpc, std::move(realOps));

            // Insert JO (jump on overflow) to trap block after the arithmetic
            auto joInstr = MInstr::make(
                MOpcode::JCC,
                std::vector<Operand>{makeImmOperand(kCondOverflow), makeLabelOperand(trapLabel)});
            block.instructions.insert(
                block.instructions.begin() + static_cast<std::ptrdiff_t>(i + 1U),
                std::move(joInstr));

            // Skip past the JO we just inserted
            ++i;
        }
    }
}

} // namespace viper::codegen::x64
