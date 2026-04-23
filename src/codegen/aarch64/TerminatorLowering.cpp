//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file TerminatorLowering.cpp
/// @brief Control-flow terminator lowering for IL to MIR conversion.
///
/// This file handles the lowering of IL terminator instructions that end
/// basic blocks and transfer control to other blocks. Terminators are
/// lowered in a separate pass after all other instructions to ensure
/// branch targets and phi-edge copies are correctly emitted.
///
/// **What are Terminators?**
/// Terminators are instructions that end a basic block and specify where
/// control flows next. IL terminators include:
/// - `br` - Unconditional branch to a single target
/// - `cbr` - Conditional branch based on a boolean value
/// - `ret` - Return from function
/// - `trap` - Abort execution with an error
///
/// **Phi-Edge Copies:**
/// SSA phi nodes are eliminated by inserting copies on CFG edges. When a
/// block branches to a target that has parameters (phi nodes), the branch
/// arguments must be copied to the parameter spill slots:
///
/// ```
/// IL:
///   block loop(counter: i64):
///     ...
///     br loop(%counter + 1)   ; pass argument to phi
///
/// MIR:
///   ; At end of block before br:
///   v1 = AddRI v0, #1         ; compute new counter
///   StrRegFpImm v1, [fp, #phi_slot]  ; store to phi spill
///   Br .Lloop
///
///   .Lloop:
///   LdrRegFpImm v0, [fp, #phi_slot]  ; reload phi value
/// ```
///
/// **Branch Lowering:**
/// | IL Terminator | MIR Sequence                                |
/// |---------------|---------------------------------------------|
/// | br target     | [phi copies] + Br .Ltarget                  |
/// | cbr %c, T, F  | [phi copies] + CmpRI/Cset + BCond/Br        |
/// | ret %v        | MovRR x0, %v + Ret                          |
/// | trap          | Bl _rt_trap                                 |
///
/// **Conditional Branch Expansion:**
/// IL cbr takes a boolean condition and two targets. Lowering produces:
/// ```
/// cbr %cond, then_block, else_block
///
/// MIR:
///   ; Emit phi-edge copies for BOTH targets (computed before condition)
///   ; Then emit condition test and branches:
///   CmpRI v_cond, #0
///   BCond ne, .Lthen_block   ; if cond != 0, branch to then
///   Br .Lelse_block          ; fall through to else
/// ```
///
/// **Trap Handling:**
/// Trap instructions call the runtime trap handler with a message:
/// ```
/// trap "index out of bounds"
///
/// MIR:
///   AdrPage x0, .Ltrap_msg_1@PAGE
///   AddPageOff x0, x0, .Ltrap_msg_1@PAGEOFF
///   Bl _rt_trap
/// ```
///
/// **Why Separate Pass?**
/// Terminators are lowered after all other instructions because:
/// 1. All values used by the terminator must be computed first
/// 2. Phi-edge copies reference block-local vreg mappings
/// 3. Fall-through and branch ordering matters for code layout
///
/// @see LowerILToMIR.cpp For overall lowering orchestration
/// @see InstrLowering.cpp For non-terminator instruction lowering
/// @see FrameBuilder.cpp For phi spill slot allocation
///
//===----------------------------------------------------------------------===//

#include "TerminatorLowering.hpp"
#include "InstrLowering.hpp"
#include "OpcodeMappings.hpp"

#include <algorithm>
#include <stdexcept>

namespace viper::codegen::aarch64 {

using il::core::Opcode;

static const char *condForOpcode(Opcode op) {
    return lookupAnyCondition(op);
}

static void emitNullTrapCall(MBasicBlock &bb) {
    bb.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(0)}});
    bb.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
}

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
        if (dstCls == RegClass::FPR) {
            if (scls != RegClass::FPR) {
                const uint16_t cvt = allocateNextVReg(nextVRegId);
                edgeBB.instrs.push_back(MInstr{MOpcode::SCvtF,
                                               {MOperand::vregOp(RegClass::FPR, cvt),
                                                MOperand::vregOp(RegClass::GPR, sv)}});
                sv = cvt;
            }
            edgeBB.instrs.push_back(MInstr{MOpcode::PhiStoreFPR,
                                           {MOperand::vregOp(RegClass::FPR, sv),
                                            MOperand::immOp(offset)}});
        } else {
            if (scls == RegClass::FPR) {
                const uint16_t cvt = allocateNextVReg(nextVRegId);
                edgeBB.instrs.push_back(MInstr{MOpcode::FCvtZS,
                                               {MOperand::vregOp(RegClass::GPR, cvt),
                                                MOperand::vregOp(RegClass::FPR, sv)}});
                sv = cvt;
            }
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
                            callee == "rt_trap_div0" || callee == "rt_trap_ovf") {
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
                        outBB.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
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
                                            outBB.instrs.push_back(
                                                MInstr{MOpcode::BCond,
                                                       {MOperand::condOp(cc),
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
                    for (std::size_t ci = 0; ci < ncases; ++ci) {
                        const auto &caseValue = il::core::switchCaseValue(term, ci);
                        const std::string &caseLabel = il::core::switchCaseLabel(term, ci);
                        long long imm = 0;
                        if (caseValue.kind == il::core::Value::Kind::ConstInt)
                            imm = caseValue.i64;
                        if (isUImm12(imm)) {
                            outBB.instrs.push_back(MInstr{
                                MOpcode::CmpRI,
                                {MOperand::vregOp(RegClass::GPR, sv), MOperand::immOp(imm)}});
                        } else {
                            const uint16_t caseV = allocateNextVReg(nextVRegId);
                            outBB.instrs.push_back(MInstr{
                                MOpcode::MovRI,
                                {MOperand::vregOp(RegClass::GPR, caseV), MOperand::immOp(imm)}});
                            outBB.instrs.push_back(MInstr{
                                MOpcode::CmpRR,
                                {MOperand::vregOp(RegClass::GPR, sv),
                                 MOperand::vregOp(RegClass::GPR, caseV)}});
                        }

                        const auto &args = il::core::switchCaseArgs(term, ci);
                        const bool needsEdge = !args.empty();
                        const std::string edgeLabel =
                            needsEdge
                                ? outBB.name + ".Lswitch_case_" + std::to_string(i) + "_" +
                                      std::to_string(ci)
                                : caseLabel;
                        outBB.instrs.push_back(MInstr{
                            MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp(edgeLabel)}});

                        if (needsEdge) {
                            MBasicBlock edgeBB;
                            edgeBB.name = edgeLabel;
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
                    }

                    const std::string &defLbl = il::core::switchDefaultLabel(term);
                    if (!defLbl.empty()) {
                        const auto &defArgs = il::core::switchDefaultArgs(term);
                        const bool needsEdge = !defArgs.empty();
                        const std::string edgeLabel =
                            needsEdge ? outBB.name + ".Lswitch_default_" + std::to_string(i) : defLbl;
                        outBB.instrs.push_back(
                            MInstr{MOpcode::Br, {MOperand::labelOp(edgeLabel)}});
                        if (needsEdge) {
                            MBasicBlock edgeBB;
                            edgeBB.name = edgeLabel;
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
