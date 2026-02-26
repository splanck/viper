//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/ErrorsEhTests.cpp
// Purpose: Validate VM error handlers resume execution using resume.same, resume.next, and resume.label.
// Key invariants: Handlers receive resume tokens and normal execution continues as specified.
// Ownership/Lifetime: Builds IL modules on the stack and executes them via the VM.
// Links: docs/specs/errors.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"
#include "vm/err_bridge.hpp"

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

Module buildErrGetKindModule()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    Instr makeStr;
    makeStr.result = builder.reserveTempId();
    makeStr.op = Opcode::ConstStr;
    makeStr.type = Type(Type::Kind::Str);
    makeStr.operands.push_back(Value::constStr("io_error"));
    entry.instructions.push_back(makeStr);

    Instr makeErr;
    makeErr.result = builder.reserveTempId();
    makeErr.op = Opcode::TrapErr;
    makeErr.type = Type(Type::Kind::Error);
    makeErr.operands.push_back(
        Value::constInt(static_cast<long long>(il::vm::ErrCode::Err_IOError)));
    makeErr.operands.push_back(Value::temp(*makeStr.result));
    entry.instructions.push_back(makeErr);

    Instr getKind;
    getKind.result = builder.reserveTempId();
    getKind.op = Opcode::ErrGetKind;
    getKind.type = Type(Type::Kind::I32);
    getKind.operands.push_back(Value::temp(*makeErr.result));
    entry.instructions.push_back(getKind);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*getKind.result));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    return module;
}

Module buildErrGetCodeModule()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    Instr makeStr;
    makeStr.result = builder.reserveTempId();
    makeStr.op = Opcode::ConstStr;
    makeStr.type = Type(Type::Kind::Str);
    makeStr.operands.push_back(Value::constStr("io_error"));
    entry.instructions.push_back(makeStr);

    Instr makeErr;
    makeErr.result = builder.reserveTempId();
    makeErr.op = Opcode::TrapErr;
    makeErr.type = Type(Type::Kind::Error);
    makeErr.operands.push_back(
        Value::constInt(static_cast<long long>(il::vm::ErrCode::Err_IOError)));
    makeErr.operands.push_back(Value::temp(*makeStr.result));
    entry.instructions.push_back(makeErr);

    Instr nullErr;
    nullErr.result = builder.reserveTempId();
    nullErr.op = Opcode::ConstNull;
    nullErr.type = Type(Type::Kind::Error);
    entry.instructions.push_back(nullErr);

    Instr getCode;
    getCode.result = builder.reserveTempId();
    getCode.op = Opcode::ErrGetCode;
    getCode.type = Type(Type::Kind::I32);
    getCode.operands.push_back(Value::temp(*nullErr.result));
    entry.instructions.push_back(getCode);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*getCode.result));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    return module;
}

Module buildErrGetIpModule()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    Instr makeStr;
    makeStr.result = builder.reserveTempId();
    makeStr.op = Opcode::ConstStr;
    makeStr.type = Type(Type::Kind::Str);
    makeStr.operands.push_back(Value::constStr("io_error"));
    entry.instructions.push_back(makeStr);

    Instr makeErr;
    makeErr.result = builder.reserveTempId();
    makeErr.op = Opcode::TrapErr;
    makeErr.type = Type(Type::Kind::Error);
    makeErr.operands.push_back(
        Value::constInt(static_cast<long long>(il::vm::ErrCode::Err_IOError)));
    makeErr.operands.push_back(Value::temp(*makeStr.result));
    entry.instructions.push_back(makeErr);

    Instr nullErr;
    nullErr.result = builder.reserveTempId();
    nullErr.op = Opcode::ConstNull;
    nullErr.type = Type(Type::Kind::Error);
    entry.instructions.push_back(nullErr);

    Instr getIp;
    getIp.result = builder.reserveTempId();
    getIp.op = Opcode::ErrGetIp;
    getIp.type = Type(Type::Kind::I64);
    getIp.operands.push_back(Value::temp(*nullErr.result));
    entry.instructions.push_back(getIp);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*getIp.result));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    return module;
}

Module buildErrGetLineModule()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    Instr makeStr;
    makeStr.result = builder.reserveTempId();
    makeStr.op = Opcode::ConstStr;
    makeStr.type = Type(Type::Kind::Str);
    makeStr.operands.push_back(Value::constStr("io_error"));
    entry.instructions.push_back(makeStr);

    Instr makeErr;
    makeErr.result = builder.reserveTempId();
    makeErr.op = Opcode::TrapErr;
    makeErr.type = Type(Type::Kind::Error);
    makeErr.operands.push_back(
        Value::constInt(static_cast<long long>(il::vm::ErrCode::Err_IOError)));
    makeErr.operands.push_back(Value::temp(*makeStr.result));
    entry.instructions.push_back(makeErr);

    Instr nullErr;
    nullErr.result = builder.reserveTempId();
    nullErr.op = Opcode::ConstNull;
    nullErr.type = Type(Type::Kind::Error);
    entry.instructions.push_back(nullErr);

    Instr getLine;
    getLine.result = builder.reserveTempId();
    getLine.op = Opcode::ErrGetLine;
    getLine.type = Type(Type::Kind::I32);
    getLine.operands.push_back(Value::temp(*nullErr.result));
    entry.instructions.push_back(getLine);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*getLine.result));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    return module;
}

/// Build a module that tests resume.same with nested EH handlers.
///
/// Flow:
///   entry:       eh.push outer_handler, br -> setup
///   setup:       eh.push inner_handler, br -> try_block
///   try_block:   sdiv.chk0 10/0 → traps → inner_handler catches
///   inner_handler: resume.same → re-executes sdiv.chk0 → traps again
///                  inner was already popped, so outer_handler catches
///   outer_handler: ret 77
///
/// Verifies resume.same correctly re-dispatches to the faulting instruction,
/// causing a second trap that the outer handler catches.
Module buildResumeSameModule()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    fn.blocks.reserve(5);
    auto &entry = builder.addBlock(fn, "entry");
    auto &setup = builder.addBlock(fn, "setup");
    auto &tryBlock = builder.addBlock(fn, "try_block");
    auto &outerHandler = builder.createBlock(
        fn,
        "outer_handler",
        {Param{"err", Type(Type::Kind::Error), 0}, Param{"tok", Type(Type::Kind::ResumeTok), 0}});
    auto &innerHandler = builder.createBlock(
        fn,
        "inner_handler",
        {Param{"err", Type(Type::Kind::Error), 0}, Param{"tok", Type(Type::Kind::ResumeTok), 0}});

    // entry: push outer handler, branch to setup
    builder.setInsertPoint(entry);
    Instr pushOuter;
    pushOuter.op = Opcode::EhPush;
    pushOuter.type = Type(Type::Kind::Void);
    pushOuter.labels.push_back("outer_handler");
    entry.instructions.push_back(pushOuter);

    Instr brSetup;
    brSetup.op = Opcode::Br;
    brSetup.type = Type(Type::Kind::Void);
    brSetup.labels.push_back("setup");
    brSetup.brArgs.push_back({});
    entry.instructions.push_back(brSetup);
    entry.terminated = true;

    // setup: push inner handler, branch to try_block
    builder.setInsertPoint(setup);
    Instr pushInner;
    pushInner.op = Opcode::EhPush;
    pushInner.type = Type(Type::Kind::Void);
    pushInner.labels.push_back("inner_handler");
    setup.instructions.push_back(pushInner);

    Instr brTry;
    brTry.op = Opcode::Br;
    brTry.type = Type(Type::Kind::Void);
    brTry.labels.push_back("try_block");
    brTry.brArgs.push_back({});
    setup.instructions.push_back(brTry);
    setup.terminated = true;

    // try_block: sdiv.chk0 10/0 (traps), eh.pop, ret 0
    builder.setInsertPoint(tryBlock);
    Instr div;
    div.result = builder.reserveTempId();
    div.op = Opcode::SDivChk0;
    div.type = Type(Type::Kind::I64);
    div.operands.push_back(Value::constInt(10));
    div.operands.push_back(Value::constInt(0));
    tryBlock.instructions.push_back(div);

    Instr popInner;
    popInner.op = Opcode::EhPop;
    popInner.type = Type(Type::Kind::Void);
    tryBlock.instructions.push_back(popInner);

    Instr popOuter;
    popOuter.op = Opcode::EhPop;
    popOuter.type = Type(Type::Kind::Void);
    tryBlock.instructions.push_back(popOuter);

    Instr retNormal;
    retNormal.op = Opcode::Ret;
    retNormal.type = Type(Type::Kind::Void);
    retNormal.operands.push_back(Value::constInt(0));
    tryBlock.instructions.push_back(retNormal);
    tryBlock.terminated = true;

    // inner_handler: pop self from EH stack, then resume.same to rethrow
    builder.setInsertPoint(innerHandler);
    Instr popSelf;
    popSelf.op = Opcode::EhPop;
    popSelf.type = Type(Type::Kind::Void);
    innerHandler.instructions.push_back(popSelf);

    Instr resumeSame;
    resumeSame.op = Opcode::ResumeSame;
    resumeSame.type = Type(Type::Kind::Void);
    resumeSame.operands.push_back(builder.blockParam(innerHandler, 1));
    innerHandler.instructions.push_back(resumeSame);
    innerHandler.terminated = true;

    // outer_handler: catch the rethrown exception, return 77
    builder.setInsertPoint(outerHandler);
    Instr retOuter;
    retOuter.op = Opcode::Ret;
    retOuter.type = Type(Type::Kind::Void);
    retOuter.operands.push_back(Value::constInt(77));
    outerHandler.instructions.push_back(retOuter);
    outerHandler.terminated = true;

    return module;
}

Module buildTrapKindReadModule()
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
        {Param{"err", Type(Type::Kind::Error), 0}, Param{"tok", Type(Type::Kind::ResumeTok), 1}});

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
    div.operands.push_back(Value::constInt(1));
    div.operands.push_back(Value::constInt(0));
    body.instructions.push_back(div);

    Instr retBody;
    retBody.op = Opcode::Ret;
    retBody.type = Type(Type::Kind::Void);
    retBody.operands.push_back(Value::constInt(0));
    body.instructions.push_back(retBody);
    body.terminated = true;

    builder.setInsertPoint(handler);
    Instr entryMarker;
    entryMarker.op = Opcode::EhEntry;
    entryMarker.type = Type(Type::Kind::Void);
    handler.instructions.push_back(entryMarker);

    Instr kind;
    kind.result = builder.reserveTempId();
    kind.op = Opcode::TrapKind;
    kind.type = Type(Type::Kind::I64);
    handler.instructions.push_back(kind);

    Instr retHandler;
    retHandler.op = Opcode::Ret;
    retHandler.type = Type(Type::Kind::Void);
    retHandler.operands.push_back(Value::temp(*kind.result));
    handler.instructions.push_back(retHandler);
    handler.terminated = true;

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

    // resume.same: inner handler rethrows via resume.same, outer handler catches and returns 77
    {
        Module module = buildResumeSameModule();
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode == 77);
    }

    {
        Module module = buildErrGetKindModule();
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode == static_cast<int64_t>(static_cast<int32_t>(il::vm::TrapKind::IOError)));
    }

    {
        Module module = buildErrGetCodeModule();
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode ==
               static_cast<int64_t>(static_cast<int32_t>(il::vm::ErrCode::Err_IOError)));
    }

    {
        Module module = buildErrGetIpModule();
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode == 0);
    }

    {
        Module module = buildErrGetLineModule();
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode == -1);
    }

    {
        Module module = buildTrapKindReadModule();
        il::vm::VM vm(module);
        const int64_t exitCode = vm.run();
        assert(exitCode ==
               static_cast<int64_t>(static_cast<int32_t>(il::vm::TrapKind::DivideByZero)));
    }

    return 0;
}
