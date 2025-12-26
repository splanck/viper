//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: codegen/aarch64/fastpaths/FastPaths_Memory.cpp
// Purpose: Fast-path pattern matching for memory operations.
//
// Summary:
//   Handles fast-path lowering for memory access patterns:
//   - alloca/store/load/ret pattern: Simple local variable round-trip
//   - load-from-param/ret pattern: Load from pointer param and return
//   - gep+load/ret pattern: Field access via GEP then load and return
//
// Invariants:
//   - Single-block functions
//   - Alloca must have been assigned a frame offset
//   - Store/load must target the same alloca
//   - Return value must come from the load
//
//===----------------------------------------------------------------------===//

#include "FastPathsInternal.hpp"

namespace viper::codegen::aarch64::fastpaths
{

using il::core::Opcode;

std::optional<MFunction> tryMemoryFastPaths(FastPathContext &ctx)
{
    if (ctx.fn.blocks.empty())
        return std::nullopt;

    const auto &bb = ctx.fn.blocks.front();
    auto &bbMir = ctx.bbOut(0);

    // =========================================================================
    // alloca/store/load/ret pattern
    // =========================================================================
    // Pattern: %local = alloca i64; store %param0, %local; %val = load %local; ret %val
    // This matches simple functions that spill a parameter and reload it.
    // Emits: str srcReg, [x29, #offset]; ldr x0, [x29, #offset]; ret
    if (!ctx.mf.frame.locals.empty() && ctx.fn.blocks.size() == 1 && bb.instructions.size() >= 4)
    {
        const auto *allocaI = &bb.instructions[bb.instructions.size() - 4];
        const auto *storeI = &bb.instructions[bb.instructions.size() - 3];
        const auto *loadI = &bb.instructions[bb.instructions.size() - 2];
        const auto *retI = &bb.instructions[bb.instructions.size() - 1];

        if (allocaI->op == Opcode::Alloca && allocaI->result && storeI->op == Opcode::Store &&
            storeI->operands.size() == 2 && loadI->op == Opcode::Load && loadI->result &&
            loadI->operands.size() == 1 && retI->op == Opcode::Ret && !retI->operands.empty())
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
                // Get offset for this alloca from frame builder
                const int offset = ctx.fb.localOffset(allocaId);
                if (offset != 0)
                {
                    // Get register holding the value to store
                    auto srcReg = ctx.getValueReg(bb, storeVal);
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
                        ctx.fb.finalize();
                        return ctx.mf;
                    }
                }
            }
        }
    }

    // =========================================================================
    // load-from-param/ret fast-path
    // =========================================================================
    // Pattern: %v = load type, %param0; ret %v
    // Simple accessor that loads from a pointer parameter and returns.
    // Emits: ldr x0, [x0] (or appropriate variant); ret
    if (ctx.fn.blocks.size() == 1 && bb.instructions.size() == 2 && !bb.params.empty())
    {
        const auto &loadI = bb.instructions[0];
        const auto &retI = bb.instructions[1];

        if (loadI.op == Opcode::Load && loadI.result && !loadI.operands.empty() &&
            retI.op == Opcode::Ret && !retI.operands.empty())
        {
            const auto &loadPtr = loadI.operands[0];
            const auto &retVal = retI.operands[0];

            // Check that we're loading from a param and returning the loaded value
            if (loadPtr.kind == il::core::Value::Kind::Temp &&
                retVal.kind == il::core::Value::Kind::Temp && retVal.id == *loadI.result)
            {
                int pIdx = indexOfParam(bb, loadPtr.id);
                if (pIdx >= 0 && static_cast<std::size_t>(pIdx) < kMaxGPRArgs)
                {
                    const PhysReg ptrReg = ctx.argOrder[static_cast<std::size_t>(pIdx)];
                    const bool isF64 = (loadI.type.kind == il::core::Type::Kind::F64) ||
                                       (ctx.fn.retType.kind == il::core::Type::Kind::F64);

                    if (isF64)
                    {
                        // ldr d0, [ptrReg]
                        bbMir.instrs.push_back(MInstr{MOpcode::LdrFprBaseImm,
                                                      {MOperand::regOp(PhysReg::V0),
                                                       MOperand::regOp(ptrReg),
                                                       MOperand::immOp(0)}});
                    }
                    else
                    {
                        // ldr x0, [ptrReg]
                        bbMir.instrs.push_back(MInstr{MOpcode::LdrRegBaseImm,
                                                      {MOperand::regOp(PhysReg::X0),
                                                       MOperand::regOp(ptrReg),
                                                       MOperand::immOp(0)}});
                    }
                    bbMir.instrs.push_back(MInstr{MOpcode::Ret, {}});
                    ctx.fb.finalize();
                    return ctx.mf;
                }
            }
        }
    }

    // =========================================================================
    // gep+load/ret fast-path
    // =========================================================================
    // Pattern: %p = gep %param0, offset; %v = load type, %p; ret %v
    // Simple field accessor that computes GEP then loads and returns.
    // Emits: ldr x0, [x0, #offset]; ret
    if (ctx.fn.blocks.size() == 1 && bb.instructions.size() == 3 && !bb.params.empty())
    {
        const auto &gepI = bb.instructions[0];
        const auto &loadI = bb.instructions[1];
        const auto &retI = bb.instructions[2];

        if (gepI.op == Opcode::GEP && gepI.result && gepI.operands.size() == 2 &&
            loadI.op == Opcode::Load && loadI.result && !loadI.operands.empty() &&
            retI.op == Opcode::Ret && !retI.operands.empty())
        {
            const auto &gepBase = gepI.operands[0];
            const auto &gepOffset = gepI.operands[1];
            const auto &loadPtr = loadI.operands[0];
            const auto &retVal = retI.operands[0];

            // Check pattern: gep from param, load from gep result, return loaded value
            if (gepBase.kind == il::core::Value::Kind::Temp &&
                gepOffset.kind == il::core::Value::Kind::ConstInt &&
                loadPtr.kind == il::core::Value::Kind::Temp && loadPtr.id == *gepI.result &&
                retVal.kind == il::core::Value::Kind::Temp && retVal.id == *loadI.result)
            {
                int pIdx = indexOfParam(bb, gepBase.id);
                if (pIdx >= 0 && static_cast<std::size_t>(pIdx) < kMaxGPRArgs)
                {
                    const PhysReg baseReg = ctx.argOrder[static_cast<std::size_t>(pIdx)];
                    const long long offset = gepOffset.i64;
                    const bool isF64 = (loadI.type.kind == il::core::Type::Kind::F64) ||
                                       (ctx.fn.retType.kind == il::core::Type::Kind::F64);

                    if (isF64)
                    {
                        // ldr d0, [baseReg, #offset]
                        bbMir.instrs.push_back(MInstr{MOpcode::LdrFprBaseImm,
                                                      {MOperand::regOp(PhysReg::V0),
                                                       MOperand::regOp(baseReg),
                                                       MOperand::immOp(offset)}});
                    }
                    else
                    {
                        // ldr x0, [baseReg, #offset]
                        bbMir.instrs.push_back(MInstr{MOpcode::LdrRegBaseImm,
                                                      {MOperand::regOp(PhysReg::X0),
                                                       MOperand::regOp(baseReg),
                                                       MOperand::immOp(offset)}});
                    }
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
