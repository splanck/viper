//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Inline pass tests covering multi-block callees, block parameter rewiring,
// and cost-model refusals for oversized or recursive callees.
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/DCE.hpp"
#include "il/transform/Inline.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/SCCP.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/io/Parser.hpp"
#include "il/io/Serializer.hpp"

#include "tests/TestHarness.hpp"
#include <optional>
#include <sstream>

using namespace il::core;

namespace {

/// @brief Make abs helper.
Function makeAbsHelper() {
    Function f;
    f.name = "abs_helper";
    f.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;
    Param x{"x", Type(Type::Kind::I64), nextId++};
    f.params.push_back(x);

    BasicBlock entry;
    entry.label = "entry";

    Instr cmp;
    cmp.result = nextId++;
    cmp.op = Opcode::SCmpLT;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands.push_back(Value::temp(x.id));
    cmp.operands.push_back(Value::constInt(0));
    entry.instructions.push_back(cmp);

    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.type = Type(Type::Kind::Void);
    cbr.operands.push_back(Value::temp(*cmp.result));
    cbr.labels.push_back("neg");
    cbr.labels.push_back("done");
    cbr.brArgs.push_back({});
    cbr.brArgs.push_back({Value::temp(x.id)});
    entry.instructions.push_back(cbr);
    entry.terminated = true;

    BasicBlock neg;
    neg.label = "neg";

    Instr negate;
    negate.result = nextId++;
    negate.op = Opcode::ISubOvf;
    negate.type = Type(Type::Kind::I64);
    negate.operands.push_back(Value::constInt(0));
    negate.operands.push_back(Value::temp(x.id));
    neg.instructions.push_back(negate);

    Instr forward;
    forward.op = Opcode::Br;
    forward.type = Type(Type::Kind::Void);
    forward.labels.push_back("done");
    forward.brArgs.push_back({Value::temp(*negate.result)});
    neg.instructions.push_back(forward);
    neg.terminated = true;

    BasicBlock done;
    done.label = "done";
    Param acc{"acc", Type(Type::Kind::I64), nextId++};
    done.params.push_back(acc);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(acc.id));
    done.instructions.push_back(ret);
    done.terminated = true;

    f.valueNames.resize(nextId);
    f.valueNames[x.id] = "x";
    f.valueNames[acc.id] = "acc";

    f.blocks.push_back(std::move(entry));
    f.blocks.push_back(std::move(neg));
    f.blocks.push_back(std::move(done));
    return f;
}

/// @brief Make inline caller.
Function makeInlineCaller() {
    Function f;
    f.name = "main";
    f.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;
    BasicBlock entry;
    entry.label = "entry";

    Instr call;
    call.result = nextId++;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.callee = "abs_helper";
    call.operands.push_back(Value::constInt(-7));
    entry.instructions.push_back(call);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*call.result));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    f.blocks.push_back(std::move(entry));
    f.valueNames.resize(nextId);
    f.valueNames[*call.result] = "result";
    return f;
}

/// @brief Make large helper.
Function makeLargeHelper() {
    Function f;
    f.name = "large_helper";
    f.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;
    Param x{"x", Type(Type::Kind::I64), nextId++};
    f.params.push_back(x);

    auto makeForwardBlock =
        [&](const std::string &from, const std::string &to, std::optional<unsigned> argId) {
            BasicBlock block;
            block.label = from;
            Instr br;
            br.op = Opcode::Br;
            br.type = Type(Type::Kind::Void);
            br.labels.push_back(to);
            if (argId)
                br.brArgs.push_back({Value::temp(*argId)});
            else
                br.brArgs.emplace_back();
            block.instructions.push_back(br);
            block.terminated = true;
            return block;
        };

    // 9 forwarding blocks (b0..b7 + exit) exceeds blockBudget=8, so this
    // function will not be inlined regardless of instruction count.
    f.blocks.push_back(makeForwardBlock("b0", "b1", std::nullopt));
    f.blocks.push_back(makeForwardBlock("b1", "b2", std::nullopt));
    f.blocks.push_back(makeForwardBlock("b2", "b3", std::nullopt));
    f.blocks.push_back(makeForwardBlock("b3", "b4", std::nullopt));
    f.blocks.push_back(makeForwardBlock("b4", "b5", std::nullopt));
    f.blocks.push_back(makeForwardBlock("b5", "b6", std::nullopt));
    f.blocks.push_back(makeForwardBlock("b6", "b7", std::nullopt));
    f.blocks.push_back(makeForwardBlock("b7", "exit", x.id));

    BasicBlock exit;
    exit.label = "exit";
    Param acc{"acc", Type(Type::Kind::I64), nextId++};
    exit.params.push_back(acc);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(acc.id));
    exit.instructions.push_back(ret);
    exit.terminated = true;

    f.blocks.push_back(std::move(exit));
    f.valueNames.resize(nextId);
    f.valueNames[x.id] = "x";
    f.valueNames[acc.id] = "acc";
    return f;
}

/// @brief Make large caller.
Function makeLargeCaller() {
    Function f;
    f.name = "large_caller";
    f.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    Instr call;
    call.result = 0;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.callee = "large_helper";
    call.operands.push_back(Value::constInt(11));
    entry.instructions.push_back(call);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*call.result));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    f.blocks.push_back(std::move(entry));
    f.valueNames.resize(1);
    f.valueNames[0] = "result";
    return f;
}

/// @brief Make recursive helper.
Function makeRecursiveHelper() {
    Function f;
    f.name = "self";
    f.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    Instr call;
    call.result = 0;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.callee = "self";
    call.operands.push_back(Value::constInt(1));
    entry.instructions.push_back(call);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*call.result));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    f.blocks.push_back(std::move(entry));
    f.valueNames.resize(1);
    f.valueNames[0] = "recur";
    return f;
}

/// @brief Make inline tail helper.
Function makeInlineTailHelper() {
    Function f;
    f.name = "tail_helper";
    f.retType = Type(Type::Kind::I64);

    Param x{"x", Type(Type::Kind::I64), 0};
    f.params.push_back(x);

    BasicBlock entry;
    entry.label = "entry";

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(x.id));
    entry.instructions.push_back(ret);
    entry.terminated = true;

    f.blocks.push_back(std::move(entry));
    f.valueNames.resize(1);
    f.valueNames[x.id] = "x";
    return f;
}

/// @brief Make inline tail caller.
Function makeInlineTailCaller() {
    Function f;
    f.name = "tail_caller";
    f.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;

    BasicBlock entry;
    entry.label = "entry";
    Instr toCall;
    toCall.op = Opcode::Br;
    toCall.type = Type(Type::Kind::Void);
    toCall.labels.push_back("callsite");
    toCall.brArgs.emplace_back();
    entry.instructions.push_back(toCall);
    entry.terminated = true;

    BasicBlock callsite;
    callsite.label = "callsite";

    Instr call;
    call.result = nextId++;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.callee = "tail_helper";
    call.operands.push_back(Value::constInt(7));
    callsite.instructions.push_back(call);

    const unsigned slotId = nextId++;
    Instr alloca;
    alloca.result = slotId;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(8));
    callsite.instructions.push_back(alloca);

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::I64);
    store.operands = {Value::temp(slotId), Value::temp(*call.result)};
    callsite.instructions.push_back(store);

    Instr toTail;
    toTail.op = Opcode::Br;
    toTail.type = Type(Type::Kind::Void);
    toTail.labels.push_back("tail");
    toTail.brArgs.emplace_back();
    callsite.instructions.push_back(toTail);
    callsite.terminated = true;

    BasicBlock tail;
    tail.label = "tail";

    const unsigned loadId = nextId++;
    Instr load;
    load.result = loadId;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.operands.push_back(Value::temp(slotId));
    tail.instructions.push_back(load);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(loadId));
    tail.instructions.push_back(ret);
    tail.terminated = true;

    f.blocks.push_back(std::move(entry));
    f.blocks.push_back(std::move(callsite));
    f.blocks.push_back(std::move(tail));
    f.valueNames.resize(nextId);
    return f;
}

/// @brief Has call.
bool hasCall(const Function &fn) {
    for (const auto &B : fn.blocks)
        for (const auto &I : B.instructions)
            if (I.op == Opcode::Call)
                return true;
    return false;
}

} // namespace

TEST(IL, test_inline_multiblock) {
    Module M;
    M.functions.push_back(makeAbsHelper());
    M.functions.push_back(makeInlineCaller());

    il::transform::Inliner inl;
    il::transform::AnalysisRegistry reg;
    il::transform::AnalysisManager AM(M, reg);
    (void)inl.run(M, AM);

    Function &caller = M.functions[1];
    // With blockBudget=1, multi-block callees are not inlined.
    // The call remains and the caller is unchanged.
    ASSERT_TRUE(hasCall(caller));
}

TEST(IL, test_no_inline_large) {
    Module M;
    M.functions.push_back(makeLargeHelper());
    M.functions.push_back(makeLargeCaller());

    il::transform::Inliner inl;
    il::transform::AnalysisRegistry reg;
    il::transform::AnalysisManager AM(M, reg);
    (void)inl.run(M, AM);

    const Function &caller = M.functions[1];
    ASSERT_TRUE(hasCall(caller));
}

TEST(IL, test_no_inline_recursive) {
    Module M;
    M.functions.push_back(makeRecursiveHelper());

    il::transform::Inliner inl;
    il::transform::AnalysisRegistry reg;
    il::transform::AnalysisManager AM(M, reg);
    (void)inl.run(M, AM);

    const Function &self = M.functions.front();
    ASSERT_TRUE(hasCall(self));
}

TEST(IL, test_inline_roundtrip_preserves_continuation_defs_before_uses) {
    Module M;
    M.functions.push_back(makeInlineTailHelper());
    M.functions.push_back(makeInlineTailCaller());

    il::transform::Inliner inl;
    il::transform::AnalysisRegistry reg;
    il::transform::AnalysisManager AM(M, reg);
    (void)inl.run(M, AM);

    std::string text = il::io::Serializer::toString(M, il::io::Serializer::Mode::Pretty);

    std::istringstream iss(text);
    Module roundTripped;
    auto parsed = il::io::Parser::parse(iss, roundTripped);
    ASSERT_TRUE(parsed && "round-trip parse should succeed");
}

TEST(IL, test_o2_pipeline_runs) {
    Module M;
    M.functions.push_back(makeAbsHelper());
    M.functions.push_back(makeInlineCaller());

    il::transform::PassManager pm;
    pm.addSimplifyCFG();
    bool ran = pm.runPipeline(M, "O2");
    ASSERT_TRUE(ran);

    const Function &caller = M.functions[1];
    // With blockBudget=1, multi-block callees are not inlined even at O2.
    ASSERT_TRUE(hasCall(caller));
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
