//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/VM_TailCallEhTests.cpp
// Purpose: Verify tail-call preserves EH state and exceptions are caught by caller.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "VMTestHook.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cstdio>

using namespace il::core;

static il::core::Module build_tco_eh_module()
{
    Module m;
    il::build::IRBuilder b(m);

    // callee() -> i64 that traps (divide by zero)
    Function &callee = b.startFunction("callee", Type(Type::Kind::I64), {});
    std::fprintf(stderr, "[EH] started callee\n");
    std::fflush(stderr);
    BasicBlock &cb = b.addBlock(callee, "entry");
    b.setInsertPoint(cb);
    Instr div;
    div.result = b.reserveTempId();
    div.op = Opcode::SDivChk0;
    div.type = Type(Type::Kind::I64);
    div.operands.push_back(Value::constInt(7));
    div.operands.push_back(Value::constInt(0));
    cb.instructions.push_back(div);
    Instr retc;
    retc.op = Opcode::Ret;
    retc.type = Type(Type::Kind::Void);
    retc.operands.push_back(Value::constInt(0));
    cb.instructions.push_back(retc);
    cb.terminated = true;

    // main() pushes handler, tailcalls callee(), handler resumes to recover
    std::fprintf(stderr, "[EH] building main\n");
    std::fflush(stderr);
    Function &mainFn = b.startFunction("main", Type(Type::Kind::I64), {});
    std::fprintf(stderr, "[EH] added function main\n");
    std::fflush(stderr);
    b.addBlock(mainFn, "entry");
    std::fprintf(stderr, "[EH] added entry block\n");
    std::fflush(stderr);
    b.addBlock(mainFn, "recover");
    std::fprintf(stderr, "[EH] added recover block\n");
    std::fflush(stderr);
    b.createBlock(
        mainFn,
        "handler",
        {Param{"err", Type(Type::Kind::Error), 0}, Param{"tok", Type(Type::Kind::ResumeTok), 0}});
    std::fprintf(stderr, "[EH] added handler block\n");
    std::fflush(stderr);

    BasicBlock *entry = nullptr;
    BasicBlock *recover = nullptr;
    BasicBlock *handler = nullptr;
    for (auto &bb : mainFn.blocks)
    {
        if (bb.label == "entry")
            entry = &bb;
        else if (bb.label == "recover")
            recover = &bb;
        else if (bb.label == "handler")
            handler = &bb;
    }
    assert(entry && recover && handler);

    b.setInsertPoint(*entry);
    std::fprintf(stderr, "[EH] entry insert\n");
    std::fflush(stderr);
    Instr push;
    push.op = Opcode::EhPush;
    push.type = Type(Type::Kind::Void);
    push.labels.push_back("handler");
    entry->instructions.push_back(push);
    std::fprintf(stderr, "[EH] pushed handler\n");
    std::fflush(stderr);
    // tailcall callee(); ret result
    unsigned dst = b.reserveTempId();
    std::fprintf(stderr, "[EH] reserved dst %u\n", dst);
    std::fflush(stderr);
    b.emitCall("callee", {}, Value::temp(dst), {0, 1, 1});
    std::fprintf(stderr, "[EH] emitted call\n");
    std::fflush(stderr);
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(dst));
    entry->instructions.push_back(ret);
    entry->terminated = true;
    std::fprintf(stderr, "[EH] entry done\n");
    std::fflush(stderr);

    b.setInsertPoint(*handler);
    // Handler resumes to recover using the token; do not pop here
    Instr resume;
    resume.op = Opcode::ResumeLabel;
    resume.type = Type(Type::Kind::Void);
    resume.operands.push_back(b.blockParam(*handler, 1));
    resume.labels.push_back("recover");
    resume.brArgs.push_back({});
    handler->instructions.push_back(resume);
    handler->terminated = true;
    std::fprintf(stderr, "[EH] handler done\n");
    std::fflush(stderr);

    b.setInsertPoint(*recover);
    std::fprintf(stderr, "[EH] recover insert\n");
    std::fflush(stderr);
    std::fprintf(stderr, "[EH] recover insert\n");
    std::fflush(stderr);
    // Pop handler at recover, then return recovery value
    Instr pop;
    pop.op = Opcode::EhPop;
    pop.type = Type(Type::Kind::Void);
    recover->instructions.push_back(pop);
    std::fprintf(stderr, "[EH] recover pop added\n");
    std::fflush(stderr);
    std::fprintf(stderr, "[EH] recover pop added\n");
    std::fflush(stderr);

    Instr retok;
    retok.op = Opcode::Ret;
    retok.type = Type(Type::Kind::Void);
    retok.operands.push_back(Value::constInt(99));
    recover->instructions.push_back(retok);
    recover->terminated = true;
    std::fprintf(stderr, "[EH] recover done\n");
    std::fflush(stderr);

    std::fprintf(stderr, "[EH] module constructed\n");
    std::fflush(stderr);
    return m;
}

int main()
{
    std::fprintf(stderr, "[EH] build module\n");
    std::fflush(stderr);
    Module m = build_tco_eh_module();
    std::fprintf(stderr, "[EH] module built\n");
    std::fflush(stderr);
    il::vm::VM vm(m);
    std::fprintf(stderr, "[EH] VM constructed\n");
    std::fflush(stderr);
    auto it = std::find_if(
        m.functions.begin(), m.functions.end(), [](const Function &f) { return f.name == "main"; });
    assert(it != m.functions.end());
    std::fprintf(stderr, "[EH] found main\n");
    std::fflush(stderr);
    auto state = il::vm::VMTestHook::prepare(vm, *it);
    std::fprintf(stderr, "[EH] prepared state\n");
    std::fflush(stderr);
    while (true)
    {
        auto res = il::vm::VMTestHook::step(vm, state);
        if (res)
        {
            // Exception from callee is caught by caller and recover returns 99
            assert(res->i64 == 99);
            break;
        }
    }

    return 0;
}
