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
    return Type(Type::Kind::I1);
}

Lowerer::IlValue Lowerer::emitBoolConst(bool v)
{
    return emitUnary(Opcode::Trunc1, ilBoolTy(), Value::constInt(v ? 1 : 0));
}

Lowerer::IlValue Lowerer::emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                                               const std::function<void(Value)> &emitElse,
                                               std::string_view thenLabelBase,
                                               std::string_view elseLabelBase,
                                               std::string_view joinLabelBase)
{
    ProcedureContext &ctx = context();
    Value slot = emitAlloca(1);

    auto labelFor = [&](std::string_view base)
    {
        std::string hint(base);
        if (BlockNamer *blockNamer = ctx.blockNames().namer())
            return blockNamer->generic(hint);
        return mangler.block(hint);
    };

    std::string thenLbl = labelFor(thenLabelBase);
    std::string elseLbl = labelFor(elseLabelBase);
    std::string joinLbl = labelFor(joinLabelBase);

    Function *func = ctx.function();
    assert(func && "emitBoolFromBranches requires an active function");
    BasicBlock *thenBlk = &builder->addBlock(*func, thenLbl);
    BasicBlock *elseBlk = &builder->addBlock(*func, elseLbl);
    BasicBlock *joinBlk = &builder->addBlock(*func, joinLbl);

    ctx.setCurrent(thenBlk);
    emitThen(slot);
    if (ctx.current() && !ctx.current()->terminated)
        emitBr(joinBlk);

    ctx.setCurrent(elseBlk);
    emitElse(slot);
    if (ctx.current() && !ctx.current()->terminated)
        emitBr(joinBlk);

    ctx.setCurrent(joinBlk);
    return emitLoad(ilBoolTy(), slot);
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
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Alloca;
    in.type = Type(Type::Kind::Ptr);
    in.operands.push_back(Value::constInt(bytes));
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitAlloca requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

Value Lowerer::emitLoad(Type ty, Value addr)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Load;
    in.type = ty;
    in.operands.push_back(addr);
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitLoad requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

void Lowerer::emitStore(Type ty, Value addr, Value val)
{
    Instr in;
    in.op = Opcode::Store;
    in.type = ty;
    in.operands = {addr, val};
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitStore requires an active block");
    block->instructions.push_back(in);
}

Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {lhs, rhs};
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitBinary requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

Value Lowerer::emitUnary(Opcode op, Type ty, Value val)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {val};
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitUnary requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

Value Lowerer::emitCheckedNeg(Type ty, Value val)
{
    return emitBinary(Opcode::ISubOvf, ty, Value::constInt(0), val);
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    Instr in;
    in.op = Opcode::Call;
    in.type = Type(Type::Kind::Void);
    in.callee = callee;
    in.operands = args;
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitCall requires an active block");
    block->instructions.push_back(in);
}

Value Lowerer::emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Call;
    in.type = ty;
    in.callee = callee;
    in.operands = args;
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitCallRet requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

Value Lowerer::emitConstStr(const std::string &globalName)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::ConstStr;
    in.type = Type(Type::Kind::Str);
    in.operands.push_back(Value::global(globalName));
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitConstStr requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
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
