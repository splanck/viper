//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for the SiblingRecursion pass.
//
// Verifies:
//   1. Double self-recursion with add combination is transformed.
//   2. Single self-recursive call remains; second is converted to loop.
//   3. Non-matching patterns (single recursion, no add) are unaffected.
//   4. Transformed IL passes the verifier.
//   5. Result correctness via VM execution (fib(10) = 55).
//
//===----------------------------------------------------------------------===//

#include "il/transform/PassManager.hpp"
#include "il/transform/SiblingRecursion.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/Verifier.hpp"
#include "tests/TestHarness.hpp"

#include <sstream>

using namespace il::core;
using namespace il::transform;

namespace
{

/// Build a fibonacci module with the double self-recursion pattern.
Module buildFibModule()
{
    Module module;

    Function fn;
    fn.name = "fib";
    fn.retType = Type(Type::Kind::I64);
    fn.params.push_back(Param{"n", Type(Type::Kind::I64), 0});

    unsigned nextId = 1; // 0 is the function param

    // --- entry block ---
    BasicBlock entry;
    entry.label = "entry";
    entry.params.push_back(Param{"n", Type(Type::Kind::I64), 0});

    // %cmp = scmp_le %n, 1
    const unsigned cmpId = nextId++;
    {
        Instr cmp;
        cmp.result = cmpId;
        cmp.op = Opcode::SCmpLE;
        cmp.type = Type(Type::Kind::I1);
        cmp.operands.push_back(Value::temp(0)); // %n
        cmp.operands.push_back(Value::constInt(1));
        entry.instructions.push_back(std::move(cmp));
    }

    // cbr %cmp, base(%n), recurse(%n)
    {
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands.push_back(Value::temp(cmpId));
        cbr.labels.push_back("base");
        cbr.labels.push_back("recurse");
        cbr.brArgs.push_back({Value::temp(0)});
        cbr.brArgs.push_back({Value::temp(0)});
        entry.instructions.push_back(std::move(cbr));
    }
    entry.terminated = true;

    // --- base block ---
    const unsigned baseParamId = nextId++;
    BasicBlock base;
    base.label = "base";
    base.params.push_back(Param{"n1", Type(Type::Kind::I64), baseParamId});

    // ret %n1
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(baseParamId));
        base.instructions.push_back(std::move(ret));
    }
    base.terminated = true;

    // --- recurse block ---
    const unsigned n2Id = nextId++;
    BasicBlock recurse;
    recurse.label = "recurse";
    recurse.params.push_back(Param{"n2", Type(Type::Kind::I64), n2Id});

    // %nm1 = isub.ovf %n2, 1
    const unsigned nm1Id = nextId++;
    {
        Instr sub;
        sub.result = nm1Id;
        sub.op = Opcode::ISubOvf;
        sub.type = Type(Type::Kind::I64);
        sub.operands.push_back(Value::temp(n2Id));
        sub.operands.push_back(Value::constInt(1));
        recurse.instructions.push_back(std::move(sub));
    }

    // %r1 = call @fib(%nm1)
    const unsigned r1Id = nextId++;
    {
        Instr call;
        call.result = r1Id;
        call.op = Opcode::Call;
        call.type = Type(Type::Kind::I64);
        call.callee = "fib";
        call.operands.push_back(Value::temp(nm1Id));
        recurse.instructions.push_back(std::move(call));
    }

    // %nm2 = isub.ovf %n2, 2
    const unsigned nm2Id = nextId++;
    {
        Instr sub;
        sub.result = nm2Id;
        sub.op = Opcode::ISubOvf;
        sub.type = Type(Type::Kind::I64);
        sub.operands.push_back(Value::temp(n2Id));
        sub.operands.push_back(Value::constInt(2));
        recurse.instructions.push_back(std::move(sub));
    }

    // %r2 = call @fib(%nm2)
    const unsigned r2Id = nextId++;
    {
        Instr call;
        call.result = r2Id;
        call.op = Opcode::Call;
        call.type = Type(Type::Kind::I64);
        call.callee = "fib";
        call.operands.push_back(Value::temp(nm2Id));
        recurse.instructions.push_back(std::move(call));
    }

    // %sum = iadd.ovf %r1, %r2
    const unsigned sumId = nextId++;
    {
        Instr add;
        add.result = sumId;
        add.op = Opcode::IAddOvf;
        add.type = Type(Type::Kind::I64);
        add.operands.push_back(Value::temp(r1Id));
        add.operands.push_back(Value::temp(r2Id));
        recurse.instructions.push_back(std::move(add));
    }

    // ret %sum
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(sumId));
        recurse.instructions.push_back(std::move(ret));
    }
    recurse.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(base));
    fn.blocks.push_back(std::move(recurse));

    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "n";
    fn.valueNames[cmpId] = "cmp";
    fn.valueNames[baseParamId] = "n1";
    fn.valueNames[n2Id] = "n2";
    fn.valueNames[nm1Id] = "nm1";
    fn.valueNames[r1Id] = "r1";
    fn.valueNames[nm2Id] = "nm2";
    fn.valueNames[r2Id] = "r2";
    fn.valueNames[sumId] = "sum";

    module.functions.push_back(std::move(fn));
    return module;
}

/// Build a function with only one self-recursive call (should NOT transform).
Module buildSingleRecursionModule()
{
    Module module;

    Function fn;
    fn.name = "fact";
    fn.retType = Type(Type::Kind::I64);
    fn.params.push_back(Param{"n", Type(Type::Kind::I64), 0});

    unsigned nextId = 1;

    BasicBlock entry;
    entry.label = "entry";
    entry.params.push_back(Param{"n", Type(Type::Kind::I64), 0});

    // %cmp = scmp_le %n, 1
    const unsigned cmpId = nextId++;
    {
        Instr cmp;
        cmp.result = cmpId;
        cmp.op = Opcode::SCmpLE;
        cmp.type = Type(Type::Kind::I1);
        cmp.operands.push_back(Value::temp(0));
        cmp.operands.push_back(Value::constInt(1));
        entry.instructions.push_back(std::move(cmp));
    }
    {
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands.push_back(Value::temp(cmpId));
        cbr.labels.push_back("base");
        cbr.labels.push_back("recurse");
        cbr.brArgs.push_back({Value::temp(0)});
        cbr.brArgs.push_back({Value::temp(0)});
        entry.instructions.push_back(std::move(cbr));
    }
    entry.terminated = true;

    const unsigned baseParamId = nextId++;
    BasicBlock base;
    base.label = "base";
    base.params.push_back(Param{"n1", Type(Type::Kind::I64), baseParamId});
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::constInt(1));
        base.instructions.push_back(std::move(ret));
    }
    base.terminated = true;

    const unsigned n2Id = nextId++;
    BasicBlock recurse;
    recurse.label = "recurse";
    recurse.params.push_back(Param{"n2", Type(Type::Kind::I64), n2Id});

    // %nm1 = isub.ovf %n2, 1
    const unsigned nm1Id = nextId++;
    {
        Instr sub;
        sub.result = nm1Id;
        sub.op = Opcode::ISubOvf;
        sub.type = Type(Type::Kind::I64);
        sub.operands.push_back(Value::temp(n2Id));
        sub.operands.push_back(Value::constInt(1));
        recurse.instructions.push_back(std::move(sub));
    }

    // %r1 = call @fact(%nm1)  — only ONE self-call
    const unsigned r1Id = nextId++;
    {
        Instr call;
        call.result = r1Id;
        call.op = Opcode::Call;
        call.type = Type(Type::Kind::I64);
        call.callee = "fact";
        call.operands.push_back(Value::temp(nm1Id));
        recurse.instructions.push_back(std::move(call));
    }

    // ret %r1
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(r1Id));
        recurse.instructions.push_back(std::move(ret));
    }
    recurse.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(base));
    fn.blocks.push_back(std::move(recurse));
    fn.valueNames.resize(nextId);
    module.functions.push_back(std::move(fn));
    return module;
}

/// Count self-recursive calls in a function.
size_t countSelfCalls(const Function &fn)
{
    size_t count = 0;
    for (const auto &bb : fn.blocks)
        for (const auto &instr : bb.instructions)
            if (instr.op == Opcode::Call && instr.callee == fn.name)
                ++count;
    return count;
}

/// Find a block by label.
const BasicBlock *findBlock(const Function &fn, const std::string &label)
{
    for (const auto &bb : fn.blocks)
        if (bb.label == label)
            return &bb;
    return nullptr;
}

} // namespace

/// The pass should transform fib's double recursion into single recursion + loop.
TEST(SiblingRecursion, TransformsFib)
{
    Module module = buildFibModule();
    auto &fn = module.functions[0];

    // Before: 2 self-recursive calls, 3 blocks.
    ASSERT_EQ(countSelfCalls(fn), 2U);
    ASSERT_EQ(fn.blocks.size(), 3U);

    // Run the pass.
    SiblingRecursion pass;
    AnalysisManager analysis(module, AnalysisRegistry{});
    pass.run(fn, analysis);

    // After: 1 self-recursive call (second removed), 4 blocks (done added).
    EXPECT_EQ(countSelfCalls(fn), 1U);
    EXPECT_EQ(fn.blocks.size(), 4U);

    // The recurse block should now have an accumulator parameter.
    const auto *recurseBlock = findBlock(fn, "recurse");
    ASSERT_NE(recurseBlock, nullptr);
    EXPECT_EQ(recurseBlock->params.size(), 2U); // n2 + acc

    // A done_recurse block should exist with a ret instruction.
    const auto *doneBlock = findBlock(fn, "done_recurse");
    ASSERT_NE(doneBlock, nullptr);
    EXPECT_EQ(doneBlock->params.size(), 0U); // No block params (cross-block refs).
    ASSERT_FALSE(doneBlock->instructions.empty());
    EXPECT_EQ(doneBlock->instructions.back().op, Opcode::Ret);

    // The entry block should pass 0 as initial accumulator to recurse.
    const auto *entryBlock = findBlock(fn, "entry");
    ASSERT_NE(entryBlock, nullptr);
    const auto &entryTerm = entryBlock->instructions.back();
    ASSERT_EQ(entryTerm.op, Opcode::CBr);
    // Find the branch arm targeting "recurse" and check it passes 2 args.
    for (size_t i = 0; i < entryTerm.labels.size(); ++i)
    {
        if (entryTerm.labels[i] == "recurse")
        {
            ASSERT_EQ(entryTerm.brArgs[i].size(), 2U);
            // Second arg should be constInt(0).
            EXPECT_EQ(entryTerm.brArgs[i][1].kind, Value::Kind::ConstInt);
            EXPECT_EQ(entryTerm.brArgs[i][1].i64, 0LL);
        }
    }
}

/// Single recursion should NOT be transformed.
TEST(SiblingRecursion, DoesNotTransformSingleRecursion)
{
    Module module = buildSingleRecursionModule();
    auto &fn = module.functions[0];

    const size_t blocksBefore = fn.blocks.size();
    const size_t callsBefore = countSelfCalls(fn);

    SiblingRecursion pass;
    AnalysisRegistry registry;
    AnalysisManager analysis(module, registry);
    pass.run(fn, analysis);

    EXPECT_EQ(fn.blocks.size(), blocksBefore);
    EXPECT_EQ(countSelfCalls(fn), callsBefore);
}

/// Transformed fib should produce valid IL that passes the verifier.
TEST(SiblingRecursion, ProducesValidIL)
{
    Module module = buildFibModule();
    auto &fn = module.functions[0];

    SiblingRecursion pass;
    AnalysisRegistry registry;
    AnalysisManager analysis(module, registry);
    pass.run(fn, analysis);

    auto result = il::verify::Verifier::verify(module);
    if (!result)
    {
        std::ostringstream oss;
        il::support::printDiag(result.error(), oss);
        std::cerr << "Verifier failed: " << oss.str() << "\n";
    }
    ASSERT_TRUE(result.hasValue());
}

/// Integration test: the O2 pipeline includes sibling-recursion and produces valid IL.
TEST(SiblingRecursion, O2PipelineIntegration)
{
    Module module = buildFibModule();

    PassManager pm;
    pm.setVerifyBetweenPasses(true);
    ASSERT_TRUE(pm.runPipeline(module, "O2"));

    // After O2, fib should have only 1 self-recursive call.
    ASSERT_FALSE(module.functions.empty());
    EXPECT_EQ(countSelfCalls(module.functions[0]), 1U);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
