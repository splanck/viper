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

namespace viper::codegen::aarch64
{

using il::core::Opcode;

static const char *condForOpcode(Opcode op)
{
    return lookupCondition(op);
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
                      uint16_t &nextVRegId)
{
    const auto &argOrder = ti.intArgOrder;

    for (std::size_t i = 0; i < fn.blocks.size(); ++i)
    {
        const auto &inBB = fn.blocks[i];
        if (inBB.instructions.empty())
            continue;
        const auto &term = inBB.instructions.back();
        auto &outBB = mf.blocks[i];
        // Use the block's tempVReg snapshot to get correct vreg mappings for temps
        // defined in this block. This avoids using overwritten values from later blocks.
        auto &blockTempVReg = blockTempVRegSnapshot[i];

        switch (term.op)
        {
            case Opcode::Br:
                if (!term.labels.empty())
                {
                    // Emit phi edge copies for target - store to spill slots
                    if (!term.brArgs.empty() && !term.brArgs[0].empty())
                    {
                        const std::string &dst = term.labels[0];
                        auto itIds = phiVregId.find(dst);
                        auto itSpill = phiSpillOffset.find(dst);
                        if (itIds != phiVregId.end() && itSpill != phiSpillOffset.end())
                        {
                            const auto &ids = itIds->second;
                            const auto &classes = phiRegClass.at(dst);
                            const auto &spillOffsets = itSpill->second;
                            // Use the block's tempVReg snapshot to find correct vreg
                            // mappings for temps defined in this block. This is critical
                            // for phi-edge copies to emit correct values (e.g., loop
                            // counter increments).
                            for (std::size_t ai = 0; ai < term.brArgs[0].size() && ai < ids.size();
                                 ++ai)
                            {
                                uint16_t sv = 0;
                                RegClass scls = RegClass::GPR;
                                if (!materializeValueToVReg(term.brArgs[0][ai],
                                                            inBB,
                                                            ti,
                                                            fb,
                                                            outBB,
                                                            blockTempVReg,
                                                            tempRegClass,
                                                            nextVRegId,
                                                            sv,
                                                            scls))
                                    continue;
                                const RegClass dstCls = classes[ai];
                                const int offset = spillOffsets[ai];
                                if (dstCls == RegClass::FPR)
                                {
                                    if (scls != RegClass::FPR)
                                    {
                                        const uint16_t cvt = nextVRegId++;
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::SCvtF,
                                                   {MOperand::vregOp(RegClass::FPR, cvt),
                                                    MOperand::vregOp(RegClass::GPR, sv)}});
                                        sv = cvt;
                                        scls = RegClass::FPR;
                                    }
                                    // Store FPR to spill slot
                                    outBB.instrs.push_back(
                                        MInstr{MOpcode::StrFprFpImm,
                                               {MOperand::vregOp(RegClass::FPR, sv),
                                                MOperand::immOp(offset)}});
                                }
                                else
                                {
                                    if (scls == RegClass::FPR)
                                    {
                                        const uint16_t cvt = nextVRegId++;
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::FCvtZS,
                                                   {MOperand::vregOp(RegClass::GPR, cvt),
                                                    MOperand::vregOp(RegClass::FPR, sv)}});
                                        sv = cvt;
                                        scls = RegClass::GPR;
                                    }
                                    // Store GPR to spill slot
                                    outBB.instrs.push_back(
                                        MInstr{MOpcode::StrRegFpImm,
                                               {MOperand::vregOp(RegClass::GPR, sv),
                                                MOperand::immOp(offset)}});
                                }
                            }
                        }
                    }
                    outBB.instrs.push_back(
                        MInstr{MOpcode::Br, {MOperand::labelOp(term.labels[0])}});
                }
                break;

            case Opcode::Trap:
            {
                // Phase A: lower trap to a helper call for diagnostics.
                // Skip emitting rt_trap if the block already has a call to a noreturn function
                // like rt_arr_oob_panic (which will abort and never return).
                bool hasNoreturnCall = false;
                for (const auto &mi : outBB.instrs)
                {
                    if (mi.opc == MOpcode::Bl && !mi.ops.empty() &&
                        mi.ops[0].kind == MOperand::Kind::Label)
                    {
                        const std::string &callee = mi.ops[0].label;
                        if (callee == "rt_arr_oob_panic" || callee == "rt_trap")
                        {
                            hasNoreturnCall = true;
                            break;
                        }
                    }
                }
                if (!hasNoreturnCall)
                {
                    outBB.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
                }
                break;
            }

            case Opcode::TrapFromErr:
            {
                // Phase A: move optional error code into x0 (when available), then call rt_trap.
                if (!term.operands.empty())
                {
                    const auto &code = term.operands[0];
                    if (code.kind == il::core::Value::Kind::ConstInt)
                    {
                        outBB.instrs.push_back(
                            MInstr{MOpcode::MovRI,
                                   {MOperand::regOp(PhysReg::X0), MOperand::immOp(code.i64)}});
                    }
                    else if (code.kind == il::core::Value::Kind::Temp)
                    {
                        int pIdx = indexOfParam(inBB, code.id);
                        if (pIdx >= 0 && static_cast<std::size_t>(pIdx) < kMaxGPRArgs)
                        {
                            const PhysReg src = argOrder[static_cast<std::size_t>(pIdx)];
                            if (src != PhysReg::X0)
                                outBB.instrs.push_back(
                                    MInstr{MOpcode::MovRR,
                                           {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                        }
                    }
                }
                outBB.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
                break;
            }

            case Opcode::CBr:
                if (term.operands.size() >= 1 && term.labels.size() == 2)
                {
                    // Emit phi copies for both edges unconditionally
                    const std::string &trueLbl = term.labels[0];
                    const std::string &falseLbl = term.labels[1];

                    auto emitEdgeCopies =
                        [&](const std::string &dst, const std::vector<il::core::Value> &args)
                    {
                        auto itIds = phiVregId.find(dst);
                        if (itIds == phiVregId.end())
                            return;
                        auto itSpill = phiSpillOffset.find(dst);
                        if (itSpill == phiSpillOffset.end())
                            return;
                        const auto &ids = itIds->second;
                        const auto &classes = phiRegClass.at(dst);
                        const auto &spillOffsets = itSpill->second;
                        // Store phi values to spill slots since register allocator
                        // releases vreg mappings at block boundaries
                        for (std::size_t ai = 0; ai < args.size() && ai < ids.size(); ++ai)
                        {
                            uint16_t sv = 0;
                            RegClass scls = RegClass::GPR;
                            if (!materializeValueToVReg(args[ai],
                                                        inBB,
                                                        ti,
                                                        fb,
                                                        outBB,
                                                        blockTempVReg,
                                                        tempRegClass,
                                                        nextVRegId,
                                                        sv,
                                                        scls))
                                continue;
                            const RegClass dstCls = classes[ai];
                            const int offset = spillOffsets[ai];
                            if (dstCls == RegClass::FPR)
                            {
                                if (scls != RegClass::FPR)
                                {
                                    const uint16_t cvt = nextVRegId++;
                                    outBB.instrs.push_back(
                                        MInstr{MOpcode::SCvtF,
                                               {MOperand::vregOp(RegClass::FPR, cvt),
                                                MOperand::vregOp(RegClass::GPR, sv)}});
                                    sv = cvt;
                                    scls = RegClass::FPR;
                                }
                                // Store FPR to spill slot
                                outBB.instrs.push_back(MInstr{MOpcode::StrFprFpImm,
                                                              {MOperand::vregOp(RegClass::FPR, sv),
                                                               MOperand::immOp(offset)}});
                            }
                            else
                            {
                                if (scls == RegClass::FPR)
                                {
                                    const uint16_t cvt = nextVRegId++;
                                    outBB.instrs.push_back(
                                        MInstr{MOpcode::FCvtZS,
                                               {MOperand::vregOp(RegClass::GPR, cvt),
                                                MOperand::vregOp(RegClass::FPR, sv)}});
                                    sv = cvt;
                                    scls = RegClass::GPR;
                                }
                                // Store GPR to spill slot
                                outBB.instrs.push_back(MInstr{MOpcode::StrRegFpImm,
                                                              {MOperand::vregOp(RegClass::GPR, sv),
                                                               MOperand::immOp(offset)}});
                            }
                        }
                    };

                    if (term.brArgs.size() > 0)
                        emitEdgeCopies(trueLbl, term.brArgs[0]);
                    if (term.brArgs.size() > 1)
                        emitEdgeCopies(falseLbl, term.brArgs[1]);

                    // Try to lower compares to cmp + b.<cond>
                    // NOTE: This optimization only works for the entry block (i == 0) where
                    // block parameters are passed in argument registers. For loop headers
                    // with phi nodes, parameters are loaded from spill slots, not registers.
                    const auto &cond = term.operands[0];
                    bool loweredViaCompare = false;
                    const bool isEntryBlock = (i == 0);
                    if (isEntryBlock && cond.kind == il::core::Value::Kind::Temp)
                    {
                        const auto it = std::find_if(inBB.instructions.begin(),
                                                     inBB.instructions.end(),
                                                     [&](const il::core::Instr &I)
                                                     { return I.result && *I.result == cond.id; });
                        if (it != inBB.instructions.end())
                        {
                            const il::core::Instr &cmpI = *it;
                            const char *cc = condForOpcode(cmpI.op);
                            if (cc && cmpI.operands.size() == 2)
                            {
                                const auto &o0 = cmpI.operands[0];
                                const auto &o1 = cmpI.operands[1];
                                if (o0.kind == il::core::Value::Kind::Temp &&
                                    o1.kind == il::core::Value::Kind::Temp)
                                {
                                    int idx0 = indexOfParam(inBB, o0.id);
                                    int idx1 = indexOfParam(inBB, o1.id);
                                    if (idx0 >= 0 && idx1 >= 0 &&
                                        static_cast<std::size_t>(idx0) < kMaxGPRArgs &&
                                        static_cast<std::size_t>(idx1) < kMaxGPRArgs)
                                    {
                                        const PhysReg src0 = argOrder[static_cast<size_t>(idx0)];
                                        const PhysReg src1 = argOrder[static_cast<size_t>(idx1)];
                                        // cmp x0, x1
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::CmpRR,
                                                   {MOperand::regOp(src0), MOperand::regOp(src1)}});
                                        outBB.instrs.push_back(MInstr{
                                            MOpcode::BCond,
                                            {MOperand::condOp(cc), MOperand::labelOp(trueLbl)}});
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::Br, {MOperand::labelOp(falseLbl)}});
                                        loweredViaCompare = true;
                                    }
                                }
                                else if (o0.kind == il::core::Value::Kind::Temp &&
                                         o1.kind == il::core::Value::Kind::ConstInt)
                                {
                                    int idx0 = indexOfParam(inBB, o0.id);
                                    if (idx0 >= 0 && static_cast<std::size_t>(idx0) < kMaxGPRArgs)
                                    {
                                        const PhysReg src0 = argOrder[static_cast<size_t>(idx0)];
                                        if (src0 != PhysReg::X0)
                                        {
                                            outBB.instrs.push_back(
                                                MInstr{MOpcode::MovRR,
                                                       {MOperand::regOp(PhysReg::X0),
                                                        MOperand::regOp(src0)}});
                                        }
                                        outBB.instrs.push_back(MInstr{MOpcode::CmpRI,
                                                                      {MOperand::regOp(PhysReg::X0),
                                                                       MOperand::immOp(o1.i64)}});
                                        outBB.instrs.push_back(MInstr{
                                            MOpcode::BCond,
                                            {MOperand::condOp(cc), MOperand::labelOp(trueLbl)}});
                                        outBB.instrs.push_back(
                                            MInstr{MOpcode::Br, {MOperand::labelOp(falseLbl)}});
                                        loweredViaCompare = true;
                                    }
                                }
                            }
                        }
                    }
                    if (!loweredViaCompare)
                    {
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
                        outBB.instrs.push_back(MInstr{
                            MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trueLbl)}});
                        outBB.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp(falseLbl)}});
                    }
                }
                break;

            default:
                break;
        }
    }
}

} // namespace viper::codegen::aarch64
