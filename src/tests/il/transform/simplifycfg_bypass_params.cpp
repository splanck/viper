//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_bypass_params.cpp
// Purpose: Verify SimplifyCFG forwards branch arguments when bypassing blocks with params.
// Key invariants: Forwarding block removal must preserve branch arguments and remove the block.
// Ownership/Lifetime: Constructs a local module and runs the pass by value.
// Links: docs/il/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/verify/Verifier.hpp"

#include "tests/TestHarness.hpp"
#include <optional>
#include <string>

TEST(IL, SimplifyCFGBypassParams) {
    using namespace il::core;

    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("bypass", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "mid", {Param{"p", Type(Type::Kind::I64), 0}});
    builder.createBlock(fn, "exit", {Param{"result", Type(Type::Kind::I64), 0}});

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &mid = fn.blocks[1];
    BasicBlock &exit = fn.blocks[2];

    builder.setInsertPoint(entry);
    builder.br(mid, {Value::constInt(7)});

    builder.setInsertPoint(mid);
    builder.br(exit, {builder.blockParam(mid, 0)});

    builder.setInsertPoint(exit);
    builder.emitRet(std::optional<Value>{builder.blockParam(exit, 0)}, {});

    auto verifyResult = il::verify::Verifier::verify(module);
    ASSERT_TRUE(verifyResult && "Module should verify before SimplifyCFG");

    il::transform::SimplifyCFG pass;
    pass.setModule(&module);
    il::transform::SimplifyCFG::Stats stats{};
    const bool changed = pass.run(fn, &stats);
    ASSERT_TRUE(changed && "SimplifyCFG should remove the forwarding block");
    ASSERT_EQ(stats.predsMerged, 1);
    ASSERT_EQ(stats.emptyBlocksRemoved, 1);

    const auto findBlock = [](const Function &function,
                              const std::string &label) -> const BasicBlock * {
        for (const auto &block : function.blocks) {
            if (block.label == label)
                return &block;
        }
        return nullptr;
    };

    const BasicBlock *midBlock = findBlock(fn, "mid");
    ASSERT_FALSE(midBlock);

    bool foundReturnOfBypassedArg = false;
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.op != Opcode::Ret || instr.operands.empty())
                continue;
            const Value &retValue = instr.operands.front();
            if (retValue.kind == Value::Kind::ConstInt && retValue.i64 == 7)
                foundReturnOfBypassedArg = true;
        }
    }
    ASSERT_TRUE(foundReturnOfBypassedArg);
}

TEST(IL, SimplifyCFGDoesNotDeleteForwarderResultsUsedByDominatedBlocks) {
    using namespace il::core;

    Module module;
    Function fn;
    fn.name = "preserve_forwarder_defs";
    fn.retType = Type(Type::Kind::I64);
    fn.params.push_back(Param{"flag", Type(Type::Kind::I1), 0});

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands = {Value::temp(0)};
        cbr.labels = {"left", "right"};
        cbr.brArgs = {{}, {}};
        entry.instructions.push_back(std::move(cbr));
        entry.terminated = true;
    }

    BasicBlock left;
    left.label = "left";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels = {"preheader"};
        br.brArgs = {{}};
        left.instructions.push_back(std::move(br));
        left.terminated = true;
    }

    BasicBlock right;
    right.label = "right";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels = {"preheader"};
        br.brArgs = {{}};
        right.instructions.push_back(std::move(br));
        right.terminated = true;
    }

    BasicBlock preheader;
    preheader.label = "preheader";
    {
        Instr def;
        def.result = 1;
        def.op = Opcode::IAddOvf;
        def.type = Type(Type::Kind::I64);
        def.operands = {Value::constInt(40), Value::constInt(2)};
        preheader.instructions.push_back(std::move(def));

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels = {"cond"};
        br.brArgs = {{}};
        preheader.instructions.push_back(std::move(br));
        preheader.terminated = true;
    }

    BasicBlock cond;
    cond.label = "cond";
    {
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands = {Value::temp(0)};
        cbr.labels = {"body", "exit"};
        cbr.brArgs = {{}, {}};
        cond.instructions.push_back(std::move(cbr));
        cond.terminated = true;
    }

    BasicBlock body;
    body.label = "body";
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(1)};
        body.instructions.push_back(std::move(ret));
        body.terminated = true;
    }

    BasicBlock exit;
    exit.label = "exit";
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::constInt(0)};
        exit.instructions.push_back(std::move(ret));
        exit.terminated = true;
    }

    fn.blocks = {std::move(entry),
                 std::move(left),
                 std::move(right),
                 std::move(preheader),
                 std::move(cond),
                 std::move(body),
                 std::move(exit)};
    fn.valueNames.resize(2);
    module.functions.push_back(std::move(fn));

    ASSERT_TRUE(il::verify::Verifier::verify(module).hasValue());

    Function &function = module.functions.front();
    il::transform::SimplifyCFG pass(/*aggressive=*/false);
    pass.setModule(&module);
    il::transform::SimplifyCFG::Stats stats{};
    pass.run(function, &stats);

    auto verifyResult = il::verify::Verifier::verify(module);
    ASSERT_TRUE(verifyResult &&
                "SimplifyCFG must preserve pure preheader definitions used by dominated blocks");
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
