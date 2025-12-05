//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: codegen/aarch64/fastpaths/FastPaths_Return.cpp
// Purpose: Fast-path pattern matching for return-related patterns.
//
// Summary:
//   Handles fast-path lowering for simple return patterns:
//   - ret %paramN: Return a parameter directly
//   - ret const i64: Return an integer constant
//   - ret (const_str/addr_of): Return a symbol address
//
// Invariants:
//   - Single-block functions with no side effects
//   - Return value must be directly available (no computation needed)
//
//===----------------------------------------------------------------------===//

#include "FastPathsInternal.hpp"

namespace viper::codegen::aarch64::fastpaths
{

using il::core::Opcode;

std::optional<MFunction> tryReturnFastPaths(FastPathContext &ctx)
{
    if (ctx.fn.blocks.empty())
        return std::nullopt;

    const auto &bb = ctx.fn.blocks.front();
    auto &bbMir = ctx.bbOut(0);

    // =========================================================================
    // ret %paramN fast-path
    // =========================================================================
    // Pattern: Single-block with no side effects, returning a parameter directly.
    // Emits: mov x0/d0, srcReg (if needed); ret
    if (ctx.fn.blocks.size() == 1 && !bb.instructions.empty() && !bb.params.empty() &&
        !hasSideEffects(bb))
    {
        const auto &retI = bb.instructions.back();
        if (retI.op == Opcode::Ret && !retI.operands.empty())
        {
            const auto &rv = retI.operands[0];
            if (rv.kind == il::core::Value::Kind::Temp)
            {
                int pIdx = indexOfParam(bb, rv.id);
                if (pIdx >= 0)
                {
                    if (ctx.fn.retType.kind == il::core::Type::Kind::F64)
                    {
                        const PhysReg src = ctx.ti.f64ArgOrder[static_cast<size_t>(pIdx)];
                        if (src != PhysReg::V0)
                            bbMir.instrs.push_back(
                                MInstr{MOpcode::FMovRR,
                                       {MOperand::regOp(PhysReg::V0), MOperand::regOp(src)}});
                    }
                    else
                    {
                        const PhysReg src = ctx.argOrder[static_cast<size_t>(pIdx)];
                        if (src != PhysReg::X0)
                            bbMir.instrs.push_back(
                                MInstr{MOpcode::MovRR,
                                       {MOperand::regOp(PhysReg::X0), MOperand::regOp(src)}});
                    }
                    bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                    ctx.fb.finalize();
                    return ctx.mf;
                }
            }
        }
    }

    // =========================================================================
    // ret (const_str/addr_of) fast-path
    // =========================================================================
    // Pattern: Return a symbol address via adrp/add sequence.
    // Emits: adrp x0, sym@PAGE; add x0, x0, sym@PAGEOFF; ret
    if (ctx.fn.blocks.size() == 1 && bb.instructions.size() >= 2)
    {
        const auto &retI = bb.instructions.back();
        if (retI.op == Opcode::Ret && !retI.operands.empty())
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
                    if ((prod.op == Opcode::ConstStr || prod.op == Opcode::AddrOf) &&
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
                        ctx.fb.finalize();
                        return ctx.mf;
                    }
                }
            }
        }
    }

    // =========================================================================
    // ret const i64 fast-path
    // =========================================================================
    // Pattern: Single-block with exactly one ret-const instruction.
    // Emits: mov x0, #imm; ret
    if (ctx.fn.blocks.size() == 1)
    {
        const auto &only = ctx.fn.blocks.front();
        if (only.instructions.size() == 1)
        {
            const auto &term = only.instructions.back();
            if (term.op == Opcode::Ret && !term.operands.empty())
            {
                const auto &v = term.operands[0];
                if (v.kind == il::core::Value::Kind::ConstInt)
                {
                    const long long imm = v.i64;
                    ctx.bbOut(0).instrs.push_back(MInstr{
                        MOpcode::MovRI, {MOperand::regOp(PhysReg::X0), MOperand::immOp(imm)}});
                    ctx.bbOut(0).instrs.push_back(MInstr{MOpcode::Ret, {}});
                    return ctx.mf;
                }
            }
        }
    }

    return std::nullopt;
}

} // namespace viper::codegen::aarch64::fastpaths
