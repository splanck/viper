// File: tests/vm/IdxChkTests.cpp
// Purpose: Verify idx.chk bounds semantics and err.get_* accessors in handler contexts.
// Key invariants: Out-of-range indices raise Bounds traps and handler error metadata is populated.
// Ownership/Lifetime: Builds IL modules on the stack and executes them via the VM.
// Links: docs/specs/errors.md

#include "il/build/IRBuilder.hpp"
#include "vm/Trap.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdint>

using namespace il::core;

namespace
{
Module buildIdxChkPassModule()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I16), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr chk;
    chk.result = builder.reserveTempId();
    chk.op = Opcode::IdxChk;
    chk.type = Type(Type::Kind::I16);
    chk.operands.push_back(Value::constInt(7));
    chk.operands.push_back(Value::constInt(0));
    chk.operands.push_back(Value::constInt(10));
    chk.loc = {1, 10, 0};
    bb.instructions.push_back(chk);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*chk.result));
    ret.loc = {1, 11, 0};
    bb.instructions.push_back(ret);
    bb.terminated = true;

    return module;
}

Module buildIdxChkTrapModule(Opcode getter, int64_t idxConst, int64_t hiConst)
{
    Module module;
    il::build::IRBuilder builder(module);

    Type retType = (getter == Opcode::ErrGetIp) ? Type(Type::Kind::I64) : Type(Type::Kind::I32);
    auto &fn = builder.startFunction("main", retType, {});
    builder.addBlock(fn, "entry");
    builder.addBlock(fn, "body");
    builder.createBlock(
        fn,
        "handler",
        {Param{"err", Type(Type::Kind::Error), 0}, Param{"tok", Type(Type::Kind::ResumeTok), 0}});

    auto &entry = fn.blocks[0];
    auto &body = fn.blocks[1];
    auto &handler = fn.blocks[2];

    builder.setInsertPoint(entry);
    Instr push;
    push.op = Opcode::EhPush;
    push.type = Type(Type::Kind::Void);
    push.labels.push_back("handler");
    push.loc = {1, 20, 0};
    entry.instructions.push_back(push);
    builder.br(body);
    entry.terminated = true;

    builder.setInsertPoint(body);
    Instr chk;
    chk.result = builder.reserveTempId();
    chk.op = Opcode::IdxChk;
    chk.type = Type(Type::Kind::I32);
    chk.operands.push_back(Value::constInt(idxConst));
    chk.operands.push_back(Value::constInt(0));
    chk.operands.push_back(Value::constInt(hiConst));
    chk.loc = {1, 42, 0};
    body.instructions.push_back(chk);

    Instr retBody;
    retBody.op = Opcode::Ret;
    retBody.type = Type(Type::Kind::Void);
    retBody.operands.push_back(Value::constInt(0));
    retBody.loc = {1, 43, 0};
    body.instructions.push_back(retBody);
    body.terminated = true;

    builder.setInsertPoint(handler);
    Instr get;
    get.result = builder.reserveTempId();
    get.op = getter;
    get.type = retType;
    get.operands.push_back(builder.blockParam(handler, 0));
    get.loc = {1, 45, 0};
    handler.instructions.push_back(get);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*get.result));
    ret.loc = {1, 46, 0};
    handler.instructions.push_back(ret);
    handler.terminated = true;

    return module;
}

int64_t runBoundsGetterWithIdx(Opcode getter, int64_t idxConst, int64_t hiConst)
{
    Module module = buildIdxChkTrapModule(getter, idxConst, hiConst);
    il::vm::VM vm(module);
    return vm.run();
}

int64_t runBoundsGetter(Opcode getter)
{
    return runBoundsGetterWithIdx(getter, 99, 10);
}
} // namespace

int main()
{
    {
        Module module = buildIdxChkPassModule();
        il::vm::VM vm(module);
        assert(vm.run() == 7);
    }

    {
        const int64_t kind = runBoundsGetter(Opcode::ErrGetKind);
        assert(kind == static_cast<int64_t>(static_cast<int32_t>(il::vm::TrapKind::Bounds)));
    }

    {
        const int64_t kindAtHigh = runBoundsGetterWithIdx(Opcode::ErrGetKind, 10, 10);
        assert(kindAtHigh == static_cast<int64_t>(static_cast<int32_t>(il::vm::TrapKind::Bounds)));
    }

    {
        const int64_t code = runBoundsGetter(Opcode::ErrGetCode);
        assert(code == 0);
    }

    {
        const int64_t ip = runBoundsGetter(Opcode::ErrGetIp);
        assert(ip == 0);
    }

    {
        const int64_t line = runBoundsGetter(Opcode::ErrGetLine);
        assert(line == 42);
    }

    return 0;
}
