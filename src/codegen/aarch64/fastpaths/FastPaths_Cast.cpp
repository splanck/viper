//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: codegen/aarch64/fastpaths/FastPaths_Cast.cpp
// Purpose: Fast-path pattern matching for type conversion operations.
//
// Summary:
//   Handles fast-path lowering for type conversion patterns:
//   - zext1/trunc1: Boolean extension/truncation
//   - cast.si_narrow.chk: Signed narrowing with range check
//   - cast.fp_to_si.rte.chk: FP to integer conversion with exactness check
//
// Invariants:
//   - Operand must be an entry parameter or the result of a supported producer
//   - Result must flow directly to a ret instruction
//   - Trap blocks are emitted for range/conversion failures
//
//===----------------------------------------------------------------------===//

#include "FastPathsInternal.hpp"

namespace viper::codegen::aarch64::fastpaths
{

using il::core::Opcode;

// Thread-local counter for trap labels (defined in main FastPaths.cpp)
thread_local unsigned trapLabelCounter = 0;

std::optional<MFunction> tryCastFastPaths(FastPathContext &ctx)
{
    if (ctx.fn.blocks.empty())
        return std::nullopt;

    const auto &bb = ctx.fn.blocks.front();
    auto &bbMir = ctx.bbOut(0);

    // Need at least 2 instructions and 1 parameter
    if (ctx.fn.blocks.size() != 1 || bb.instructions.size() < 2 || bb.params.empty())
        return std::nullopt;

    const auto &binI = bb.instructions[bb.instructions.size() - 2];
    const auto &retI = bb.instructions.back();

    // Must be a cast instruction feeding ret
    if (retI.op != Opcode::Ret || !binI.result || retI.operands.empty() ||
        retI.operands[0].kind != il::core::Value::Kind::Temp || retI.operands[0].id != *binI.result)
        return std::nullopt;

    // =========================================================================
    // zext1/trunc1: Boolean extension/truncation
    // =========================================================================
    // Pattern: zext1/trunc1 %param -> %r; ret %r
    // Both operations mask to lowest bit: and x0, x0, #1
    if (binI.op == Opcode::Zext1 || binI.op == Opcode::Trunc1)
    {
        const auto &o0 = binI.operands[0];
        if (o0.kind == il::core::Value::Kind::Temp)
        {
            int pIdx = indexOfParam(bb, o0.id);
            if (pIdx >= 0)
            {
                PhysReg src = ctx.argOrder[static_cast<std::size_t>(pIdx)];
                if (src != PhysReg::X0)
                    bbMir.instrs.push_back(MInstr{
                        MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                // and x0, x0, #1 via scratch register
                bbMir.instrs.push_back(
                    MInstr{MOpcode::MovRI, {MOperand::regOp(kScratchGPR), MOperand::immOp(1)}});
                bbMir.instrs.push_back(MInstr{MOpcode::AndRRR,
                                              {MOperand::regOp(PhysReg::X0),
                                               MOperand::regOp(PhysReg::X0),
                                               MOperand::regOp(kScratchGPR)}});
                bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                ctx.fb.finalize();
                return ctx.mf;
            }
        }
        // Fall through to generic lowering if operand is not a param
    }

    // =========================================================================
    // cast.si_narrow.chk: Signed narrowing with range check
    // =========================================================================
    // Pattern: cast.si_narrow.chk %param -> %r; ret %r
    // Emits: sign-extend truncation, compare, trap on mismatch
    if (binI.op == Opcode::CastSiNarrowChk)
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
                src = ctx.argOrder[static_cast<std::size_t>(pIdx)];
        }
        if (src != PhysReg::X0)
            bbMir.instrs.push_back(
                MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});

        // tmp = (x0 << sh) >> sh  (sign-extended truncation)
        if (sh > 0)
        {
            bbMir.instrs.push_back(MInstr{
                MOpcode::LslRI,
                {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::immOp(sh)}});
            bbMir.instrs.push_back(MInstr{
                MOpcode::AsrRI,
                {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::X0), MOperand::immOp(sh)}});
        }

        // Compare restored value to source in scratch register
        bbMir.instrs.push_back(
            MInstr{MOpcode::MovRR, {MOperand::regOp(kScratchGPR), MOperand::regOp(src)}});
        bbMir.instrs.push_back(
            MInstr{MOpcode::CmpRR, {MOperand::regOp(PhysReg::X0), MOperand::regOp(kScratchGPR)}});

        // If not equal, branch to a trap block
        const std::string trapLabel = ".Ltrap_cast_" + std::to_string(trapLabelCounter++);
        bbMir.instrs.push_back(
            MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trapLabel)}});

        // Fall-through: range is OK, return
        bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});

        // Append trap block to function with a call to rt_trap
        ctx.mf.blocks.emplace_back();
        ctx.mf.blocks.back().name = trapLabel;
        ctx.mf.blocks.back().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
        ctx.fb.finalize();
        return ctx.mf;
    }

    // =========================================================================
    // cast.fp_to_si.rte.chk: FP to integer with exactness check
    // =========================================================================
    // Pattern: cast.fp_to_si.rte.chk %param -> %r; ret %r
    // Emits: fcvtzs, scvtf, fcmp, trap on mismatch
    if (binI.op == Opcode::CastFpToSiRteChk)
    {
        const auto &o0 = binI.operands[0];
        if (o0.kind == il::core::Value::Kind::Temp)
        {
            int pIdx = indexOfParam(bb, o0.id);
            if (pIdx >= 0)
            {
                const PhysReg s = ctx.ti.f64ArgOrder[static_cast<std::size_t>(pIdx)];
                if (s != PhysReg::V0)
                    bbMir.instrs.push_back(MInstr{
                        MOpcode::FMovRR, {MOperand::regOp(PhysReg::V0), MOperand::regOp(s)}});
            }
        }

        // x0 = fcvtzs d0
        bbMir.instrs.push_back(
            MInstr{MOpcode::FCvtZS, {MOperand::regOp(PhysReg::X0), MOperand::regOp(PhysReg::V0)}});

        // d1 = scvtf x0; fcmp d0, d1; b.ne trap
        bbMir.instrs.push_back(
            MInstr{MOpcode::SCvtF, {MOperand::regOp(PhysReg::V1), MOperand::regOp(PhysReg::X0)}});
        bbMir.instrs.push_back(
            MInstr{MOpcode::FCmpRR, {MOperand::regOp(PhysReg::V0), MOperand::regOp(PhysReg::V1)}});

        const std::string trapLabel2 = ".Ltrap_fpcast_" + std::to_string(trapLabelCounter++);
        bbMir.instrs.push_back(
            MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp(trapLabel2)}});

        // Fall-through: value is exact, return
        bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});

        ctx.mf.blocks.emplace_back();
        ctx.mf.blocks.back().name = trapLabel2;
        ctx.mf.blocks.back().instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap")}});
        ctx.fb.finalize();
        return ctx.mf;
    }

    return std::nullopt;
}

} // namespace viper::codegen::aarch64::fastpaths
