//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: codegen/aarch64/fastpaths/FastPaths_Arithmetic.cpp
// Purpose: Fast-path pattern matching for arithmetic operations.
//
// Summary:
//   Handles fast-path lowering for arithmetic patterns:
//   - Integer RR ops: add/sub/mul/and/or/xor on entry params feeding ret
//   - Integer RI ops: add/sub/shl/lshr/ashr with immediate operands
//   - Integer comparisons: icmp.eq/ne, scmp.lt/le/gt/ge, ucmp.lt/le/gt/ge
//
// Invariants:
//   - Operands must be entry parameters or constant immediates
//   - Result must flow directly to a ret instruction
//   - Parameters must fit within the ABI register argument limit
//
//===----------------------------------------------------------------------===//

#include "FastPathsInternal.hpp"

namespace viper::codegen::aarch64::fastpaths
{

using il::core::Opcode;

std::optional<MFunction> tryIntArithmeticFastPaths(FastPathContext &ctx)
{
    if (ctx.fn.blocks.empty())
        return std::nullopt;

    const auto &bb = ctx.fn.blocks.front();
    auto &bbMir = ctx.bbOut(0);

    // =========================================================================
    // RR ops on entry params feeding ret
    // =========================================================================
    // Pattern: binop %p0, %p1 -> %r; ret %r
    // Handles: add, sub, mul, and, or, xor, comparisons
    if (ctx.fn.blocks.size() == 1 && bb.instructions.size() >= 2 && bb.params.size() >= 2)
    {
        const auto &opI = bb.instructions[bb.instructions.size() - 2];
        const auto &retI = bb.instructions.back();
        if ((opI.op == Opcode::Add || opI.op == Opcode::IAddOvf || opI.op == Opcode::Sub ||
             opI.op == Opcode::ISubOvf || opI.op == Opcode::Mul || opI.op == Opcode::IMulOvf ||
             opI.op == Opcode::And || opI.op == Opcode::Or || opI.op == Opcode::Xor ||
             opI.op == Opcode::ICmpEq || opI.op == Opcode::ICmpNe || opI.op == Opcode::SCmpLT ||
             opI.op == Opcode::SCmpLE || opI.op == Opcode::SCmpGT || opI.op == Opcode::SCmpGE ||
             opI.op == Opcode::UCmpLT || opI.op == Opcode::UCmpLE || opI.op == Opcode::UCmpGT ||
             opI.op == Opcode::UCmpGE) &&
            retI.op == Opcode::Ret && opI.result && !retI.operands.empty())
        {
            const auto &retV = retI.operands[0];
            if (retV.kind == il::core::Value::Kind::Temp && retV.id == *opI.result &&
                opI.operands.size() == 2 && opI.operands[0].kind == il::core::Value::Kind::Temp &&
                opI.operands[1].kind == il::core::Value::Kind::Temp)
            {
                const int idx0 = indexOfParam(bb, opI.operands[0].id);
                const int idx1 = indexOfParam(bb, opI.operands[1].id);
                if (idx0 >= 0 && idx1 >= 0 && static_cast<std::size_t>(idx0) < kMaxGPRArgs &&
                    static_cast<std::size_t>(idx1) < kMaxGPRArgs)
                {
                    const PhysReg src0 = ctx.argOrder[static_cast<size_t>(idx0)];
                    const PhysReg src1 = ctx.argOrder[static_cast<size_t>(idx1)];
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
                    return ctx.mf;
                }
            }
        }
    }

    // =========================================================================
    // RI ops: add/sub/shl/lshr/ashr with immediate
    // =========================================================================
    // Pattern: binop %param, #imm -> %r; ret %r
    if (ctx.fn.blocks.size() == 1 && bb.instructions.size() >= 2 && !bb.params.empty())
    {
        const auto &binI = bb.instructions[bb.instructions.size() - 2];
        const auto &retI = bb.instructions.back();
        const bool isAdd = (binI.op == Opcode::Add || binI.op == Opcode::IAddOvf);
        const bool isSub = (binI.op == Opcode::Sub || binI.op == Opcode::ISubOvf);
        const bool isShl = (binI.op == Opcode::Shl);
        const bool isLShr = (binI.op == Opcode::LShr);
        const bool isAShr = (binI.op == Opcode::AShr);
        const bool isICmpImm = (lookupCondition(binI.op) != nullptr);

        if ((isAdd || isSub || isShl || isLShr || isAShr) && retI.op == Opcode::Ret &&
            binI.result && !retI.operands.empty() && binI.operands.size() == 2)
        {
            const auto &retV = retI.operands[0];
            if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result)
            {
                const auto &o0 = binI.operands[0];
                const auto &o1 = binI.operands[1];
                auto emitImm = [&](unsigned paramIndex, long long imm)
                {
                    const PhysReg src = ctx.argOrder[paramIndex];
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
                            return ctx.mf;
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
                                return ctx.mf;
                            }
                    }
                }
            }
        }

        // Immediate comparisons
        if (isICmpImm && retI.op == Opcode::Ret && binI.result && !retI.operands.empty() &&
            binI.operands.size() == 2)
        {
            const auto &retV = retI.operands[0];
            if (retV.kind == il::core::Value::Kind::Temp && retV.id == *binI.result)
            {
                const auto &o0 = binI.operands[0];
                const auto &o1 = binI.operands[1];
                auto emitCmpImm = [&](unsigned paramIndex, long long imm)
                {
                    const PhysReg src = ctx.argOrder[paramIndex];
                    if (src != PhysReg::X0)
                        bbMir.instrs.push_back(MInstr{
                            MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
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
                            return ctx.mf;
                        }
                }
            }
        }
    }

    return std::nullopt;
}

// =========================================================================
// Floating-point RR ops
// =========================================================================
// Pattern: fop %p0, %p1 -> %r; ret %r
// Handles: fadd, fsub, fmul, fdiv

std::optional<MFunction> tryFPArithmeticFastPaths(FastPathContext &ctx)
{
    if (ctx.fn.blocks.empty())
        return std::nullopt;

    const auto &bb = ctx.fn.blocks.front();
    auto &bbMir = ctx.bbOut(0);

    if (ctx.fn.blocks.size() == 1 && bb.instructions.size() >= 2 && bb.params.size() >= 2)
    {
        const auto &opI = bb.instructions[bb.instructions.size() - 2];
        const auto &retI = bb.instructions.back();
        const bool isFAdd = (opI.op == Opcode::FAdd);
        const bool isFSub = (opI.op == Opcode::FSub);
        const bool isFMul = (opI.op == Opcode::FMul);
        const bool isFDiv = (opI.op == Opcode::FDiv);
        if ((isFAdd || isFSub || isFMul || isFDiv) && retI.op == Opcode::Ret && opI.result &&
            !retI.operands.empty())
        {
            const auto &retV = retI.operands[0];
            if (retV.kind == il::core::Value::Kind::Temp && retV.id == *opI.result &&
                opI.operands.size() == 2 && opI.operands[0].kind == il::core::Value::Kind::Temp &&
                opI.operands[1].kind == il::core::Value::Kind::Temp)
            {
                const int idx0 = indexOfParam(bb, opI.operands[0].id);
                const int idx1 = indexOfParam(bb, opI.operands[1].id);
                if (idx0 >= 0 && idx1 >= 0 && static_cast<std::size_t>(idx0) < kMaxFPRArgs &&
                    static_cast<std::size_t>(idx1) < kMaxFPRArgs)
                {
                    const PhysReg src0 = ctx.ti.f64ArgOrder[static_cast<std::size_t>(idx0)];
                    const PhysReg src1 = ctx.ti.f64ArgOrder[static_cast<std::size_t>(idx1)];
                    // Normalize to d0,d1 using FPR scratch register
                    bbMir.instrs.push_back(MInstr{
                        MOpcode::FMovRR, {MOperand::regOp(kScratchFPR), MOperand::regOp(src1)}});
                    bbMir.instrs.push_back(MInstr{
                        MOpcode::FMovRR, {MOperand::regOp(PhysReg::V0), MOperand::regOp(src0)}});
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
                    ctx.fb.finalize();
                    return ctx.mf;
                }
            }
        }
    }

    return std::nullopt;
}

} // namespace viper::codegen::aarch64::fastpaths
