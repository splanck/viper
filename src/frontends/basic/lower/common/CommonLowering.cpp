//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/common/CommonLowering.cpp
// Purpose: Implement reusable IL emission helpers shared across BASIC lowering.
// Key invariants: All helpers assume the Lowerer selected an active basic block
//                 before emitting instructions and respect the current source
//                 location tracked by the lowering context.
// Ownership/Lifetime: Borrows the Lowerer without taking ownership of IR
//                     builders, functions, or AST nodes.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/common/CommonLowering.hpp"

#include "frontends/basic/Lowerer.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"

#include <cassert>
#include <utility>

using namespace il::core;

namespace il::frontends::basic::lower::common
{

CommonLowering::CommonLowering(Lowerer &lowerer) noexcept : lowerer_(&lowerer) {}

CommonLowering::Type CommonLowering::ilBoolTy() const
{
    return Type(Type::Kind::I1);
}

CommonLowering::Value CommonLowering::emitBoolConst(bool v)
{
    return emitUnary(Opcode::Trunc1, ilBoolTy(), Value::constInt(v ? 1 : 0));
}

CommonLowering::Value CommonLowering::emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                                                           const std::function<void(Value)> &emitElse,
                                                           std::string_view thenLabelBase,
                                                           std::string_view elseLabelBase,
                                                           std::string_view joinLabelBase)
{
    auto &ctx = lowerer_->context();
    Value slot = emitAlloca(1);

    std::string thenLbl = makeBlockLabel(thenLabelBase);
    std::string elseLbl = makeBlockLabel(elseLabelBase);
    std::string joinLbl = makeBlockLabel(joinLabelBase);

    Function *func = ctx.function();
    assert(func && "emitBoolFromBranches requires an active function");

    BasicBlock *thenBlk = &lowerer_->builder->addBlock(*func, thenLbl);
    BasicBlock *elseBlk = &lowerer_->builder->addBlock(*func, elseLbl);
    BasicBlock *joinBlk = &lowerer_->builder->addBlock(*func, joinLbl);

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

CommonLowering::Value CommonLowering::emitAlloca(int bytes)
{
    unsigned id = lowerer_->nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Alloca;
    in.type = Type(Type::Kind::Ptr);
    in.operands.push_back(Value::constInt(bytes));
    in.loc = lowerer_->curLoc;
    BasicBlock *block = lowerer_->context().current();
    assert(block && "emitAlloca requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

CommonLowering::Value CommonLowering::emitLoad(Type ty, Value addr)
{
    unsigned id = lowerer_->nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Load;
    in.type = ty;
    in.operands.push_back(addr);
    in.loc = lowerer_->curLoc;
    BasicBlock *block = lowerer_->context().current();
    assert(block && "emitLoad requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

void CommonLowering::emitStore(Type ty, Value addr, Value val)
{
    Instr in;
    in.op = Opcode::Store;
    in.type = ty;
    in.operands = {addr, val};
    in.loc = lowerer_->curLoc;
    BasicBlock *block = lowerer_->context().current();
    assert(block && "emitStore requires an active block");
    block->instructions.push_back(in);
}

CommonLowering::Value CommonLowering::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    unsigned id = lowerer_->nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {lhs, rhs};
    in.loc = lowerer_->curLoc;
    BasicBlock *block = lowerer_->context().current();
    assert(block && "emitBinary requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

CommonLowering::Value CommonLowering::emitUnary(Opcode op, Type ty, Value val)
{
    unsigned id = lowerer_->nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {val};
    in.loc = lowerer_->curLoc;
    BasicBlock *block = lowerer_->context().current();
    assert(block && "emitUnary requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

CommonLowering::Value CommonLowering::emitConstI64(std::int64_t v) const
{
    return Value::constInt(v);
}

CommonLowering::Value CommonLowering::emitZext1ToI64(Value val)
{
    return emitUnary(Opcode::Zext1, Type(Type::Kind::I64), val);
}

CommonLowering::Value CommonLowering::emitISub(Value lhs, Value rhs)
{
    return emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), lhs, rhs);
}

CommonLowering::Value CommonLowering::emitBasicLogicalI64(Value b1)
{
    if (lowerer_->context().current() == nullptr)
    {
        if (b1.kind == Value::Kind::ConstInt)
            return Value::constInt(b1.i64 != 0 ? -1 : 0);
        return Value::constInt(0);
    }
    Value i64zero = emitConstI64(0);
    Value zext = emitZext1ToI64(b1);
    return emitISub(i64zero, zext);
}

CommonLowering::Value CommonLowering::emitCheckedNeg(Type ty, Value val)
{
    return emitBinary(Opcode::ISubOvf, ty, Value::constInt(0), val);
}

void CommonLowering::emitBr(BasicBlock *target)
{
    BasicBlock *block = lowerer_->context().current();
    assert(block && "emitBr requires an active block");

    if (block == target)
        return;

    Instr in;
    in.op = Opcode::Br;
    in.type = Type(Type::Kind::Void);
    if (target->label.empty())
        target->label = lowerer_->nextFallbackBlockLabel();
    in.labels.push_back(target->label);
    in.loc = lowerer_->curLoc;
    block->instructions.push_back(in);
    block->terminated = true;
}

void CommonLowering::emitCBr(Value cond, BasicBlock *t, BasicBlock *f)
{
    Instr in;
    in.op = Opcode::CBr;
    in.type = Type(Type::Kind::Void);
    in.operands.push_back(cond);
    in.labels.push_back(t->label);
    in.labels.push_back(f->label);
    in.loc = lowerer_->curLoc;
    BasicBlock *block = lowerer_->context().current();
    assert(block && "emitCBr requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

CommonLowering::Value CommonLowering::emitCallRet(Type ty,
                                                  const std::string &callee,
                                                  const std::vector<Value> &args)
{
    unsigned id = lowerer_->nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Call;
    in.type = ty;
    in.callee = callee;
    in.operands = args;
    in.loc = lowerer_->curLoc;
    BasicBlock *block = lowerer_->context().current();
    assert(block && "emitCallRet requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

void CommonLowering::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    Instr in;
    in.op = Opcode::Call;
    in.type = Type(Type::Kind::Void);
    in.callee = callee;
    in.operands = args;
    in.loc = lowerer_->curLoc;
    BasicBlock *block = lowerer_->context().current();
    assert(block && "emitCall requires an active block");
    block->instructions.push_back(in);
}

CommonLowering::Value CommonLowering::emitConstStr(const std::string &globalName)
{
    unsigned id = lowerer_->nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::ConstStr;
    in.type = Type(Type::Kind::Str);
    in.operands.push_back(Value::global(globalName));
    in.loc = lowerer_->curLoc;
    BasicBlock *block = lowerer_->context().current();
    assert(block && "emitConstStr requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

std::string CommonLowering::makeBlockLabel(std::string_view base) const
{
    auto &ctx = lowerer_->context();
    if (auto *blockNamer = ctx.blockNames().namer())
        return blockNamer->generic(std::string(base));
    return lowerer_->mangler.block(std::string(base));
}

} // namespace il::frontends::basic::lower::common
