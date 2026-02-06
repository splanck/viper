//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/TcoStringRetainTests.cpp
// Purpose: Verify that tail-call optimization correctly handles string
//          retain/release ordering to prevent use-after-free when args alias
//          the frame's parameter slots (self-assignment during tail call).
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdint>

using namespace il::core;

namespace
{

/// Build a module with a recursive function that passes its string parameter
/// back to itself via tail call. This tests the self-assignment case where
/// args[i] aliases fr.params[id].
Module buildTcoStringSelfAssignModule()
{
    Module module;
    il::build::IRBuilder builder(module);

    // countdown(n: i32, s: str) -> i32
    {
        std::vector<Param> countdownParams;
        countdownParams.push_back(Param{"n", Type(Type::Kind::I32), 0});
        countdownParams.push_back(Param{"s", Type(Type::Kind::Str), 1});

        Function &fn = builder.startFunction(
            "countdown",
            Type(Type::Kind::I32),
            countdownParams);

        // Create all blocks first to avoid reference invalidation from vector realloc
        builder.createBlock(fn, "entry", countdownParams);
        builder.addBlock(fn, "recurse");
        builder.addBlock(fn, "done");

        // Access blocks by index after all are created
        auto &countEntry = fn.blocks[0];
        auto &recurse = fn.blocks[1];
        auto &done = fn.blocks[2];

        // entry: check if n == 0
        builder.setInsertPoint(countEntry);
        Instr cmp;
        cmp.result = builder.reserveTempId();
        cmp.op = Opcode::ICmpEq;
        cmp.type = Type(Type::Kind::I1);
        cmp.operands.push_back(builder.blockParam(countEntry, 0)); // n
        cmp.operands.push_back(Value::constInt(0));
        cmp.loc = {1, 1, 0};
        countEntry.instructions.push_back(cmp);

        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands.push_back(Value::temp(*cmp.result));
        cbr.labels.push_back("done");
        cbr.labels.push_back("recurse");
        cbr.brArgs.push_back({}); // done: no args
        cbr.brArgs.push_back({}); // recurse: no args
        cbr.loc = {1, 2, 0};
        countEntry.instructions.push_back(cbr);
        countEntry.terminated = true;

        // recurse: call countdown(n-1, s)
        builder.setInsertPoint(recurse);
        Instr sub;
        sub.result = builder.reserveTempId();
        sub.op = Opcode::Sub;
        sub.type = Type(Type::Kind::I32);
        sub.operands.push_back(builder.blockParam(countEntry, 0)); // n
        sub.operands.push_back(Value::constInt(1));
        sub.loc = {1, 3, 0};
        recurse.instructions.push_back(sub);

        Instr callSelf;
        callSelf.result = builder.reserveTempId();
        callSelf.op = Opcode::Call;
        callSelf.type = Type(Type::Kind::I32);
        callSelf.callee = "countdown";
        callSelf.operands.push_back(Value::temp(*sub.result));
        callSelf.operands.push_back(builder.blockParam(countEntry, 1)); // s - same string param
        callSelf.loc = {1, 4, 0};
        recurse.instructions.push_back(callSelf);

        Instr retRecurse;
        retRecurse.op = Opcode::Ret;
        retRecurse.type = Type(Type::Kind::Void);
        retRecurse.operands.push_back(Value::temp(*callSelf.result));
        retRecurse.loc = {1, 5, 0};
        recurse.instructions.push_back(retRecurse);
        recurse.terminated = true;

        // done: return 42 (success sentinel)
        builder.setInsertPoint(done);
        Instr retDone;
        retDone.op = Opcode::Ret;
        retDone.type = Type(Type::Kind::Void);
        retDone.operands.push_back(Value::constInt(42));
        retDone.loc = {1, 6, 0};
        done.instructions.push_back(retDone);
        done.terminated = true;
    }

    // main() -> i32 : calls countdown(5, "hello")
    {
        Function &fn = builder.startFunction("main", Type(Type::Kind::I32), {});
        BasicBlock &entry = builder.addBlock(fn, "entry");
        builder.setInsertPoint(entry);

        Instr call;
        call.result = builder.reserveTempId();
        call.op = Opcode::Call;
        call.type = Type(Type::Kind::I32);
        call.callee = "countdown";
        call.operands.push_back(Value::constInt(5));
        call.operands.push_back(Value::constStr("hello"));
        call.loc = {2, 1, 0};
        entry.instructions.push_back(call);

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(*call.result));
        ret.loc = {2, 2, 0};
        entry.instructions.push_back(ret);
        entry.terminated = true;
    }

    return module;
}

} // namespace

int main()
{
    // This test exercises the TCO path where a string parameter is passed
    // back to the same function. With incorrect retain/release ordering,
    // the string would be freed before being retained, causing a dangling
    // pointer or crash.
    {
        Module module = buildTcoStringSelfAssignModule();
        il::vm::VM vm(module);
        const int64_t result = vm.run();
        assert(result == 42 && "TCO string self-assignment should not crash");
    }

    return 0;
}
