//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/peephole/BranchOpt.cpp
// Purpose: Branch optimizations for the AArch64 peephole optimizer: CBZ/CBNZ
//          fusion, cset branch fusion, block reordering, and condition
//          inversion.
//
// Key invariants:
//   - Branch rewrites preserve control-flow semantics.
//   - Block reordering only moves cold blocks (trap/error handlers) to the end.
//
// Ownership/Lifetime:
//   - Operates on mutable MFunction/instruction vectors owned by the caller.
//
// Links: codegen/aarch64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "BranchOpt.hpp"

#include "PeepholeCommon.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace viper::codegen::aarch64::peephole {
namespace {

/// @brief Return true if @p text contains @p marker as a label-like token.
/// @details Cold-block matching remains name-based for compatibility with the
///          existing MIR, but it now requires token boundaries so unrelated labels
///          such as "stereotype" do not become cold just because they contain
///          "error" or "trap" as a substring.
/// @param text Label or symbol text to inspect.
/// @param marker Cold marker token such as "trap", "panic", or "error".
/// @return True when @p marker appears at a label-token boundary.
[[nodiscard]] bool hasColdMarkerToken(const std::string &text, std::string_view marker) noexcept {
    std::size_t pos = text.find(marker);
    while (pos != std::string::npos) {
        const bool beforeOk =
            pos == 0 || text[pos - 1] == '_' || text[pos - 1] == '.' || text[pos - 1] == '$';
        const std::size_t after = pos + marker.size();
        const bool afterOk =
            after == text.size() || text[after] == '_' || text[after] == '.' || text[after] == '$';
        if (beforeOk && afterOk)
            return true;
        pos = text.find(marker, pos + 1);
    }
    return false;
}

/// @brief Check if a block is a cold block (trap handler, error block).
[[nodiscard]] bool isColdBlock(const MBasicBlock &block) noexcept {
    if (hasColdMarkerToken(block.name, "trap"))
        return true;
    if (hasColdMarkerToken(block.name, "error"))
        return true;
    if (hasColdMarkerToken(block.name, "panic"))
        return true;

    if (block.instrs.size() == 1) {
        const auto &instr = block.instrs[0];
        if (instr.opc == MOpcode::Bl) {
            if (!instr.ops.empty() && instr.ops[0].kind == MOperand::Kind::Label) {
                const auto &label = instr.ops[0].label;
                if (hasColdMarkerToken(label, "trap") || hasColdMarkerToken(label, "panic"))
                    return true;
            }
        }
    }
    return false;
}

/// @brief Return true if @p opc is a control-transfer boundary for local scans.
/// @details CSET-to-branch fusion cannot cross instructions that may leave the
///          block, call into unknown code, or otherwise make condition flags
///          unavailable to the fused branch.
[[nodiscard]] bool isControlBarrier(MOpcode opc) noexcept {
    return opc == MOpcode::Br || opc == MOpcode::BCond || opc == MOpcode::Cbz ||
           opc == MOpcode::Cbnz || opc == MOpcode::Ret || opc == MOpcode::Bl ||
           opc == MOpcode::Blr;
}

/// @brief Return true if moving block @p idx to the end preserves implicit fallthrough.
/// @details The layout optimizer can only move a cold block when the preceding
///          block cannot implicitly fall through into it. Conditional branches
///          are deliberately rejected because their not-taken path is layout
///          dependent in this MIR.
[[nodiscard]] bool canMoveColdBlock(const MFunction &fn, std::size_t idx) noexcept {
    if (idx == 0 || idx >= fn.blocks.size())
        return false;
    const auto &prev = fn.blocks[idx - 1];
    if (prev.instrs.empty())
        return false;
    const MOpcode last = prev.instrs.back().opc;
    return last == MOpcode::Br || last == MOpcode::Ret;
}

} // namespace

const char *invertCondition(const char *cond) noexcept {
    if (!cond)
        return nullptr;
    if (std::strcmp(cond, "eq") == 0)
        return "ne";
    if (std::strcmp(cond, "ne") == 0)
        return "eq";
    if (std::strcmp(cond, "lt") == 0)
        return "ge";
    if (std::strcmp(cond, "ge") == 0)
        return "lt";
    if (std::strcmp(cond, "gt") == 0)
        return "le";
    if (std::strcmp(cond, "le") == 0)
        return "gt";
    if (std::strcmp(cond, "hi") == 0)
        return "ls";
    if (std::strcmp(cond, "ls") == 0)
        return "hi";
    if (std::strcmp(cond, "hs") == 0)
        return "lo";
    if (std::strcmp(cond, "lo") == 0)
        return "hs";
    if (std::strcmp(cond, "mi") == 0)
        return "pl";
    if (std::strcmp(cond, "pl") == 0)
        return "mi";
    if (std::strcmp(cond, "vs") == 0)
        return "vc";
    if (std::strcmp(cond, "vc") == 0)
        return "vs";
    return nullptr;
}

/// @brief Test whether @p instr is an unconditional `B label` to @p label.
/// @param instr Machine instruction to classify.
/// @param label Target block name to compare against.
/// @return True if @p instr is `B` and its label operand equals @p label.
bool isBranchTo(const MInstr &instr, const std::string &label) noexcept {
    if (instr.opc != MOpcode::Br)
        return false;
    if (instr.ops.empty() || instr.ops[0].kind != MOperand::Kind::Label)
        return false;
    return instr.ops[0].label == label;
}

/// @brief Fuse `CMP Xn, #0` / `TST Xn, Xn` followed by `B.eq` / `B.ne` into `CBZ` / `CBNZ`.
/// @details The shorter `CBZ`/`CBNZ` form encodes both the compare-with-zero and the
///          branch in a single instruction, freeing the flag pipeline. Fusion requires:
///          (1) the compare must be against zero (either `CMP` with imm 0 or a `TST`
///          of a register against itself); (2) the operand must be a GPR; (3) the
///          following branch must be `B.eq` (→ `CBZ`) or `B.ne` (→ `CBNZ`).
/// @param instrs Instruction list being scanned (mutated in place).
/// @param idx    Index of the compare-against-zero candidate.
/// @param stats  Peephole statistics counter (incremented on success).
/// @return True if the fusion was applied at @p idx.
bool tryCbzCbnzFusion(std::vector<MInstr> &instrs, std::size_t idx, PeepholeStats &stats) {
    if (idx + 1 >= instrs.size())
        return false;

    const auto &cmpInstr = instrs[idx];
    const auto &brInstr = instrs[idx + 1];

    bool isCmpZero = false;
    MOperand regOp;

    if (cmpInstr.opc == MOpcode::CmpRI && cmpInstr.ops.size() == 2 && isPhysReg(cmpInstr.ops[0]) &&
        isImmValue(cmpInstr.ops[1], 0)) {
        isCmpZero = true;
        regOp = cmpInstr.ops[0];
    } else if (cmpInstr.opc == MOpcode::TstRR && cmpInstr.ops.size() == 2 &&
               isPhysReg(cmpInstr.ops[0]) && samePhysReg(cmpInstr.ops[0], cmpInstr.ops[1])) {
        isCmpZero = true;
        regOp = cmpInstr.ops[0];
    }

    if (!isCmpZero)
        return false;

    if (regOp.reg.cls != RegClass::GPR)
        return false;

    if (brInstr.opc != MOpcode::BCond || brInstr.ops.size() != 2)
        return false;
    if (brInstr.ops[0].kind != MOperand::Kind::Cond || brInstr.ops[1].kind != MOperand::Kind::Label)
        return false;

    const char *cond = brInstr.ops[0].cond;
    if (!cond)
        return false;

    MOpcode fusedOpc;
    if (std::strcmp(cond, "eq") == 0)
        fusedOpc = MOpcode::Cbz;
    else if (std::strcmp(cond, "ne") == 0)
        fusedOpc = MOpcode::Cbnz;
    else
        return false;

    instrs[idx].opc = fusedOpc;
    instrs[idx].ops.clear();
    instrs[idx].ops.push_back(regOp);
    instrs[idx].ops.push_back(brInstr.ops[1]);

    instrs.erase(instrs.begin() + static_cast<std::ptrdiff_t>(idx + 1));
    ++stats.cbzFusions;
    return true;
}

/// @brief Fold `CSET Xd, cond` + `CBZ Xd, label` (or `CBNZ`) into `B.cond label`.
/// @details `CSET` materialises a 0/1 boolean into a GPR, and a subsequent compare-to-zero
///          branch on that boolean is equivalent to branching on the original condition
///          (inverted for `CBZ`). Fusion requires that `Xd` is dead after the branch
///          (no later use before redefinition). On success the two-instruction sequence
///          becomes a single `B.cond` with the appropriate condition code.
/// @param instrs Instruction list being scanned (mutated in place).
/// @param idx    Index of the `CSET` to consider.
/// @param stats  Peephole statistics counter (incremented on success).
/// @return True if the fusion was applied at @p idx.
bool tryCsetBranchFusion(std::vector<MInstr> &instrs, std::size_t idx, PeepholeStats &stats) {
    if (idx >= instrs.size())
        return false;

    const auto &csetInstr = instrs[idx];
    if (csetInstr.opc != MOpcode::Cset || csetInstr.ops.size() != 2)
        return false;
    if (csetInstr.ops[0].kind != MOperand::Kind::Reg ||
        csetInstr.ops[1].kind != MOperand::Kind::Cond)
        return false;

    const MOperand csetReg = csetInstr.ops[0];
    const char *cond = csetInstr.ops[1].cond;
    if (!cond)
        return false;

    for (std::size_t j = idx + 1; j < instrs.size(); ++j) {
        const auto &next = instrs[j];

        if ((next.opc == MOpcode::Cbnz || next.opc == MOpcode::Cbz) && next.ops.size() == 2 &&
            isPhysReg(next.ops[0]) && samePhysReg(next.ops[0], csetReg) &&
            next.ops[1].kind == MOperand::Kind::Label) {
            const char *brCond = cond;
            if (next.opc == MOpcode::Cbz) {
                brCond = invertCondition(cond);
                if (!brCond)
                    return false;
            }

            bool regDead = true;
            for (std::size_t k = j + 1; k < instrs.size(); ++k) {
                const auto &later = instrs[k];
                if (isControlBarrier(later.opc))
                    break;
                for (std::size_t oi = 0; oi < later.ops.size(); ++oi) {
                    if (oi == 0 && definesReg(later, csetReg)) {
                        break;
                    }
                    if (isPhysReg(later.ops[oi]) && samePhysReg(later.ops[oi], csetReg)) {
                        regDead = false;
                        break;
                    }
                }
                if (!regDead)
                    break;
            }
            if (!regDead)
                return false;

            instrs[j] = MInstr{MOpcode::BCond, {MOperand::condOp(brCond), next.ops[1]}};
            instrs.erase(instrs.begin() + static_cast<std::ptrdiff_t>(idx));
            ++stats.cbzFusions;
            return true;
        }

        switch (next.opc) {
            case MOpcode::CmpRR:
            case MOpcode::CmpRI:
            case MOpcode::TstRR:
            case MOpcode::FCmpRR:
            case MOpcode::AddsRRR:
            case MOpcode::SubsRRR:
            case MOpcode::AddsRI:
            case MOpcode::SubsRI:
            case MOpcode::Cset:
            case MOpcode::Bl:
            case MOpcode::Blr:
                return false;
            default:
                break;
        }

        for (const auto &op : next.ops) {
            if (isPhysReg(op) && samePhysReg(op, csetReg))
                return false;
        }

        if (isControlBarrier(next.opc))
            return false;
    }
    return false;
}

std::size_t reorderBlocks(MFunction &fn) {
    if (fn.blocks.size() <= 2)
        return 0;

    std::vector<std::size_t> coldIndices;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        if (isColdBlock(fn.blocks[i]) && canMoveColdBlock(fn, i))
            coldIndices.push_back(i);
    }

    if (coldIndices.empty())
        return 0;

    std::vector<MBasicBlock> reordered;
    reordered.reserve(fn.blocks.size());

    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        bool cold = std::find(coldIndices.begin(), coldIndices.end(), i) != coldIndices.end();
        if (!cold)
            reordered.push_back(std::move(fn.blocks[i]));
    }

    for (std::size_t idx : coldIndices)
        reordered.push_back(std::move(fn.blocks[idx]));

    fn.blocks = std::move(reordered);
    return coldIndices.size();
}

} // namespace viper::codegen::aarch64::peephole
