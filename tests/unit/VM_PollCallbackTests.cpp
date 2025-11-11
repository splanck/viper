// File: tests/unit/VM_PollCallbackTests.cpp
// Purpose: Verify periodic host callback invocation and pause behaviour.
// Invariants: Default config (N=0 or null callback) incurs no pauses.

#include "il/build/IRBuilder.hpp"
#include "viper/vm/VM.hpp"

#include <cassert>
#include <iostream>

using il::vm::Runner;
using il::vm::RunConfig;

static il::core::Module makeTrivialModule()
{
    using namespace il::core;
    Module m;
    il::build::IRBuilder b(m);
    Function &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    BasicBlock &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);

    unsigned t0 = b.reserveTempId();
    Instr add;
    add.result = t0;
    add.op = Opcode::Add;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::constInt(1));
    add.operands.push_back(Value::constInt(2));
    bb.instructions.push_back(add);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(t0));
    bb.instructions.push_back(ret);
    bb.terminated = true;
    return m;
}

int main()
{
    // Case 1: Default config -> runs to completion.
    {
        auto module = makeTrivialModule();
        Runner r(module, {});
        auto status = r.continueRun();
        assert(status == Runner::RunStatus::Halted);
    }

    // Case 2: Poll every instruction, return false on first callback -> Paused.
    {
        auto module = makeTrivialModule();
        RunConfig cfg{};
        cfg.interruptEveryN = 1;
        int calls = 0;
        cfg.pollCallback = [&calls](il::vm::VM &) {
            std::cerr << "poll #" << (calls + 1) << "\n";
            ++calls;
            return false; // request pause
        };
        Runner r(module, cfg);
        auto status = r.continueRun();
        assert(status == Runner::RunStatus::Paused);
        assert(calls >= 1);
    }

    return 0;
}
