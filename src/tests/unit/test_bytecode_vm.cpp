// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// test_bytecode_vm.cpp - Tests for the bytecode VM
//
// Tests compilation and execution of IL programs using the bytecode VM.

#include "bytecode/BytecodeCompiler.hpp"
#include "bytecode/BytecodeModule.hpp"
#include "bytecode/BytecodeVM.hpp"
#include "il/build/IRBuilder.hpp"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace il::core;
using namespace il::build;
using namespace viper::bytecode;

using il::core::BasicBlock;

/// Create a simple addition function for testing
/// func @add(i64 %a, i64 %b) -> i64
///   entry:
///     %result = add %a, %b
///     ret %result
static Module createAddModule() {
    Module m;
    IRBuilder b(m);

    // func @add(i64 %a, i64 %b) -> i64
    auto& fn = b.startFunction("add", Type(Type::Kind::I64), {
        Param{"a", Type(Type::Kind::I64), 0},
        Param{"b", Type(Type::Kind::I64), 1}
    });

    auto& entry = b.addBlock(fn, "entry");
    b.setInsertPoint(entry);

    // %result = add %a, %b
    Instr addInstr;
    addInstr.result = b.reserveTempId();  // temp 2 (after params 0 and 1)
    addInstr.op = Opcode::Add;
    addInstr.type = Type(Type::Kind::I64);
    addInstr.operands.push_back(Value::temp(0));  // param a
    addInstr.operands.push_back(Value::temp(1));  // param b
    addInstr.loc = {1, 1, 1};
    entry.instructions.push_back(addInstr);

    // ret %result
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*addInstr.result));
    ret.loc = {1, 1, 1};
    entry.instructions.push_back(ret);

    return m;
}

/// Create a function that tests conditional branching
/// func @abs(i64 %n) -> i64
///   entry:
///     %cmp = scmp_lt %n, 0
///     cbr %cmp, negative, positive
///   negative:
///     %neg = sub 0, %n
///     ret %neg
///   positive:
///     ret %n
static Module createAbsModule() {
    Module m;
    IRBuilder b(m);

    auto& fn = b.startFunction("abs", Type(Type::Kind::I64), {
        Param{"n", Type(Type::Kind::I64), 0}
    });

    // Create all blocks first to avoid reference invalidation from vector reallocation
    b.addBlock(fn, "entry");
    b.addBlock(fn, "negative");
    b.addBlock(fn, "positive");

    // Get fresh references after all blocks are added
    BasicBlock& entry = fn.blocks[0];
    BasicBlock& negative = fn.blocks[1];
    BasicBlock& positive = fn.blocks[2];

    // Entry block
    b.setInsertPoint(entry);

    // %cmp = scmp_lt %n, 0
    Instr cmpInstr;
    cmpInstr.result = b.reserveTempId();
    cmpInstr.op = Opcode::SCmpLT;
    cmpInstr.type = Type(Type::Kind::I1);
    cmpInstr.operands.push_back(Value::temp(0));
    cmpInstr.operands.push_back(Value::constInt(0));
    cmpInstr.loc = {1, 1, 1};
    entry.instructions.push_back(cmpInstr);

    // cbr %cmp, negative, positive
    b.cbr(Value::temp(*cmpInstr.result), negative, {}, positive, {});

    // Negative block: %neg = sub 0, %n; ret %neg
    b.setInsertPoint(negative);
    Instr subInstr;
    subInstr.result = b.reserveTempId();
    subInstr.op = Opcode::Sub;
    subInstr.type = Type(Type::Kind::I64);
    subInstr.operands.push_back(Value::constInt(0));
    subInstr.operands.push_back(Value::temp(0));
    subInstr.loc = {1, 1, 1};
    negative.instructions.push_back(subInstr);

    Instr retNeg;
    retNeg.op = Opcode::Ret;
    retNeg.type = Type(Type::Kind::Void);
    retNeg.operands.push_back(Value::temp(*subInstr.result));
    retNeg.loc = {1, 1, 1};
    negative.instructions.push_back(retNeg);

    // Positive block: ret %n
    b.setInsertPoint(positive);
    Instr retPos;
    retPos.op = Opcode::Ret;
    retPos.type = Type(Type::Kind::Void);
    retPos.operands.push_back(Value::temp(0));
    retPos.loc = {1, 1, 1};
    positive.instructions.push_back(retPos);

    return m;
}

/// Create a recursive fibonacci function
/// func @fib(i64 %n) -> i64
///   entry:
///     %cmp = scmp_le %n, 1
///     cbr %cmp, base, recurse
///   base:
///     ret %n
///   recurse:
///     %nm1 = sub %n, 1
///     %fib1 = call @fib(%nm1)
///     %nm2 = sub %n, 2
///     %fib2 = call @fib(%nm2)
///     %result = add %fib1, %fib2
///     ret %result
static Module createFibModule() {
    Module m;
    IRBuilder b(m);

    auto& fn = b.startFunction("fib", Type(Type::Kind::I64), {
        Param{"n", Type(Type::Kind::I64), 0}
    });

    // Create all blocks first to avoid reference invalidation from vector reallocation
    b.addBlock(fn, "entry");
    b.addBlock(fn, "base");
    b.addBlock(fn, "recurse");

    // Get fresh references after all blocks are added
    BasicBlock& entry = fn.blocks[0];
    BasicBlock& base = fn.blocks[1];
    BasicBlock& recurse = fn.blocks[2];

    // Entry block: check if n <= 1
    b.setInsertPoint(entry);

    // %cmp = scmp_le %n, 1
    Instr cmpInstr;
    cmpInstr.result = b.reserveTempId();  // temp 1
    cmpInstr.op = Opcode::SCmpLE;
    cmpInstr.type = Type(Type::Kind::I1);
    cmpInstr.operands.push_back(Value::temp(0));  // param n
    cmpInstr.operands.push_back(Value::constInt(1));
    cmpInstr.loc = {1, 1, 1};
    entry.instructions.push_back(cmpInstr);

    // cbr %cmp, base, recurse
    b.cbr(Value::temp(*cmpInstr.result), base, {}, recurse, {});

    // Base case: return n
    b.setInsertPoint(base);
    Instr retBase;
    retBase.op = Opcode::Ret;
    retBase.type = Type(Type::Kind::Void);
    retBase.operands.push_back(Value::temp(0));  // Return param n
    retBase.loc = {1, 1, 1};
    base.instructions.push_back(retBase);

    // Recursive case
    b.setInsertPoint(recurse);

    // %nm1 = sub %n, 1
    Instr nm1Instr;
    nm1Instr.result = b.reserveTempId();
    nm1Instr.op = Opcode::Sub;
    nm1Instr.type = Type(Type::Kind::I64);
    nm1Instr.operands.push_back(Value::temp(0));
    nm1Instr.operands.push_back(Value::constInt(1));
    nm1Instr.loc = {1, 1, 1};
    recurse.instructions.push_back(nm1Instr);

    // %fib1 = call @fib(%nm1)
    Instr call1;
    call1.result = b.reserveTempId();
    call1.op = Opcode::Call;
    call1.type = Type(Type::Kind::I64);
    call1.callee = "fib";
    call1.operands.push_back(Value::temp(*nm1Instr.result));
    call1.loc = {1, 1, 1};
    recurse.instructions.push_back(call1);

    // %nm2 = sub %n, 2
    Instr nm2Instr;
    nm2Instr.result = b.reserveTempId();
    nm2Instr.op = Opcode::Sub;
    nm2Instr.type = Type(Type::Kind::I64);
    nm2Instr.operands.push_back(Value::temp(0));
    nm2Instr.operands.push_back(Value::constInt(2));
    nm2Instr.loc = {1, 1, 1};
    recurse.instructions.push_back(nm2Instr);

    // %fib2 = call @fib(%nm2)
    Instr call2;
    call2.result = b.reserveTempId();
    call2.op = Opcode::Call;
    call2.type = Type(Type::Kind::I64);
    call2.callee = "fib";
    call2.operands.push_back(Value::temp(*nm2Instr.result));
    call2.loc = {1, 1, 1};
    recurse.instructions.push_back(call2);

    // %result = add %fib1, %fib2
    Instr addInstr;
    addInstr.result = b.reserveTempId();
    addInstr.op = Opcode::Add;
    addInstr.type = Type(Type::Kind::I64);
    addInstr.operands.push_back(Value::temp(*call1.result));
    addInstr.operands.push_back(Value::temp(*call2.result));
    addInstr.loc = {1, 1, 1};
    recurse.instructions.push_back(addInstr);

    // ret %result
    Instr retRecurse;
    retRecurse.op = Opcode::Ret;
    retRecurse.type = Type(Type::Kind::Void);
    retRecurse.operands.push_back(Value::temp(*addInstr.result));
    retRecurse.loc = {1, 1, 1};
    recurse.instructions.push_back(retRecurse);

    return m;
}

/// Test basic bytecode encoding/decoding
static void test_bytecode_encoding() {
    std::cout << "  test_bytecode_encoding: ";

    // Test encodeOp8 / decodeArg8_0
    uint32_t instr = encodeOp8(BCOpcode::LOAD_LOCAL, 42);
    assert(decodeOpcode(instr) == BCOpcode::LOAD_LOCAL);
    assert(decodeArg8_0(instr) == 42);

    // Test encodeOp16 / decodeArg16
    instr = encodeOp16(BCOpcode::CALL, 1234);
    assert(decodeOpcode(instr) == BCOpcode::CALL);
    assert(decodeArg16(instr) == 1234);

    // Test encodeOpI16 / decodeArgI16 with negative value
    instr = encodeOpI16(BCOpcode::JUMP, -100);
    assert(decodeOpcode(instr) == BCOpcode::JUMP);
    assert(decodeArgI16(instr) == -100);

    // Test encodeOpI24 / decodeArgI24
    instr = encodeOpI24(BCOpcode::JUMP_LONG, -10000);
    assert(decodeOpcode(instr) == BCOpcode::JUMP_LONG);
    assert(decodeArgI24(instr) == -10000);

    std::cout << "PASSED\n";
}

/// Test basic addition function
static void test_add_function() {
    std::cout << "  test_add_function: ";

    // Create IL module with add function
    Module ilModule = createAddModule();

    // Compile to bytecode
    BytecodeCompiler compiler;
    BytecodeModule bcModule = compiler.compile(ilModule);

    // Verify compilation
    assert(bcModule.functions.size() == 1);
    assert(bcModule.functions[0].name == "add");
    assert(bcModule.functions[0].numParams == 2);

    // Execute
    BytecodeVM vm;
    vm.load(&bcModule);

    BCSlot result = vm.exec("add", {BCSlot::fromInt(3), BCSlot::fromInt(5)});
    assert(vm.state() == VMState::Halted);
    assert(result.i64 == 8);

    // Test with different values
    result = vm.exec("add", {BCSlot::fromInt(100), BCSlot::fromInt(-50)});
    assert(result.i64 == 50);

    std::cout << "PASSED\n";
}

/// Test absolute value function (conditional branching)
static void test_abs_function() {
    std::cout << "  test_abs_function: ";

    // Create IL module with abs function
    Module ilModule = createAbsModule();

    // Compile to bytecode
    BytecodeCompiler compiler;
    BytecodeModule bcModule = compiler.compile(ilModule);

    // Verify compilation
    assert(bcModule.functions.size() == 1);
    assert(bcModule.functions[0].name == "abs");

    // Execute
    BytecodeVM vm;
    vm.load(&bcModule);

    // Test positive
    BCSlot result = vm.exec("abs", {BCSlot::fromInt(5)});
    assert(vm.state() == VMState::Halted);
    assert(result.i64 == 5);

    // Test negative
    result = vm.exec("abs", {BCSlot::fromInt(-10)});
    assert(vm.state() == VMState::Halted);
    assert(result.i64 == 10);

    // Test zero
    result = vm.exec("abs", {BCSlot::fromInt(0)});
    assert(vm.state() == VMState::Halted);
    assert(result.i64 == 0);

    std::cout << "PASSED\n";
}

/// Test fibonacci function (small values)
static void test_fib_small() {
    std::cout << "  test_fib_small: ";

    // Create IL module with fib function
    Module ilModule = createFibModule();

    // Compile to bytecode
    BytecodeCompiler compiler;
    BytecodeModule bcModule = compiler.compile(ilModule);

    // Verify compilation
    assert(bcModule.functions.size() == 1);
    assert(bcModule.functions[0].name == "fib");

    // Execute
    BytecodeVM vm;
    vm.load(&bcModule);

    // Test known fibonacci values
    // fib(0) = 0, fib(1) = 1, fib(2) = 1, fib(3) = 2, fib(4) = 3, fib(5) = 5
    // fib(6) = 8, fib(7) = 13, fib(8) = 21, fib(9) = 34, fib(10) = 55
    int64_t expected[] = {0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55};

    for (int i = 0; i <= 10; ++i) {
        BCSlot result = vm.exec("fib", {BCSlot::fromInt(i)});
        if (vm.state() != VMState::Halted) {
            std::cerr << "fib(" << i << ") failed with trap: "
                      << vm.trapMessage() << "\n";
            assert(false);
        }
        if (result.i64 != expected[i]) {
            std::cerr << "fib(" << i << ") = " << result.i64
                      << ", expected " << expected[i] << "\n";
            assert(false);
        }
    }

    std::cout << "PASSED\n";
}

/// Benchmark fibonacci function
static void test_fib_benchmark() {
    std::cout << "  test_fib_benchmark: ";

    // Create IL module with fib function
    Module ilModule = createFibModule();

    // Compile to bytecode
    BytecodeCompiler compiler;
    BytecodeModule bcModule = compiler.compile(ilModule);

    // Execute
    BytecodeVM vm;
    vm.load(&bcModule);

    // Benchmark fib(20)
    auto start = std::chrono::high_resolution_clock::now();
    BCSlot result = vm.exec("fib", {BCSlot::fromInt(20)});
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    assert(vm.state() == VMState::Halted);
    assert(result.i64 == 6765);  // fib(20) = 6765

    std::cout << "fib(20)=" << result.i64
              << " in " << duration.count() << "ms"
              << " (" << vm.instrCount() << " instructions)"
              << " PASSED\n";
}

/// Test native function calls
/// Creates a function that calls a native "square" function
static void test_native_calls() {
    std::cout << "  test_native_calls: ";

    // Build bytecode module manually
    BytecodeModule bcModule;
    bcModule.magic = kBytecodeModuleMagic;
    bcModule.version = kBytecodeVersion;
    bcModule.flags = 0;

    // Add native function reference: square(i64) -> i64
    uint32_t nativeIdx = bcModule.addNativeFunc("square", 1, true);
    assert(nativeIdx == 0);

    // Create a function that calls the native function:
    // func @call_square(i64 %n) -> i64
    //   %result = call_native @square(%n)
    //   ret %result
    BytecodeFunction func;
    func.name = "call_square";
    func.numParams = 1;
    func.numLocals = 2;  // 1 param + 1 result
    func.maxStack = 2;

    // LOAD_LOCAL 0       ; push %n
    func.code.push_back(encodeOp8(BCOpcode::LOAD_LOCAL, 0));
    // CALL_NATIVE 0, 1   ; call native[0] with 1 arg
    func.code.push_back(encodeOp88(BCOpcode::CALL_NATIVE, 0, 1));
    // STORE_LOCAL 1      ; store result to local[1]
    func.code.push_back(encodeOp8(BCOpcode::STORE_LOCAL, 1));
    // LOAD_LOCAL 1       ; push result
    func.code.push_back(encodeOp8(BCOpcode::LOAD_LOCAL, 1));
    // RET
    func.code.push_back(encodeOp(BCOpcode::RETURN));

    bcModule.functions.push_back(std::move(func));
    bcModule.functionIndex["call_square"] = 0;

    // Create VM and register native handler
    BytecodeVM vm;
    vm.registerNativeHandler("square", [](BCSlot* args, uint32_t argCount, BCSlot* result) {
        assert(argCount == 1);
        int64_t n = args[0].i64;
        result->i64 = n * n;
    });

    vm.load(&bcModule);

    // Test with several values
    BCSlot result = vm.exec("call_square", {BCSlot::fromInt(5)});
    assert(vm.state() == VMState::Halted);
    assert(result.i64 == 25);  // 5^2 = 25

    result = vm.exec("call_square", {BCSlot::fromInt(10)});
    assert(result.i64 == 100);  // 10^2 = 100

    result = vm.exec("call_square", {BCSlot::fromInt(-7)});
    assert(result.i64 == 49);  // (-7)^2 = 49

    std::cout << "PASSED\n";
}

/// Benchmark comparing switch vs threaded dispatch
static void test_dispatch_benchmark() {
    std::cout << "  test_dispatch_benchmark:\n";

    // Create IL module with fib function
    Module ilModule = createFibModule();

    // Compile to bytecode
    BytecodeCompiler compiler;
    BytecodeModule bcModule = compiler.compile(ilModule);

    // Benchmark with threaded dispatch (default)
    {
        BytecodeVM vm;
        vm.setThreadedDispatch(true);
        vm.load(&bcModule);

        auto start = std::chrono::high_resolution_clock::now();
        BCSlot result = vm.exec("fib", {BCSlot::fromInt(25)});
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        assert(vm.state() == VMState::Halted);
        assert(result.i64 == 75025);  // fib(25) = 75025

        std::cout << "    threaded: fib(25)=" << result.i64
                  << " in " << duration.count() << "us"
                  << " (" << vm.instrCount() << " instrs)\n";
    }

    // Benchmark with switch dispatch
    {
        BytecodeVM vm;
        vm.setThreadedDispatch(false);
        vm.load(&bcModule);

        auto start = std::chrono::high_resolution_clock::now();
        BCSlot result = vm.exec("fib", {BCSlot::fromInt(25)});
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        assert(vm.state() == VMState::Halted);
        assert(result.i64 == 75025);  // fib(25) = 75025

        std::cout << "    switch:   fib(25)=" << result.i64
                  << " in " << duration.count() << "us"
                  << " (" << vm.instrCount() << " instrs)\n";
    }

    std::cout << "    PASSED\n";
}

/// Test a native function that takes multiple arguments
static void test_native_multi_args() {
    std::cout << "  test_native_multi_args: ";

    BytecodeModule bcModule;
    bcModule.magic = kBytecodeModuleMagic;
    bcModule.version = kBytecodeVersion;
    bcModule.flags = 0;

    // Add native function: add3(i64, i64, i64) -> i64
    uint32_t nativeIdx = bcModule.addNativeFunc("add3", 3, true);
    assert(nativeIdx == 0);

    // func @call_add3(i64 %a, i64 %b, i64 %c) -> i64
    //   %result = call_native @add3(%a, %b, %c)
    //   ret %result
    BytecodeFunction func;
    func.name = "call_add3";
    func.numParams = 3;
    func.numLocals = 4;  // 3 params + 1 result
    func.maxStack = 4;

    // Push args in order
    func.code.push_back(encodeOp8(BCOpcode::LOAD_LOCAL, 0));  // push %a
    func.code.push_back(encodeOp8(BCOpcode::LOAD_LOCAL, 1));  // push %b
    func.code.push_back(encodeOp8(BCOpcode::LOAD_LOCAL, 2));  // push %c
    // CALL_NATIVE 0, 3
    func.code.push_back(encodeOp88(BCOpcode::CALL_NATIVE, 0, 3));
    // RET (result is on stack)
    func.code.push_back(encodeOp(BCOpcode::RETURN));

    bcModule.functions.push_back(std::move(func));
    bcModule.functionIndex["call_add3"] = 0;

    BytecodeVM vm;
    vm.registerNativeHandler("add3", [](BCSlot* args, uint32_t argCount, BCSlot* result) {
        assert(argCount == 3);
        result->i64 = args[0].i64 + args[1].i64 + args[2].i64;
    });

    vm.load(&bcModule);

    BCSlot result = vm.exec("call_add3", {BCSlot::fromInt(10), BCSlot::fromInt(20), BCSlot::fromInt(30)});
    assert(vm.state() == VMState::Halted);
    assert(result.i64 == 60);  // 10 + 20 + 30

    result = vm.exec("call_add3", {BCSlot::fromInt(1), BCSlot::fromInt(2), BCSlot::fromInt(3)});
    assert(result.i64 == 6);

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "Running bytecode VM tests...\n";

    test_bytecode_encoding();
    test_add_function();
    test_abs_function();
    test_fib_small();
    test_fib_benchmark();
    test_dispatch_benchmark();
    test_native_calls();
    test_native_multi_args();

    std::cout << "All bytecode VM tests PASSED!\n";
    return 0;
}
