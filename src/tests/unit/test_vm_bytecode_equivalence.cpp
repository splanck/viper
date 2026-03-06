//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_bytecode_equivalence.cpp
// Purpose: Verify that the regular VM and BytecodeVM produce identical results
//          for the same IL programs.
// Key invariants:
//   - Both VMs must return the same value for every test program.
//   - Programs are built once and executed on both VMs independently.
// Ownership/Lifetime:
//   - Builds ephemeral IL modules; each test is self-contained.
// Links: vm/VM.hpp, bytecode/BytecodeVM.hpp, bytecode/BytecodeCompiler.hpp
//
//===----------------------------------------------------------------------===//

#include "bytecode/BytecodeCompiler.hpp"
#include "bytecode/BytecodeModule.hpp"
#include "bytecode/BytecodeVM.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tests/common/PosixCompat.h"
#include "vm/VM.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

using namespace il::core;
using namespace il::build;
using namespace viper::bytecode;

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

/// @brief Run a module on the regular VM (calls "main", returns i64).
static int64_t runRegularVM(const Module &m)
{
    il::vm::VM vm(m);
    return vm.run();
}

/// @brief Compile a module to bytecode and run "main" on the BytecodeVM.
static BCSlot runBytecodeVM(const Module &m)
{
    BytecodeCompiler compiler;
    BytecodeModule bcMod = compiler.compile(m);
    BytecodeVM bvm;
    bvm.load(&bcMod);
    BCSlot result = bvm.exec("main", {});
    assert(bvm.state() == VMState::Halted && "BytecodeVM did not halt cleanly");
    return result;
}

//===----------------------------------------------------------------------===//
// Test 1: Simple integer addition — (17 + 25) = 42
//===----------------------------------------------------------------------===//

static void test_add_equivalence()
{
    std::cout << "  test_add_equivalence: ";

    Module m;
    IRBuilder b(m);

    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = b.addBlock(fn, "entry");
    b.setInsertPoint(entry);

    // %0 = add 17, 25
    Instr addInstr;
    addInstr.result = b.reserveTempId();
    addInstr.op = Opcode::Add;
    addInstr.type = Type(Type::Kind::I64);
    addInstr.operands.push_back(Value::constInt(17));
    addInstr.operands.push_back(Value::constInt(25));
    addInstr.loc = {1, 1, 1};
    entry.instructions.push_back(addInstr);

    // ret %0
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*addInstr.result));
    ret.loc = {1, 1, 1};
    entry.instructions.push_back(ret);

    int64_t vmResult = runRegularVM(m);
    BCSlot bcResult = runBytecodeVM(m);

    assert(vmResult == 42 && "Regular VM: expected 42");
    assert(bcResult.i64 == 42 && "BytecodeVM: expected 42");
    assert(vmResult == bcResult.i64 && "VMs disagree on addition result");

    std::cout << "PASSED\n";
}

//===----------------------------------------------------------------------===//
// Test 2: Fibonacci — fib(10) = 55
//===----------------------------------------------------------------------===//

/// @brief Build a module with a recursive fib function called from main.
/// main returns fib(10).
static Module buildFibModule()
{
    Module m;

    // --- func @fib(i64 %0) -> i64 ---
    {
        Function fib;
        fib.name = "fib";
        fib.retType = Type(Type::Kind::I64);
        fib.params.push_back(Param{"n", Type(Type::Kind::I64), 0});

        // entry block with param %0
        BasicBlock entry;
        entry.label = "entry";
        entry.params.push_back(Param{"n", Type(Type::Kind::I64), 0});

        // %1 = scmp_le %0, 1
        Instr cmp;
        cmp.result = 1;
        cmp.op = Opcode::SCmpLE;
        cmp.type = Type(Type::Kind::I1);
        cmp.operands = {Value::temp(0), Value::constInt(1)};
        cmp.loc = {1, 1, 1};
        entry.instructions.push_back(cmp);

        // cbr %1, ^base, ^recurse
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands = {Value::temp(1)};
        cbr.labels = {"base", "recurse"};
        cbr.loc = {1, 1, 1};
        entry.instructions.push_back(cbr);
        entry.terminated = true;

        // base: ret %0
        BasicBlock base;
        base.label = "base";
        Instr retBase;
        retBase.op = Opcode::Ret;
        retBase.type = Type(Type::Kind::Void);
        retBase.operands = {Value::temp(0)};
        retBase.loc = {1, 1, 1};
        base.instructions.push_back(retBase);
        base.terminated = true;

        // recurse:
        BasicBlock recurse;
        recurse.label = "recurse";

        // %2 = sub %0, 1
        Instr nm1;
        nm1.result = 2;
        nm1.op = Opcode::Sub;
        nm1.type = Type(Type::Kind::I64);
        nm1.operands = {Value::temp(0), Value::constInt(1)};
        nm1.loc = {1, 1, 1};
        recurse.instructions.push_back(nm1);

        // %3 = call @fib(%2)
        Instr call1;
        call1.result = 3;
        call1.op = Opcode::Call;
        call1.type = Type(Type::Kind::I64);
        call1.callee = "fib";
        call1.operands = {Value::temp(2)};
        call1.loc = {1, 1, 1};
        recurse.instructions.push_back(call1);

        // %4 = sub %0, 2
        Instr nm2;
        nm2.result = 4;
        nm2.op = Opcode::Sub;
        nm2.type = Type(Type::Kind::I64);
        nm2.operands = {Value::temp(0), Value::constInt(2)};
        nm2.loc = {1, 1, 1};
        recurse.instructions.push_back(nm2);

        // %5 = call @fib(%4)
        Instr call2;
        call2.result = 5;
        call2.op = Opcode::Call;
        call2.type = Type(Type::Kind::I64);
        call2.callee = "fib";
        call2.operands = {Value::temp(4)};
        call2.loc = {1, 1, 1};
        recurse.instructions.push_back(call2);

        // %6 = add %3, %5
        Instr addFib;
        addFib.result = 6;
        addFib.op = Opcode::Add;
        addFib.type = Type(Type::Kind::I64);
        addFib.operands = {Value::temp(3), Value::temp(5)};
        addFib.loc = {1, 1, 1};
        recurse.instructions.push_back(addFib);

        // ret %6
        Instr retRec;
        retRec.op = Opcode::Ret;
        retRec.type = Type(Type::Kind::Void);
        retRec.operands = {Value::temp(6)};
        retRec.loc = {1, 1, 1};
        recurse.instructions.push_back(retRec);
        recurse.terminated = true;

        fib.blocks.push_back(std::move(entry));
        fib.blocks.push_back(std::move(base));
        fib.blocks.push_back(std::move(recurse));
        fib.valueNames.resize(7);
        m.functions.push_back(std::move(fib));
    }

    // --- func @main() -> i64 ---
    {
        Function mainFn;
        mainFn.name = "main";
        mainFn.retType = Type(Type::Kind::I64);

        BasicBlock entry;
        entry.label = "entry";

        // %0 = call @fib(10)
        Instr callFib;
        callFib.result = 0;
        callFib.op = Opcode::Call;
        callFib.type = Type(Type::Kind::I64);
        callFib.callee = "fib";
        callFib.operands = {Value::constInt(10)};
        callFib.loc = {1, 1, 1};
        entry.instructions.push_back(callFib);

        // ret %0
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(0)};
        ret.loc = {1, 1, 1};
        entry.instructions.push_back(ret);
        entry.terminated = true;

        mainFn.blocks.push_back(std::move(entry));
        mainFn.valueNames.resize(1);
        m.functions.push_back(std::move(mainFn));
    }

    return m;
}

static void test_fib_equivalence()
{
    std::cout << "  test_fib_equivalence: ";

    Module m = buildFibModule();

    int64_t vmResult = runRegularVM(m);
    BCSlot bcResult = runBytecodeVM(m);

    assert(vmResult == 55 && "Regular VM: fib(10) should be 55");
    assert(bcResult.i64 == 55 && "BytecodeVM: fib(10) should be 55");
    assert(vmResult == bcResult.i64 && "VMs disagree on fib(10)");

    std::cout << "PASSED\n";
}

//===----------------------------------------------------------------------===//
// Test 3: Conditional branching — abs(-7) = 7
//===----------------------------------------------------------------------===//

static void test_conditional_equivalence()
{
    std::cout << "  test_conditional_equivalence: ";

    Module m;
    IRBuilder b(m);

    // func @main() -> i64
    // Computes abs(-7) inline using conditional branching.
    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});

    b.addBlock(fn, "entry");
    b.addBlock(fn, "negative");
    b.addBlock(fn, "positive");

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &negative = fn.blocks[1];
    BasicBlock &positive = fn.blocks[2];

    // entry:
    b.setInsertPoint(entry);

    // %0 = add -7, 0   (load -7 into a temp)
    Instr load;
    load.result = b.reserveTempId();
    load.op = Opcode::Add;
    load.type = Type(Type::Kind::I64);
    load.operands.push_back(Value::constInt(-7));
    load.operands.push_back(Value::constInt(0));
    load.loc = {1, 1, 1};
    entry.instructions.push_back(load);

    // %1 = scmp_lt %0, 0
    Instr cmpInstr;
    cmpInstr.result = b.reserveTempId();
    cmpInstr.op = Opcode::SCmpLT;
    cmpInstr.type = Type(Type::Kind::I1);
    cmpInstr.operands.push_back(Value::temp(*load.result));
    cmpInstr.operands.push_back(Value::constInt(0));
    cmpInstr.loc = {1, 1, 1};
    entry.instructions.push_back(cmpInstr);

    // cbr %1, negative, positive
    b.cbr(Value::temp(*cmpInstr.result), negative, {}, positive, {});

    // negative: %2 = sub 0, %0; ret %2
    b.setInsertPoint(negative);
    Instr subInstr;
    subInstr.result = b.reserveTempId();
    subInstr.op = Opcode::Sub;
    subInstr.type = Type(Type::Kind::I64);
    subInstr.operands.push_back(Value::constInt(0));
    subInstr.operands.push_back(Value::temp(*load.result));
    subInstr.loc = {1, 1, 1};
    negative.instructions.push_back(subInstr);

    Instr retNeg;
    retNeg.op = Opcode::Ret;
    retNeg.type = Type(Type::Kind::Void);
    retNeg.operands.push_back(Value::temp(*subInstr.result));
    retNeg.loc = {1, 1, 1};
    negative.instructions.push_back(retNeg);

    // positive: ret %0
    b.setInsertPoint(positive);
    Instr retPos;
    retPos.op = Opcode::Ret;
    retPos.type = Type(Type::Kind::Void);
    retPos.operands.push_back(Value::temp(*load.result));
    retPos.loc = {1, 1, 1};
    positive.instructions.push_back(retPos);

    int64_t vmResult = runRegularVM(m);
    BCSlot bcResult = runBytecodeVM(m);

    assert(vmResult == 7 && "Regular VM: abs(-7) should be 7");
    assert(bcResult.i64 == 7 && "BytecodeVM: abs(-7) should be 7");
    assert(vmResult == bcResult.i64 && "VMs disagree on conditional result");

    std::cout << "PASSED\n";
}

//===----------------------------------------------------------------------===//
// Test 4: Float arithmetic — (3.5 + 2.25) * 4.0 = 23.0
// Both VMs return the float bits as int64; we compare bit patterns.
//===----------------------------------------------------------------------===//

static void test_float_equivalence()
{
    std::cout << "  test_float_equivalence: ";

    Module m;
    IRBuilder b(m);

    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = b.addBlock(fn, "entry");
    b.setInsertPoint(entry);

    // %0 = fadd 3.5, 2.25    -> 5.75
    Instr faddInstr;
    faddInstr.result = b.reserveTempId();
    faddInstr.op = Opcode::FAdd;
    faddInstr.type = Type(Type::Kind::F64);
    faddInstr.operands.push_back(Value::constFloat(3.5));
    faddInstr.operands.push_back(Value::constFloat(2.25));
    faddInstr.loc = {1, 1, 1};
    entry.instructions.push_back(faddInstr);

    // %1 = fmul %0, 4.0      -> 23.0
    Instr fmulInstr;
    fmulInstr.result = b.reserveTempId();
    fmulInstr.op = Opcode::FMul;
    fmulInstr.type = Type(Type::Kind::F64);
    fmulInstr.operands.push_back(Value::temp(*faddInstr.result));
    fmulInstr.operands.push_back(Value::constFloat(4.0));
    fmulInstr.loc = {1, 1, 1};
    entry.instructions.push_back(fmulInstr);

    // ret %1 — the F64 bits are returned as i64 by VM::run()
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*fmulInstr.result));
    ret.loc = {1, 1, 1};
    entry.instructions.push_back(ret);

    int64_t vmResult = runRegularVM(m);
    BCSlot bcResult = runBytecodeVM(m);

    // Both VMs store the F64 result in i64 storage via the Slot/BCSlot union.
    // Reinterpret the bits back to double for verification.
    double vmDouble = 0.0;
    double bcDouble = 0.0;
    std::memcpy(&vmDouble, &vmResult, sizeof(double));
    std::memcpy(&bcDouble, &bcResult.i64, sizeof(double));

    assert(vmDouble == 23.0 && "Regular VM: (3.5 + 2.25) * 4.0 should be 23.0");
    assert(bcDouble == 23.0 && "BytecodeVM: (3.5 + 2.25) * 4.0 should be 23.0");
    assert(vmResult == bcResult.i64 && "VMs disagree on float result bit pattern");

    std::cout << "PASSED\n";
}

//===----------------------------------------------------------------------===//
// Test 5: Multi-step arithmetic — ((5 * 8) - 3) + 1 = 38
//===----------------------------------------------------------------------===//

static void test_multistep_equivalence()
{
    std::cout << "  test_multistep_equivalence: ";

    Module m;
    IRBuilder b(m);

    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &entry = b.addBlock(fn, "entry");
    b.setInsertPoint(entry);

    // %0 = mul 5, 8   -> 40
    Instr mulInstr;
    mulInstr.result = b.reserveTempId();
    mulInstr.op = Opcode::Mul;
    mulInstr.type = Type(Type::Kind::I64);
    mulInstr.operands.push_back(Value::constInt(5));
    mulInstr.operands.push_back(Value::constInt(8));
    mulInstr.loc = {1, 1, 1};
    entry.instructions.push_back(mulInstr);

    // %1 = sub %0, 3  -> 37
    Instr subInstr;
    subInstr.result = b.reserveTempId();
    subInstr.op = Opcode::Sub;
    subInstr.type = Type(Type::Kind::I64);
    subInstr.operands.push_back(Value::temp(*mulInstr.result));
    subInstr.operands.push_back(Value::constInt(3));
    subInstr.loc = {1, 1, 1};
    entry.instructions.push_back(subInstr);

    // %2 = add %1, 1  -> 38
    Instr addInstr;
    addInstr.result = b.reserveTempId();
    addInstr.op = Opcode::Add;
    addInstr.type = Type(Type::Kind::I64);
    addInstr.operands.push_back(Value::temp(*subInstr.result));
    addInstr.operands.push_back(Value::constInt(1));
    addInstr.loc = {1, 1, 1};
    entry.instructions.push_back(addInstr);

    // ret %2
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*addInstr.result));
    ret.loc = {1, 1, 1};
    entry.instructions.push_back(ret);

    int64_t vmResult = runRegularVM(m);
    BCSlot bcResult = runBytecodeVM(m);

    assert(vmResult == 38 && "Regular VM: ((5*8)-3)+1 should be 38");
    assert(bcResult.i64 == 38 && "BytecodeVM: ((5*8)-3)+1 should be 38");
    assert(vmResult == bcResult.i64 && "VMs disagree on multi-step arithmetic");

    std::cout << "PASSED\n";
}

//===----------------------------------------------------------------------===//
// Main
//===----------------------------------------------------------------------===//

int main()
{
    VIPER_DISABLE_ABORT_DIALOG();
    std::cout << "Running VM vs BytecodeVM equivalence tests...\n";

    test_add_equivalence();
    test_fib_equivalence();
    test_conditional_equivalence();
    test_float_equivalence();
    test_multistep_equivalence();

    std::cout << "All VM vs BytecodeVM equivalence tests PASSED!\n";
    return 0;
}
