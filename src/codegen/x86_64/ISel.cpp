//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/ISel.cpp
// Purpose: Define instruction selection helpers that map pseudo Machine IR
//          emitted by LowerILToMIR into concrete x86-64 encodings.
// Key invariants:
//   - Transformations preserve instruction ordering while rewriting
//     opcode/operand combinations to legal encodings (e.g. cmp immediate
//     forms or inserting movzx after setcc).
//   - Resulting instruction streams remain valid for register allocation
//     and emission.
// Ownership/Lifetime:
//   - Operates entirely in-place on Machine IR borrowed from callers without
//     allocating persistent auxiliary structures.
// Links: codegen/x86_64/ISel.hpp,
//        codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#include "ISel.hpp"
#include "OperandRoles.hpp"
#include "OperandUtils.hpp"
#include "Unsupported.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace viper::codegen::x64 {

namespace {

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
void ensureMovzxAfterSetcc(MBasicBlock &block, std::size_t index) {
    if (index >= block.instructions.size()) {
        return;
    }
    auto &setcc = block.instructions[index];
    Operand *destOperand = nullptr;
    for (auto &operand : setcc.operands) {
        if (std::holds_alternative<OpReg>(operand)) {
            destOperand = &operand;
            break;
        }
    }
    if (!destOperand) {
        return;
    }

    if (const auto *destReg = asReg(*destOperand); destReg && destReg->cls != RegClass::GPR) {
        return;
    }

    if (index + 1 < block.instructions.size()) {
        auto &next = block.instructions[index + 1];
        if (next.opcode == MOpcode::MOVZXrr8 && next.operands.size() >= 2 &&
            sameRegister(next.operands[0], *destOperand) &&
            sameRegister(next.operands[1], *destOperand)) {
            return;
        }
    }

    MInstr movzx =
        MInstr::make(MOpcode::MOVZXrr8,
                     std::vector<Operand>{cloneOperand(*destOperand), cloneOperand(*destOperand)});
    block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(index + 1),
                              std::move(movzx));
}

[[nodiscard]] bool fitsSignedImm32(int64_t value) noexcept {
    return value >= std::numeric_limits<int32_t>::min() &&
           value <= std::numeric_limits<int32_t>::max();
}

void observeVirtualRegister(const OpReg &reg, uint16_t &maxId) noexcept {
    if (!reg.isPhys) {
        maxId = std::max(maxId, reg.idOrPhys);
    }
}

[[nodiscard]] uint32_t nextVirtualGprId(const MFunction &func) {
    uint16_t maxId = 0;
    for (const auto &block : func.blocks) {
        for (const auto &instr : block.instructions) {
            for (const auto &operand : instr.operands) {
                if (const auto *reg = asReg(operand)) {
                    observeVirtualRegister(*reg, maxId);
                    continue;
                }

                const auto *mem = asMem(operand);
                if (!mem) {
                    continue;
                }
                observeVirtualRegister(mem->base, maxId);
                if (mem->hasIndex) {
                    observeVirtualRegister(mem->index, maxId);
                }
            }
        }
    }
    return static_cast<uint32_t>(maxId) + 1U;
}

[[nodiscard]] Operand makeTempGprOperand(uint32_t &nextVreg) {
    if (nextVreg > std::numeric_limits<uint16_t>::max()) {
        phaseAUnsupported("too many virtual registers in function");
    }
    return makeVRegOperand(RegClass::GPR, static_cast<uint16_t>(nextVreg++));
}

bool materialiseImmediateRhs(MBasicBlock &block,
                             std::size_t index,
                             uint32_t &nextVreg,
                             MOpcode registerOpcode) {
    if (index >= block.instructions.size()) {
        return false;
    }

    auto &instr = block.instructions[index];
    if (instr.operands.size() < 2 || !isImm(instr.operands[1])) {
        return false;
    }

    const Operand immediate = cloneOperand(instr.operands[1]);
    const Operand temp = makeTempGprOperand(nextVreg);
    MInstr materialise =
        MInstr::make(MOpcode::MOVri, std::vector<Operand>{cloneOperand(temp), immediate});
    materialise.loc = instr.loc;

    instr.opcode = registerOpcode;
    instr.operands[1] = cloneOperand(temp);
    block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(index),
                              std::move(materialise));
    return true;
}

/// @brief Normalise CMP opcodes and materialise unencodable immediates.
///
/// @return True when a MOVri was inserted before @p index.
bool legaliseCmp(MBasicBlock &block, std::size_t index, uint32_t &nextVreg) {
    if (index >= block.instructions.size()) {
        return false;
    }

    auto &instr = block.instructions[index];
    if (instr.operands.size() < 2) {
        return false;
    }

    if ((instr.opcode == MOpcode::CMPrr || instr.opcode == MOpcode::CMPri) &&
        isImm(instr.operands[1])) {
        const auto &imm = std::get<OpImm>(instr.operands[1]);
        if (fitsSignedImm32(imm.val)) {
            instr.opcode = MOpcode::CMPri;
            return false;
        }
        return materialiseImmediateRhs(block, index, nextVreg, MOpcode::CMPrr);
    }

    if (instr.opcode == MOpcode::CMPri && !isImm(instr.operands[1])) {
        instr.opcode = MOpcode::CMPrr;
    }
    return false;
}

[[nodiscard]] std::optional<OpMem> combineLeaMemUse(const OpMem &leaMem, const OpMem &useMem) {
    if (leaMem.hasIndex && useMem.hasIndex) {
        return std::nullopt;
    }

    const int64_t combinedDisp = static_cast<int64_t>(leaMem.disp) + useMem.disp;
    if (combinedDisp < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
        combinedDisp > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
        return std::nullopt;
    }

    OpMem combined = leaMem;
    combined.disp = static_cast<int32_t>(combinedDisp);
    if (useMem.hasIndex) {
        combined.index = useMem.index;
        combined.scale = useMem.scale;
        combined.hasIndex = true;
    }
    return combined;
}

/// @brief Canonicalise add/sub opcodes to legal immediate/register forms.
///
/// @details Instruction selection prefers `add` with signed-imm32 immediates
///          because those map directly to x86-64 ALU encodings. Larger
///          immediates, or subtraction immediates whose negation cannot be
///          encoded, are materialised in a temporary GPR.
///
/// @return True when a MOVri was inserted before @p index.
bool legaliseAddSub(MBasicBlock &block, std::size_t index, uint32_t &nextVreg) {
    auto &instr = block.instructions[index];
    if (instr.operands.size() < 2) {
        return false;
    }
    switch (instr.opcode) {
        case MOpcode::ADDrr:
        case MOpcode::ADDri:
            if (isImm(instr.operands[1])) {
                const auto &imm = std::get<OpImm>(instr.operands[1]);
                if (fitsSignedImm32(imm.val)) {
                    instr.opcode = MOpcode::ADDri;
                    return false;
                }
                return materialiseImmediateRhs(block, index, nextVreg, MOpcode::ADDrr);
            }
            if (instr.opcode == MOpcode::ADDri) {
                instr.opcode = MOpcode::ADDrr;
            }
            break;
        case MOpcode::SUBrr:
            if (auto *imm = asImm(instr.operands[1])) {
                if (imm->val != std::numeric_limits<int64_t>::min()) {
                    const int64_t negated = -imm->val;
                    if (fitsSignedImm32(negated)) {
                        imm->val = negated;
                        instr.opcode = MOpcode::ADDri;
                        return false;
                    }
                }
                return materialiseImmediateRhs(block, index, nextVreg, MOpcode::SUBrr);
            }
            break;
        default:
            break;
    }
    return false;
}

/// @brief Canonicalise bitwise opcodes to legal operand kinds and zeroing idioms.
///
/// @details Normalises AND/OR/XOR to use immediate forms when the second operand
///          is a signed-imm32 literal and falls back to register forms otherwise.
///          Larger constants are materialised into temporary GPRs. Additionally
///          rewrites register self-XOR into the canonical 32-bit self-XOR used by
///          later peephole passes to recognise zeroing patterns.
///
/// @return True when a MOVri was inserted before @p index.
bool legaliseBitwise(MBasicBlock &block, std::size_t index, uint32_t &nextVreg) {
    auto &instr = block.instructions[index];
    if (instr.operands.size() < 2) {
        return false;
    }

    const auto convertForm = [&](MOpcode rr, MOpcode ri) -> bool {
        if (instr.opcode != rr && instr.opcode != ri) {
            return false;
        }

        if (isImm(instr.operands[1])) {
            const auto &imm = std::get<OpImm>(instr.operands[1]);
            if (fitsSignedImm32(imm.val)) {
                instr.opcode = ri;
                return false;
            }
            return materialiseImmediateRhs(block, index, nextVreg, rr);
        }

        if (instr.opcode == ri && !isImm(instr.operands[1])) {
            instr.opcode = rr;
        }
        return false;
    };

    if (convertForm(MOpcode::ANDrr, MOpcode::ANDri) ||
        convertForm(MOpcode::ORrr, MOpcode::ORri) ||
        convertForm(MOpcode::XORrr, MOpcode::XORri)) {
        return true;
    }

    if (instr.opcode == MOpcode::XORrr) {
        if (sameRegister(instr.operands[0], instr.operands[1])) {
            const auto *dst = asReg(instr.operands[0]);
            if (dst && dst->cls == RegClass::GPR) {
                instr.opcode = MOpcode::XORrr32;
                instr.operands[1] = cloneOperand(instr.operands[0]);
            }
        }
        return false;
    }

    // Do not rewrite XORri $0 as a self-XOR.  "x ^ 0" is an identity, while
    // "xor r32, r32" zeroes the destination.
    return false;
}

bool appendGprMove(std::vector<MInstr> &out, const Operand &dst, const Operand &src) {
    if (std::holds_alternative<OpImm>(src)) {
        out.push_back(MInstr::make(MOpcode::MOVri,
                                   std::vector<Operand>{cloneOperand(dst), cloneOperand(src)}));
        return true;
    }
    if (std::holds_alternative<OpReg>(src)) {
        out.push_back(MInstr::make(MOpcode::MOVrr,
                                   std::vector<Operand>{cloneOperand(dst), cloneOperand(src)}));
        return true;
    }
    return false;
}

/// @brief Attempt to lower a GPR select pseudo into TEST/MOV/CMOV sequence.
///
/// @details Matches the explicit SELECT_GPR pseudo emitted by LowerILToMIR and
///          rebuilds it as a flags-setting TEST followed by MOV (false path)
///          and CMOVNE (true path).
///
/// @param func Machine function supplying the label allocator.
/// @param block Machine basic block undergoing transformation.
/// @param index Index of the candidate SELECT_GPR instruction within the block.
/// @return @c true when the pseudo was replaced.
bool lowerGprSelect(MFunction &func, MBasicBlock &block, std::size_t index) {
    auto &selectInstr = block.instructions[index];
    if (selectInstr.opcode != MOpcode::SELECT_GPR || selectInstr.operands.size() != 4) {
        return false;
    }

    const auto *destReg = asReg(selectInstr.operands[0]);
    if (!destReg || destReg->cls != RegClass::GPR) {
        return false;
    }

    const Operand &condVal = selectInstr.operands[1];
    const Operand &falseVal = selectInstr.operands[2];
    const Operand &trueVal = selectInstr.operands[3];
    if (!std::holds_alternative<OpReg>(condVal)) {
        return false;
    }

    std::vector<MInstr> replacement{};
    replacement.push_back(MInstr::make(MOpcode::TESTrr,
                                       std::vector<Operand>{cloneOperand(condVal),
                                                            cloneOperand(condVal)}));

    if (!appendGprMove(replacement, selectInstr.operands[0], falseVal)) {
        return false;
    }

    if (std::holds_alternative<OpReg>(trueVal)) {
        replacement.push_back(MInstr::make(
            MOpcode::CMOVNErr,
            std::vector<Operand>{cloneOperand(selectInstr.operands[0]), cloneOperand(trueVal)}));
    } else if (std::holds_alternative<OpImm>(trueVal)) {
        const std::string doneLabel = func.makeLocalLabel(".Lselect_gpr_done");
        replacement.push_back(MInstr::make(
            MOpcode::JCC, std::vector<Operand>{makeImmOperand(0), makeLabelOperand(doneLabel)}));
        if (!appendGprMove(replacement, selectInstr.operands[0], trueVal)) {
            return false;
        }
        replacement.push_back(
            MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(doneLabel)}));
    } else {
        return false;
    }

    auto beginIt = block.instructions.begin() + static_cast<std::ptrdiff_t>(index);
    block.instructions.erase(beginIt);
    // Recalculate insert position since erase invalidates iterators
    block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(index),
                              replacement.begin(),
                              replacement.end());
    return true;
}

/// @brief Lower XMM select pseudos into TEST/JCC/MOVSD branch sequences.
///
/// @details Matches the explicit SELECT_XMM pseudo emitted by the IL bridge and
///          rewrites it into a small branchy sequence using unique local
///          labels. The rewritten sequence uses `movsd` for both true and false
///          paths and explicit jumps to the join point so later layout changes
///          cannot turn the false arm into a fallthrough past the epilogue.
///
/// @param func Machine function supplying the label allocator.
/// @param block Machine basic block containing the pattern.
/// @param index Index of the SELECT_XMM pseudo inside @p block.
/// @return @c true when a select pattern was rewritten.
bool lowerXmmSelect(MFunction &func, MBasicBlock &block, std::size_t index) {
    auto &selectInstr = block.instructions[index];
    if (selectInstr.opcode != MOpcode::SELECT_XMM || selectInstr.operands.size() != 4) {
        return false;
    }

    const auto *destReg = asReg(selectInstr.operands[0]);
    if (!destReg || destReg->cls != RegClass::XMM) {
        return false;
    }

    const Operand &condVal = selectInstr.operands[1];
    const Operand &falseVal = selectInstr.operands[2];
    const Operand &trueVal = selectInstr.operands[3];
    if (!std::holds_alternative<OpReg>(falseVal) || !std::holds_alternative<OpReg>(trueVal)) {
        return false;
    }
    if (!std::holds_alternative<OpReg>(condVal)) {
        return false;
    }

    const std::string falseLabel = func.makeLocalLabel(".Lfalse");
    const std::string endLabel = func.makeLocalLabel(".Lend");

    std::vector<MInstr> replacement{};
    replacement.push_back(MInstr::make(MOpcode::TESTrr,
                                       std::vector<Operand>{cloneOperand(condVal),
                                                            cloneOperand(condVal)}));
    replacement.push_back(MInstr::make(
        MOpcode::JCC, std::vector<Operand>{makeImmOperand(0), makeLabelOperand(falseLabel)}));
    replacement.push_back(MInstr::make(
        MOpcode::MOVSDrr,
        std::vector<Operand>{cloneOperand(selectInstr.operands[0]), cloneOperand(trueVal)}));
    replacement.push_back(
        MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(endLabel)}));
    replacement.push_back(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(falseLabel)}));
    replacement.push_back(MInstr::make(
        MOpcode::MOVSDrr,
        std::vector<Operand>{cloneOperand(selectInstr.operands[0]), cloneOperand(falseVal)}));
    replacement.push_back(
        MInstr::make(MOpcode::JMP, std::vector<Operand>{makeLabelOperand(endLabel)}));
    replacement.push_back(
        MInstr::make(MOpcode::LABEL, std::vector<Operand>{makeLabelOperand(endLabel)}));

    auto beginIt = block.instructions.begin() + static_cast<std::ptrdiff_t>(index);
    block.instructions.erase(beginIt);
    block.instructions.insert(block.instructions.begin() + static_cast<std::ptrdiff_t>(index),
                              replacement.begin(),
                              replacement.end());
    return true;
}

bool instructionUsesRegister(const MInstr &instr, const Operand &needle) {
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isDef;
        if (!isUse) {
            continue;
        }

        if (sameRegister(instr.operands[idx], needle)) {
            return true;
        }

        const auto *mem = std::get_if<OpMem>(&instr.operands[idx]);
        if (!mem) {
            continue;
        }
        if (sameRegister(Operand{mem->base}, needle)) {
            return true;
        }
        if (mem->hasIndex && sameRegister(Operand{mem->index}, needle)) {
            return true;
        }
    }
    return false;
}

template <typename CountMap>
void countVirtualRegisterUses(const MInstr &instr, CountMap &useCount) {
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isDef;
        if (!isUse) {
            continue;
        }

        if (const auto *reg = asReg(instr.operands[idx])) {
            if (!reg->isPhys) {
                ++useCount[reg->idOrPhys];
            }
            continue;
        }

        const auto *mem = std::get_if<OpMem>(&instr.operands[idx]);
        if (!mem) {
            continue;
        }
        if (!mem->base.isPhys) {
            ++useCount[mem->base.idOrPhys];
        }
        if (mem->hasIndex && !mem->index.isPhys) {
            ++useCount[mem->index.idOrPhys];
        }
    }
}

std::size_t countVirtualRegisterUsesInInstr(const MInstr &instr, uint16_t vreg) {
    std::size_t count = 0;
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isDef;
        if (!isUse) {
            continue;
        }

        if (const auto *reg = asReg(instr.operands[idx])) {
            if (!reg->isPhys && reg->idOrPhys == vreg) {
                ++count;
            }
            continue;
        }

        const auto *mem = std::get_if<OpMem>(&instr.operands[idx]);
        if (!mem) {
            continue;
        }
        if (!mem->base.isPhys && mem->base.idOrPhys == vreg) {
            ++count;
        }
        if (mem->hasIndex && !mem->index.isPhys && mem->index.idOrPhys == vreg) {
            ++count;
        }
    }
    return count;
}

std::size_t countVirtualRegisterUsesInRange(const MBasicBlock &block,
                                            uint16_t vreg,
                                            std::size_t begin,
                                            std::size_t end) {
    const std::size_t limit = std::min(end, block.instructions.size());
    std::size_t count = 0;
    for (std::size_t idx = begin; idx < limit; ++idx) {
        count += countVirtualRegisterUsesInInstr(block.instructions[idx], vreg);
    }
    return count;
}

std::size_t countVirtualRegisterUsesInFunction(const MFunction &func, uint16_t vreg) {
    std::size_t count = 0;
    for (const auto &block : func.blocks) {
        for (const auto &instr : block.instructions) {
            count += countVirtualRegisterUsesInInstr(instr, vreg);
        }
    }
    return count;
}

/// @brief Check whether a register has uses outside the local fold window.
/// @details The compare/branch fold may only remove boolean materialisation
///          when the materialized value feeds the local test and nothing else.
///          Block-argument edge copies live in separate synthetic blocks, so the
///          check must scan the full function rather than only the current block.
bool registerUsedOutsideFoldPattern(const MFunction &func,
                                    const MBasicBlock &patternBlock,
                                    std::size_t firstAllowedUse,
                                    std::size_t lastAllowedUse,
                                    const Operand &needle) {
    for (const auto &candidateBlock : func.blocks) {
        for (std::size_t idx = 0; idx < candidateBlock.instructions.size(); ++idx) {
            if (&candidateBlock == &patternBlock && idx >= firstAllowedUse &&
                idx <= lastAllowedUse) {
                continue;
            }
            if (instructionUsesRegister(candidateBlock.instructions[idx], needle)) {
                return true;
            }
        }
    }
    return false;
}

bool flagsReadBeforeClobber(const std::vector<MInstr> &instrs, std::size_t index) {
    for (std::size_t scan = index + 1; scan < instrs.size(); ++scan) {
        const MOpcode opcode = instrs[scan].opcode;
        if (usesEFlags(opcode)) {
            return true;
        }
        if (definesEFlags(opcode)) {
            return false;
        }
        if (opcode == MOpcode::LABEL) {
            return true;
        }
    }
    return false;
}

/// @brief Fold compare/setcc/test branch chains back into a direct flags branch.
/// @details Matches the common sequence:
///            cmp/ucomis
///            setcc %v
///            movzx %v, %v   ; optional
///            test  %v, %v
///            jne   target
///          and rewrites it so the branch uses the original compare flags
///          directly. This removes redundant boolean materialisation when the
///          compare result is consumed only by the terminating branch.
bool foldCompareBranch(const MFunction &func, MBasicBlock &block, std::size_t index) {
    if (index + 3 >= block.instructions.size()) {
        return false;
    }

    const auto isCompare = [](MOpcode opcode) {
        return opcode == MOpcode::CMPrr || opcode == MOpcode::CMPri || opcode == MOpcode::UCOMIS;
    };

    auto &cmpInstr = block.instructions[index];
    if (!isCompare(cmpInstr.opcode)) {
        return false;
    }

    auto &setccInstr = block.instructions[index + 1];
    if (setccInstr.opcode != MOpcode::SETcc || setccInstr.operands.size() < 2) {
        return false;
    }

    const auto *setCond = asImm(setccInstr.operands[0]);
    const auto *setDst = asReg(setccInstr.operands[1]);
    if (!setCond || !setDst || setDst->cls != RegClass::GPR) {
        return false;
    }

    std::size_t testIndex = index + 2;
    if (block.instructions[testIndex].opcode == MOpcode::MOVZXrr8) {
        const auto &movzxInstr = block.instructions[testIndex];
        if (movzxInstr.operands.size() < 2 ||
            !sameRegister(movzxInstr.operands[0], setccInstr.operands[1]) ||
            !sameRegister(movzxInstr.operands[1], setccInstr.operands[1])) {
            return false;
        }
        ++testIndex;
    }

    if (testIndex + 1 >= block.instructions.size()) {
        return false;
    }

    auto &testInstr = block.instructions[testIndex];
    if (testInstr.opcode != MOpcode::TESTrr || testInstr.operands.size() < 2) {
        return false;
    }
    if (!sameRegister(testInstr.operands[0], setccInstr.operands[1]) ||
        !sameRegister(testInstr.operands[1], setccInstr.operands[1])) {
        return false;
    }

    if (registerUsedOutsideFoldPattern(
            func, block, index + 2, testIndex, setccInstr.operands[1])) {
        return false;
    }

    auto &jccInstr = block.instructions[testIndex + 1];
    if (jccInstr.opcode != MOpcode::JCC || jccInstr.operands.size() < 2) {
        return false;
    }
    const auto *branchCond = asImm(jccInstr.operands[0]);
    if (!branchCond || branchCond->val != 1) {
        return false;
    }
    for (std::size_t useIdx = testIndex + 2; useIdx < block.instructions.size(); ++useIdx) {
        for (const auto &operand : block.instructions[useIdx].operands) {
            if (sameRegister(operand, setccInstr.operands[1])) {
                return false;
            }
            if (const auto *mem = std::get_if<OpMem>(&operand)) {
                const Operand base = mem->base;
                if (sameRegister(base, setccInstr.operands[1])) {
                    return false;
                }
                if (mem->hasIndex) {
                    const Operand indexOp = mem->index;
                    if (sameRegister(indexOp, setccInstr.operands[1])) {
                        return false;
                    }
                }
            }
        }
    }

    jccInstr.operands[0] = makeImmOperand(setCond->val);
    auto eraseBegin = block.instructions.begin() + static_cast<std::ptrdiff_t>(index + 1);
    auto eraseEnd = block.instructions.begin() + static_cast<std::ptrdiff_t>(testIndex + 1);
    block.instructions.erase(eraseBegin, eraseEnd);
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
void ISel::lowerArithmetic(MFunction &func) const {
    (void)target_;
    uint32_t nextVreg = nextVirtualGprId(func);
    for (auto &block : func.blocks) {
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
            bool insertedMaterialise = false;
            switch (block.instructions[idx].opcode) {
                case MOpcode::ADDrr:
                case MOpcode::ADDri:
                case MOpcode::SUBrr:
                    insertedMaterialise = legaliseAddSub(block, idx, nextVreg);
                    break;
                case MOpcode::ANDrr:
                case MOpcode::ANDri:
                case MOpcode::ORrr:
                case MOpcode::ORri:
                case MOpcode::XORrr:
                case MOpcode::XORri:
                    insertedMaterialise = legaliseBitwise(block, idx, nextVreg);
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
            if (insertedMaterialise) {
                ++idx;
            }
        }
    }

    // Replace IMULrr-by-small-constant (3, 5, 9) with LEA strength reductions
    // before folding LEA addresses into memory operands.
    lowerMulToLea(func);

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
void ISel::lowerCompareAndBranch(MFunction &func) const {
    (void)target_;
    uint32_t nextVreg = nextVirtualGprId(func);
    for (auto &block : func.blocks) {
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
            if (foldCompareBranch(func, block, idx)) {
                if (legaliseCmp(block, idx, nextVreg)) {
                    ++idx;
                }
                continue;
            }
            auto &instr = block.instructions[idx];
            switch (instr.opcode) {
                case MOpcode::CMPrr:
                case MOpcode::CMPri:
                    if (legaliseCmp(block, idx, nextVreg)) {
                        ++idx;
                    }
                    break;
                case MOpcode::UCOMIS:
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
void ISel::lowerSelect(MFunction &func) const {
    (void)target_;
    for (auto &block : func.blocks) {
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
            if (lowerXmmSelect(func, block, idx)) {
                idx += 6;
                continue;
            }

            if (lowerGprSelect(func, block, idx)) {
                idx += 2;
                continue;
            }

            auto &instr = block.instructions[idx];
            if (instr.opcode == MOpcode::SETcc) {
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
void ISel::foldSibAddressing(MFunction &func) const {
    (void)target_;

    struct ShlInfo {
        std::size_t defIdx{0};
        uint16_t srcVreg{0};
        uint8_t scale{1}; // 2, 4, or 8
    };

    struct AddInfo {
        std::size_t defIdx{0};
        uint16_t baseVreg{0};
        uint16_t shiftedVreg{0};
    };

    /// @brief MOVrr definition info for O(1) lookup.
    struct MovInfo {
        std::size_t defIdx{0};
        uint16_t srcVreg{0};
        bool srcIsPhys{false};
    };

    for (auto &block : func.blocks) {
        std::unordered_map<uint16_t, ShlInfo> shlDefs; // result vreg -> info
        std::unordered_map<uint16_t, AddInfo> addDefs; // result vreg -> info
        std::unordered_map<uint16_t, MovInfo> movDefs; // dest vreg -> info (for O(1) lookup)

        // First pass: record SHL, ADD, and MOVrr definitions.
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
            const auto &instr = block.instructions[idx];

            // Record MOVrr for O(1) lookup later (replaces O(n) linear scan)
            if (instr.opcode == MOpcode::MOVrr && instr.operands.size() >= 2) {
                const auto *dst = asReg(instr.operands[0]);
                const auto *src = asReg(instr.operands[1]);
                if (dst && src && !dst->isPhys && dst->cls == RegClass::GPR) {
                    MovInfo info{};
                    info.defIdx = idx;
                    info.srcVreg = src->idOrPhys;
                    info.srcIsPhys = src->isPhys;
                    movDefs[dst->idOrPhys] = info;
                }
            }

            // Record SHLri with shift 1, 2, or 3 (scale 2, 4, 8)
            if (instr.opcode == MOpcode::SHLri && instr.operands.size() >= 2) {
                const auto *dst = asReg(instr.operands[0]);
                const auto *shiftAmt = asImm(instr.operands[1]);
                if (dst && !dst->isPhys && dst->cls == RegClass::GPR && shiftAmt) {
                    const int64_t shift = shiftAmt->val;
                    if (shift >= 1 && shift <= 3) {
                        ShlInfo info{};
                        info.defIdx = idx;
                        info.srcVreg = dst->idOrPhys; // SHL is destructive, src == dst
                        info.scale = static_cast<uint8_t>(1 << shift);
                        shlDefs[dst->idOrPhys] = info;
                    }
                }
            }

            // Record ADDrr where one operand might be from SHL
            if (instr.opcode == MOpcode::ADDrr && instr.operands.size() >= 2) {
                const auto *dst = asReg(instr.operands[0]);
                const auto *src = asReg(instr.operands[1]);
                if (dst && src && !dst->isPhys && !src->isPhys && dst->cls == RegClass::GPR &&
                    src->cls == RegClass::GPR) {
                    // Check if src is from a SHL - then dst is base + shifted
                    if (shlDefs.count(src->idOrPhys)) {
                        AddInfo info{};
                        info.defIdx = idx;
                        info.baseVreg = dst->idOrPhys;
                        info.shiftedVreg = src->idOrPhys;
                        addDefs[dst->idOrPhys] = info;
                    }
                }
            }

        }

        // Second pass: find memory operands using ADD results and transform
        std::vector<std::size_t> toErase;
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
            auto &instr = block.instructions[idx];

            for (auto &op : instr.operands) {
                auto *mem = std::get_if<OpMem>(&op);
                if (!mem || mem->hasIndex) {
                    continue; // Skip if not memory or already has index
                }

                if (mem->base.isPhys) {
                    continue;
                }

                const uint16_t baseId = mem->base.idOrPhys;
                auto addIt = addDefs.find(baseId);
                if (addIt == addDefs.end()) {
                    continue;
                }

                const AddInfo &addInfo = addIt->second;
                auto shlIt = shlDefs.find(addInfo.shiftedVreg);
                if (shlIt == shlDefs.end()) {
                    continue;
                }

                const ShlInfo &shlInfo = shlIt->second;

                if (shlInfo.defIdx >= addInfo.defIdx || addInfo.defIdx >= idx) {
                    continue;
                }

                // Check that the ADD result and SHL result are single-use after
                // their definitions. The destructive source reads of SHL/ADD
                // are not uses of the newly defined values.
                if (countVirtualRegisterUsesInRange(
                        block, baseId, addInfo.defIdx + 1, block.instructions.size()) != 1) {
                    continue;
                }
                if (countVirtualRegisterUsesInRange(
                        block, addInfo.shiftedVreg, shlInfo.defIdx + 1,
                        block.instructions.size()) != 1) {
                    continue;
                }

                // Find the original index register before the SHL using O(1) map lookup
                uint16_t indexVreg = shlInfo.srcVreg;
                auto movIt = movDefs.find(shlInfo.srcVreg);
                if (movIt != movDefs.end() && !movIt->second.srcIsPhys &&
                    movIt->second.defIdx < shlInfo.defIdx &&
                    countVirtualRegisterUsesInRange(block, shlInfo.srcVreg,
                                                    movIt->second.defIdx + 1,
                                                    shlInfo.defIdx + 1) == 1) {
                    indexVreg = movIt->second.srcVreg;
                    toErase.push_back(movIt->second.defIdx); // Mark MOV for removal
                }

                // Find the original base register before ADD using O(1) map lookup
                uint16_t realBaseVreg = addInfo.baseVreg;
                auto baseMovIt = movDefs.find(addInfo.baseVreg);
                if (baseMovIt != movDefs.end() && baseMovIt->second.defIdx < addInfo.defIdx &&
                    countVirtualRegisterUsesInRange(block, addInfo.baseVreg,
                                                    baseMovIt->second.defIdx + 1,
                                                    addInfo.defIdx + 1) == 1) {
                    if (!baseMovIt->second.srcIsPhys) {
                        realBaseVreg = baseMovIt->second.srcVreg;
                    } else {
                        // Base is a physical register - use it directly
                        mem->base.isPhys = true;
                        mem->base.idOrPhys = baseMovIt->second.srcVreg;
                    }
                    toErase.push_back(baseMovIt->second.defIdx);
                }

                // Update memory operand with SIB addressing
                if (!mem->base.isPhys) {
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
        for (std::size_t eraseIdx : toErase) {
            if (eraseIdx < block.instructions.size()) {
                block.instructions.erase(block.instructions.begin() +
                                         static_cast<std::ptrdiff_t>(eraseIdx));
            }
        }
    }
}

/// @brief Replace IMULrr-by-small-constant with LEA strength reduction.
///
/// @details For each block, scans for the pattern:
///   MOVri  constReg, <factor>   (factor ∈ {3, 5, 9})
///   IMULrr dstReg, constReg
///
/// and rewrites it to:
///   LEA dstReg, [dstReg + dstReg * scale]
///
/// where scale = factor - 1 ∈ {2, 4, 8}.
///
/// The transformation avoids the higher latency of IMUL (3+ cycles) and
/// the flag-clobbering side-effect.  Only unchecked IMULrr is eligible;
/// IMULOvfrr carries an overflow trap and must remain as IMUL.  The
/// supplying MOVri is erased when the constant virtual register has exactly
/// one use (the IMUL itself).
///
/// @param func Machine function to transform.
void ISel::lowerMulToLea(MFunction &func) const {
    (void)target_;

    // Map factor values {3, 5, 9} to the SIB scale {2, 4, 8}.
    // scale = factor - 1; LEA [base + index*scale] = base*(1 + scale) = base*factor.
    auto factorToScale = [](int64_t factor) -> uint8_t {
        if (factor == 3)
            return 2;
        if (factor == 5)
            return 4;
        if (factor == 9)
            return 8;
        return 0; // not a LEA-eligible constant
    };

    for (auto &block : func.blocks) {
        // First pass: collect MOVri definitions of eligible constants
        // and count all vreg uses so we can check single-use invariant.
        struct MovRiDef {
            std::size_t defIdx{0};
            int64_t factor{0};
            uint8_t scale{0}; // precomputed LEA scale
        };

        std::unordered_map<uint16_t, MovRiDef> constDefs; // vreg id → MOVri info
        std::unordered_map<uint16_t, int> useCount;       // vreg id → use count

        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
            const auto &instr = block.instructions[idx];

            // Record single-constant MOVri definitions.
            if (instr.opcode == MOpcode::MOVri && instr.operands.size() >= 2) {
                const auto *dst = asReg(instr.operands[0]);
                const auto *imm = asImm(instr.operands[1]);
                if (dst && !dst->isPhys && dst->cls == RegClass::GPR && imm) {
                    const uint8_t scale = factorToScale(imm->val);
                    if (scale != 0) {
                        MovRiDef def{};
                        def.defIdx = idx;
                        def.factor = imm->val;
                        def.scale = scale;
                        constDefs[dst->idOrPhys] = def;
                    }
                }
            }

            countVirtualRegisterUses(instr, useCount);
        }

        // Second pass: find eligible IMULrr and rewrite.
        std::vector<std::size_t> toErase;

        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
            auto &instr = block.instructions[idx];

            if (instr.opcode != MOpcode::IMULrr || instr.operands.size() < 2)
                continue;

            // IMULrr layout: operands[0] = dst (also src0), operands[1] = src1
            const auto *dst = asReg(instr.operands[0]);
            const auto *src = asReg(instr.operands[1]);
            if (!dst || !src)
                continue;

            // Both registers must be virtual GPRs for this transformation.
            if (dst->isPhys || src->isPhys)
                continue;
            if (dst->cls != RegClass::GPR || src->cls != RegClass::GPR)
                continue;
            if (dst->idOrPhys == src->idOrPhys)
                continue;

            // The source register must have been defined by a MOVri with an
            // eligible factor.
            const uint16_t srcId = src->idOrPhys;
            auto defIt = constDefs.find(srcId);
            if (defIt == constDefs.end())
                continue;

            const MovRiDef &def = defIt->second;
            if (def.defIdx >= idx)
                continue;

            if (flagsReadBeforeClobber(block.instructions, idx))
                continue;

            // The constant register must have exactly one read-use (this IMUL).
            auto useIt = useCount.find(srcId);
            if (useIt == useCount.end() || useIt->second != 1)
                continue;

            // Build the replacement LEA: dst ← [dst + dst * scale]
            // OpMem with base == index captures dst*(1+scale) = dst*factor.
            const uint16_t dstId = dst->idOrPhys;
            OpMem leaMem{};
            leaMem.base.isPhys = false;
            leaMem.base.cls = RegClass::GPR;
            leaMem.base.idOrPhys = dstId;
            leaMem.index.isPhys = false;
            leaMem.index.cls = RegClass::GPR;
            leaMem.index.idOrPhys = dstId;
            leaMem.scale = def.scale;
            leaMem.disp = 0;
            leaMem.hasIndex = true;

            instr.opcode = MOpcode::LEA;
            instr.operands[0] = cloneOperand(instr.operands[0]); // dst unchanged
            instr.operands[1] = Operand{leaMem};

            // Trim any trailing operands (IMULrr has exactly 2, LEA has 2).
            instr.operands.resize(2);

            // Mark the supplying MOVri for removal.
            toErase.push_back(def.defIdx);
        }

        // Erase marked instructions in reverse index order.
        std::sort(toErase.begin(), toErase.end(), std::greater<std::size_t>());
        toErase.erase(std::unique(toErase.begin(), toErase.end()), toErase.end());
        for (const std::size_t eraseIdx : toErase) {
            if (eraseIdx < block.instructions.size()) {
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
void ISel::foldLeaIntoMem(MFunction &func) const {
    (void)target_;
    for (auto &block : func.blocks) {
        std::unordered_map<uint16_t, std::size_t> leaDefIdx; // vreg id -> instr idx

        // First pass: record LEA defs.
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
            const auto &instr = block.instructions[idx];
            if (instr.opcode == MOpcode::LEA && instr.operands.size() >= 2) {
                if (const auto *dst = asReg(instr.operands[0])) {
                    if (!dst->isPhys && dst->cls == RegClass::GPR) {
                        leaDefIdx[dst->idOrPhys] = idx;
                    }
                }
            }
        }

        // Second pass: try to fold at use sites and erase the LEA.
        std::vector<std::size_t> toErase;
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
            auto &instr = block.instructions[idx];
            for (auto &op : instr.operands) {
                if (auto *mem = std::get_if<OpMem>(&op)) {
                    const OpReg &base = mem->base;
                    if (!base.isPhys && base.cls == RegClass::GPR) {
                        const uint16_t v = base.idOrPhys;
                        auto defIt = leaDefIdx.find(v);
                        if (defIt == leaDefIdx.end()) {
                            continue;
                        }

                        const std::size_t defIndex = defIt->second;
                        if (defIndex >= idx || defIndex >= block.instructions.size()) {
                            continue;
                        }

                        if (countVirtualRegisterUsesInFunction(func, v) != 1) {
                            continue;
                        }

                        const auto &defInstr = block.instructions[defIndex];
                        if (defInstr.opcode == MOpcode::LEA && defInstr.operands.size() >= 2) {
                            if (const auto *srcMem = std::get_if<OpMem>(&defInstr.operands[1])) {
                                if (auto combined = combineLeaMemUse(*srcMem, *mem)) {
                                    *mem = *combined;
                                    toErase.push_back(defIndex);
                                    leaDefIdx.erase(defIt);
                                }
                            }
                        }
                    }
                }
            }
        }

        std::sort(toErase.begin(), toErase.end(), std::greater<std::size_t>());
        toErase.erase(std::unique(toErase.begin(), toErase.end()), toErase.end());
        for (std::size_t eraseIdx : toErase) {
            if (eraseIdx < block.instructions.size()) {
                block.instructions.erase(block.instructions.begin() +
                                         static_cast<std::ptrdiff_t>(eraseIdx));
            }
        }
    }
}

/// @brief Verify that no select pseudo-instructions survived ISel.
/// @details Lowering emits explicit SELECT_GPR/SELECT_XMM pseudos. They must be
///          replaced before allocation and emission.
void ISel::validateSelectLowering(const MFunction &func) const {
    for (const auto &block : func.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.opcode == MOpcode::SELECT_GPR || instr.opcode == MOpcode::SELECT_XMM) {
                phaseAUnsupported(("select pseudo survived ISel (opcode=" +
                                   std::to_string(static_cast<int>(instr.opcode)) +
                                   ", operands=" + std::to_string(instr.operands.size()) + ")")
                                      .c_str());
            }
            if ((instr.opcode == MOpcode::MOVri || instr.opcode == MOpcode::MOVrr ||
                 instr.opcode == MOpcode::MOVSDrr) &&
                instr.operands.size() > 2) {
                phaseAUnsupported(("legacy select placeholder survived ISel (opcode=" +
                                   std::to_string(static_cast<int>(instr.opcode)) +
                                   ", operands=" + std::to_string(instr.operands.size()) + ")")
                                      .c_str());
            }
        }
    }
}

} // namespace viper::codegen::x64
