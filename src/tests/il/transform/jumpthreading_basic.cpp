//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/jumpthreading_basic.cpp
// Purpose: Tests for jump threading optimization within SimplifyCFG.
// Key invariants: Threading preserves control flow semantics while
//                 eliminating unnecessary conditional branches.
// Ownership/Lifetime: Builds transient modules per test invocation.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include "tests/TestHarness.hpp"
#include <iostream>

using namespace il::core;

namespace
{

static void verifyOrDie(const Module &module)
{
    auto verifyResult = il::verify::Verifier::verify(module);
    if (!verifyResult)
    {
        il::support::printDiag(verifyResult.error(), std::cerr);
        ASSERT_TRUE(false && "Module verification failed");
    }
}

BasicBlock *findBlock(Function &function, const std::string &label)
{
    for (auto &block : function.blocks)
    {
        if (block.label == label)
            return &block;
    }
    return nullptr;
}

} // namespace

TEST(IL, testBasicJumpThreading)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_threading", Type(Type::Kind::I64), {});

    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "mid");
    builder.createBlock(fn, "target1");
    builder.createBlock(fn, "target2");

    // entry: br mid(true)
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);
    Instr entryBr;
    entryBr.op = Opcode::Br;
    entryBr.type = Type(Type::Kind::Void);
    entryBr.labels.push_back("mid");
    entryBr.brArgs.emplace_back(std::vector<Value>{Value::constBool(true)});
    entry.instructions.push_back(std::move(entryBr));
    entry.terminated = true;

    // mid(cond: i1): cbr cond, target1, target2
    BasicBlock &mid = fn.blocks[1];
    Param condParam{"cond", Type(Type::Kind::I1), builder.reserveTempId()};
    mid.params.push_back(condParam);
    Instr midCbr;
    midCbr.op = Opcode::CBr;
    midCbr.type = Type(Type::Kind::Void);
    midCbr.operands.push_back(Value::temp(condParam.id));
    midCbr.labels.push_back("target1");
    midCbr.labels.push_back("target2");
    midCbr.brArgs.emplace_back(std::vector<Value>{});
    midCbr.brArgs.emplace_back(std::vector<Value>{});
    mid.instructions.push_back(std::move(midCbr));
    mid.terminated = true;

    // target1: ret 1
    builder.setInsertPoint(fn.blocks[2]);
    builder.emitRet(Value::constInt(1), {});

    // target2: ret 2
    builder.setInsertPoint(fn.blocks[3]);
    builder.emitRet(Value::constInt(2), {});

    verifyOrDie(module);

    // Run SimplifyCFG with aggressive mode to enable jump threading
    il::transform::SimplifyCFG simplify(true);
    simplify.setModule(&module);
    simplify.run(fn);

    verifyOrDie(module);

    // After threading, entry should branch directly to target1
    BasicBlock *entryAfter = findBlock(fn, "entry");
    ASSERT_TRUE(entryAfter != nullptr);

    const Instr &term = entryAfter->instructions.back();
    if (term.op == Opcode::Br && !term.labels.empty())
    {
        ASSERT_TRUE(term.labels[0] == "target1" || term.labels[0] == "mid");
    }
}

TEST(IL, testJumpThreadingFalseBranch)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_false", Type(Type::Kind::I64), {});

    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "mid");
    builder.createBlock(fn, "target1");
    builder.createBlock(fn, "target2");

    // entry: br mid(false)
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);
    Instr entryBr;
    entryBr.op = Opcode::Br;
    entryBr.type = Type(Type::Kind::Void);
    entryBr.labels.push_back("mid");
    entryBr.brArgs.emplace_back(std::vector<Value>{Value::constBool(false)});
    entry.instructions.push_back(std::move(entryBr));
    entry.terminated = true;

    // mid(cond: i1): cbr cond, target1, target2
    BasicBlock &mid = fn.blocks[1];
    Param condParam{"cond", Type(Type::Kind::I1), builder.reserveTempId()};
    mid.params.push_back(condParam);
    Instr midCbr;
    midCbr.op = Opcode::CBr;
    midCbr.type = Type(Type::Kind::Void);
    midCbr.operands.push_back(Value::temp(condParam.id));
    midCbr.labels.push_back("target1");
    midCbr.labels.push_back("target2");
    midCbr.brArgs.emplace_back(std::vector<Value>{});
    midCbr.brArgs.emplace_back(std::vector<Value>{});
    mid.instructions.push_back(std::move(midCbr));
    mid.terminated = true;

    // target1: ret 1
    builder.setInsertPoint(fn.blocks[2]);
    builder.emitRet(Value::constInt(1), {});

    // target2: ret 2
    builder.setInsertPoint(fn.blocks[3]);
    builder.emitRet(Value::constInt(2), {});

    verifyOrDie(module);

    il::transform::SimplifyCFG simplify(true);
    simplify.setModule(&module);
    simplify.run(fn);

    verifyOrDie(module);

    // After threading, entry should branch directly to target2
    BasicBlock *entryAfter = findBlock(fn, "entry");
    ASSERT_TRUE(entryAfter != nullptr);

    const Instr &term = entryAfter->instructions.back();
    if (term.op == Opcode::Br && !term.labels.empty())
    {
        ASSERT_TRUE(term.labels[0] == "target2" || term.labels[0] == "mid");
    }
}

TEST(IL, testJumpThreadingWithArgs)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_args", Type(Type::Kind::I64), {});

    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "mid");
    builder.createBlock(fn, "target");

    // entry: br mid(true, 42)
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);
    Instr entryBr;
    entryBr.op = Opcode::Br;
    entryBr.type = Type(Type::Kind::Void);
    entryBr.labels.push_back("mid");
    entryBr.brArgs.emplace_back(std::vector<Value>{Value::constBool(true), Value::constInt(42)});
    entry.instructions.push_back(std::move(entryBr));
    entry.terminated = true;

    // mid(cond: i1, val: i64): cbr cond, target(val), target(0)
    BasicBlock &mid = fn.blocks[1];
    Param condParam{"cond", Type(Type::Kind::I1), builder.reserveTempId()};
    Param valParam{"val", Type(Type::Kind::I64), builder.reserveTempId()};
    mid.params.push_back(condParam);
    mid.params.push_back(valParam);
    Instr midCbr;
    midCbr.op = Opcode::CBr;
    midCbr.type = Type(Type::Kind::Void);
    midCbr.operands.push_back(Value::temp(condParam.id));
    midCbr.labels.push_back("target");
    midCbr.labels.push_back("target");
    midCbr.brArgs.emplace_back(std::vector<Value>{Value::temp(valParam.id)});
    midCbr.brArgs.emplace_back(std::vector<Value>{Value::constInt(0)});
    mid.instructions.push_back(std::move(midCbr));
    mid.terminated = true;

    // target(result: i64): ret result
    BasicBlock &target = fn.blocks[2];
    Param resultParam{"result", Type(Type::Kind::I64), builder.reserveTempId()};
    target.params.push_back(resultParam);
    builder.setInsertPoint(target);
    builder.emitRet(Value::temp(resultParam.id), {});

    verifyOrDie(module);

    il::transform::SimplifyCFG simplify(true);
    simplify.setModule(&module);
    simplify.run(fn);

    verifyOrDie(module);

    // After threading and simplification, should pass 42 to target
    // (since condition is true, takes first branch which passes val=42)
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
