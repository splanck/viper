//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/FastPaths.cpp
// Purpose: Fast-path pattern matching for common IL patterns.
//
// This file contains optimized lowering paths for simple function patterns
// that can be matched and lowered directly without the full generic lowering.
//
//===----------------------------------------------------------------------===//

#include "FastPaths.hpp"
#include "InstrLowering.hpp"
#include "LoweringContext.hpp"
#include "OpcodeMappings.hpp"
#include "il/core/Instr.hpp"

#include <algorithm>

namespace viper::codegen::aarch64
{

using il::core::Opcode;

// Counter for generating unique trap labels
static thread_local unsigned trapLabelCounter = 0;

std::optional<MFunction> tryFastPaths(const il::core::Function &fn,
                                       const TargetInfo &ti,
                                       FrameBuilder &fb,
                                       MFunction &mf)
{
    const auto &argOrder = ti.intArgOrder;
    
    auto bbOut = [&](std::size_t idx) -> MBasicBlock & { return mf.blocks[idx]; };

    // Helper to get the register holding a value
    auto getValueReg = [&](const il::core::BasicBlock &bb,
                           const il::core::Value &val) -> std::optional<PhysReg>
    {
        if (val.kind == il::core::Value::Kind::Temp)
        {
            // Check if it's a parameter
            int pIdx = indexOfParam(bb, val.id);
            if (pIdx >= 0 && static_cast<std::size_t>(pIdx) < kMaxGPRArgs)
            {
                return argOrder[static_cast<size_t>(pIdx)];
            }
        }
        return std::nullopt;
    };

    if (!fn.blocks.empty())
    {
        const auto &bb = fn.blocks.front();
        auto &bbMir = bbOut(0);

        // Simple alloca/store/load/ret pattern: %local = alloca i64; store %param0, %local;
        // %val = load %local; ret %val
        if (!mf.frame.locals.empty() && fn.blocks.size() == 1 && bb.instructions.size() >= 4)
        {
            // Look for: alloca, store, load, ret
            const auto *allocaI = &bb.instructions[bb.instructions.size() - 4];
            const auto *storeI = &bb.instructions[bb.instructions.size() - 3];
            const auto *loadI = &bb.instructions[bb.instructions.size() - 2];
            const auto *retI = &bb.instructions[bb.instructions.size() - 1];

            if (allocaI->op == il::core::Opcode::Alloca && allocaI->result &&
                storeI->op == il::core::Opcode::Store && storeI->operands.size() == 2 &&
                loadI->op == il::core::Opcode::Load && loadI->result &&
                loadI->operands.size() == 1 && retI->op == il::core::Opcode::Ret &&
                !retI->operands.empty())
            {
                const unsigned allocaId = *allocaI->result;
                const auto &storePtr = storeI->operands[0]; // pointer is operand 0
                const auto &storeVal = storeI->operands[1]; // value is operand 1
                const auto &loadPtr = loadI->operands[0];
                const auto &retVal = retI->operands[0];

                // Check that store and load both target the same alloca
                if (storePtr.kind == il::core::Value::Kind::Temp && storePtr.id == allocaId &&
                    loadPtr.kind == il::core::Value::Kind::Temp && loadPtr.id == allocaId &&
                    retVal.kind == il::core::Value::Kind::Temp && retVal.id == *loadI->result)
                {
                    // Get offset for this alloca
                    // Query assigned offset from frame builder
                    const int offset = fb.localOffset(allocaId);
                    if (offset != 0)
                    {
                        // Get register holding the value to store
                        auto srcReg = getValueReg(bb, storeVal);
                        if (srcReg)
                        {
                            // str srcReg, [x29, #offset]
                            bbMir.instrs.push_back(
                                MInstr{MOpcode::StrRegFpImm,
                                       {MOperand::regOp(*srcReg), MOperand::immOp(offset)}});
                            // ldr x0, [x29, #offset]
                            bbMir.instrs.push_back(
                                MInstr{MOpcode::LdrRegFpImm,
                                       {MOperand::regOp(PhysReg::X0), MOperand::immOp(offset)}});
                            // ret
                            bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                            fb.finalize();
                            return mf;
                        }
                    }
                }
            }
        }

        // ret %paramN fast-path (only when no side effects to lower)
        if (fn.blocks.size() == 1 && !bb.instructions.empty() && !bb.params.empty() &&
            !hasSideEffects(bb))
        {
            const auto &retI = bb.instructions.back();
            if (retI.op == il::core::Opcode::Ret && !retI.operands.empty())
            {
                const auto &rv = retI.operands[0];
                if (rv.kind == il::core::Value::Kind::Temp)
                {
                    int pIdx = indexOfParam(bb, rv.id);
                    if (pIdx >= 0)
                    {
                        if (fn.retType.kind == il::core::Type::Kind::F64)
                        {
                            const PhysReg src = ti.f64ArgOrder[static_cast<size_t>(pIdx)];
                            if (src != PhysReg::V0)
                                bbMir.instrs.push_back(
                                    MInstr{MOpcode::FMovRR,
                                           {MOperand::regOp(PhysReg::V0), MOperand::regOp(src)}});
                        }
                        else
                        {
                            const PhysReg src = argOrder[static_cast<size_t>(pIdx)];
                            if (src != PhysReg::X0)
                                bbMir.instrs.push_back(
                                    MInstr{MOpcode::MovRR,
                                           {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                        }
                        bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                        fb.finalize();
                        return mf;
                    }
                }
            }
        }

        // ret (const_str/addr_of) fast-path: materialize symbol address directly to x0
        if (fn.blocks.size() == 1 && bb.instructions.size() >= 2)
        {
            const auto &retI = bb.instructions.back();
            if (retI.op == il::core::Opcode::Ret && !retI.operands.empty())
            {
                const auto &rv = retI.operands[0];
                if (rv.kind == il::core::Value::Kind::Temp)
                {
                    const unsigned rid = rv.id;
                    auto prodIt = std::find_if(bb.instructions.begin(),
                                               bb.instructions.end(),
                                               [&](const il::core::Instr &I)
                                               { return I.result && *I.result == rid; });
                    if (prodIt != bb.instructions.end())
                    {
                        const auto &prod = *prodIt;
                        if ((prod.op == il::core::Opcode::ConstStr ||
                             prod.op == il::core::Opcode::AddrOf) &&
                            !prod.operands.empty() &&
                            prod.operands[0].kind == il::core::Value::Kind::GlobalAddr)
                        {
                            const std::string &sym = prod.operands[0].str;
                            bbMir.instrs.push_back(
                                MInstr{MOpcode::AdrPage,
                                       {MOperand::regOp(PhysReg::X0), MOperand::labelOp(sym)}});
                            bbMir.instrs.push_back(MInstr{MOpcode::AddPageOff,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::labelOp(sym)}});
                            bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                            fb.finalize();
                            return mf;
                        }
                    }
                }
            }
        }

        // rr ops on entry params feeding ret
        if (fn.blocks.size() == 1 && bb.instructions.size() >= 2 && bb.params.size() >= 2)
        {
            const auto &opI = bb.instructions[bb.instructions.size() - 2];
            const auto &retI = bb.instructions.back();
            if ((opI.op == il::core::Opcode::Add || opI.op == il::core::Opcode::IAddOvf ||
                 opI.op == il::core::Opcode::Sub || opI.op == il::core::Opcode::ISubOvf ||
                 opI.op == il::core::Opcode::Mul || opI.op == il::core::Opcode::IMulOvf ||
                 opI.op == il::core::Opcode::And || opI.op == il::core::Opcode::Or ||
                 opI.op == il::core::Opcode::Xor || opI.op == il::core::Opcode::ICmpEq ||
                 opI.op == il::core::Opcode::ICmpNe || opI.op == il::core::Opcode::SCmpLT ||
                 opI.op == il::core::Opcode::SCmpLE || opI.op == il::core::Opcode::SCmpGT ||
                 opI.op == il::core::Opcode::SCmpGE || opI.op == il::core::Opcode::UCmpLT ||
                 opI.op == il::core::Opcode::UCmpLE || opI.op == il::core::Opcode::UCmpGT ||
                 opI.op == il::core::Opcode::UCmpGE) &&
                retI.op == il::core::Opcode::Ret && opI.result && !retI.operands.empty())
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *opI.result &&
                    opI.operands.size() == 2 &&
                    opI.operands[0].kind == il::core::Value::Kind::Temp &&
                    opI.operands[1].kind == il::core::Value::Kind::Temp)
                {
                    const int idx0 = indexOfParam(bb, opI.operands[0].id);
                    const int idx1 = indexOfParam(bb, opI.operands[1].id);
                    if (idx0 >= 0 && idx1 >= 0 && static_cast<std::size_t>(idx0) < kMaxGPRArgs &&
                        static_cast<std::size_t>(idx1) < kMaxGPRArgs)
                    {
                        const PhysReg src0 = argOrder[static_cast<size_t>(idx0)];
                        const PhysReg src1 = argOrder[static_cast<size_t>(idx1)];
                        // Normalize to x0,x1 using scratch register
                        bbMir.instrs.push_back(MInstr{
                            MOpcode::MovRR, {MOperand::regOp(kScratchGPR), MOperand::regOp(src1)}});
                        bbMir.instrs.push_back(MInstr{
                            MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src0)}});
                        bbMir.instrs.push_back(
                            MInstr{MOpcode::MovRR,
                                   {MOperand::regOp(PhysReg::X1), MOperand::regOp(kScratchGPR)}});
                        switch (opI.op)
                        {
                            case Opcode::Add:
                            case Opcode::IAddOvf:
                                bbMir.instrs.push_back(MInstr{MOpcode::AddRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Sub:
                            case Opcode::ISubOvf:
                                bbMir.instrs.push_back(MInstr{MOpcode::SubRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Mul:
                            case Opcode::IMulOvf:
                                bbMir.instrs.push_back(MInstr{MOpcode::MulRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::And:
                                bbMir.instrs.push_back(MInstr{MOpcode::AndRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Or:
                                bbMir.instrs.push_back(MInstr{MOpcode::OrrRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::Xor:
                                bbMir.instrs.push_back(MInstr{MOpcode::EorRRR,
                                                              {MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X0),
                                                               MOperand::regOp(PhysReg::X1)}});
                                break;
                            case Opcode::ICmpEq:
                            case Opcode::ICmpNe:
                            case Opcode::SCmpLT:
                            case Opcode::SCmpLE:
                            case Opcode::SCmpGT:
                            case Opcode::SCmpGE:
                            case Opcode::UCmpLT:
                            case Opcode::UCmpLE:
                            case Opcode::UCmpGT:
                            case Opcode::UCmpGE:
                                bbMir.instrs.push_back(MInstr{
                                    MOpcode::CmpRR,
                                    {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X1)}});
                                bbMir.instrs.push_back(
                                    MInstr{MOpcode::Cset,
                                           {MOperand::regOp(PhysReg::X0),
                                            MOperand::condOp(lookupCondition(opI.op))}});
                                break;
                            default:
                                break;
                        }
                        bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                        return mf;
                    }
                }
            }
        }

        // Floating rr ops on entry params feeding ret (fadd/fsub/fmul/fdiv)
        if (fn.blocks.size() == 1 && bb.instructions.size() >= 2 && bb.params.size() >= 2)
        {
            const auto &opI = bb.instructions[bb.instructions.size() - 2];
            const auto &retI = bb.instructions.back();
            const bool isFAdd = (opI.op == il::core::Opcode::FAdd);
            const bool isFSub = (opI.op == il::core::Opcode::FSub);
            const bool isFMul = (opI.op == il::core::Opcode::FMul);
            const bool isFDiv = (opI.op == il::core::Opcode::FDiv);
            if ((isFAdd || isFSub || isFMul || isFDiv) && retI.op == il::core::Opcode::Ret &&
                opI.result && !retI.operands.empty())
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *opI.result &&
                    opI.operands.size() == 2 &&
                    opI.operands[0].kind == il::core::Value::Kind::Temp &&
                    opI.operands[1].kind == il::core::Value::Kind::Temp)
                {
                    const int idx0 = indexOfParam(bb, opI.operands[0].id);
                    const int idx1 = indexOfParam(bb, opI.operands[1].id);
                    if (idx0 >= 0 && idx1 >= 0 && static_cast<std::size_t>(idx0) < kMaxFPRArgs &&
                        static_cast<std::size_t>(idx1) < kMaxFPRArgs)
                    {
                        const PhysReg src0 = ti.f64ArgOrder[static_cast<std::size_t>(idx0)];
                        const PhysReg src1 = ti.f64ArgOrder[static_cast<std::size_t>(idx1)];
                        // Normalize to d0,d1 using FPR scratch register
                        bbMir.instrs.push_back(
                            MInstr{MOpcode::FMovRR,
                                   {MOperand::regOp(kScratchFPR), MOperand::regOp(src1)}});
                        bbMir.instrs.push_back(
                            MInstr{MOpcode::FMovRR,
                                   {MOperand::regOp(PhysReg::V0), MOperand::regOp(src0)}});
                        bbMir.instrs.push_back(
                            MInstr{MOpcode::FMovRR,
                                   {MOperand::regOp(PhysReg::V1), MOperand::regOp(kScratchFPR)}});
                        if (isFAdd)
                            bbMir.instrs.push_back(MInstr{MOpcode::FAddRRR,
                                                          {MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V1)}});
                        else if (isFSub)
                            bbMir.instrs.push_back(MInstr{MOpcode::FSubRRR,
                                                          {MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V1)}});
                        else if (isFMul)
                            bbMir.instrs.push_back(MInstr{MOpcode::FMulRRR,
                                                          {MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V1)}});
                        else if (isFDiv)
                            bbMir.instrs.push_back(MInstr{MOpcode::FDivRRR,
                                                          {MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V0),
                                                           MOperand::regOp(PhysReg::V1)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                        fb.finalize();
                        return mf;
                    }
                }
            }
        }

        // ri/shift-imm and immediate compares
        if (fn.blocks.size() == 1 && bb.instructions.size() >= 2 && !bb.params.empty())
        {
            const auto &binI = bb.instructions[bb.instructions.size() - 2];
            const auto &retI = bb.instructions.back();
            // Unary integer/FP casts feeding return
            if (retI.op == il::core::Opcode::Ret && binI.result && !retI.operands.empty() &&
                retI.operands[0].kind == il::core::Value::Kind::Temp &&
                retI.operands[0].id == *binI.result)
            {
                // zext1: zero-extend i1 -> i64, or trunc1: i64->i1
                // Only handle when operand is a direct param (otherwise fall through to generic)
                if (binI.op == il::core::Opcode::Zext1 || binI.op == il::core::Opcode::Trunc1)
                {
                    const auto &o0 = binI.operands[0];
                    // Only apply fast-path if operand is a param
                    if (o0.kind == il::core::Value::Kind::Temp)
                    {
                        int pIdx = indexOfParam(bb, o0.id);
                        if (pIdx >= 0)
                        {
                            PhysReg src = argOrder[static_cast<std::size_t>(pIdx)];
                            if (src != PhysReg::X0)
                                bbMir.instrs.push_back(
                                    MInstr{MOpcode::MovRR,
                                           {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                            // and x0, x0, #1 via scratch register
                            bbMir.instrs.push_back(
                                MInstr{MOpcode::MovRI,
                                       {MOperand::regOp(kScratchGPR), MOperand::immOp(1)}});
                            bbMir.instrs.push_back(MInstr{MOpcode::AndRRR,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(kScratchGPR)}});
                            bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                            fb.finalize();
                            return mf;
                        }
                    }
                    // Fall through to generic lowering if operand is not a param
                }
                // cast.si_narrow.chk: signed narrowing to target width with range check
                if (binI.op == il::core::Opcode::CastSiNarrowChk)
                {
                    // Determine target width from binI.type
                    int bits = 64;
                    if (binI.type.kind == il::core::Type::Kind::I16)
                        bits = 16;
                    else if (binI.type.kind == il::core::Type::Kind::I32)
                        bits = 32;
                    else if (binI.type.kind == il::core::Type::Kind::I64)
                        bits = 64;
                    const int sh = 64 - bits;
                    const auto &o0 = binI.operands[0];
                    PhysReg src = PhysReg::X0;
                    if (o0.kind == il::core::Value::Kind::Temp)
                    {
                        int pIdx = indexOfParam(bb, o0.id);
                        if (pIdx >= 0)
                            src = argOrder[static_cast<std::size_t>(pIdx)];
                    }
                    if (src != PhysReg::X0)
                        bbMir.instrs.push_back(MInstr{
                            MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                    // tmp = (x0 << sh) >> sh  (sign-extended truncation)
                    if (sh > 0)
                    {
                        bbMir.instrs.push_back(MInstr{MOpcode::LslRI,
                                                      {MOperand::regOp(PhysReg::X0),
                                                       MOperand::regOp(PhysReg::X0),
                                                       MOperand::immOp(sh)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::AsrRI,
                                                      {MOperand::regOp(PhysReg::X0),
                                                       MOperand::regOp(PhysReg::X0),
                                                       MOperand::immOp(sh)}});
                    }
                    // Compare restored value to source in scratch register
                    bbMir.instrs.push_back(MInstr{
                        MOpcode::MovRR, {MOperand::regOp(kScratchGPR), MOperand::regOp(src)}});
                    bbMir.instrs.push_back(
                        MInstr{MOpcode::CmpRR,
                               {MOperand::regOp(PhysReg::X0), MOperand::regOp(kScratchGPR)}});
                    // If not equal, branch to a trap block
                    // Use a unique local label with counter suffix
                    const std::string trapLabel =
                        ".Ltrap_cast_" + std::to_string(trapLabelCounter++);
                    bbMir.instrs.push_back(MInstr{
                        MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trapLabel)}});
                    // Fall-through: range is OK, return
                    bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                    // Append trap block to function with a call to rt_trap
                    mf.blocks.emplace_back();
                    mf.blocks.back().name = trapLabel;
                    mf.blocks.back().instrs.push_back(
                        MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
                    fb.finalize();
                    return mf;
                }
                // cast.fp_to_si.rte.chk: convert to int with fcvtzs and compare round-trip
                if (binI.op == il::core::Opcode::CastFpToSiRteChk)
                {
                    // Load operand to d0
                    const auto &o0 = binI.operands[0];
                    if (o0.kind == il::core::Value::Kind::Temp)
                    {
                        int pIdx = indexOfParam(bb, o0.id);
                        if (pIdx >= 0)
                        {
                            const PhysReg s = ti.f64ArgOrder[static_cast<std::size_t>(pIdx)];
                            if (s != PhysReg::V0)
                                bbMir.instrs.push_back(
                                    MInstr{MOpcode::FMovRR,
                                           {MOperand::regOp(PhysReg::V0), MOperand::regOp(s)}});
                        }
                    }
                    // x0 = fcvtzs d0
                    bbMir.instrs.push_back(
                        MInstr{MOpcode::FCvtZS,
                               {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::V0)}});
                    // d1 = scvtf x0; fcmp d0, d1; b.ne trap
                    bbMir.instrs.push_back(
                        MInstr{MOpcode::SCvtF,
                               {MOperand::regOp(PhysReg::V1), MOperand::regOp(PhysReg::X0)}});
                    bbMir.instrs.push_back(
                        MInstr{MOpcode::FCmpRR,
                               {MOperand::regOp(PhysReg::V0), MOperand::regOp(PhysReg::V1)}});
                    const std::string trapLabel2 =
                        ".Ltrap_fpcast_" + std::to_string(trapLabelCounter++);
                    bbMir.instrs.push_back(MInstr{
                        MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trapLabel2)}});
                    // Fall-through: value is exact, return
                    bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                    mf.blocks.emplace_back();
                    mf.blocks.back().name = trapLabel2;
                    mf.blocks.back().instrs.push_back(
                        MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
                    fb.finalize();
                    return mf;
                }
            }
            // call @callee(args...) feeding ret (only when exactly 2 instructions)
            // Permit extra producer instructions before the call; only the final
            // two instructions need to be call + ret for this fast path.
            if (bb.instructions.size() >= 2 && binI.op == il::core::Opcode::Call &&
                retI.op == il::core::Opcode::Ret && binI.result && !retI.operands.empty())
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result &&
                    !binI.callee.empty())
                {
                    // Use generalized vreg-based lowering when we exceed register args
                    // or when any argument is floating-point.
                    bool hasFloatArg = false;
                    for (const auto &arg : binI.operands)
                    {
                        if (arg.kind == il::core::Value::Kind::ConstFloat)
                        {
                            hasFloatArg = true;
                            break;
                        }
                        if (arg.kind == il::core::Value::Kind::Temp)
                        {
                            int p = indexOfParam(bb, arg.id);
                            if (p >= 0 && p < static_cast<int>(bb.params.size()) &&
                                bb.params[static_cast<std::size_t>(p)].type.kind ==
                                    il::core::Type::Kind::F64)
                            {
                                hasFloatArg = true;
                                break;
                            }
                        }
                    }
                    if (binI.operands.size() > ti.intArgOrder.size() || hasFloatArg)
                    {
                        LoweredCall seq{};
                        std::unordered_map<unsigned, uint16_t> tempVReg;
                        uint16_t nextVRegId = 1;
                        if (lowerCallWithArgs(binI, bb, ti, fb, bbMir, seq, tempVReg, nextVRegId))
                        {
                            for (auto &mi : seq.prefix)
                                bbMir.instrs.push_back(std::move(mi));
                            bbMir.instrs.push_back(std::move(seq.call));
                            for (auto &mi : seq.postfix)
                                bbMir.instrs.push_back(std::move(mi));
                            bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                            fb.finalize();
                            return mf;
                        }
                    }
                    // Single-block, marshal only entry params and const i64 to integer arg regs.
                    const std::size_t nargs = binI.operands.size();
                    if (nargs <= ti.intArgOrder.size())
                    {
                        // Build move plan for reg->reg moves; immediates applied after.
                        struct Move
                        {
                            PhysReg dst;
                            PhysReg src;
                        };

                        std::vector<Move> moves;
                        std::vector<std::pair<PhysReg, long long>> immLoads;
                        std::vector<std::pair<std::size_t, PhysReg>>
                            tempRegs; // (arg index, reg holding computed temp)
                        // Scratch registers for temporary computations during call lowering.
                        // We track which registers are available and bail out if exhausted.
                        constexpr std::size_t kScratchPoolSize = 2;
                        const PhysReg scratchPool[kScratchPoolSize] = {kScratchGPR, PhysReg::X10};
                        std::size_t scratchUsed = 0;
                        auto isParamTemp = [&](const il::core::Value &v, unsigned &outIdx) -> bool
                        {
                            if (v.kind != il::core::Value::Kind::Temp)
                                return false;
                            int p = indexOfParam(bb, v.id);
                            if (p >= 0)
                            {
                                outIdx = static_cast<unsigned>(p);
                                return true;
                            }
                            return false;
                        };
                        auto computeTempTo = [&](const il::core::Instr &prod,
                                                 PhysReg dstReg) -> bool
                        {
                            // Helpers
                            auto rr_emit = [&](MOpcode opc, unsigned p0, unsigned p1)
                            {
                                const PhysReg r0 = argOrder[p0];
                                const PhysReg r1 = argOrder[p1];
                                bbMir.instrs.push_back(MInstr{opc,
                                                              {MOperand::regOp(dstReg),
                                                               MOperand::regOp(r0),
                                                               MOperand::regOp(r1)}});
                            };
                            auto ri_emit = [&](MOpcode opc, unsigned p0, long long imm)
                            {
                                const PhysReg r0 = argOrder[p0];
                                bbMir.instrs.push_back(MInstr{opc,
                                                              {MOperand::regOp(dstReg),
                                                               MOperand::regOp(r0),
                                                               MOperand::immOp(imm)}});
                            };
                            // RR patterns: both operands are entry params
                            if (prod.op == il::core::Opcode::Add ||
                                prod.op == il::core::Opcode::IAddOvf ||
                                prod.op == il::core::Opcode::Sub ||
                                prod.op == il::core::Opcode::ISubOvf ||
                                prod.op == il::core::Opcode::Mul ||
                                prod.op == il::core::Opcode::IMulOvf ||
                                prod.op == il::core::Opcode::And ||
                                prod.op == il::core::Opcode::Or || prod.op == il::core::Opcode::Xor)
                            {
                                if (prod.operands.size() != 2)
                                    return false;
                                if (prod.operands[0].kind == il::core::Value::Kind::Temp &&
                                    prod.operands[1].kind == il::core::Value::Kind::Temp)
                                {
                                    int i0 = indexOfParam(bb, prod.operands[0].id);
                                    int i1 = indexOfParam(bb, prod.operands[1].id);
                                    if (i0 >= 0 && i1 >= 0 &&
                                        static_cast<std::size_t>(i0) < kMaxGPRArgs &&
                                        static_cast<std::size_t>(i1) < kMaxGPRArgs)
                                    {
                                        MOpcode opc = MOpcode::AddRRR;
                                        if (prod.op == il::core::Opcode::Add ||
                                            prod.op == il::core::Opcode::IAddOvf)
                                            opc = MOpcode::AddRRR;
                                        else if (prod.op == il::core::Opcode::Sub ||
                                                 prod.op == il::core::Opcode::ISubOvf)
                                            opc = MOpcode::SubRRR;
                                        else if (prod.op == il::core::Opcode::Mul ||
                                                 prod.op == il::core::Opcode::IMulOvf)
                                            opc = MOpcode::MulRRR;
                                        else if (prod.op == il::core::Opcode::And)
                                            opc = MOpcode::AndRRR;
                                        else if (prod.op == il::core::Opcode::Or)
                                            opc = MOpcode::OrrRRR;
                                        else if (prod.op == il::core::Opcode::Xor)
                                            opc = MOpcode::EorRRR;
                                        rr_emit(opc,
                                                static_cast<unsigned>(i0),
                                                static_cast<unsigned>(i1));
                                        return true;
                                    }
                                }
                            }
                            // RI patterns: param + imm for add/sub/shift
                            if (prod.op == il::core::Opcode::Shl ||
                                prod.op == il::core::Opcode::LShr ||
                                prod.op == il::core::Opcode::AShr ||
                                prod.op == il::core::Opcode::Add ||
                                prod.op == il::core::Opcode::IAddOvf ||
                                prod.op == il::core::Opcode::Sub ||
                                prod.op == il::core::Opcode::ISubOvf)
                            {
                                if (prod.operands.size() != 2)
                                    return false;
                                const auto &o0 = prod.operands[0];
                                const auto &o1 = prod.operands[1];
                                if (o0.kind == il::core::Value::Kind::Temp &&
                                    o1.kind == il::core::Value::Kind::ConstInt)
                                {
                                    int ip = indexOfParam(bb, o0.id);
                                    if (ip >= 0 && static_cast<std::size_t>(ip) < kMaxGPRArgs)
                                    {
                                        if (prod.op == il::core::Opcode::Shl)
                                            ri_emit(
                                                MOpcode::LslRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::LShr)
                                            ri_emit(
                                                MOpcode::LsrRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::AShr)
                                            ri_emit(
                                                MOpcode::AsrRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::Add ||
                                                 prod.op == il::core::Opcode::IAddOvf)
                                            ri_emit(
                                                MOpcode::AddRI, static_cast<unsigned>(ip), o1.i64);
                                        else if (prod.op == il::core::Opcode::Sub ||
                                                 prod.op == il::core::Opcode::ISubOvf)
                                            ri_emit(
                                                MOpcode::SubRI, static_cast<unsigned>(ip), o1.i64);
                                        return true;
                                    }
                                }
                                else if (o1.kind == il::core::Value::Kind::Temp &&
                                         o0.kind == il::core::Value::Kind::ConstInt)
                                {
                                    int ip = indexOfParam(bb, o1.id);
                                    if (ip >= 0 && static_cast<std::size_t>(ip) < kMaxGPRArgs)
                                    {
                                        if (prod.op == il::core::Opcode::Shl)
                                            ri_emit(
                                                MOpcode::LslRI, static_cast<unsigned>(ip), o0.i64);
                                        else if (prod.op == il::core::Opcode::LShr)
                                            ri_emit(
                                                MOpcode::LsrRI, static_cast<unsigned>(ip), o0.i64);
                                        else if (prod.op == il::core::Opcode::AShr)
                                            ri_emit(
                                                MOpcode::AsrRI, static_cast<unsigned>(ip), o0.i64);
                                        else if (prod.op == il::core::Opcode::Add ||
                                                 prod.op == il::core::Opcode::IAddOvf)
                                            ri_emit(
                                                MOpcode::AddRI, static_cast<unsigned>(ip), o0.i64);
                                        // Sub with const first not supported
                                        return true;
                                    }
                                }
                            }
                            // Compare patterns: produce 0/1 in dstReg via cmp + cset
                            if (prod.op == il::core::Opcode::ICmpEq ||
                                prod.op == il::core::Opcode::ICmpNe ||
                                prod.op == il::core::Opcode::SCmpLT ||
                                prod.op == il::core::Opcode::SCmpLE ||
                                prod.op == il::core::Opcode::SCmpGT ||
                                prod.op == il::core::Opcode::SCmpGE ||
                                prod.op == il::core::Opcode::UCmpLT ||
                                prod.op == il::core::Opcode::UCmpLE ||
                                prod.op == il::core::Opcode::UCmpGT ||
                                prod.op == il::core::Opcode::UCmpGE)
                            {
                                if (prod.operands.size() != 2)
                                    return false;
                                const auto &o0 = prod.operands[0];
                                const auto &o1 = prod.operands[1];
                                const char *cc = lookupCondition(prod.op);
                                if (!cc)
                                    return false;
                                if (o0.kind == il::core::Value::Kind::Temp &&
                                    o1.kind == il::core::Value::Kind::Temp)
                                {
                                    int i0 = indexOfParam(bb, o0.id);
                                    int i1 = indexOfParam(bb, o1.id);
                                    if (i0 >= 0 && i1 >= 0 &&
                                        static_cast<std::size_t>(i0) < kMaxGPRArgs &&
                                        static_cast<std::size_t>(i1) < kMaxGPRArgs)
                                    {
                                        const PhysReg r0 = argOrder[i0];
                                        const PhysReg r1 = argOrder[i1];
                                        bbMir.instrs.push_back(
                                            MInstr{MOpcode::CmpRR,
                                                   {MOperand::regOp(r0), MOperand::regOp(r1)}});
                                        bbMir.instrs.push_back(MInstr{
                                            MOpcode::Cset,
                                            {MOperand::regOp(dstReg), MOperand::condOp(cc)}});
                                        return true;
                                    }
                                }
                                if (o0.kind == il::core::Value::Kind::Temp &&
                                    o1.kind == il::core::Value::Kind::ConstInt)
                                {
                                    int i0 = indexOfParam(bb, o0.id);
                                    if (i0 >= 0 && static_cast<std::size_t>(i0) < kMaxGPRArgs)
                                    {
                                        const PhysReg r0 = argOrder[i0];
                                        // cmp r0, #imm; cset dst, cc
                                        bbMir.instrs.push_back(
                                            MInstr{MOpcode::CmpRI,
                                                   {MOperand::regOp(r0), MOperand::immOp(o1.i64)}});
                                        bbMir.instrs.push_back(MInstr{
                                            MOpcode::Cset,
                                            {MOperand::regOp(dstReg), MOperand::condOp(cc)}});
                                        return true;
                                    }
                                }
                            }
                            return false;
                        };
                        // Split into register and stack arguments
                        const std::size_t nReg = argOrder.size();
                        const std::size_t nRegArgs = (nargs < nReg) ? nargs : nReg;
                        const std::size_t nStackArgs = (nargs > nReg) ? (nargs - nReg) : 0;
                        bool supported = true;
                        // Register args: plan moves/imm loads/temps for 0..nRegArgs-1
                        for (std::size_t i = 0; i < nRegArgs; ++i)
                        {
                            const PhysReg dst = argOrder[i];
                            const auto &arg = binI.operands[i];
                            if (arg.kind == il::core::Value::Kind::ConstInt)
                            {
                                immLoads.emplace_back(dst, arg.i64);
                            }
                            else
                            {
                                unsigned pIdx = 0;
                                if (isParamTemp(arg, pIdx) && pIdx < argOrder.size())
                                {
                                    const PhysReg src = argOrder[pIdx];
                                    if (src != dst)
                                        moves.push_back(Move{dst, src});
                                }
                                else
                                {
                                    // Attempt to compute temp into a scratch then marshal it.
                                    if (arg.kind == il::core::Value::Kind::Temp &&
                                        scratchUsed < kScratchPoolSize)
                                    {
                                        auto it = std::find_if(
                                            bb.instructions.begin(),
                                            bb.instructions.end(),
                                            [&](const il::core::Instr &I)
                                            { return I.result && *I.result == arg.id; });
                                        if (it != bb.instructions.end())
                                        {
                                            const PhysReg dstScratch = scratchPool[scratchUsed];
                                            if (computeTempTo(*it, dstScratch))
                                            {
                                                tempRegs.emplace_back(i, dstScratch);
                                                ++scratchUsed;
                                                continue;
                                            }
                                        }
                                    }
                                    supported = false;
                                    break; // unsupported temp
                                }
                            }
                        }
                        if (!supported)
                        { /* fallthrough: no call lowering */
                        }
                        else
                        {
                            // Include temp-reg moves into overall move list
                            for (auto &tr : tempRegs)
                            {
                                const PhysReg dstArg = argOrder[tr.first];
                                if (dstArg != tr.second)
                                    moves.push_back(Move{dstArg, tr.second});
                            }
                            // Resolve reg moves with scratch X9 to break cycles.
                            auto hasDst = [&](PhysReg r)
                            {
                                for (auto &m : moves)
                                    if (m.dst == r)
                                        return true;
                                return false;
                            };
                            // Perform until empty
                            while (!moves.empty())
                            {
                                bool progressed = false;
                                for (auto it = moves.begin(); it != moves.end();)
                                {
                                    if (!hasDst(it->src))
                                    {
                                        bbMir.instrs.push_back(MInstr{
                                            MOpcode::MovRR,
                                            {MOperand::regOp(it->dst), MOperand::regOp(it->src)}});
                                        it = moves.erase(it);
                                        progressed = true;
                                    }
                                    else
                                    {
                                        ++it;
                                    }
                                }
                                if (!progressed)
                                {
                                    // Break cycle using scratch register
                                    const PhysReg cycleSrc = moves.front().src;
                                    bbMir.instrs.push_back(MInstr{
                                        MOpcode::MovRR,
                                        {MOperand::regOp(kScratchGPR), MOperand::regOp(cycleSrc)}});
                                    for (auto &m : moves)
                                        if (m.src == cycleSrc)
                                            m.src = kScratchGPR;
                                }
                            }
                            // Apply immediates
                            for (auto &pr : immLoads)
                                bbMir.instrs.push_back(MInstr{
                                    MOpcode::MovRI,
                                    {MOperand::regOp(pr.first), MOperand::immOp(pr.second)}});

                            // Stack args: allocate area, materialize values, store at [sp, #offset]
                            if (nStackArgs > 0)
                            {
                                long long frameBytes =
                                    static_cast<long long>(nStackArgs) * kSlotSizeBytes;
                                if (frameBytes % kStackAlignment != 0LL)
                                    frameBytes += kSlotSizeBytes; // Align to 16 bytes
                                // Reserve in frame builder, do not emit dynamic SP adjust
                                fb.setMaxOutgoingBytes(static_cast<int>(frameBytes));
                                for (std::size_t i = nReg; i < nargs; ++i)
                                {
                                    const auto &arg = binI.operands[i];
                                    PhysReg valReg = kScratchGPR;
                                    if (arg.kind == il::core::Value::Kind::ConstInt)
                                    {
                                        // Use a scratch reg to hold the constant.
                                        // If pool exhausted, bail out rather than risk conflicts.
                                        if (scratchUsed >= kScratchPoolSize)
                                        {
                                            supported = false;
                                            break;
                                        }
                                        const PhysReg tmp = scratchPool[scratchUsed++];
                                        bbMir.instrs.push_back(MInstr{
                                            MOpcode::MovRI,
                                            {MOperand::regOp(tmp), MOperand::immOp(arg.i64)}});
                                        valReg = tmp;
                                    }
                                    else if (arg.kind == il::core::Value::Kind::Temp)
                                    {
                                        unsigned pIdx = 0;
                                        if (isParamTemp(arg, pIdx) && pIdx < argOrder.size())
                                        {
                                            valReg = argOrder[pIdx];
                                        }
                                        else
                                        {
                                            // Compute limited temps into scratch.
                                            // Bail out if scratch pool exhausted.
                                            if (scratchUsed >= kScratchPoolSize)
                                            {
                                                supported = false;
                                                break;
                                            }
                                            valReg = scratchPool[scratchUsed++];
                                            auto it = std::find_if(
                                                bb.instructions.begin(),
                                                bb.instructions.end(),
                                                [&](const il::core::Instr &I)
                                                { return I.result && *I.result == arg.id; });
                                            if (it == bb.instructions.end() ||
                                                !computeTempTo(*it, valReg))
                                            {
                                                supported = false;
                                                break;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        supported = false;
                                        break;
                                    }
                                    const long long off = static_cast<long long>((i - nReg) * 8ULL);
                                    bbMir.instrs.push_back(
                                        MInstr{MOpcode::StrRegSpImm,
                                               {MOperand::regOp(valReg), MOperand::immOp(off)}});
                                }
                                if (!supported)
                                {
                                    // If unsupported mid-way, do not emit call sequence.
                                    bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                                    fb.finalize();
                                    return mf;
                                }
                                // Emit call
                                bbMir.instrs.push_back(
                                    MInstr{MOpcode::Bl, {MOperand::labelOp(binI.callee)}});
                                bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                                fb.finalize();
                                return mf;
                            }
                            // No stack args; emit call directly
                            bbMir.instrs.push_back(
                                MInstr{MOpcode::Bl, {MOperand::labelOp(binI.callee)}});
                            bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                            fb.finalize();
                            return mf;
                        }
                    }
                }
            }
            const bool isAdd =
                (binI.op == il::core::Opcode::Add || binI.op == il::core::Opcode::IAddOvf);
            const bool isSub =
                (binI.op == il::core::Opcode::Sub || binI.op == il::core::Opcode::ISubOvf);
            const bool isShl = (binI.op == il::core::Opcode::Shl);
            const bool isLShr = (binI.op == il::core::Opcode::LShr);
            const bool isAShr = (binI.op == il::core::Opcode::AShr);
            const bool isICmpImm = (lookupCondition(binI.op) != nullptr);
            if ((isAdd || isSub || isShl || isLShr || isAShr) && retI.op == il::core::Opcode::Ret &&
                binI.result && !retI.operands.empty() && binI.operands.size() == 2)
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result)
                {
                    const auto &o0 = binI.operands[0];
                    const auto &o1 = binI.operands[1];
                    auto emitImm = [&](unsigned paramIndex, long long imm)
                    {
                        const PhysReg src = argOrder[paramIndex];
                        bbMir.instrs.push_back(MInstr{
                            MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                        if (isAdd)
                            bbMir.instrs.push_back(MInstr{MOpcode::AddRI,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::immOp(imm)}});
                        else if (isSub)
                            bbMir.instrs.push_back(MInstr{MOpcode::SubRI,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::immOp(imm)}});
                        else if (isShl)
                            bbMir.instrs.push_back(MInstr{MOpcode::LslRI,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::immOp(imm)}});
                        else if (isLShr)
                            bbMir.instrs.push_back(MInstr{MOpcode::LsrRI,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::immOp(imm)}});
                        else if (isAShr)
                            bbMir.instrs.push_back(MInstr{MOpcode::AsrRI,
                                                          {MOperand::regOp(PhysReg::X0),
                                                           MOperand::regOp(PhysReg::X0),
                                                           MOperand::immOp(imm)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                    };
                    if (o0.kind == il::core::Value::Kind::Temp &&
                        o1.kind == il::core::Value::Kind::ConstInt)
                    {
                        for (size_t i = 0; i < bb.params.size(); ++i)
                            if (bb.params[i].id == o0.id && i < kMaxGPRArgs)
                            {
                                emitImm(static_cast<unsigned>(i), o1.i64);
                                return mf;
                            }
                    }
                    if (o1.kind == il::core::Value::Kind::Temp &&
                        o0.kind == il::core::Value::Kind::ConstInt)
                    {
                        if (isAdd || isShl || isLShr || isAShr)
                        {
                            for (size_t i = 0; i < bb.params.size(); ++i)
                                if (bb.params[i].id == o1.id && i < kMaxGPRArgs)
                                {
                                    emitImm(static_cast<unsigned>(i), o0.i64);
                                    return mf;
                                }
                        }
                    }
                }
            }
            if (isICmpImm && retI.op == il::core::Opcode::Ret && binI.result &&
                !retI.operands.empty() && binI.operands.size() == 2)
            {
                const auto &retV = retI.operands[0];
                if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result)
                {
                    const auto &o0 = binI.operands[0];
                    const auto &o1 = binI.operands[1];
                    auto emitCmpImm = [&](unsigned paramIndex, long long imm)
                    {
                        const PhysReg src = argOrder[paramIndex];
                        if (src != PhysReg::X0)
                            bbMir.instrs.push_back(
                                MInstr{MOpcode::MovRR,
                                       {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                        bbMir.instrs.push_back(MInstr{
                            MOpcode::CmpRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                        bbMir.instrs.push_back(MInstr{MOpcode::Cset,
                                                      {MOperand::regOp(PhysReg::X0),
                                                       MOperand::condOp(lookupCondition(binI.op))}});
                        bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                    };
                    if (o0.kind == il::core::Value::Kind::Temp &&
                        o1.kind == il::core::Value::Kind::ConstInt)
                    {
                        for (size_t i = 0; i < bb.params.size(); ++i)
                            if (bb.params[i].id == o0.id && i < kMaxGPRArgs)
                            {
                                emitCmpImm(static_cast<unsigned>(i), o1.i64);
                                return mf;
                            }
                    }
                }
            }
        }

        // ret const i64 short-path: only when function is single-block with ONLY a ret const.
        if (fn.blocks.size() == 1)
        {
            const auto &only = fn.blocks.front();
            if (only.instructions.size() == 1) // Changed: exactly one instruction
            {
                const auto &term = only.instructions.back();
                if (term.op == il::core::Opcode::Ret && !term.operands.empty())
                {
                    const auto &v = term.operands[0];
                    if (v.kind == il::core::Value::Kind::ConstInt)
                    {
                        const long long imm = v.i64;
                        // Prefer movz/movk path in AsmEmitter for wide values.
                        bbOut(0).instrs.push_back(MInstr{
                            MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                        bbOut(0).instrs.push_back(MInstr{MOpcode::Ret, {}});
                        return mf;
                    }
                }
            }
        }
    }

    // No fast-path matched
    return std::nullopt;
}

} // namespace viper::codegen::aarch64
