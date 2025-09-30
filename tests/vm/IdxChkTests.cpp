// File: tests/vm/IdxChkTests.cpp
// Purpose: Validate idx.chk bounds handling and err.get_* accessors in the VM.
// Key invariants: Out-of-range indices raise Bounds and populate handler error metadata.
// Ownership/Lifetime: Builds transient modules executed via the VM runtime.
// Links: docs/specs/errors.md

#include "il/build/IRBuilder.hpp"
#include "vm/Trap.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <functional>

using namespace il::core;
namespace
{
constexpr uint32_t kFaultLine = 123;

Module buildIdxChkTrapModule(Type retType,
                             const std::function<void(il::build::IRBuilder &,
                                                        BasicBlock &,
                                                        Value)> &emitHandler)
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", retType, {});
    fn.blocks.reserve(3);
    auto &entry = builder.addBlock(fn, "entry");
    auto &body = builder.addBlock(fn, "body");
    auto &handler = builder.createBlock(
        fn,
        "handler",
        {Param{"err", Type(Type::Kind::Error), 0}});

    builder.setInsertPoint(entry);
    Instr push;
    push.op = Opcode::EhPush;
    push.type = Type(Type::Kind::Void);
    push.labels.push_back("handler");
    entry.instructions.push_back(push);

    Instr br;
    br.op = Opcode::Br;
    br.type = Type(Type::Kind::Void);
    br.labels.push_back("body");
    br.brArgs.push_back({});
    entry.instructions.push_back(br);
    entry.terminated = true;

    builder.setInsertPoint(body);
    Instr chk;
    chk.result = builder.reserveTempId();
    chk.op = Opcode::IdxChk;
    chk.type = Type(Type::Kind::I32);
    chk.operands.push_back(Value::constInt(5));
    chk.operands.push_back(Value::constInt(0));
    chk.operands.push_back(Value::constInt(3));
    chk.loc = {1, kFaultLine, 1};
    body.instructions.push_back(chk);

    Instr retBody;
    retBody.op = Opcode::Ret;
    retBody.type = Type(Type::Kind::Void);
    retBody.operands.push_back(Value::constInt(0));
    body.instructions.push_back(retBody);
    body.terminated = true;

    builder.setInsertPoint(handler);
    emitHandler(builder, handler, builder.blockParam(handler, 0));
    handler.terminated = true;

    return module;
}

Module buildIdxChkInRangeModule()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I32), {});
    fn.blocks.reserve(1);
    auto &entry = builder.addBlock(fn, "entry");

    builder.setInsertPoint(entry);
    Instr chk;
    chk.result = builder.reserveTempId();
    chk.op = Opcode::IdxChk;
    chk.type = Type(Type::Kind::I32);
    chk.operands.push_back(Value::constInt(7));
    chk.operands.push_back(Value::constInt(0));
    chk.operands.push_back(Value::constInt(10));
    entry.instructions.push_back(chk);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*chk.result));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    return module;
}
} // namespace

int main()
{
    {
        Module module = buildIdxChkTrapModule(Type(Type::Kind::I32),
                                              [](il::build::IRBuilder &builder,
                                                 BasicBlock &handler,
                                                 Value errParam)
                                              {
                                                  Instr getKind;
                                                  getKind.result = builder.reserveTempId();
                                                  getKind.op = Opcode::ErrGetKind;
                                                  getKind.type = Type(Type::Kind::I32);
                                                  getKind.operands.push_back(errParam);
                                                  handler.instructions.push_back(getKind);

                                                  Instr ret;
                                                  ret.op = Opcode::Ret;
                                                  ret.type = Type(Type::Kind::Void);
                                                  ret.operands.push_back(Value::temp(*getKind.result));
                                                  handler.instructions.push_back(ret);
                                              });
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode == static_cast<int64_t>(il::vm::TrapKind::Bounds));
    }

    {
        Module module = buildIdxChkTrapModule(Type(Type::Kind::I32),
                                              [](il::build::IRBuilder &builder,
                                                 BasicBlock &handler,
                                                 Value errParam)
                                              {
                                                  Instr getCode;
                                                  getCode.result = builder.reserveTempId();
                                                  getCode.op = Opcode::ErrGetCode;
                                                  getCode.type = Type(Type::Kind::I32);
                                                  getCode.operands.push_back(errParam);
                                                  handler.instructions.push_back(getCode);

                                                  Instr ret;
                                                  ret.op = Opcode::Ret;
                                                  ret.type = Type(Type::Kind::Void);
                                                  ret.operands.push_back(Value::temp(*getCode.result));
                                                  handler.instructions.push_back(ret);
                                              });
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode == 0);
    }

    {
        Module module = buildIdxChkTrapModule(Type(Type::Kind::I64),
                                              [](il::build::IRBuilder &builder,
                                                 BasicBlock &handler,
                                                 Value errParam)
                                              {
                                                  Instr getIp;
                                                  getIp.result = builder.reserveTempId();
                                                  getIp.op = Opcode::ErrGetIp;
                                                  getIp.type = Type(Type::Kind::I64);
                                                  getIp.operands.push_back(errParam);
                                                  handler.instructions.push_back(getIp);

                                                  Instr ret;
                                                  ret.op = Opcode::Ret;
                                                  ret.type = Type(Type::Kind::Void);
                                                  ret.operands.push_back(Value::temp(*getIp.result));
                                                  handler.instructions.push_back(ret);
                                              });
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode == 0);
    }

    {
        Module module = buildIdxChkTrapModule(Type(Type::Kind::I32),
                                              [](il::build::IRBuilder &builder,
                                                 BasicBlock &handler,
                                                 Value errParam)
                                              {
                                                  Instr getLine;
                                                  getLine.result = builder.reserveTempId();
                                                  getLine.op = Opcode::ErrGetLine;
                                                  getLine.type = Type(Type::Kind::I32);
                                                  getLine.operands.push_back(errParam);
                                                  handler.instructions.push_back(getLine);

                                                  Instr ret;
                                                  ret.op = Opcode::Ret;
                                                  ret.type = Type(Type::Kind::Void);
                                                  ret.operands.push_back(Value::temp(*getLine.result));
                                                  handler.instructions.push_back(ret);
                                              });
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode == static_cast<int64_t>(kFaultLine));
    }

    {
        Module module = buildIdxChkInRangeModule();
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode == 7);
    }

    return 0;
}
