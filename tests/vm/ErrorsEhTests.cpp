// File: tests/vm/ErrorsEhTests.cpp
// Purpose: Validate VM error handlers resume execution using resume.next and resume.label.
// Key invariants: Handlers receive resume tokens and normal execution continues as specified.
// Ownership/Lifetime: Builds IL modules on the stack and executes them via the VM.
// Links: docs/specs/errors.md

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>

using namespace il::core;

namespace
{
Module buildResumeNextModule()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    fn.blocks.reserve(3);
    auto &entry = builder.addBlock(fn, "entry");
    auto &body = builder.addBlock(fn, "body");
    auto &handler = builder.createBlock(
        fn,
        "handler",
        {Param{"err", Type(Type::Kind::Error), 0}, Param{"tok", Type(Type::Kind::ResumeTok), 0}});

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
    Instr div;
    div.result = builder.reserveTempId();
    div.op = Opcode::SDivChk0;
    div.type = Type(Type::Kind::I64);
    div.operands.push_back(Value::constInt(10));
    div.operands.push_back(Value::constInt(0));
    body.instructions.push_back(div);

    Instr pop;
    pop.op = Opcode::EhPop;
    pop.type = Type(Type::Kind::Void);
    body.instructions.push_back(pop);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(42));
    body.instructions.push_back(ret);
    body.terminated = true;

    builder.setInsertPoint(handler);
    Instr resume;
    resume.op = Opcode::ResumeNext;
    resume.type = Type(Type::Kind::Void);
    resume.operands.push_back(builder.blockParam(handler, 1));
    handler.instructions.push_back(resume);
    handler.terminated = true;

    return module;
}

Module buildResumeLabelModule()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    fn.blocks.reserve(4);
    auto &entry = builder.addBlock(fn, "entry");
    auto &body = builder.addBlock(fn, "body");
    auto &recover = builder.addBlock(fn, "recover");
    auto &handler = builder.createBlock(
        fn,
        "handler",
        {Param{"err", Type(Type::Kind::Error), 0}, Param{"tok", Type(Type::Kind::ResumeTok), 0}});

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
    Instr div;
    div.result = builder.reserveTempId();
    div.op = Opcode::SDivChk0;
    div.type = Type(Type::Kind::I64);
    div.operands.push_back(Value::constInt(7));
    div.operands.push_back(Value::constInt(0));
    body.instructions.push_back(div);

    Instr pop;
    pop.op = Opcode::EhPop;
    pop.type = Type(Type::Kind::Void);
    body.instructions.push_back(pop);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(0));
    body.instructions.push_back(ret);
    body.terminated = true;

    builder.setInsertPoint(handler);
    Instr resume;
    resume.op = Opcode::ResumeLabel;
    resume.type = Type(Type::Kind::Void);
    resume.operands.push_back(builder.blockParam(handler, 1));
    resume.labels.push_back("recover");
    resume.brArgs.push_back({});
    handler.instructions.push_back(resume);
    handler.terminated = true;

    builder.setInsertPoint(recover);
    Instr popRecover;
    popRecover.op = Opcode::EhPop;
    popRecover.type = Type(Type::Kind::Void);
    recover.instructions.push_back(popRecover);

    Instr retRecover;
    retRecover.op = Opcode::Ret;
    retRecover.type = Type(Type::Kind::Void);
    retRecover.operands.push_back(Value::constInt(99));
    recover.instructions.push_back(retRecover);
    recover.terminated = true;

    return module;
}
} // namespace

int main()
{
    {
        Module module = buildResumeNextModule();
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode == 42);
    }

    {
        Module module = buildResumeLabelModule();
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode == 99);
    }

    return 0;
}
