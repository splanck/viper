//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/ISel.cpp
// Purpose: Define instruction selection helpers that map pseudo Machine IR
//          emitted by LowerILToMIR into concrete x86-64 encodings for the Phase
//          A backend experiment.
// Key invariants: Transformations preserve instruction ordering while rewriting
//                 opcode/operand combinations to legal encodings (e.g. `cmp`
//                 immediate forms or inserting `movzx` after `setcc`). Resulting
//                 instruction streams remain valid for register allocation and
//                 emission.
// Ownership/Lifetime: Operates entirely in-place on Machine IR graphs borrowed
//                     from callers without allocating persistent auxiliary
//                     structures.
// Links: docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "ISel.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace viper::codegen::x64
{

namespace
{

/// @brief Produce a copy of a Machine IR operand.
///
/// @details Machine IR operands are small value types.  This helper exists to
///          make clone intent explicit at call sites where code constructs new
///          instructions from existing operands (for example inserting a
///          `movzx` after a `setcc`).
///
/// @param operand Operand to copy.
/// @return A value-equal copy of @p operand.
[[nodiscard]] Operand cloneOperand(const Operand &operand)
{
    return operand;
}

/// @brief Determine whether an operand stores an immediate value.
///
/// @param operand Operand to classify.
/// @return @c true when the operand holds an @ref OpImm payload.
[[nodiscard]] bool isImm(const Operand &operand) noexcept
{
    return std::holds_alternative<OpImm>(operand);
}

/// @brief View an operand as an immediate when possible.
///
/// @details Wraps @ref std::get_if to centralise the cast and emphasise the
///          nullable nature of the conversion.
///
/// @param operand Operand to reinterpret.
/// @return Pointer to the @ref OpImm payload or @c nullptr on mismatch.
[[nodiscard]] OpImm *asImm(Operand &operand) noexcept
{
    return std::get_if<OpImm>(&operand);
}

/// @brief View a mutable operand as a register reference.
///
/// @param operand Operand to reinterpret.
/// @return Pointer to the @ref OpReg payload or @c nullptr when not a register.
[[nodiscard]] OpReg *asReg(Operand &operand) noexcept
{
    return std::get_if<OpReg>(&operand);
}

/// @brief View a read-only operand as a register reference.
///
/// @param operand Operand to reinterpret.
/// @return Pointer to the @ref OpReg payload or @c nullptr when not a register.
[[nodiscard]] const OpReg *asReg(const Operand &operand) noexcept
{
    return std::get_if<OpReg>(&operand);
}

/// @brief Compare two operands for register identity.
///
/// @details The check covers both physical and virtual registers by comparing
///          the register class, physical flag, and numeric identifier.  Used to
///          detect whether two operands refer to the same register so peepholes
///          can avoid duplicating work.
///
/// @param lhs First operand to compare.
/// @param rhs Second operand to compare.
/// @return @c true when both operands refer to the same register.
[[nodiscard]] bool sameRegister(const Operand &lhs, const Operand &rhs) noexcept
{
    const auto *lhsReg = asReg(lhs);
    const auto *rhsReg = asReg(rhs);
    if (!lhsReg || !rhsReg)
    {
        return false;
    }
    return lhsReg->isPhys == rhsReg->isPhys && lhsReg->cls == rhsReg->cls &&
           lhsReg->idOrPhys == rhsReg->idOrPhys;
}

/// @brief Ensure a zero-extension follows a @c setcc instruction.
///
/// @details The lowering pipeline expects boolean results to be materialised as
///          0/1 integers.  `setcc` writes a byte, so this helper inserts a
///          `movzx` when the subsequent instruction does not already perform the
///          zero-extension.  The helper scans for the destination register,
///          reuses it as both operands of the new instruction, and inserts the
///          @c movzx immediately after @p index in @p block.
///
/// @param block Machine basic block containing the @c setcc.
/// @param index Index of the @c setcc instruction within the block.
void ensureMovzxAfterSetcc(MBasicBlock &block, std::size_t index)
{
    if (index >= block.instructions.size())
    {
        return;
    }
    auto &setcc = block.instructions[index];
    Operand *destOperand = nullptr;
    for (auto &operand : setcc.operands)
    {
        if (std::holds_alternative<OpReg>(operand))
        {
            destOperand = &operand;
            break;
        }
    }
    if (!destOperand)
    {
        return;
    }

    if (const auto *destReg = asReg(*destOperand); destReg && destReg->cls != RegClass::GPR)
    {
        return;
    }

    if (index + 1 < block.instructions.size())
    {
        auto &next = block.instructions[index + 1];
        if (next.opcode == MOpcode::MOVZXrr32 && next.operands.size() >= 2 &&
            sameRegister(next.operands[0], *destOperand) &&
            sameRegister(next.operands[1], *destOperand))
        {
            return;
        }
    }

    MInstr movzx =
        MInstr::make(MOpcode::MOVZXrr32,
                     std::vector<Operand>{cloneOperand(*destOperand), cloneOperand(*destOperand)});
    block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(index + 1),
                              std::move(movzx));
}

/// @brief Normalise CMP opcodes based on operand kinds.
///
/// @details Some passes emit `cmp` using register-register opcodes even when the
///          right-hand side is an immediate and vice versa.  This helper flips
///          between @ref MOpcode::CMPrr and @ref MOpcode::CMPri so the encoding
///          matches operand types, ensuring later passes do not need to handle
///          redundant cases.
///
/// @param instr Instruction to canonicalise in place.
void canonicaliseCmp(MInstr &instr)
{
    if (instr.operands.size() < 2)
    {
        return;
    }
    if (instr.opcode == MOpcode::CMPrr && isImm(instr.operands[1]))
    {
        instr.opcode = MOpcode::CMPri;
    }
    if (instr.opcode == MOpcode::CMPri && !isImm(instr.operands[1]))
    {
        instr.opcode = MOpcode::CMPrr;
    }
}

/// @brief Canonicalise add/sub opcodes to use immediate forms when possible.
///
/// @details Instruction selection prefers `add` with immediates because it
///          exposes more opportunities for constant folding in later passes.  If
///          a subtraction uses an immediate the helper negates the constant and
///          replaces the opcode with `add` to keep the IR uniform.
///
/// @param instr Instruction to canonicalise in place.
void canonicaliseAddSub(MInstr &instr)
{
    if (instr.operands.size() < 2)
    {
        return;
    }
    switch (instr.opcode)
    {
        case MOpcode::ADDrr:
            if (isImm(instr.operands[1]))
            {
                instr.opcode = MOpcode::ADDri;
            }
            break;
        case MOpcode::SUBrr:
            if (auto *imm = asImm(instr.operands[1]))
            {
                imm->val = -imm->val;
                instr.opcode = MOpcode::ADDri;
            }
            break;
        default:
            break;
    }
}

/// @brief Canonicalise bitwise opcodes to match operand kinds and zeroing idioms.
///
/// @details Normalises AND/OR/XOR to use immediate forms when the second operand
///          is a literal and falls back to register forms otherwise. Additionally
///          rewrites XOR against zero into the canonical 32-bit self-XOR used by
///          later peephole passes to recognise zeroing patterns.
///
/// @param instr Instruction to canonicalise in place.
void canonicaliseBitwise(MInstr &instr)
{
    if (instr.operands.size() < 2)
    {
        return;
    }

    const auto convertForm = [&](MOpcode rr, MOpcode ri)
    {
        if (instr.opcode == rr && isImm(instr.operands[1]))
        {
            instr.opcode = ri;
            return true;
        }
        if (instr.opcode == ri && !isImm(instr.operands[1]))
        {
            instr.opcode = rr;
            return true;
        }
        return false;
    };

    convertForm(MOpcode::ANDrr, MOpcode::ANDri);
    convertForm(MOpcode::ORrr, MOpcode::ORri);
    convertForm(MOpcode::XORrr, MOpcode::XORri);

    if (instr.opcode == MOpcode::XORrr)
    {
        if (sameRegister(instr.operands[0], instr.operands[1]))
        {
            const auto *dst = asReg(instr.operands[0]);
            if (dst && dst->cls == RegClass::GPR)
            {
                instr.opcode = MOpcode::XORrr32;
                instr.operands[1] = cloneOperand(instr.operands[0]);
            }
        }
        return;
    }

    if (instr.opcode == MOpcode::XORri)
    {
        auto *imm = asImm(instr.operands[1]);
        if (imm && imm->val == 0)
        {
            const auto *dst = asReg(instr.operands[0]);
            if (dst && dst->cls == RegClass::GPR)
            {
                instr.opcode = MOpcode::XORrr32;
                instr.operands[1] = cloneOperand(instr.operands[0]);
            }
        }
    }
}

/// @brief Attempt to lower a GPR select placeholder into TEST/MOV/CMOV sequence.
///
/// @details Matches the three-instruction pattern emitted by LowerILToMIR
///          (MOV false/true metadata, TEST cond, SETcc mask) when the result
///          resides in the GPR class.  The helper rebuilds the sequence using a
///          flags-setting TEST followed by MOV (false path) and CMOVNE (true
///          path).  When the pattern does not match, the function leaves the
///          block untouched so other passes may handle it.
///
/// @param block Machine basic block undergoing transformation.
/// @param index Index of the candidate MOV instruction within the block.
/// @return @c true when the placeholder was replaced.
bool lowerGprSelect(MBasicBlock &block, std::size_t index)
{
    if (index + 2 >= block.instructions.size())
    {
        return false;
    }

    auto &movInstr = block.instructions[index];
    if (!((movInstr.opcode == MOpcode::MOVrr || movInstr.opcode == MOpcode::MOVri) &&
          movInstr.operands.size() >= 3))
    {
        return false;
    }

    const auto *destReg = asReg(movInstr.operands[0]);
    if (!destReg || destReg->cls != RegClass::GPR)
    {
        return false;
    }

    const Operand &falseVal = movInstr.operands[1];
    const Operand &trueVal = movInstr.operands[2];
    if (std::holds_alternative<OpImm>(trueVal))
    {
        return false;
    }

    auto &testInstr = block.instructions[index + 1];
    if (testInstr.opcode != MOpcode::TESTrr || testInstr.operands.size() < 2)
    {
        return false;
    }

    if (!sameRegister(testInstr.operands[0], testInstr.operands[1]))
    {
        return false;
    }

    auto &setccInstr = block.instructions[index + 2];
    if (setccInstr.opcode != MOpcode::SETcc)
    {
        return false;
    }

    bool destReferenced = false;
    for (const auto &operand : setccInstr.operands)
    {
        if (sameRegister(operand, movInstr.operands[0]))
        {
            destReferenced = true;
            break;
        }
    }
    if (!destReferenced)
    {
        return false;
    }

    std::vector<MInstr> replacement{};
    replacement.push_back(MInstr::make(MOpcode::TESTrr,
                                       std::vector<Operand>{cloneOperand(testInstr.operands[0]),
                                                            cloneOperand(testInstr.operands[1])}));

    const bool falseIsImm = std::holds_alternative<OpImm>(falseVal);
    replacement.push_back(MInstr::make(
        falseIsImm ? MOpcode::MOVri : MOpcode::MOVrr,
        std::vector<Operand>{cloneOperand(movInstr.operands[0]), cloneOperand(falseVal)}));

    replacement.push_back(MInstr::make(
        MOpcode::CMOVNErr,
        std::vector<Operand>{cloneOperand(movInstr.operands[0]), cloneOperand(trueVal)}));

    auto beginIt = block.instructions.begin() + static_cast<std::ptrdiff_t>(index);
    block.instructions.erase(beginIt, beginIt + 3);
    block.instructions.insert(beginIt, replacement.begin(), replacement.end());
    return true;
}

/// @brief Lower XMM select pseudos into TEST/JCC/MOVSD branch sequences.
///
/// @details Matches the three-instruction pattern emitted by the IL bridge for
///          floating-point selects (MOV placeholder, TEST cond, SETcc) and
///          rewrites it into a small branchy sequence using unique local
///          labels. The rewritten sequence uses `movsd` for both true and false
///          paths so that register allocators can reason about the value flow.
///
/// @param func Machine function supplying the label allocator.
/// @param block Machine basic block containing the pattern.
/// @param index Index of the MOV placeholder inside @p block.
/// @return @c true when a select pattern was rewritten.
bool lowerXmmSelect(MFunction &func, MBasicBlock &block, std::size_t index)
{
    if (index + 2 >= block.instructions.size())
    {
        return false;
    }

    auto &movInstr = block.instructions[index];
    if (movInstr.opcode != MOpcode::MOVSDrr || movInstr.operands.size() < 3)
    {
        return false;
    }

    const auto *destReg = asReg(movInstr.operands[0]);
    if (!destReg || destReg->cls != RegClass::XMM)
    {
        return false;
    }

    const Operand &falseVal = movInstr.operands[1];
    const Operand &trueVal = movInstr.operands[2];
    if (!std::holds_alternative<OpReg>(falseVal) || !std::holds_alternative<OpReg>(trueVal))
    {
        return false;
    }

    auto &testInstr = block.instructions[index + 1];
    if (testInstr.opcode != MOpcode::TESTrr || testInstr.operands.size() < 2)
    {
        return false;
    }

    if (!sameRegister(testInstr.operands[0], testInstr.operands[1]))
    {
        return false;
    }

    auto &setccInstr = block.instructions[index + 2];
    if (setccInstr.opcode != MOpcode::SETcc)
    {
        return false;
    }

    bool destReferenced = false;
    for (const auto &operand : setccInstr.operands)
    {
        if (sameRegister(operand, movInstr.operands[0]))
        {
            destReferenced = true;
            break;
        }
    }
    if (!destReferenced)
    {
        return false;
    }

    const std::string falseLabel = func.makeLocalLabel(".Lfalse");
    const std::string endLabel = func.makeLocalLabel(".Lend");

    std::vector<MInstr> replacement{};
    replacement.push_back(MInstr::make(MOpcode::TESTrr,
                                       std::vector<Operand>{cloneOperand(testInstr.operands[0]),
                                                            cloneOperand(testInstr.operands[1])}));
    replacement.push_back(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(0), makeLabelOperand(falseLabel)}));
    replacement.push_back(MInstr::make(
        MOpcode::MOVSDrr,
        std::vector<Operand>{cloneOperand(movInstr.operands[0]), cloneOperand(trueVal)}));
    replacement.push_back(
        MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(endLabel)}));
    replacement.push_back(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(falseLabel)}));
    replacement.push_back(MInstr::make(
        MOpcode::MOVSDrr,
        std::vector<Operand>{cloneOperand(movInstr.operands[0]), cloneOperand(falseVal)}));
    replacement.push_back(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(endLabel)}));

    auto beginIt = block.instructions.begin() + static_cast<std::ptrdiff_t>(index);
    block.instructions.erase(beginIt, beginIt + 3);
    block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(index),
                              replacement.begin(),
                              replacement.end());
    return true;
}

} // namespace

/// @brief Construct an instruction selector bound to a target description.
///
/// @param target Target description supplying register and ABI metadata.
ISel::ISel(const TargetInfo &target) noexcept : target_{&target} {}

/// @brief Lower arithmetic pseudos into canonical Machine IR encodings.
///
/// @details Walks every instruction in the function and normalises add/sub
///          forms via @ref canonicaliseAddSub.  Floating-point instructions are
///          currently emitted in legal form and therefore left untouched.  The
///          target reference is unused for Phase A but retained for future
///          expansion.
///
/// @param func Machine function undergoing selection.
void ISel::lowerArithmetic(MFunction &func) const
{
    (void)target_;
    for (auto &block : func.blocks)
    {
        for (auto &instr : block.instructions)
        {
            switch (instr.opcode)
            {
                case MOpcode::ADDrr:
                case MOpcode::ADDri:
                case MOpcode::SUBrr:
                    canonicaliseAddSub(instr);
                    break;
                case MOpcode::ANDrr:
                case MOpcode::ANDri:
                case MOpcode::ORrr:
                case MOpcode::ORri:
                case MOpcode::XORrr:
                case MOpcode::XORri:
                    canonicaliseBitwise(instr);
                    break;
                case MOpcode::IMULrr:
                case MOpcode::FADD:
                case MOpcode::FSUB:
                case MOpcode::FMUL:
                case MOpcode::FDIV:
                    // These already encode legal register-register forms in Phase A.
                    break;
                default:
                    break;
            }
        }
    }

    // After normalising arithmetic, fold trivial address computations into
    // users to reduce register pressure and improve addressing modes.
    foldLeaIntoMem(func);
}

/// @brief Lower compare and branch constructs to legal encodings.
///
/// @details Canonicalises compare opcodes, ensures boolean materialisation via
///          @ref ensureMovzxAfterSetcc, and converts stray `test` instructions
///          with immediate operands into `cmp` against zero.  The pass operates
///          locally within each block without changing control-flow structure.
///
/// @param func Machine function undergoing selection.
void ISel::lowerCompareAndBranch(MFunction &func) const
{
    (void)target_;
    for (auto &block : func.blocks)
    {
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx)
        {
            auto &instr = block.instructions[idx];
            switch (instr.opcode)
            {
                case MOpcode::CMPrr:
                case MOpcode::CMPri:
                    canonicaliseCmp(instr);
                    break;
                case MOpcode::SETcc:
                    ensureMovzxAfterSetcc(block, idx);
                    break;
                case MOpcode::TESTrr:
                    if (instr.operands.size() >= 2 && isImm(instr.operands[1]))
                    {
                        // Replace TEST with CMP against zero when a constant sneaks through.
                        instr.opcode = MOpcode::CMPri;
                        instr.operands[1] = makeImmOperand(0);
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

/// @brief Lower select constructs to ensure boolean values are extended.
///
/// @details Selection uses @ref ensureMovzxAfterSetcc to make sure any
///          `setcc` results feeding selects are promoted to full integers.  The
///          method is intentionally conservative and only inserts missing
///          zero-extensions.
///
/// @param func Machine function undergoing selection.
void ISel::lowerSelect(MFunction &func) const
{
    (void)target_;
    for (auto &block : func.blocks)
    {
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx)
        {
            if (lowerXmmSelect(func, block, idx))
            {
                idx += 6;
                continue;
            }

            if (lowerGprSelect(block, idx))
            {
                idx += 2;
                continue;
            }

            auto &instr = block.instructions[idx];
            if (instr.opcode == MOpcode::SETcc)
            {
                ensureMovzxAfterSetcc(block, idx);
            }
        }
    }
}

/// \brief Fold single-use LEA temps into memory operands.
/// \details For each block, finds LEA-def'd virtual registers with a single use
///          as a base in a memory operand and replaces the user with the LEA's
///          addressing mode, erasing the defining LEA.
void ISel::foldLeaIntoMem(MFunction &func) const
{
    (void)target_;
    for (auto &block : func.blocks)
    {
        std::unordered_map<uint16_t, std::size_t> leaDefIdx; // vreg id -> instr idx
        std::unordered_map<uint16_t, std::size_t> useCount;  // vreg id -> uses in block

        // First pass: record LEA defs and count vreg uses (regs and mem bases/indexes).
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx)
        {
            const auto &instr = block.instructions[idx];
            if (instr.opcode == MOpcode::LEA && instr.operands.size() >= 2)
            {
                if (const auto *dst = asReg(instr.operands[0]))
                {
                    if (!dst->isPhys && dst->cls == RegClass::GPR)
                    {
                        leaDefIdx[dst->idOrPhys] = idx;
                    }
                }
            }

            for (const auto &op : instr.operands)
            {
                if (const auto *r = asReg(op))
                {
                    if (!r->isPhys)
                    {
                        ++useCount[r->idOrPhys];
                    }
                }
                else if (const auto *mem = std::get_if<OpMem>(&op))
                {
                    if (!mem->base.isPhys)
                    {
                        ++useCount[mem->base.idOrPhys];
                    }
                    if (mem->hasIndex && !mem->index.isPhys)
                    {
                        ++useCount[mem->index.idOrPhys];
                    }
                }
            }
        }

        // Second pass: try to fold at use sites and erase the LEA.
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx)
        {
            auto &instr = block.instructions[idx];
            bool foldedAny = false;
            for (auto &op : instr.operands)
            {
                if (auto *mem = std::get_if<OpMem>(&op))
                {
                    const OpReg &base = mem->base;
                    if (!base.isPhys && base.cls == RegClass::GPR)
                    {
                        const uint16_t v = base.idOrPhys;
                        auto defIt = leaDefIdx.find(v);
                        auto useIt = useCount.find(v);
                        if (defIt != leaDefIdx.end() && useIt != useCount.end() &&
                            useIt->second == 1)
                        {
                            const std::size_t defIndex = defIt->second;
                            if (defIndex < block.instructions.size())
                            {
                                const auto &defInstr = block.instructions[defIndex];
                                if (defInstr.opcode == MOpcode::LEA &&
                                    defInstr.operands.size() >= 2)
                                {
                                    if (const auto *srcMem =
                                            std::get_if<OpMem>(&defInstr.operands[1]))
                                    {
                                        // Replace the memory operand with the LEA's addressing
                                        // mode.
                                        *mem = *srcMem;
                                        // Erase the defining LEA.
                                        block.instructions.erase(
                                            block.instructions.begin() +
                                            static_cast<std::ptrdiff_t>(defIndex));
                                        // Adjust current index when the erased def was before this
                                        // instr.
                                        if (defIndex < idx)
                                        {
                                            --idx;
                                        }
                                        foldedAny = true;
                                        // Prevent re-use: remove from maps.
                                        leaDefIdx.erase(defIt);
                                        useCount.erase(useIt);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            (void)foldedAny;
        }
    }
}

} // namespace viper::codegen::x64
