// File: src/frontends/basic/lower/Emitter.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements the IL emission helper composed by the BASIC lowerer.
// Key invariants: Helpers append instructions to the current block when set.
// Ownership/Lifetime: Borrows Lowerer-managed contexts and IR builder state.
// Links: docs/codemap.md

#include "frontends/basic/lower/Emitter.hpp"

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include <cassert>
#include <optional>
#include <utility>

using namespace il::core;

namespace il::frontends::basic::lower
{


Emitter::Emitter(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

Emitter::Type Emitter::ilBoolTy() const
{
    return Type(Type::Kind::I1);
}

Emitter::Value Emitter::emitBoolConst(bool v)
{
    return emitUnary(Opcode::Trunc1, ilBoolTy(), Value::constInt(v ? 1 : 0));
}

Emitter::Value Emitter::emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                                             const std::function<void(Value)> &emitElse,
                                             std::string_view thenLabelBase,
                                             std::string_view elseLabelBase,
                                             std::string_view joinLabelBase)
{
    auto &ctx = lowerer_.context();
    Value slot = emitAlloca(1);

    auto labelFor = [&](std::string_view base)
    {
        std::string hint(base);
        if (auto *blockNamer = ctx.blockNames().namer())
            return blockNamer->generic(hint);
        return lowerer_.mangler.block(hint);
    };

    std::string thenLbl = labelFor(thenLabelBase);
    std::string elseLbl = labelFor(elseLabelBase);
    std::string joinLbl = labelFor(joinLabelBase);

    Function *func = ctx.function();
    assert(func && "emitBoolFromBranches requires an active function");
    BasicBlock *thenBlk = &lowerer_.builder->addBlock(*func, thenLbl);
    BasicBlock *elseBlk = &lowerer_.builder->addBlock(*func, elseLbl);
    BasicBlock *joinBlk = &lowerer_.builder->addBlock(*func, joinLbl);

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

Emitter::Value Emitter::emitAlloca(int bytes)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Alloca;
    in.type = Type(Type::Kind::Ptr);
    in.operands.push_back(Value::constInt(bytes));
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitAlloca requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

Emitter::Value Emitter::emitLoad(Type ty, Value addr)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Load;
    in.type = ty;
    in.operands.push_back(addr);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitLoad requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

void Emitter::emitStore(Type ty, Value addr, Value val)
{
    Instr in;
    in.op = Opcode::Store;
    in.type = ty;
    in.operands = {addr, val};
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitStore requires an active block");
    block->instructions.push_back(in);
}

Emitter::Value Emitter::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {lhs, rhs};
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitBinary requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

Emitter::Value Emitter::emitUnary(Opcode op, Type ty, Value val)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {val};
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitUnary requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

Emitter::Value Emitter::emitConstI64(std::int64_t v)
{
    return Value::constInt(v);
}

Emitter::Value Emitter::emitZext1ToI64(Value val)
{
    return emitUnary(Opcode::Zext1, Type(Type::Kind::I64), val);
}

Emitter::Value Emitter::emitISub(Value lhs, Value rhs)
{
    return emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), lhs, rhs);
}

Emitter::Value Emitter::emitBasicLogicalI64(Value b1)
{
    if (lowerer_.context().current() == nullptr)
    {
        if (b1.kind == Value::Kind::ConstInt)
            return Value::constInt(b1.i64 != 0 ? -1 : 0);
        return Value::constInt(0);
    }
    Value i64zero = emitConstI64(0);
    Value zext = emitZext1ToI64(b1);
    return emitISub(i64zero, zext);
}

Emitter::Value Emitter::emitCheckedNeg(Type ty, Value val)
{
    return emitBinary(Opcode::ISubOvf, ty, Value::constInt(0), val);
}

void Emitter::emitBr(BasicBlock *target)
{
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitBr requires an active block");

    if (block == target)
        return;

    Instr in;
    in.op = Opcode::Br;
    in.type = Type(Type::Kind::Void);
    if (target->label.empty())
        target->label = lowerer_.nextFallbackBlockLabel();
    in.labels.push_back(target->label);
    in.loc = lowerer_.curLoc;
    block->instructions.push_back(in);
    block->terminated = true;
}

void Emitter::emitCBr(Value cond, BasicBlock *t, BasicBlock *f)
{
    Instr in;
    in.op = Opcode::CBr;
    in.type = Type(Type::Kind::Void);
    in.operands.push_back(cond);
    in.labels.push_back(t->label);
    in.labels.push_back(f->label);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitCBr requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

Emitter::Value Emitter::emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Call;
    in.type = ty;
    in.callee = callee;
    in.operands = args;
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitCallRet requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

void Emitter::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    Instr in;
    in.op = Opcode::Call;
    in.type = Type(Type::Kind::Void);
    in.callee = callee;
    in.operands = args;
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitCall requires an active block");
    block->instructions.push_back(in);
}

Emitter::Value Emitter::emitConstStr(const std::string &globalName)
{
    unsigned id = lowerer_.nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::ConstStr;
    in.type = Type(Type::Kind::Str);
    in.operands.push_back(Value::global(globalName));
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitConstStr requires an active block");
    block->instructions.push_back(in);
    return Value::temp(id);
}

void Emitter::storeArray(Value slot, Value value)
{
    lowerer_.requireArrayI32Retain();
    emitCall("rt_arr_i32_retain", {value});
    Value oldValue = emitLoad(Type(Type::Kind::Ptr), slot);
    lowerer_.requireArrayI32Release();
    emitCall("rt_arr_i32_release", {oldValue});
    emitStore(Type(Type::Kind::Ptr), slot, value);
}

void Emitter::releaseArrayLocals(const std::unordered_set<std::string> &paramNames)
{
    bool requested = false;
    for (auto &[name, info] : lowerer_.symbols)
    {
        if (!info.referenced || !info.slotId || !info.isArray)
            continue;
        if (paramNames.contains(name))
            continue;
        Value slot = Value::temp(*info.slotId);
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);
        if (!requested)
        {
            lowerer_.requireArrayI32Release();
            requested = true;
        }
        emitCall("rt_arr_i32_release", {handle});
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    }
}

void Emitter::releaseArrayParams(const std::unordered_set<std::string> &paramNames)
{
    if (paramNames.empty())
        return;
    bool requested = false;
    for (auto &[name, info] : lowerer_.symbols)
    {
        if (!info.referenced || !info.slotId || !info.isArray)
            continue;
        if (!paramNames.contains(name))
            continue;
        Value slot = Value::temp(*info.slotId);
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);
        if (!requested)
        {
            lowerer_.requireArrayI32Release();
            requested = true;
        }
        emitCall("rt_arr_i32_release", {handle});
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    }
}

void Emitter::releaseObjectLocals(const std::unordered_set<std::string> &paramNames)
{
    auto releaseSlot = [this](Lowerer::SymbolInfo &info)
    {
        if (!lowerer_.builder || !info.slotId)
            return;
        auto &ctx = lowerer_.context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (!func || !origin)
            return;

        std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
        Value slot = Value::temp(*info.slotId);

        lowerer_.curLoc = {};
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);

        lowerer_.requestHelper(Lowerer::RuntimeFeature::ObjReleaseChk0);
        lowerer_.requestHelper(Lowerer::RuntimeFeature::ObjFree);

        Value shouldDestroy = emitCallRet(ilBoolTy(), "rt_obj_release_check0", {handle});

        std::string destroyLabel;
        if (auto *blockNamer = ctx.blockNames().namer())
            destroyLabel = blockNamer->generic("obj_epilogue_dtor");
        else
            destroyLabel = lowerer_.mangler.block("obj_epilogue_dtor");
        std::size_t destroyIdx = func->blocks.size();
        lowerer_.builder->addBlock(*func, destroyLabel);

        std::string contLabel;
        if (auto *blockNamer = ctx.blockNames().namer())
            contLabel = blockNamer->generic("obj_epilogue_cont");
        else
            contLabel = lowerer_.mangler.block("obj_epilogue_cont");
        std::size_t contIdx = func->blocks.size();
        lowerer_.builder->addBlock(*func, contLabel);

        BasicBlock *destroyBlk = &func->blocks[destroyIdx];
        BasicBlock *contBlk = &func->blocks[contIdx];

        ctx.setCurrent(&func->blocks[originIdx]);
        lowerer_.curLoc = {};
        emitCBr(shouldDestroy, destroyBlk, contBlk);

        ctx.setCurrent(destroyBlk);
        lowerer_.curLoc = {};
        if (!info.objectClass.empty())
            emitCall(mangleClassDtor(info.objectClass), {handle});
        emitCall("rt_obj_free", {handle});
        emitBr(contBlk);

        ctx.setCurrent(contBlk);
        lowerer_.curLoc = {};
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    };

    for (auto &[name, info] : lowerer_.symbols)
    {
        if (!info.referenced || !info.isObject)
            continue;
        if (name == "ME")
            continue;
        if (paramNames.contains(name))
            continue;
        if (!info.slotId)
            continue;
        releaseSlot(info);
    }
}

void Emitter::releaseObjectParams(const std::unordered_set<std::string> &paramNames)
{
    if (paramNames.empty())
        return;

    auto releaseSlot = [this](Lowerer::SymbolInfo &info)
    {
        if (!lowerer_.builder || !info.slotId)
            return;
        auto &ctx = lowerer_.context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (!func || !origin)
            return;

        std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
        Value slot = Value::temp(*info.slotId);

        lowerer_.curLoc = {};
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);

        lowerer_.requestHelper(Lowerer::RuntimeFeature::ObjReleaseChk0);
        lowerer_.requestHelper(Lowerer::RuntimeFeature::ObjFree);

        Value shouldDestroy = emitCallRet(ilBoolTy(), "rt_obj_release_check0", {handle});

        std::string destroyLabel;
        if (auto *blockNamer = ctx.blockNames().namer())
            destroyLabel = blockNamer->generic("obj_epilogue_dtor");
        else
            destroyLabel = lowerer_.mangler.block("obj_epilogue_dtor");
        std::size_t destroyIdx = func->blocks.size();
        lowerer_.builder->addBlock(*func, destroyLabel);

        std::string contLabel;
        if (auto *blockNamer = ctx.blockNames().namer())
            contLabel = blockNamer->generic("obj_epilogue_cont");
        else
            contLabel = lowerer_.mangler.block("obj_epilogue_cont");
        std::size_t contIdx = func->blocks.size();
        lowerer_.builder->addBlock(*func, contLabel);

        BasicBlock *destroyBlk = &func->blocks[destroyIdx];
        BasicBlock *contBlk = &func->blocks[contIdx];

        ctx.setCurrent(&func->blocks[originIdx]);
        lowerer_.curLoc = {};
        emitCBr(shouldDestroy, destroyBlk, contBlk);

        ctx.setCurrent(destroyBlk);
        lowerer_.curLoc = {};
        if (!info.objectClass.empty())
            emitCall(mangleClassDtor(info.objectClass), {handle});
        emitCall("rt_obj_free", {handle});
        emitBr(contBlk);

        ctx.setCurrent(contBlk);
        lowerer_.curLoc = {};
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    };

    for (auto &[name, info] : lowerer_.symbols)
    {
        if (!info.referenced || !info.isObject)
            continue;
        if (name == "ME")
            continue;
        if (!paramNames.contains(name))
            continue;
        if (!info.slotId)
            continue;
        releaseSlot(info);
    }
}

void Emitter::emitTrap()
{
    Instr in;
    in.op = Opcode::Trap;
    in.type = Type(Type::Kind::Void);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitTrap requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

void Emitter::emitTrapFromErr(Value errCode)
{
    Instr in;
    in.op = Opcode::TrapFromErr;
    in.type = Type(Type::Kind::I32);
    in.operands.push_back(errCode);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitTrapFromErr requires an active block");
    block->instructions.push_back(std::move(in));
    block->terminated = true;
}

void Emitter::emitEhPush(BasicBlock *handler)
{
    assert(handler && "emitEhPush requires a handler block");
    Instr in;
    in.op = Opcode::EhPush;
    in.type = Type(Type::Kind::Void);
    in.labels.push_back(handler->label);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitEhPush requires an active block");
    block->instructions.push_back(in);
}

void Emitter::emitEhPop()
{
    Instr in;
    in.op = Opcode::EhPop;
    in.type = Type(Type::Kind::Void);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitEhPop requires an active block");
    block->instructions.push_back(in);
}

void Emitter::emitEhPopForReturn()
{
    if (!lowerer_.context().errorHandlers().active())
        return;
    emitEhPop();
}

void Emitter::clearActiveErrorHandler()
{
    auto &ctx = lowerer_.context();
    if (ctx.errorHandlers().active())
        emitEhPop();
    ctx.errorHandlers().setActive(false);
    ctx.errorHandlers().setActiveIndex(std::nullopt);
    ctx.errorHandlers().setActiveLine(std::nullopt);
}

Emitter::BasicBlock *Emitter::ensureErrorHandlerBlock(int targetLine)
{
    auto &ctx = lowerer_.context();
    Function *func = ctx.function();
    assert(func && "ensureErrorHandlerBlock requires an active function");

    auto &handlers = ctx.errorHandlers().blocks();
    auto it = handlers.find(targetLine);
    if (it != handlers.end())
        return &func->blocks[it->second];

    std::string base = "handler_L" + std::to_string(targetLine);
    std::string label;
    if (auto *blockNamer = ctx.blockNames().namer())
        label = blockNamer->tag(base);
    else
        label = lowerer_.mangler.block(base);

    std::vector<il::core::Param> params = {{"err", Type(Type::Kind::Error)},
                                           {"tok", Type(Type::Kind::ResumeTok)}};
    BasicBlock &bb = lowerer_.builder->createBlock(*func, label, params);

    Instr entry;
    entry.op = Opcode::EhEntry;
    entry.type = Type(Type::Kind::Void);
    entry.loc = {};
    bb.instructions.push_back(entry);

    size_t idx = static_cast<size_t>(&bb - &func->blocks[0]);
    handlers[targetLine] = idx;
    ctx.errorHandlers().handlerTargets()[idx] = targetLine;
    return &bb;
}

void Emitter::emitRet(Value v)
{
    emitEhPopForReturn();
    Instr in;
    in.op = Opcode::Ret;
    in.type = Type(Type::Kind::Void);
    in.operands.push_back(v);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitRet requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

void Emitter::emitRetVoid()
{
    emitEhPopForReturn();
    Instr in;
    in.op = Opcode::Ret;
    in.type = Type(Type::Kind::Void);
    in.loc = lowerer_.curLoc;
    BasicBlock *block = lowerer_.context().current();
    assert(block && "emitRetVoid requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

} // namespace il::frontends::basic::lower
