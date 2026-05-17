//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/TerminatorLowering.cpp
// Purpose: Control-flow terminator lowering for IL→MIR conversion.
//          Handles br, cbr, trap, TrapFromErr, switch, and resume.label.
//
// Key invariants:
//   - Terminators are lowered after all non-terminator instructions so
//     branch targets and phi-edge vreg mappings are fully resolved.
//   - SSA phi-edge copies are inserted as PhiStoreGPR/PhiStoreFPR
//     instructions into predecessor (or edge split) blocks.
//   - Switch trees with >3 cases are lowered to a recursive binary search
//     over new auxiliary blocks.
//
// Ownership/Lifetime:
//   - Modifies mf in place; all map/builder arguments are borrowed.
//
// Links: codegen/aarch64/TerminatorLowering.hpp,
//        codegen/aarch64/InstrLowering.hpp,
//        codegen/aarch64/OpcodeMappings.hpp
//
//===----------------------------------------------------------------------===//

#include "TerminatorLowering.hpp"
#include "FpCompareLowering.hpp"
#include "InstrLowering.hpp"
#include "OpcodeMappings.hpp"

#include <algorithm>
#include <stdexcept>

namespace viper::codegen::aarch64 {

using il::core::Opcode;

/// @brief Return the AArch64 condition code string for a comparison opcode, or nullptr.
static const char *condForOpcode(Opcode op) {
    return lookupAnyCondition(op);
}

/// @brief Emit a rt_trap_string call with a null (0) message payload into @p bb.
static void emitNullTrapCall(MBasicBlock &bb) {
    bb.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
    bb.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_string")}});
}

/// @brief One case arm in a switch/select table: the integer value and the MIR branch target.
struct SwitchCase {
    long long value{0};      ///< Integer constant matched by this case.
    std::string branchLabel; ///< MIR block label to branch to when matched.
};

/// @brief Emit a compare of @p scrutineeVReg against @p imm into @p bb.
/// @details Uses CmpRI for small non-negative immediates; falls back to
///          MovRI + CmpRR for larger values.
static void emitSwitchCompare(MBasicBlock &bb,
                              uint16_t scrutineeVReg,
                              long long imm,
                              uint16_t &nextVRegId) {
    if (isUImm12(imm)) {
        bb.instrs.push_back(MInstr{MOpcode::CmpRI,
                                   {MOperand::vregOp(RegClass::GPR, scrutineeVReg),
                                    MOperand::immOp(imm)}});
        return;
    }

    const uint16_t caseVReg = allocateNextVReg(nextVRegId);
    bb.instrs.push_back(MInstr{
        MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, caseVReg), MOperand::immOp(imm)}});
    bb.instrs.push_back(MInstr{MOpcode::CmpRR,
                               {MOperand::vregOp(RegClass::GPR, scrutineeVReg),
                                MOperand::vregOp(RegClass::GPR, caseVReg)}});
}

/// @brief Emit a reload of the switch scrutinee from its spill slot into a fresh vreg.
/// @details Used in binary search auxiliary blocks that need the scrutinee after
///          the original vreg's live range has ended.
/// @return The new vreg holding the reloaded scrutinee value.
static uint16_t reloadSwitchScrutinee(MBasicBlock &bb, int spillOffset, uint16_t &nextVRegId) {
    const uint16_t reloaded = allocateNextVReg(nextVRegId);
    bb.instrs.push_back(MInstr{MOpcode::LdrRegFpImm,
                               {MOperand::vregOp(RegClass::GPR, reloaded),
                                MOperand::immOp(spillOffset)}});
    return reloaded;
}

/// @brief Emit a linear scan of @p cases[begin..end) against @p scrutineeVReg.
/// @details Used for small case counts (≤3). Emits a CmpRI/CmpRR + BCond pair
///          per case, followed by an unconditional branch to @p defaultBranchLabel.
static void emitLinearSwitch(MBasicBlock &bb,
                             uint16_t scrutineeVReg,
                             const std::vector<SwitchCase> &cases,
                             std::size_t begin,
                             std::size_t end,
                             const std::string &defaultBranchLabel,
                             uint16_t &nextVRegId) {
    for (std::size_t idx = begin; idx < end; ++idx) {
        const auto &switchCase = cases[idx];
        emitSwitchCompare(bb, scrutineeVReg, switchCase.value, nextVRegId);
        bb.instrs.push_back(MInstr{
            MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(switchCase.branchLabel)}});
    }
    if (!defaultBranchLabel.empty())
        bb.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(defaultBranchLabel)}});
}

/// @brief Recursively emit a binary search tree for @p cases[begin..end) into @p mf.
/// @details Picks a pivot at the midpoint, emits a compare + branch-on-equal, then
///          creates two new MIR blocks for the left and right sub-ranges. The scrutinee
///          is reloaded from @p spillOffset in each auxiliary block. Falls back to
///          `emitLinearSwitch` when the sub-range has ≤3 entries.
/// @param mf             Output MIR function (new auxiliary blocks are appended).
/// @param bb             Current dispatch block to emit the compare/branches into.
/// @param scrutineeVReg  Virtual register holding the switch value in @p bb.
/// @param cases          Sorted case table for the entire switch.
/// @param begin          Start index of the current sub-range (inclusive).
/// @param end            End index of the current sub-range (exclusive).
/// @param defaultBranchLabel Label of the default target block.
/// @param spillOffset    FP-relative spill slot for the scrutinee (used in sub-blocks).
/// @param labelPrefix    Prefix for generated auxiliary block label names.
/// @param auxCounter     Counter for unique auxiliary block name suffixes.
/// @param nextVRegId     Counter for fresh virtual register allocation.
static void emitSwitchTree(MFunction &mf,
                           MBasicBlock &bb,
                           uint16_t scrutineeVReg,
                           const std::vector<SwitchCase> &cases,
                           std::size_t begin,
                           std::size_t end,
                           const std::string &defaultBranchLabel,
                           int spillOffset,
                           const std::string &labelPrefix,
                           std::size_t &auxCounter,
                           uint16_t &nextVRegId) {
    const std::size_t count = end - begin;
    if (count <= 3) {
        emitLinearSwitch(bb, scrutineeVReg, cases, begin, end, defaultBranchLabel, nextVRegId);
        return;
    }

    const std::size_t pivotIndex = begin + count / 2;
    const SwitchCase &pivot = cases[pivotIndex];
    const std::string leftLabel =
        labelPrefix + ".Lswitch_left_" + std::to_string(auxCounter++);
    const std::string rightLabel =
        labelPrefix + ".Lswitch_right_" + std::to_string(auxCounter++);

    emitSwitchCompare(bb, scrutineeVReg, pivot.value, nextVRegId);
    bb.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(pivot.branchLabel)}});
    bb.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("lt"), MOperand::labelOp(leftLabel)}});
    bb.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(rightLabel)}});

    mf.blocks.emplace_back();
    auto &leftBB = mf.blocks.back();
    leftBB.name = leftLabel;
    const uint16_t leftScrutinee = reloadSwitchScrutinee(leftBB, spillOffset, nextVRegId);
    emitSwitchTree(mf,
                   leftBB,
                   leftScrutinee,
                   cases,
                   begin,
                   pivotIndex,
                   defaultBranchLabel,
                   spillOffset,
                   leftLabel,
                   auxCounter,
                   nextVRegId);

    mf.blocks.emplace_back();
    auto &rightBB = mf.blocks.back();
    rightBB.name = rightLabel;
    const uint16_t rightScrutinee = reloadSwitchScrutinee(rightBB, spillOffset, nextVRegId);
    emitSwitchTree(mf,
                   rightBB,
                   rightScrutinee,
                   cases,
                   pivotIndex + 1,
                   end,
                   defaultBranchLabel,
                   spillOffset,
                   rightLabel,
                   auxCounter,
                   nextVRegId);
}

/// @brief Emit PhiStoreGPR/PhiStoreFPR instructions into @p edgeBB for the phi
///        parameters of block @p dst, materializing each argument from @p args.
/// @details Called when a branch to @p dst carries arguments. Each argument is
///          materialized to a vreg and then stored to the corresponding phi spill
///          slot. Throws if an argument cannot be materialized or has a class mismatch.
/// @param edgeBB         The MIR block (edge or source) that receives the phi stores.
/// @param dst            IL block name whose phi slots receive the copies.
/// @param args           IL branch argument values (one per phi parameter in dst).
/// @param inBB           IL basic block containing the branch instruction.
/// @param ti             Target info for register class resolution.
/// @param fb             Frame builder for phi spill slot allocation.
/// @param blockTempVReg  Block-local temp-ID → vreg mapping snapshot.
/// @param tempRegClass   Global temp-ID → register class map.
/// @param nextVRegId     Counter for fresh virtual register allocation.
/// @param phiRegClass    Block label → per-parameter register class list.
/// @param phiSpillOffset Block label → per-parameter FP-relative spill offsets.
static void emitPhiEdgeCopies(
    MBasicBlock &edgeBB,
    const std::string &dst,
    const std::vector<il::core::Value> &args,
    const il::core::BasicBlock &inBB,
    const TargetInfo &ti,
    FrameBuilder &fb,
    std::unordered_map<unsigned, uint16_t> &blockTempVReg,
    std::unordered_map<unsigned, RegClass> &tempRegClass,
    uint16_t &nextVRegId,
    const std::unordered_map<std::string, std::vector<RegClass>> &phiRegClass,
    const std::unordered_map<std::string, std::vector<int>> &phiSpillOffset) {
    auto itSpill = phiSpillOffset.find(dst);
    if (itSpill == phiSpillOffset.end())
        return;
    auto itClass = phiRegClass.find(dst);
    if (itClass == phiRegClass.end())
        return;
    const auto &classes = itClass->second;
    const auto &spillOffsets = itSpill->second;
    for (std::size_t ai = 0; ai < args.size() && ai < spillOffsets.size(); ++ai) {
        uint16_t sv = 0;
        RegClass scls = RegClass::GPR;
        if (!materializeValueToVReg(args[ai],
                                    inBB,
                                    ti,
                                    fb,
                                    edgeBB,
                                    blockTempVReg,
                                    tempRegClass,
                                    nextVRegId,
                                    sv,
                                    scls)) {
            throw std::runtime_error("AArch64 terminator lowering: failed to materialize phi-edge "
                                     "argument for block '" +
                                     dst + "'");
        }
        const RegClass dstCls = classes[ai];
        const int offset = spillOffsets[ai];
        if (scls != dstCls) {
            throw std::runtime_error("AArch64 terminator lowering: phi-edge argument register class "
                                     "mismatch for block '" +
                                     dst + "'");
        }
        if (dstCls == RegClass::FPR) {
            edgeBB.instrs.push_back(MInstr{MOpcode::PhiStoreFPR,
                                           {MOperand::vregOp(RegClass::FPR, sv),
                                            MOperand::immOp(offset)}});
        } else {
            edgeBB.instrs.push_back(MInstr{MOpcode::PhiStoreGPR,
                                           {MOperand::vregOp(RegClass::GPR, sv),
                                            MOperand::immOp(offset)}});
        }
    }
}

void lowerTerminators(const il::core::Function &fn,
                      MFunction &mf,
                      const TargetInfo &ti,
                      FrameBuilder &fb,
                      const std::unordered_map<std::string, std::vector<uint16_t>> &phiVregId,
                      const std::unordered_map<std::string, std::vector<RegClass>> &phiRegClass,
                      const std::unordered_map<std::string, std::vector<int>> &phiSpillOffset,
                      std::vector<std::unordered_map<unsigned, uint16_t>> &blockTempVRegSnapshot,
                      std::unordered_map<unsigned, RegClass> &tempRegClass,
                      uint16_t &nextVRegId) {
    std::size_t switchAuxCounter = 0;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto &inBB = fn.blocks[i];
        if (inBB.instructions.empty())
            continue;
        const auto &term = inBB.instructions.back();
        auto &outBB = mf.blocks[i];
        // Use the block's tempVReg snapshot to get correct vreg mappings for temps
        // defined in this block. This avoids using overwritten values from later blocks.
        auto &blockTempVReg = blockTempVRegSnapshot[i];

        switch (term.op) {
            case Opcode::Br:
                if (!term.labels.empty()) {
                    // Emit phi edge copies for target - store to spill slots
                    if (!term.brArgs.empty() && !term.brArgs[0].empty()) {
                        const std::string &dst = term.labels[0];
                        emitPhiEdgeCopies(outBB,
                                          dst,
                                          term.brArgs[0],
                                          inBB,
                                          ti,
                                          fb,
                                          blockTempVReg,
                                          tempRegClass,
                                          nextVRegId,
                                          phiRegClass,
                                          phiSpillOffset);
                    }
                    outBB.instrs.push_back(
                        MInstr{MOpcode::Br, {MOperand::labelOp(term.labels[0])}});
                }
                break;

            case Opcode::Trap: {
                // Phase A: lower trap to a helper call for diagnostics.
                // Skip emitting rt_trap if the block already has a call to a noreturn function
                // like rt_arr_oob_panic (which will abort and never return).
                bool hasNoreturnCall = false;
                for (const auto &mi : outBB.instrs) {
                    if (mi.opc == MOpcode::Bl && !mi.ops.empty() &&
                        mi.ops[0].kind == MOperand::Kind::Label) {
                        const std::string &callee = mi.ops[0].label;
                        if (callee == "rt_arr_oob_panic" || callee == "rt_trap" ||
                            callee == "rt_trap_string" || callee == "rt_trap_div0" ||
                            callee == "rt_trap_ovf") {
                            hasNoreturnCall = true;
                            break;
                        }
                    }
                }
                if (!hasNoreturnCall) {
                    if (!term.operands.empty()) {
                        uint16_t trapArg = 0;
                        RegClass trapArgCls = RegClass::GPR;
                        if (!materializeValueToVReg(term.operands[0],
                                                    inBB,
                                                    ti,
                                                    fb,
                                                    outBB,
                                                    blockTempVReg,
                                                    tempRegClass,
                                                    nextVRegId,
                                                    trapArg,
                                                    trapArgCls) ||
                            trapArgCls != RegClass::GPR) {
                            throw std::runtime_error(
                                "AArch64 terminator lowering: failed to materialize trap payload");
                        }
                        outBB.instrs.push_back(
                            MInstr{MOpcode::MovRR,
                                   {MOperand::regOp(PhysReg::X0),
                                    MOperand::vregOp(RegClass::GPR, trapArg)}});
                        outBB.instrs.push_back(
                            MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_string")}});
                    } else {
                        emitNullTrapCall(outBB);
                    }
                }
                break;
            }

            case Opcode::TrapFromErr: {
                bool hasRaiseErrorCall = false;
                for (const auto &mi : outBB.instrs) {
                    if (mi.opc == MOpcode::Bl && !mi.ops.empty() &&
                        mi.ops[0].kind == MOperand::Kind::Label &&
                        mi.ops[0].label == "rt_trap_raise_error") {
                        hasRaiseErrorCall = true;
                        break;
                    }
                }
                if (!hasRaiseErrorCall) {
                    if (term.operands.empty()) {
                        emitNullTrapCall(outBB);
                    } else {
                        uint16_t errToken = 0;
                        RegClass errCls = RegClass::GPR;
                        if (!materializeValueToVReg(term.operands[0],
                                                    inBB,
                                                    ti,
                                                    fb,
                                                    outBB,
                                                    blockTempVReg,
                                                    tempRegClass,
                                                    nextVRegId,
                                                    errToken,
                                                    errCls) ||
                            errCls != RegClass::GPR) {
                            throw std::runtime_error(
                                "AArch64 terminator lowering: failed to materialize trap error token");
                        }
                        outBB.instrs.push_back(
                            MInstr{MOpcode::MovRR,
                                   {MOperand::regOp(PhysReg::X0),
                                    MOperand::vregOp(RegClass::GPR, errToken)}});
                        outBB.instrs.push_back(
                            MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_raise_error")}});
                    }
                }
                break;
            }

            case Opcode::CBr:
                if (term.operands.size() >= 1 && term.labels.size() == 2) {
                    const std::string &trueLbl = term.labels[0];
                    const std::string &falseLbl = term.labels[1];
                    const bool sameTarget = (trueLbl == falseLbl);
                    const bool needTrueEdge =
                        sameTarget || (term.brArgs.size() > 0 && !term.brArgs[0].empty());
                    const bool needFalseEdge =
                        sameTarget || (term.brArgs.size() > 1 && !term.brArgs[1].empty());
                    const std::string trueEdgeLbl =
                        needTrueEdge ? outBB.name + ".Ledge_true_" + std::to_string(i) : trueLbl;
                    const std::string falseEdgeLbl =
                        needFalseEdge ? outBB.name + ".Ledge_false_" + std::to_string(i)
                                      : falseLbl;

                    // Try to lower compare producers directly to cmp/fcmp + b.<cond>.
                    const auto &cond = term.operands[0];
                    bool loweredViaCompare = false;
                    if (cond.kind == il::core::Value::Kind::Temp) {
                        const auto it = std::find_if(inBB.instructions.begin(),
                                                     inBB.instructions.end(),
                                                     [&](const il::core::Instr &I) {
                                                         return I.result && *I.result == cond.id;
                                                     });
                        if (it != inBB.instructions.end()) {
                            const il::core::Instr &cmpI = *it;
                            const char *cc = condForOpcode(cmpI.op);
                            if (cc && cmpI.operands.size() == 2) {
                                uint16_t lhs = 0;
                                RegClass lhsCls = RegClass::GPR;
                                if (materializeValueToVReg(cmpI.operands[0],
                                                           inBB,
                                                           ti,
                                                           fb,
                                                           outBB,
                                                           blockTempVReg,
                                                           tempRegClass,
                                                           nextVRegId,
                                                           lhs,
                                                           lhsCls)) {
                                    const bool isFpCompare = isFloatingPointCompareOp(cmpI.op);
                                    if (isFpCompare) {
                                        uint16_t rhs = 0;
                                        RegClass rhsCls = RegClass::FPR;
                                        if (materializeValueToVReg(cmpI.operands[1],
                                                                   inBB,
                                                                   ti,
                                                                   fb,
                                                                   outBB,
                                                                   blockTempVReg,
                                                                   tempRegClass,
                                                                   nextVRegId,
                                                                   rhs,
                                                                   rhsCls) &&
                                            lhsCls == RegClass::FPR && rhsCls == RegClass::FPR) {
                                            outBB.instrs.push_back(MInstr{
                                                MOpcode::FCmpRR,
                                                {MOperand::vregOp(RegClass::FPR, lhs),
                                                 MOperand::vregOp(RegClass::FPR, rhs)}});
                                            (void)cc;
                                            const uint16_t cmpResult = allocateNextVReg(nextVRegId);
                                            emitFpCompareResult(
                                                outBB, cmpI.op, cmpResult, nextVRegId);
                                            outBB.instrs.push_back(
                                                MInstr{MOpcode::Cbnz,
                                                       {MOperand::vregOp(RegClass::GPR, cmpResult),
                                                        MOperand::labelOp(trueEdgeLbl)}});
                                            outBB.instrs.push_back(
                                                MInstr{MOpcode::Br,
                                                       {MOperand::labelOp(falseEdgeLbl)}});
                                            loweredViaCompare = true;
                                        }
                                    } else if (cmpI.operands[1].kind == il::core::Value::Kind::ConstInt &&
                                               isUImm12(cmpI.operands[1].i64)) {
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::CmpRI,
                                                   {MOperand::vregOp(RegClass::GPR, lhs),
                                                    MOperand::immOp(cmpI.operands[1].i64)}});
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::BCond,
                                                   {MOperand::condOp(cc),
                                                    MOperand::labelOp(trueEdgeLbl)}});
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::Br, {MOperand::labelOp(falseEdgeLbl)}});
                                        loweredViaCompare = true;
                                    } else {
                                        uint16_t rhs = 0;
                                        RegClass rhsCls = RegClass::GPR;
                                        if (materializeValueToVReg(cmpI.operands[1],
                                                                   inBB,
                                                                   ti,
                                                                   fb,
                                                                   outBB,
                                                                   blockTempVReg,
                                                                   tempRegClass,
                                                                   nextVRegId,
                                                                   rhs,
                                                                   rhsCls) &&
                                            lhsCls == RegClass::GPR && rhsCls == RegClass::GPR) {
                                            outBB.instrs.push_back(MInstr{
                                                MOpcode::CmpRR,
                                                {MOperand::vregOp(RegClass::GPR, lhs),
                                                 MOperand::vregOp(RegClass::GPR, rhs)}});
                                            outBB.instrs.push_back(
                                                MInstr{MOpcode::BCond,
                                                       {MOperand::condOp(cc),
                                                        MOperand::labelOp(trueEdgeLbl)}});
                                            outBB.instrs.push_back(
                                                MInstr{MOpcode::Br,
                                                       {MOperand::labelOp(falseEdgeLbl)}});
                                            loweredViaCompare = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (!loweredViaCompare) {
                        // Materialize boolean and branch on non-zero
                        // Use the block's tempVReg snapshot to get correct vreg mappings
                        uint16_t cv = 0;
                        RegClass cc = RegClass::GPR;
                        materializeValueToVReg(cond,
                                               inBB,
                                               ti,
                                               fb,
                                               outBB,
                                               blockTempVReg,
                                               tempRegClass,
                                               nextVRegId,
                                               cv,
                                               cc);
                        outBB.instrs.push_back(
                            MInstr{MOpcode::CmpRI,
                                   {MOperand::vregOp(RegClass::GPR, cv), MOperand::immOp(0)}});
                        outBB.instrs.push_back(
                            MInstr{MOpcode::BCond,
                                   {MOperand::condOp("ne"), MOperand::labelOp(trueEdgeLbl)}});
                        outBB.instrs.push_back(
                            MInstr{MOpcode::Br, {MOperand::labelOp(falseEdgeLbl)}});
                    }

                    if (needTrueEdge) {
                        MBasicBlock trueEdgeBB;
                        trueEdgeBB.name = trueEdgeLbl;
                        if (term.brArgs.size() > 0)
                            emitPhiEdgeCopies(trueEdgeBB,
                                              trueLbl,
                                              term.brArgs[0],
                                              inBB,
                                              ti,
                                              fb,
                                              blockTempVReg,
                                              tempRegClass,
                                              nextVRegId,
                                              phiRegClass,
                                              phiSpillOffset);
                        trueEdgeBB.instrs.push_back(
                            MInstr{MOpcode::Br, {MOperand::labelOp(trueLbl)}});
                        mf.blocks.push_back(std::move(trueEdgeBB));
                    }

                    if (needFalseEdge) {
                        MBasicBlock falseEdgeBB;
                        falseEdgeBB.name = falseEdgeLbl;
                        if (term.brArgs.size() > 1)
                            emitPhiEdgeCopies(falseEdgeBB,
                                              falseLbl,
                                              term.brArgs[1],
                                              inBB,
                                              ti,
                                              fb,
                                              blockTempVReg,
                                              tempRegClass,
                                              nextVRegId,
                                              phiRegClass,
                                              phiSpillOffset);
                        falseEdgeBB.instrs.push_back(
                            MInstr{MOpcode::Br, {MOperand::labelOp(falseLbl)}});
                        mf.blocks.push_back(std::move(falseEdgeBB));
                    }
                }
                break;

            case Opcode::SwitchI32:
                if (!term.operands.empty()) {
                    uint16_t sv = 0;
                    RegClass scls = RegClass::GPR;
                    if (!materializeValueToVReg(term.operands[0],
                                                inBB,
                                                ti,
                                                fb,
                                                outBB,
                                                blockTempVReg,
                                                tempRegClass,
                                                nextVRegId,
                                                sv,
                                                scls)) {
                        throw std::runtime_error(
                            "AArch64 terminator lowering: failed to materialize switch scrutinee");
                    }
                    if (scls != RegClass::GPR) {
                        throw std::runtime_error(
                            "AArch64 terminator lowering: switch scrutinee lowered to non-GPR");
                    }

                    const std::size_t ncases = il::core::switchCaseCount(term);
                    std::vector<SwitchCase> cases;
                    cases.reserve(ncases);
                    for (std::size_t ci = 0; ci < ncases; ++ci) {
                        const auto &caseValue = il::core::switchCaseValue(term, ci);
                        const std::string &caseLabel = il::core::switchCaseLabel(term, ci);
                        long long imm = 0;
                        if (caseValue.kind == il::core::Value::Kind::ConstInt)
                            imm = caseValue.i64;

                        const auto &args = il::core::switchCaseArgs(term, ci);
                        std::string branchLabel = caseLabel;
                        if (!args.empty()) {
                            branchLabel = outBB.name + ".Lswitch_case_" + std::to_string(i) + "_" +
                                          std::to_string(ci);
                            MBasicBlock edgeBB;
                            edgeBB.name = branchLabel;
                            emitPhiEdgeCopies(edgeBB,
                                              caseLabel,
                                              args,
                                              inBB,
                                              ti,
                                              fb,
                                              blockTempVReg,
                                              tempRegClass,
                                              nextVRegId,
                                              phiRegClass,
                                              phiSpillOffset);
                            edgeBB.instrs.push_back(
                                MInstr{MOpcode::Br, {MOperand::labelOp(caseLabel)}});
                            mf.blocks.push_back(std::move(edgeBB));
                        }
                        cases.push_back(SwitchCase{imm, std::move(branchLabel)});
                    }

                    const std::string &defLbl = il::core::switchDefaultLabel(term);
                    std::string defaultBranchLabel = defLbl;
                    if (!defLbl.empty()) {
                        const auto &defArgs = il::core::switchDefaultArgs(term);
                        if (!defArgs.empty()) {
                            defaultBranchLabel =
                                outBB.name + ".Lswitch_default_" + std::to_string(i);
                            MBasicBlock edgeBB;
                            edgeBB.name = defaultBranchLabel;
                            emitPhiEdgeCopies(edgeBB,
                                              defLbl,
                                              defArgs,
                                              inBB,
                                              ti,
                                              fb,
                                              blockTempVReg,
                                              tempRegClass,
                                              nextVRegId,
                                              phiRegClass,
                                              phiSpillOffset);
                            edgeBB.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(defLbl)}});
                            mf.blocks.push_back(std::move(edgeBB));
                        }
                    }

                    std::sort(cases.begin(),
                              cases.end(),
                              [](const SwitchCase &lhs, const SwitchCase &rhs) {
                                  return lhs.value < rhs.value;
                              });

                    if (cases.size() <= 3) {
                        emitLinearSwitch(
                            outBB, sv, cases, 0, cases.size(), defaultBranchLabel, nextVRegId);
                    } else {
                        const uint32_t switchSpillKey = 0xE0000000u + static_cast<uint32_t>(i);
                        const int switchSpillOffset = fb.ensureSpill(switchSpillKey);
                        outBB.instrs.push_back(MInstr{MOpcode::StrRegFpImm,
                                                      {MOperand::vregOp(RegClass::GPR, sv),
                                                       MOperand::immOp(switchSpillOffset)}});
                        emitSwitchTree(mf,
                                       outBB,
                                       sv,
                                       cases,
                                       0,
                                       cases.size(),
                                       defaultBranchLabel,
                                       switchSpillOffset,
                                       outBB.name + ".Lswitch_tree_" + std::to_string(i),
                                       switchAuxCounter,
                                       nextVRegId);
                    }
                }
                break;

            case Opcode::ResumeLabel:
                // resume.label is a branch to an explicit target label.
                // The resume token operand is ignored in native codegen.
                if (!term.labels.empty()) {
                    outBB.instrs.push_back(
                        MInstr{MOpcode::Br, {MOperand::labelOp(term.labels[0])}});
                }
                break;

            default:
                break;
        }
    }
}

} // namespace viper::codegen::aarch64
