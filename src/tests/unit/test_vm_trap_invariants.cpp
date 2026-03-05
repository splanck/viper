//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_trap_invariants.cpp
// Purpose: Verify VM trap invariants including:
//          - Trap category diagnostics (divide-by-zero, overflow, etc.)
//          - IP, line number, and message correctness
//          - Exception handler integration
// Key invariants: Trap metadata accurately reflects the fault location.
// Ownership/Lifetime: Test constructs IL modules and executes VM.
// Links: vm/TrapInvariants.hpp
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cstdio>
#include <string>

using namespace il::core;
using viper::tests::VmFixture;

//===----------------------------------------------------------------------===//
// Test: Trap instruction produces correct diagnostics (DomainError)
//===----------------------------------------------------------------------===//

static int testTrapInstruction()
{
    Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);

    Instr trap;
    trap.op = Opcode::Trap;
    trap.type = Type(Type::Kind::Void);
    trap.loc = {1, 42, 1}; // {file_id, line, column}
    bb.instructions.push_back(trap);

    // ret is unreachable but required for well-formed IL
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(0));
    bb.instructions.push_back(ret);

    VmFixture fixture;
    const std::string out = fixture.captureTrap(m);

    if (out.find("line 42") == std::string::npos)
    {
        std::fprintf(stderr, "FAIL testTrapInstruction: missing 'line 42' in: %s\n", out.c_str());
        return 1;
    }
    if (out.find("DomainError") == std::string::npos)
    {
        std::fprintf(stderr, "FAIL testTrapInstruction: missing 'DomainError' in: %s\n",
                     out.c_str());
        return 1;
    }

    std::printf("  testTrapInstruction: PASSED\n");
    return 0;
}

//===----------------------------------------------------------------------===//
// Test: Division by zero produces correct trap kind
//===----------------------------------------------------------------------===//

static int testDivideByZeroTrap()
{
    Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);

    // %0 = const 10
    Instr i0;
    i0.op = Opcode::Add;
    i0.result = 0;
    i0.type = Type(Type::Kind::I64);
    i0.operands.push_back(Value::constInt(10));
    i0.operands.push_back(Value::constInt(0));
    bb.instructions.push_back(i0);

    // %1 = const 0
    Instr i1;
    i1.op = Opcode::Add;
    i1.result = 1;
    i1.type = Type(Type::Kind::I64);
    i1.operands.push_back(Value::constInt(0));
    i1.operands.push_back(Value::constInt(0));
    bb.instructions.push_back(i1);

    // %2 = sdiv.chk0 %0, %1  (should trap on divide by zero)
    Instr div;
    div.op = Opcode::SDivChk0;
    div.result = 2;
    div.type = Type(Type::Kind::I64);
    div.operands.push_back(Value::temp(0));
    div.operands.push_back(Value::temp(1));
    div.loc = {1, 100, 1}; // {file_id, line, column}
    bb.instructions.push_back(div);

    // ret %2
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(2));
    bb.instructions.push_back(ret);

    VmFixture fixture;
    const std::string out = fixture.captureTrap(m);

    if (out.find("DivideByZero") == std::string::npos)
    {
        std::fprintf(stderr, "FAIL testDivideByZeroTrap: missing 'DivideByZero' in: %s\n",
                     out.c_str());
        return 1;
    }
    if (out.find("line 100") == std::string::npos)
    {
        std::fprintf(stderr, "FAIL testDivideByZeroTrap: missing 'line 100' in: %s\n",
                     out.c_str());
        return 1;
    }

    std::printf("  testDivideByZeroTrap: PASSED\n");
    return 0;
}

//===----------------------------------------------------------------------===//
// Test: Overflow trap produces correct kind
//===----------------------------------------------------------------------===//

static int testOverflowTrap()
{
    Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);

    // %0 = const INT64_MAX
    Instr i0;
    i0.op = Opcode::Add;
    i0.result = 0;
    i0.type = Type(Type::Kind::I64);
    i0.operands.push_back(Value::constInt(INT64_MAX));
    i0.operands.push_back(Value::constInt(0));
    bb.instructions.push_back(i0);

    // %1 = const 1
    Instr i1;
    i1.op = Opcode::Add;
    i1.result = 1;
    i1.type = Type(Type::Kind::I64);
    i1.operands.push_back(Value::constInt(1));
    i1.operands.push_back(Value::constInt(0));
    bb.instructions.push_back(i1);

    // %2 = iadd.ovf %0, %1  (should trap on overflow)
    Instr add;
    add.op = Opcode::IAddOvf;
    add.result = 2;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::temp(0));
    add.operands.push_back(Value::temp(1));
    add.loc = {1, 200, 1}; // {file_id, line, column}
    bb.instructions.push_back(add);

    // ret %2
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(2));
    bb.instructions.push_back(ret);

    VmFixture fixture;
    const std::string out = fixture.captureTrap(m);

    if (out.find("Overflow") == std::string::npos)
    {
        std::fprintf(stderr, "FAIL testOverflowTrap: missing 'Overflow' in: %s\n", out.c_str());
        return 1;
    }
    if (out.find("line 200") == std::string::npos)
    {
        std::fprintf(stderr, "FAIL testOverflowTrap: missing 'line 200' in: %s\n", out.c_str());
        return 1;
    }

    std::printf("  testOverflowTrap: PASSED\n");
    return 0;
}

//===----------------------------------------------------------------------===//
// Test: Bounds check trap (idx.chk)
//===----------------------------------------------------------------------===//

static int testBoundsTrap()
{
    Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);

    // %0 = const 10 (index)
    Instr i0;
    i0.op = Opcode::Add;
    i0.result = 0;
    i0.type = Type(Type::Kind::I64);
    i0.operands.push_back(Value::constInt(10));
    i0.operands.push_back(Value::constInt(0));
    bb.instructions.push_back(i0);

    // %1 = const 5 (length - index >= length, so out of bounds)
    Instr i1;
    i1.op = Opcode::Add;
    i1.result = 1;
    i1.type = Type(Type::Kind::I64);
    i1.operands.push_back(Value::constInt(5));
    i1.operands.push_back(Value::constInt(0));
    bb.instructions.push_back(i1);

    // %2 = idx.chk %0, lo=0, hi=%1  (index 10 not in [0,5), should trap)
    Instr chk;
    chk.op = Opcode::IdxChk;
    chk.result = 2;
    chk.type = Type(Type::Kind::I64);
    chk.operands.push_back(Value::temp(0));
    chk.operands.push_back(Value::constInt(0));
    chk.operands.push_back(Value::temp(1));
    chk.loc = {1, 300, 1}; // {file_id, line, column}
    bb.instructions.push_back(chk);

    // ret %2
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(2));
    bb.instructions.push_back(ret);

    VmFixture fixture;
    const std::string out = fixture.captureTrap(m);

    if (out.find("Bounds") == std::string::npos)
    {
        std::fprintf(stderr, "FAIL testBoundsTrap: missing 'Bounds' in: %s\n", out.c_str());
        return 1;
    }
    if (out.find("line 300") == std::string::npos)
    {
        std::fprintf(stderr, "FAIL testBoundsTrap: missing 'line 300' in: %s\n", out.c_str());
        return 1;
    }

    std::printf("  testBoundsTrap: PASSED\n");
    return 0;
}

//===----------------------------------------------------------------------===//
// Test: Successful execution produces no trap
//===----------------------------------------------------------------------===//

static int testSuccessfulExecutionNoTrap()
{
    Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);

    Instr i0;
    i0.op = Opcode::Add;
    i0.result = 0;
    i0.type = Type(Type::Kind::I64);
    i0.operands.push_back(Value::constInt(42));
    i0.operands.push_back(Value::constInt(0));
    bb.instructions.push_back(i0);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(0));
    bb.instructions.push_back(ret);

    VmFixture fixture;
    int64_t result = fixture.run(m);
    if (result != 42)
    {
        std::fprintf(stderr, "FAIL testSuccessfulExecutionNoTrap: expected 42, got %lld\n",
                     static_cast<long long>(result));
        return 1;
    }

    std::printf("  testSuccessfulExecutionNoTrap: PASSED\n");
    return 0;
}

//===----------------------------------------------------------------------===//
// Main
//===----------------------------------------------------------------------===//

int main()
{
    std::printf("Running trap invariant tests...\n");

    int failures = 0;
    failures += testTrapInstruction();
    failures += testDivideByZeroTrap();
    failures += testOverflowTrap();
    failures += testBoundsTrap();
    failures += testSuccessfulExecutionNoTrap();

    if (failures > 0)
    {
        std::fprintf(stderr, "%d trap invariant test(s) FAILED.\n", failures);
        return 1;
    }
    std::printf("All trap invariant tests passed.\n");
    return 0;
}
