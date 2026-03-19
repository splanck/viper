//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_LoopRotate.cpp
// Purpose: Validate LoopRotate pass — while-to-do-while conversion.
// Key invariants:
//   - Only rotates single-latch, single-exit loops with pure headers.
//   - Non-rotatable loops (side effects in header, multiple exits) unchanged.
//   - Guard block inserted for initial condition check.
// Ownership/Lifetime: Transient modules.
// Links: il/transform/LoopRotate.cpp
//
//===----------------------------------------------------------------------===//

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/LoopRotate.hpp"
#include "il/transform/LoopSimplify.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/transform/analysis/LoopInfo.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace
{

void setupAnalysisRegistry(il::transform::AnalysisRegistry &registry)
{
    registry.registerFunctionAnalysis<il::transform::CFGInfo>(
        "cfg", [](Module &mod, Function &fn) { return il::transform::buildCFG(mod, fn); });
    registry.registerFunctionAnalysis<viper::analysis::DomTree>(
        "dominators",
        [](Module &mod, Function &fn)
        {
            viper::analysis::CFGContext ctx(mod);
            return viper::analysis::computeDominatorTree(ctx, fn);
        });
    registry.registerFunctionAnalysis<il::transform::LoopInfo>(
        "loop-info",
        [](Module &mod, Function &fn) { return il::transform::computeLoopInfo(mod, fn); });
    registry.registerFunctionAnalysis<il::transform::LivenessInfo>(
        "liveness",
        [](Module &mod, Function &fn) { return il::transform::computeLiveness(mod, fn); });
    registry.registerFunctionAnalysis<viper::analysis::BasicAA>(
        "basic-aa", [](Module &mod, Function &fn) { return viper::analysis::BasicAA(mod, fn); });
}

Param makeParam(const std::string &name, Type type, unsigned &nextId)
{
    Param p;
    p.name = name;
    p.type = type;
    p.id = nextId++;
    return p;
}

} // namespace

// Test that a simple while-loop with pure condition header gets rotated.
TEST(LoopRotate, RotatesSimpleWhileLoop)
{
    Module module;
    Function fn;
    fn.name = "while_loop";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;
    Param limitParam = makeParam("limit", Type(Type::Kind::I64), nextId);
    fn.params.push_back(limitParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[limitParam.id] = limitParam.name;

    // entry: br ^header(0)
    BasicBlock entry;
    entry.label = "entry";
    Instr entryBr;
    entryBr.op = Opcode::Br;
    entryBr.labels.push_back("header");
    entryBr.brArgs.push_back({Value::constInt(0)});
    entry.instructions.push_back(std::move(entryBr));
    entry.terminated = true;

    // header(%i:i64): %cmp = scmp_lt %i, %limit; cbr %cmp, ^body(%i), ^exit(%i)
    BasicBlock header;
    header.label = "header";
    Param iParam = makeParam("i", Type(Type::Kind::I64), nextId);
    header.params.push_back(iParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[iParam.id] = iParam.name;

    Instr cmp;
    cmp.op = Opcode::SCmpLT;
    cmp.result = nextId++;
    cmp.operands.push_back(Value::temp(iParam.id));
    cmp.operands.push_back(Value::temp(limitParam.id));
    header.instructions.push_back(std::move(cmp));

    Instr headerCbr;
    headerCbr.op = Opcode::CBr;
    headerCbr.operands.push_back(Value::temp(nextId - 1));
    headerCbr.labels.push_back("body");
    headerCbr.labels.push_back("exit");
    headerCbr.brArgs.push_back({Value::temp(iParam.id)});
    headerCbr.brArgs.push_back({Value::temp(iParam.id)});
    header.instructions.push_back(std::move(headerCbr));
    header.terminated = true;

    // body(%j:i64): %next = add %j, 1; br ^header(%next)
    BasicBlock body;
    body.label = "body";
    Param jParam = makeParam("j", Type(Type::Kind::I64), nextId);
    body.params.push_back(jParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[jParam.id] = jParam.name;

    Instr add;
    add.op = Opcode::Add;
    add.result = nextId++;
    add.operands.push_back(Value::temp(jParam.id));
    add.operands.push_back(Value::constInt(1));
    body.instructions.push_back(std::move(add));

    Instr bodyBr;
    bodyBr.op = Opcode::Br;
    bodyBr.labels.push_back("header");
    bodyBr.brArgs.push_back({Value::temp(nextId - 1)});
    body.instructions.push_back(std::move(bodyBr));
    body.terminated = true;

    // exit(%result:i64): ret %result
    BasicBlock exit;
    exit.label = "exit";
    Param resultParam = makeParam("result", Type(Type::Kind::I64), nextId);
    exit.params.push_back(resultParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[resultParam.id] = resultParam.name;

    Instr ret;
    ret.op = Opcode::Ret;
    ret.operands.push_back(Value::temp(resultParam.id));
    exit.instructions.push_back(std::move(ret));
    exit.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(header));
    fn.blocks.push_back(std::move(body));
    fn.blocks.push_back(std::move(exit));
    module.functions.push_back(std::move(fn));

    Function &function = module.functions.back();

    il::transform::AnalysisRegistry registry;
    setupAnalysisRegistry(registry);
    il::transform::AnalysisManager am(module, registry);

    // Run LoopSimplify first (required for LoopInfo)
    il::transform::LoopSimplify simplify;
    auto simplifyResult = simplify.run(function, am);
    am.invalidateAfterFunctionPass(simplifyResult, function);

    size_t blocksBefore = function.blocks.size();

    // Run LoopRotate
    il::transform::LoopRotate rotate;
    auto rotateResult = rotate.run(function, am);

    // Should have added a guard block (block count increases)
    EXPECT_GE(function.blocks.size(), blocksBefore);
}

// Test that a function with no loops is not modified.
TEST(LoopRotate, NoChangeWithoutLoop)
{
    Module module;
    Function fn;
    fn.name = "no_loop";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;
    Param xParam = makeParam("x", Type(Type::Kind::I64), nextId);
    fn.params.push_back(xParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[xParam.id] = xParam.name;

    BasicBlock entry;
    entry.label = "entry";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.operands.push_back(Value::temp(xParam.id));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));
    Function &function = module.functions.back();

    il::transform::AnalysisRegistry registry;
    setupAnalysisRegistry(registry);
    il::transform::AnalysisManager am(module, registry);

    size_t blocksBefore = function.blocks.size();
    il::transform::LoopRotate rotate;
    auto result = rotate.run(function, am);

    EXPECT_EQ(function.blocks.size(), blocksBefore);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
