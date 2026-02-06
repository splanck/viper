//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/BoundsCheckNormalizeTests.cpp
// Purpose: Verify that performBoundsCheck returns normalized (zero-based) indices
//          when the lower bound is non-zero.
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "vm/Trap.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdint>

using namespace il::core;

namespace
{

/// Build a module that runs idx.chk with the given index, lower, and upper bounds.
/// Returns the normalized index on success.
Module buildBoundsCheckModule(int64_t idx, int64_t lo, int64_t hi)
{
    Module module;
    il::build::IRBuilder builder(module);

    auto &fn = builder.startFunction("main", Type(Type::Kind::I32), {});
    builder.addBlock(fn, "entry");
    builder.addBlock(fn, "body");
    builder.createBlock(
        fn,
        "handler",
        {Param{"err", Type(Type::Kind::Error), 0}, Param{"tok", Type(Type::Kind::ResumeTok), 0}});

    auto &entry = fn.blocks[0];
    auto &body = fn.blocks[1];
    auto &handler = fn.blocks[2];

    // Entry: push handler and branch to body
    builder.setInsertPoint(entry);
    Instr push;
    push.op = Opcode::EhPush;
    push.type = Type(Type::Kind::Void);
    push.labels.push_back("handler");
    push.loc = {1, 1, 0};
    entry.instructions.push_back(push);
    builder.br(body);
    entry.terminated = true;

    // Body: idx.chk with lo and hi
    builder.setInsertPoint(body);
    Instr chk;
    chk.result = builder.reserveTempId();
    chk.op = Opcode::IdxChk;
    chk.type = Type(Type::Kind::I32);
    chk.operands.push_back(Value::constInt(idx));
    chk.operands.push_back(Value::constInt(lo));
    chk.operands.push_back(Value::constInt(hi));
    chk.loc = {1, 10, 0};
    body.instructions.push_back(chk);

    Instr retBody;
    retBody.op = Opcode::Ret;
    retBody.type = Type(Type::Kind::Void);
    retBody.operands.push_back(Value::temp(*chk.result));
    retBody.loc = {1, 11, 0};
    body.instructions.push_back(retBody);
    body.terminated = true;

    // Handler: return -1 to indicate trap
    builder.setInsertPoint(handler);
    Instr retHandler;
    retHandler.op = Opcode::Ret;
    retHandler.type = Type(Type::Kind::Void);
    retHandler.operands.push_back(Value::constInt(-1));
    retHandler.loc = {1, 20, 0};
    handler.instructions.push_back(retHandler);
    handler.terminated = true;

    return module;
}

int64_t runBoundsCheck(int64_t idx, int64_t lo, int64_t hi)
{
    Module module = buildBoundsCheckModule(idx, lo, hi);
    il::vm::VM vm(module);
    return vm.run();
}

} // namespace

int main()
{
    // Test 1: Zero-based array (lo=0): idx=7 in [0,10) -> normalized=7
    assert(runBoundsCheck(7, 0, 10) == 7);

    // Test 2: Non-zero lower bound: idx=12 in [10,20) -> normalized=2
    assert(runBoundsCheck(12, 10, 20) == 2);

    // Test 3: Non-zero lower bound: idx=10 in [10,20) -> normalized=0
    assert(runBoundsCheck(10, 10, 20) == 0);

    // Test 4: Non-zero lower bound: idx=19 in [10,20) -> normalized=9
    assert(runBoundsCheck(19, 10, 20) == 9);

    // Test 5: Negative lower bound: idx=0 in [-5,5) -> normalized=5
    assert(runBoundsCheck(0, -5, 5) == 5);

    // Test 6: Negative lower bound: idx=-5 in [-5,5) -> normalized=0
    assert(runBoundsCheck(-5, -5, 5) == 0);

    // Test 7: Out of bounds (should trap, handler returns -1)
    assert(runBoundsCheck(20, 10, 20) == -1);

    // Test 8: Out of bounds below (should trap, handler returns -1)
    assert(runBoundsCheck(9, 10, 20) == -1);

    // Test 9: Single-element range: idx=5 in [5,6) -> normalized=0
    assert(runBoundsCheck(5, 5, 6) == 0);

    return 0;
}
