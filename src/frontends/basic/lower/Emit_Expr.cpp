//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements expression emission helpers for the BASIC lowerer.
/// @details Functions in this unit create temporaries, allocate stack slots,
/// and build data-flow operations while respecting the lowerer's block
/// invariants. Each helper assumes the caller has positioned the active block
/// and will only append non-terminating instructions, leaving terminator
/// emission to control helpers. Temporary values remain owned by the lowerer
/// and are tracked through the shared ProcedureContext.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <cassert>
#include <utility>

using namespace il::core;

namespace il::frontends::basic
{

Lowerer::IlType Lowerer::ilBoolTy()
{
    return emitter().ilBoolTy();
}

Lowerer::IlValue Lowerer::emitBoolConst(bool v)
{
    return emitter().emitBoolConst(v);
}

Lowerer::IlValue Lowerer::emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                                               const std::function<void(Value)> &emitElse,
                                               std::string_view thenLabelBase,
                                               std::string_view elseLabelBase,
                                               std::string_view joinLabelBase)
{
    return emitter().emitBoolFromBranches(emitThen, emitElse, thenLabelBase, elseLabelBase, joinLabelBase);
}

Lowerer::ArrayAccess Lowerer::lowerArrayAccess(const ArrayExpr &expr, ArrayAccessKind kind)
{
    requireArrayI32Len();
    requireArrayOobPanic();
    if (kind == ArrayAccessKind::Load)
        requireArrayI32Get();
    else
        requireArrayI32Set();

    ProcedureContext &ctx = context();
    const auto *info = findSymbol(expr.name);
    assert(info && info->slotId);
    Value slot = Value::temp(*info->slotId);
    Value base = emitLoad(Type(Type::Kind::Ptr), slot);
    RVal idx = lowerExpr(*expr.index);
    idx = coerceToI64(std::move(idx), expr.loc);
    Value index = idx.value;
    curLoc = expr.loc;

    Value len = emitCallRet(Type(Type::Kind::I64), "rt_arr_i32_len", {base});
    Value isNeg = emitBinary(Opcode::SCmpLT, ilBoolTy(), index, Value::constInt(0));
    Value tooHigh = emitBinary(Opcode::SCmpGE, ilBoolTy(), index, len);
    Value isNeg64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), isNeg);
    Value tooHigh64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), tooHigh);
    Value oobInt = emitBinary(Opcode::Or, Type(Type::Kind::I64), isNeg64, tooHigh64);
    Value oobCond = emitBinary(Opcode::ICmpNe, ilBoolTy(), oobInt, Value::constInt(0));

    Function *func = ctx.function();
    assert(func && ctx.current());
    size_t curIdx = static_cast<size_t>(ctx.current() - &func->blocks[0]);
    unsigned bcId = ctx.consumeBoundsCheckId();
    BlockNamer *blockNamer = ctx.blockNames().namer();
    size_t okIdx = func->blocks.size();
    std::string okLbl = blockNamer ? blockNamer->tag("bc_ok" + std::to_string(bcId))
                                   : mangler.block("bc_ok" + std::to_string(bcId));
    builder->addBlock(*func, okLbl);
    size_t oobIdx = func->blocks.size();
    std::string oobLbl = blockNamer ? blockNamer->tag("bc_oob" + std::to_string(bcId))
                                    : mangler.block("bc_oob" + std::to_string(bcId));
    builder->addBlock(*func, oobLbl);
    BasicBlock *ok = &func->blocks[okIdx];
    BasicBlock *oob = &func->blocks[oobIdx];
    ctx.setCurrent(&func->blocks[curIdx]);
    emitCBr(oobCond, oob, ok);

    ctx.setCurrent(oob);
    emitCall("rt_arr_oob_panic", {index, len});
    emitTrap();

    ctx.setCurrent(ok);
    return ArrayAccess{base, index};
}

Value Lowerer::emitAlloca(int bytes)
{
    return emitter().emitAlloca(bytes);
}

Value Lowerer::emitLoad(Type ty, Value addr)
{
    return emitter().emitLoad(ty, addr);
}

void Lowerer::emitStore(Type ty, Value addr, Value val)
{
    emitter().emitStore(ty, addr, val);
}

Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    return emitter().emitBinary(op, ty, lhs, rhs);
}

Value Lowerer::emitUnary(Opcode op, Type ty, Value val)
{
    return emitter().emitUnary(op, ty, val);
}

Value Lowerer::emitCheckedNeg(Type ty, Value val)
{
    return emitter().emitCheckedNeg(ty, val);
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    emitter().emitCall(callee, args);
}

Value Lowerer::emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args)
{
    return emitter().emitCallRet(ty, callee, args);
}

Value Lowerer::emitConstStr(const std::string &globalName)
{
    return emitter().emitConstStr(globalName);
}

std::string Lowerer::getStringLabel(const std::string &s)
{
    auto &info = ensureSymbol(s);
    if (!info.stringLabel.empty())
        return info.stringLabel;
    std::string name = ".L" + std::to_string(nextStringId++);
    builder->addGlobalStr(name, s);
    info.stringLabel = name;
    return info.stringLabel;
}

} // namespace il::frontends::basic
