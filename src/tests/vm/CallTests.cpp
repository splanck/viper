//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/CallTests.cpp
// Purpose: Validate VM handler for direct function call opcode (Call).
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

using namespace il::core;

namespace
{
// Build a module with a helper function and main that calls it
void buildCallModule(Module &module, int64_t arg)
{
    il::build::IRBuilder builder(module);

    // Build helper function first (so emitCall can find its return type)
    std::vector<Param> helperParams;
    helperParams.push_back(Param{"x", Type(Type::Kind::I64), 0});

    // Build helper function completely first (to avoid nextTemp confusion)
    Function &helperFn = builder.startFunction("helper", Type(Type::Kind::I64), helperParams);
    BasicBlock &helperBB = builder.createBlock(helperFn, "entry", helperParams);
    builder.setInsertPoint(helperBB);

    // helper(x) returns x * 2
    Instr mul;
    mul.result = builder.reserveTempId();
    mul.op = Opcode::Mul;
    mul.type = Type(Type::Kind::I64);
    mul.operands.push_back(builder.blockParam(helperBB, 0)); // Parameter x
    mul.operands.push_back(Value::constInt(2));
    mul.loc = {1, 1, 1};
    helperBB.instructions.push_back(mul);

    Instr helperRet;
    helperRet.op = Opcode::Ret;
    helperRet.type = Type(Type::Kind::Void);
    helperRet.loc = {1, 1, 1};
    helperRet.operands.push_back(Value::temp(*mul.result));
    helperBB.instructions.push_back(helperRet);

    // Now build main function
    Function &mainFn = builder.startFunction("main", Type(Type::Kind::I64), {});
    BasicBlock &mainBB = builder.addBlock(mainFn, "entry");
    builder.setInsertPoint(mainBB);

    // Call helper with argument
    const auto callResult = builder.reserveTempId();
    builder.emitCall("helper", {Value::constInt(arg)}, Value::temp(callResult), {1, 1, 1});

    Instr mainRet;
    mainRet.op = Opcode::Ret;
    mainRet.type = Type(Type::Kind::Void);
    mainRet.loc = {1, 1, 1};
    mainRet.operands.push_back(Value::temp(callResult));
    mainBB.instructions.push_back(mainRet);
}

// Build a module with multiple arguments
void buildMultiArgCallModule(Module &module, int64_t a, int64_t b, int64_t c)
{
    il::build::IRBuilder builder(module);

    // Build sum3 function first (so emitCall can find its return type)
    std::vector<Param> sum3Params;
    sum3Params.push_back(Param{"a", Type(Type::Kind::I64), 0});
    sum3Params.push_back(Param{"b", Type(Type::Kind::I64), 1});
    sum3Params.push_back(Param{"c", Type(Type::Kind::I64), 2});
    Function &sum3Fn = builder.startFunction("sum3", Type(Type::Kind::I64), sum3Params);
    BasicBlock &sum3BB = builder.createBlock(sum3Fn, "entry", sum3Params);
    builder.setInsertPoint(sum3BB);

    // sum3(a, b, c) returns a + b + c
    Instr add1;
    add1.result = builder.reserveTempId();
    add1.op = Opcode::Add;
    add1.type = Type(Type::Kind::I64);
    add1.operands.push_back(builder.blockParam(sum3BB, 0)); // a
    add1.operands.push_back(builder.blockParam(sum3BB, 1)); // b
    add1.loc = {1, 1, 1};
    sum3BB.instructions.push_back(add1);

    Instr add2;
    add2.result = builder.reserveTempId();
    add2.op = Opcode::Add;
    add2.type = Type(Type::Kind::I64);
    add2.operands.push_back(Value::temp(*add1.result));
    add2.operands.push_back(builder.blockParam(sum3BB, 2)); // c
    add2.loc = {1, 1, 1};
    sum3BB.instructions.push_back(add2);

    Instr sum3Ret;
    sum3Ret.op = Opcode::Ret;
    sum3Ret.type = Type(Type::Kind::Void);
    sum3Ret.loc = {1, 1, 1};
    sum3Ret.operands.push_back(Value::temp(*add2.result));
    sum3BB.instructions.push_back(sum3Ret);

    // Now build main function
    Function &mainFn = builder.startFunction("main", Type(Type::Kind::I64), {});
    BasicBlock &mainBB = builder.addBlock(mainFn, "entry");
    builder.setInsertPoint(mainBB);

    const auto callResult = builder.reserveTempId();
    builder.emitCall(
        "sum3", {Value::constInt(a), Value::constInt(b), Value::constInt(c)}, Value::temp(callResult), {1, 1, 1});

    Instr mainRet;
    mainRet.op = Opcode::Ret;
    mainRet.type = Type(Type::Kind::Void);
    mainRet.loc = {1, 1, 1};
    mainRet.operands.push_back(Value::temp(callResult));
    mainBB.instructions.push_back(mainRet);
}

int64_t runCall(int64_t arg)
{
    Module module;
    buildCallModule(module, arg);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

int64_t runMultiArgCall(int64_t a, int64_t b, int64_t c)
{
    Module module;
    buildMultiArgCallModule(module, a, b, c);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

} // namespace

int main()
{
    //=========================================================================
    // Basic call tests
    //=========================================================================

    // helper(x) returns x * 2
    assert(runCall(0) == 0);
    assert(runCall(1) == 2);
    assert(runCall(5) == 10);
    assert(runCall(-3) == -6);
    assert(runCall(100) == 200);

    //=========================================================================
    // Multi-argument call tests
    //=========================================================================

    // sum3(a, b, c) returns a + b + c
    assert(runMultiArgCall(1, 2, 3) == 6);
    assert(runMultiArgCall(0, 0, 0) == 0);
    assert(runMultiArgCall(-1, 1, 0) == 0);
    assert(runMultiArgCall(10, 20, 30) == 60);
    assert(runMultiArgCall(-5, -5, 10) == 0);

    return 0;
}
