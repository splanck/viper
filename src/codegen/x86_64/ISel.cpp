//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "OperandUtils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace viper::codegen::x64
{

namespace
{

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
                // Guard against INT64_MIN: negation of the minimum signed value
                // is undefined behaviour in C++.  Leave the SUB form intact.
                if (imm->val != std::numeric_limits<int64_t>::min())
                {
                    imm->val = -imm->val;
                    instr.opcode = MOpcode::ADDri;
                }
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
    // Recalculate insert position since erase invalidates iterators
    block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(index),
                              replacement.begin(),
                              replacement.end());
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

    // Fold SHL+ADD patterns into SIB addressing modes for load/store.
    foldSibAddressing(func);
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
                    // TESTrr should always have two register operands (self-test).
                    // Do NOT rewrite to CMP $0 if an immediate sneaks through:
                    // TEST computes (reg AND imm) while CMP computes (reg - 0),
                    // which set different flags for non-zero masks.
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

/// \brief Fold SHL+ADD sequences into SIB addressing modes.
/// \details Matches patterns where a shift by 1, 2, or 3 (scale 2, 4, 8) is added
///          to a base pointer, and the result is used as a memory base. Transforms
///          into disp(base, index, scale) addressing to reduce instruction count.
///          Optimized to use O(1) map lookups instead of O(n) linear scans for
///          MOVrr definitions.
void ISel::foldSibAddressing(MFunction &func) const
{
    (void)target_;

    struct ShlInfo
    {
        std::size_t defIdx{0};
        uint16_t srcVreg{0};
        uint8_t scale{1}; // 2, 4, or 8
    };

    struct AddInfo
    {
        std::size_t defIdx{0};
        uint16_t baseVreg{0};
        uint16_t shiftedVreg{0};
    };

    /// @brief MOVrr definition info for O(1) lookup.
    struct MovInfo
    {
        std::size_t defIdx{0};
        uint16_t srcVreg{0};
        bool srcIsPhys{false};
    };

    for (auto &block : func.blocks)
    {
        std::unordered_map<uint16_t, ShlInfo> shlDefs; // result vreg -> info
        std::unordered_map<uint16_t, AddInfo> addDefs; // result vreg -> info
        std::unordered_map<uint16_t, MovInfo> movDefs; // dest vreg -> info (for O(1) lookup)
        std::unordered_map<uint16_t, std::size_t> useCount;

        // First pass: record SHL, ADD, and MOVrr definitions, count uses
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx)
        {
            const auto &instr = block.instructions[idx];

            // Record MOVrr for O(1) lookup later (replaces O(n) linear scan)
            if (instr.opcode == MOpcode::MOVrr && instr.operands.size() >= 2)
            {
                const auto *dst = asReg(instr.operands[0]);
                const auto *src = asReg(instr.operands[1]);
                if (dst && src && !dst->isPhys && dst->cls == RegClass::GPR)
                {
                    MovInfo info{};
                    info.defIdx = idx;
                    info.srcVreg = src->idOrPhys;
                    info.srcIsPhys = src->isPhys;
                    movDefs[dst->idOrPhys] = info;
                }
            }

            // Record SHLri with shift 1, 2, or 3 (scale 2, 4, 8)
            if (instr.opcode == MOpcode::SHLri && instr.operands.size() >= 2)
            {
                const auto *dst = asReg(instr.operands[0]);
                const auto *shiftAmt = asImm(instr.operands[1]);
                if (dst && !dst->isPhys && dst->cls == RegClass::GPR && shiftAmt)
                {
                    const int64_t shift = shiftAmt->val;
                    if (shift >= 1 && shift <= 3)
                    {
                        ShlInfo info{};
                        info.defIdx = idx;
                        info.srcVreg = dst->idOrPhys; // SHL is destructive, src == dst
                        info.scale = static_cast<uint8_t>(1 << shift);
                        shlDefs[dst->idOrPhys] = info;
                    }
                }
            }

            // Record ADDrr where one operand might be from SHL
            if (instr.opcode == MOpcode::ADDrr && instr.operands.size() >= 2)
            {
                const auto *dst = asReg(instr.operands[0]);
                const auto *src = asReg(instr.operands[1]);
                if (dst && src && !dst->isPhys && !src->isPhys && dst->cls == RegClass::GPR &&
                    src->cls == RegClass::GPR)
                {
                    // Check if src is from a SHL - then dst is base + shifted
                    if (shlDefs.count(src->idOrPhys))
                    {
                        AddInfo info{};
                        info.defIdx = idx;
                        info.baseVreg = dst->idOrPhys;
                        info.shiftedVreg = src->idOrPhys;
                        addDefs[dst->idOrPhys] = info;
                    }
                }
            }

            // Count all vreg uses
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

        // Second pass: find memory operands using ADD results and transform
        std::vector<std::size_t> toErase;
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx)
        {
            auto &instr = block.instructions[idx];

            for (auto &op : instr.operands)
            {
                auto *mem = std::get_if<OpMem>(&op);
                if (!mem || mem->hasIndex)
                {
                    continue; // Skip if not memory or already has index
                }

                if (mem->base.isPhys)
                {
                    continue;
                }

                const uint16_t baseId = mem->base.idOrPhys;
                auto addIt = addDefs.find(baseId);
                if (addIt == addDefs.end())
                {
                    continue;
                }

                const AddInfo &addInfo = addIt->second;
                auto shlIt = shlDefs.find(addInfo.shiftedVreg);
                if (shlIt == shlDefs.end())
                {
                    continue;
                }

                const ShlInfo &shlInfo = shlIt->second;

                // Check that the ADD result and SHL result are single-use
                auto addUseIt = useCount.find(baseId);
                auto shlUseIt = useCount.find(addInfo.shiftedVreg);
                if (addUseIt == useCount.end() || shlUseIt == useCount.end())
                {
                    continue;
                }
                if (addUseIt->second != 1 || shlUseIt->second != 1)
                {
                    continue; // Multiple uses - can't fold
                }

                // Find the original index register before the SHL using O(1) map lookup
                uint16_t indexVreg = shlInfo.srcVreg;
                auto movIt = movDefs.find(shlInfo.srcVreg);
                if (movIt != movDefs.end() && !movIt->second.srcIsPhys &&
                    movIt->second.defIdx < shlInfo.defIdx)
                {
                    indexVreg = movIt->second.srcVreg;
                    toErase.push_back(movIt->second.defIdx); // Mark MOV for removal
                }

                // Find the original base register before ADD using O(1) map lookup
                uint16_t realBaseVreg = addInfo.baseVreg;
                auto baseMovIt = movDefs.find(addInfo.baseVreg);
                if (baseMovIt != movDefs.end() && baseMovIt->second.defIdx < addInfo.defIdx)
                {
                    if (!baseMovIt->second.srcIsPhys)
                    {
                        realBaseVreg = baseMovIt->second.srcVreg;
                    }
                    else
                    {
                        // Base is a physical register - use it directly
                        mem->base.isPhys = true;
                        mem->base.idOrPhys = baseMovIt->second.srcVreg;
                    }
                    toErase.push_back(baseMovIt->second.defIdx);
                }

                // Update memory operand with SIB addressing
                if (!mem->base.isPhys)
                {
                    mem->base.idOrPhys = realBaseVreg;
                }
                mem->index.isPhys = false;
                mem->index.cls = RegClass::GPR;
                mem->index.idOrPhys = indexVreg;
                mem->scale = shlInfo.scale;
                mem->hasIndex = true;

                // Mark SHL and ADD for removal
                toErase.push_back(shlInfo.defIdx);
                toErase.push_back(addInfo.defIdx);
            }
        }

        // Erase marked instructions in reverse order
        std::sort(toErase.begin(), toErase.end(), std::greater<std::size_t>());
        toErase.erase(std::unique(toErase.begin(), toErase.end()), toErase.end());
        for (std::size_t eraseIdx : toErase)
        {
            if (eraseIdx < block.instructions.size())
            {
                block.instructions.erase(block.instructions.begin() +
                                         static_cast<std::ptrdiff_t>(eraseIdx));
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
