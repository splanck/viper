//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/LowerOvf.cpp
// Purpose: Expand overflow-checked arithmetic pseudos (ADDOvfrr, SUBOvfrr,
//          IMULOvfrr) into real instructions with a conditional trap branch.
// Key invariants:
//   - Each overflow-checked op emits: <arith> dest, lhs, rhs; JO .Ltrap_ovf.
//   - A single trap block per function is reused to minimise code growth.
//   - The pass executes between IL→MIR lowering and register allocation.
// Ownership/Lifetime:
//   - Mutates the MFunction in-place; no persistent auxiliary structures.
// Links: codegen/x86_64/LowerILToMIR.hpp,
//        codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#include "MachineIR.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace viper::codegen::x64 {

namespace {

/// @brief Locate a basic block index using its label, if present.
[[nodiscard]] std::optional<std::size_t> findBlock(const MFunction &fn, const std::string &label) {
    for (std::size_t idx = 0; idx < fn.blocks.size(); ++idx) {
        if (fn.blocks[idx].label == label) {
            return idx;
        }
    }
    return std::nullopt;
}

/// @brief Produce a shallow copy of a Machine IR operand.
[[nodiscard]] Operand cloneOp(const Operand &operand) {
    return operand;
}

} // namespace

/// @brief Rewrite overflow-checked arithmetic pseudos into guarded sequences.
///
/// @details Walks each machine basic block looking for ADDOvfrr, SUBOvfrr, and
///          IMULOvfrr pseudo-ops. Each is replaced with the real arithmetic
///          instruction (ADDrr, SUBrr, IMULrr) followed by a JCC with overflow
///          condition to a shared trap block. The trap block calls
///          rt_trap_ovf, which matches the no-argument lowering pattern.
///
/// @param fn Machine IR function being rewritten in place.
void lowerOverflowOps(MFunction &fn) {
    const std::string trapLabel = ".Ltrap_ovf_" + fn.name;
    std::optional<std::size_t> trapIndex{};

    auto ensureTrapBlock = [&]() -> std::size_t {
        if (trapIndex) {
            return *trapIndex;
        }

        if (auto existing = findBlock(fn, trapLabel)) {
            trapIndex = *existing;
            auto &trapBlock = fn.blocks[*trapIndex];
            const bool hasRuntimeCall = std::any_of(
                trapBlock.instructions.begin(),
                trapBlock.instructions.end(),
                [](const MInstr &instr) {
                    if (instr.opcode != MOpcode::CALL || instr.operands.empty()) {
                        return false;
                    }
                    const auto *label = std::get_if<OpLabel>(&instr.operands.front());
                    return label && label->name == "rt_trap_ovf";
                });
            auto ud2It = std::find_if(
                trapBlock.instructions.begin(),
                trapBlock.instructions.end(),
                [](const MInstr &instr) { return instr.opcode == MOpcode::UD2; });
            if (!hasRuntimeCall) {
                MInstr call =
                    MInstr::make(MOpcode::CALL, std::vector<Operand>{makeLabelOperand("rt_trap_ovf")});
                if (ud2It != trapBlock.instructions.end()) {
                    ud2It = trapBlock.instructions.insert(ud2It, std::move(call));
                    ++ud2It;
                } else {
                    trapBlock.append(std::move(call));
                    ud2It = trapBlock.instructions.end();
                }
            }
            if (ud2It == trapBlock.instructions.end()) {
                trapBlock.append(MInstr::make(MOpcode::UD2));
            }
            return *trapIndex;
        }

        MBasicBlock trapBlock{};
        trapBlock.label = trapLabel;
        trapBlock.append(
            MInstr::make(MOpcode::CALL, std::vector<Operand>{makeLabelOperand("rt_trap_ovf")}));
        trapBlock.append(MInstr::make(MOpcode::UD2));
        fn.blocks.push_back(std::move(trapBlock));
        trapIndex = fn.blocks.size() - 1U;
        return *trapIndex;
    };

    // Condition code 12 = "o" (overflow)
    constexpr int64_t kCondOverflow = 12;

    // Pre-scan: if any overflow pseudo exists, create the trap block up front
    // so that fn.blocks is not reallocated while we hold references into it.
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.opcode == MOpcode::ADDOvfrr || instr.opcode == MOpcode::SUBOvfrr ||
                instr.opcode == MOpcode::IMULOvfrr) {
                ensureTrapBlock();
                goto rewrite; // trap block created, proceed to rewriting
            }
        }
    }
    return; // no overflow pseudos found

rewrite:
    // The trap block index is now stable — fn.blocks will not reallocate.
    // Do not assume the trap block is last: division lowering can create the
    // same overflow trap before appending continuation blocks.
    const std::size_t trapBlockIndex = *trapIndex;
    for (std::size_t blockIdx = 0; blockIdx < fn.blocks.size(); ++blockIdx) {
        if (blockIdx == trapBlockIndex) {
            continue;
        }
        auto &block = fn.blocks[blockIdx];
        for (std::size_t i = 0; i < block.instructions.size(); ++i) {
            const MInstr &instr = block.instructions[i];
            MOpcode realOpc;
            switch (instr.opcode) {
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

            if (instr.operands.size() < 2U) {
                continue;
            }

            // Clone operands from the pseudo instruction
            std::vector<Operand> realOps;
            realOps.reserve(instr.operands.size());
            for (const auto &op : instr.operands) {
                realOps.push_back(cloneOp(op));
            }

            // Replace pseudo with real arithmetic instruction
            block.instructions[i] = MInstr::make(realOpc, std::move(realOps));

            // Insert JO (jump on overflow) to trap block after the arithmetic
            auto joInstr = MInstr::make(
                MOpcode::JCC,
                std::vector<Operand>{makeImmOperand(kCondOverflow), makeLabelOperand(trapLabel)});
            block.instructions.insert(block.instructions.begin() +
                                          static_cast<std::ptrdiff_t>(i + 1U),
                                      std::move(joInstr));

            // Skip past the JO we just inserted
            ++i;
        }
    }
}

} // namespace viper::codegen::x64
