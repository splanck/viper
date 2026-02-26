//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the lowering pass that expands signed and unsigned 64-bit division
// and remainder pseudos into explicit CQO/IDIV or XOR/DIV sequences for the
// x86-64 backend.  The implementation guards each operation with a
// division-by-zero test, branching to a lazily created trap block when necessary
// so runtime behaviour matches the VM's expectations.
//
// The pass executes between IL→MIR lowering and register allocation.  It keeps
// operand usage confined to general-purpose registers, builds continuation
// blocks to preserve instruction order, and reuses a single trap block per
// function to minimise code growth.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Division lowering utilities for the Phase A x86-64 backend.
/// @details Contains helpers for cloning operands, locating trap blocks, and
///          synthesising continuation labels so lowered control flow mirrors the
///          pseudo-instruction semantics emitted earlier in the pipeline.

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

/// @brief Produce a shallow copy of a Machine IR operand.
///
/// @details Operands in Phase A Machine IR are small value types, so copying by
///          value preserves all necessary information when duplicating operands
///          across newly emitted instructions.  The helper centralises the
///          intent and documents that no deep cloning is required.
///
/// @param operand Operand instance to duplicate.
/// @return Copy of @p operand that can be reused in emitted instructions.
[[nodiscard]] Operand cloneOperand(const Operand &operand)
{
    return operand;
}

/// @brief Locate a basic block index using its label, if present.
///
/// @details Iterates over @p fn until it finds a block whose @c label matches
///          the requested string.  The search allows the lowering pass to reuse
///          an existing trap block rather than materialising a duplicate.
///
/// @param fn Function currently being rewritten.
/// @param label Block label to search for.
/// @return Index of the block or empty optional when no block matches.
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

/// @brief Generate a unique label for the continuation block after a pseudo.
///
/// @details Prefers reusing the source block or function name to keep emitted
///          labels stable between compilations.  When neither is available a
///          synthetic prefix is used.  The @p sequence counter differentiates
///          multiple lowered pseudos originating from the same block.
///
/// @param fn Function currently being processed.
/// @param block Basic block that owned the pseudo instruction.
/// @param sequence Running identifier incremented per lowered pseudo.
/// @return Deterministic label for the continuation block.
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

/// @brief Create an operand referencing a physical general-purpose register.
///
/// @details The lowered IDIV sequence uses the SysV ABI registers RAX and RDX.
///          This helper ensures the correct register class is used whenever the
///          sequence materialises operands for those registers.
///
/// @param reg Physical register enumerator.
/// @return Operand representing @p reg.
[[nodiscard]] Operand makePhysRegOperand(PhysReg reg)
{
    return x64::makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(reg));
}

/// @brief Return log2(v) if v is a positive power of 2, else -1.
[[nodiscard]] int log2IfPowerOf2(int64_t v)
{
    if (v <= 0 || (v & (v - 1)) != 0)
        return -1;
    int log = 0;
    while ((1LL << log) != v)
        ++log;
    return log;
}

/// @brief Scan backward in a block for a MOVri that loads into the given vreg.
/// @return The immediate value, or nullopt if not found.
[[nodiscard]] std::optional<int64_t> findVRegConstant(const MBasicBlock &block,
                                                      std::size_t beforeIdx,
                                                      const Operand &regOp)
{
    if (!std::holds_alternative<OpReg>(regOp))
        return std::nullopt;
    const auto &target = std::get<OpReg>(regOp);
    // Only look for non-physical vregs
    if (target.isPhys)
        return std::nullopt;

    for (std::size_t i = beforeIdx; i > 0; --i)
    {
        const auto &instr = block.instructions[i - 1];
        if (instr.opcode == MOpcode::MOVri && instr.operands.size() >= 2)
        {
            if (std::holds_alternative<OpReg>(instr.operands[0]))
            {
                const auto &dst = std::get<OpReg>(instr.operands[0]);
                if (dst.cls == target.cls && dst.idOrPhys == target.idOrPhys && !dst.isPhys)
                {
                    if (std::holds_alternative<OpImm>(instr.operands[1]))
                        return std::get<OpImm>(instr.operands[1]).val;
                }
            }
        }
        // If the vreg is redefined by another instruction, stop looking.
        if (instr.operands.size() >= 1 && std::holds_alternative<OpReg>(instr.operands[0]))
        {
            const auto &dst = std::get<OpReg>(instr.operands[0]);
            if (dst.cls == target.cls && dst.idOrPhys == target.idOrPhys && !dst.isPhys)
                break;
        }
    }
    return std::nullopt;
}

} // namespace

/// @brief Rewrite division and remainder pseudos into explicit guarded sequences.
///
/// @details Walks each machine basic block in search of signed or unsigned
///          integer division pseudos.  Matching instructions are replaced with a
///          guarded control-flow pattern: the divisor is tested for zero, a
///          shared trap block is invoked when necessary, and otherwise the
///          CQO/IDIV (signed) or XOR/DIV (unsigned) sequence executes using the
///          SysV register convention.  The
///          remaining instructions from the original block are moved into a
///          freshly created continuation block so the program order remains
///          intact after the branch sequence.  A single trap block is allocated
///          lazily and reused for every lowered pseudo within the function.
///
/// @param fn Machine IR function being rewritten in place.
void lowerSignedDivRem(MFunction &fn)
{
    // Make trap label unique per function to avoid conflicts when assembling
    const std::string trapLabel = ".Ltrap_div0_" + fn.name;
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
            const bool isSignedDiv = candidate.opcode == MOpcode::DIVS64rr;
            const bool isSignedRem = candidate.opcode == MOpcode::REMS64rr;
            const bool isUnsignedDiv = candidate.opcode == MOpcode::DIVU64rr;
            const bool isUnsignedRem = candidate.opcode == MOpcode::REMU64rr;
            if (!isSignedDiv && !isSignedRem && !isUnsignedDiv && !isUnsignedRem)
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

            // ── Power-of-2 fast path for unsigned division/remainder ──────
            // Unsigned div by constant power-of-2: replace IDIV with SHR.
            // Unsigned rem by constant power-of-2: replace IDIV with AND mask.
            // This avoids the expensive IDIV (20-40 cycle latency).
            if (isUnsignedDiv || isUnsignedRem)
            {
                auto constVal = findVRegConstant(block, instrIdx, divisorOp);
                if (constVal)
                {
                    int log = log2IfPowerOf2(*constVal);
                    if (log >= 0 && log <= 63)
                    {
                        const Operand destClone = cloneOperand(candidate.operands[0]);
                        const Operand dividendClone = cloneOperand(dividendOp);

                        // Move dividend to dest first (SHR/AND are in-place).
                        if (std::holds_alternative<OpImm>(dividendClone))
                        {
                            block.instructions[instrIdx] =
                                MInstr::make(MOpcode::MOVri,
                                             std::vector<Operand>{cloneOperand(destClone),
                                                                  cloneOperand(dividendClone)});
                        }
                        else
                        {
                            block.instructions[instrIdx] =
                                MInstr::make(MOpcode::MOVrr,
                                             std::vector<Operand>{cloneOperand(destClone),
                                                                  cloneOperand(dividendClone)});
                        }

                        if (isUnsignedDiv)
                        {
                            // udiv x, 2^k  ->  shr x, k
                            block.instructions.insert(
                                block.instructions.begin() +
                                    static_cast<std::ptrdiff_t>(instrIdx + 1),
                                MInstr::make(MOpcode::SHRri,
                                             std::vector<Operand>{cloneOperand(destClone),
                                                                  makeImmOperand(log)}));
                        }
                        else
                        {
                            // urem x, 2^k  ->  and x, (2^k - 1)
                            block.instructions.insert(
                                block.instructions.begin() +
                                    static_cast<std::ptrdiff_t>(instrIdx + 1),
                                MInstr::make(MOpcode::ANDri,
                                             std::vector<Operand>{cloneOperand(destClone),
                                                                  makeImmOperand(*constVal - 1)}));
                        }
                        instrIdx += 1; // skip the inserted instruction
                        continue;
                    }
                }
            }

            MInstr pseudo = std::move(block.instructions[instrIdx]);
            const bool isDiv = isSignedDiv || isUnsignedDiv;
            const bool isSigned = isSignedDiv || isSignedRem;

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

            auto &currentBlock = fn.blocks[blockIdx];

            const Operand destOp = cloneOperand(pseudo.operands[0]);
            const Operand dividendClone = cloneOperand(dividendOp);
            const Operand divisorClone = cloneOperand(divisorOp);

            currentBlock.append(MInstr::make(
                MOpcode::TESTrr,
                std::vector<Operand>{cloneOperand(divisorClone), cloneOperand(divisorClone)}));
            currentBlock.append(
                MInstr::make(MOpcode::JCC,
                             std::vector<Operand>{makeImmOperand(0), makeLabelOperand(trapLabel)}));

            const Operand raxOp = makePhysRegOperand(PhysReg::RAX);
            const Operand rdxOp = makePhysRegOperand(PhysReg::RDX);

            if (std::holds_alternative<OpImm>(dividendClone))
            {
                currentBlock.append(MInstr::make(
                    MOpcode::MOVri,
                    std::vector<Operand>{cloneOperand(raxOp), cloneOperand(dividendClone)}));
            }
            else
            {
                currentBlock.append(MInstr::make(
                    MOpcode::MOVrr,
                    std::vector<Operand>{cloneOperand(raxOp), cloneOperand(dividendClone)}));
            }

            if (isSigned)
            {
                currentBlock.append(MInstr::make(MOpcode::CQO, {}));
                currentBlock.append(MInstr::make(MOpcode::IDIVrm,
                                                 std::vector<Operand>{cloneOperand(divisorClone)}));
            }
            else
            {
                currentBlock.append(
                    MInstr::make(MOpcode::XORrr32,
                                 std::vector<Operand>{cloneOperand(rdxOp), cloneOperand(rdxOp)}));
                currentBlock.append(
                    MInstr::make(MOpcode::DIVrm, std::vector<Operand>{cloneOperand(divisorClone)}));
            }

            const Operand resultPhys = isDiv ? raxOp : rdxOp;
            currentBlock.append(
                MInstr::make(MOpcode::MOVrr,
                             std::vector<Operand>{cloneOperand(destOp), cloneOperand(resultPhys)}));

            currentBlock.append(MInstr::make(
                MOpcode::JMP, std::vector<Operand>{makeLabelOperand(afterBlock.label)}));

            fn.blocks.push_back(std::move(afterBlock));

            instrIdx = currentBlock.instructions.size();
        }
    }
}

} // namespace viper::codegen::x64
