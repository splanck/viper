//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_EHOpt.cpp
// Purpose: Validate EH optimization — dead eh.push/eh.pop removal.
// Key invariants:
//   - Pairs with no throwing instructions between them are removed.
//   - Pairs with calls/traps between them are preserved.
//   - Nested EH is left alone.
// Ownership/Lifetime: Builds transient modules per test.
// Links: il/transform/EHOpt.cpp
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/transform/EHOpt.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace {

Instr makeSimple(Opcode op) {
    Instr i;
    i.op = op;
    return i;
}

Instr makeEhPush(const std::string &handler) {
    Instr i;
    i.op = Opcode::EhPush;
    i.labels.push_back(handler);
    return i;
}

Instr makeEhPop() {
    Instr i;
    i.op = Opcode::EhPop;
    return i;
}

Instr makeAdd(unsigned result, unsigned lhs, unsigned rhs) {
    Instr i;
    i.op = Opcode::Add;
    i.result = result;
    i.operands.push_back(Value::temp(lhs));
    i.operands.push_back(Value::temp(rhs));
    return i;
}

Instr makeCall(const std::string &fn, unsigned result) {
    Instr i;
    i.op = Opcode::Call;
    i.result = result;
    i.labels.push_back(fn);
    return i;
}

Instr makeRet(unsigned val) {
    Instr i;
    i.op = Opcode::Ret;
    i.operands.push_back(Value::temp(val));
    return i;
}

} // namespace

TEST(EHOpt, RemovesDeadPairWithNoThrowingInstructions) {
    Module mod;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    {
        Param p;
        p.name = "x";
        p.type = Type(Type::Kind::I64);
        p.id = 0;
        entry.params.push_back(std::move(p));
    }
    entry.instructions.push_back(makeEhPush("handler"));
    entry.instructions.push_back(makeAdd(1, 0, 0)); // pure — no throw
    entry.instructions.push_back(makeEhPop());
    entry.instructions.push_back(makeRet(1));
    entry.terminated = true;

    BasicBlock handler;
    handler.label = "handler";
    handler.instructions.push_back(makeRet(0));
    handler.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(handler));
    mod.functions.push_back(std::move(fn));

    bool changed = il::transform::ehOpt(mod);
    EXPECT_TRUE(changed);

    // EhPush and EhPop should be removed; only Add and Ret remain
    const auto &instrs = mod.functions[0].blocks[0].instructions;
    for (const auto &i : instrs) {
        EXPECT_NE(i.op, Opcode::EhPush);
        EXPECT_NE(i.op, Opcode::EhPop);
    }
}

TEST(EHOpt, PreservesPairWithCall) {
    Module mod;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    {
        Param p;
        p.name = "x";
        p.type = Type(Type::Kind::I64);
        p.id = 0;
        entry.params.push_back(std::move(p));
    }
    entry.instructions.push_back(makeEhPush("handler"));
    entry.instructions.push_back(makeCall("@foo", 1)); // can throw
    entry.instructions.push_back(makeEhPop());
    entry.instructions.push_back(makeRet(1));
    entry.terminated = true;

    BasicBlock handler;
    handler.label = "handler";
    handler.instructions.push_back(makeRet(0));
    handler.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(handler));
    mod.functions.push_back(std::move(fn));

    bool changed = il::transform::ehOpt(mod);
    EXPECT_FALSE(changed);

    // EhPush and EhPop should be preserved
    bool hasEhPush = false, hasEhPop = false;
    for (const auto &i : mod.functions[0].blocks[0].instructions) {
        if (i.op == Opcode::EhPush)
            hasEhPush = true;
        if (i.op == Opcode::EhPop)
            hasEhPop = true;
    }
    EXPECT_TRUE(hasEhPush);
    EXPECT_TRUE(hasEhPop);
}

TEST(EHOpt, PreservesPairWithCheckedDiv) {
    Module mod;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    {
        Param p;
        p.name = "x";
        p.type = Type(Type::Kind::I64);
        p.id = 0;
        entry.params.push_back(std::move(p));
    }

    Instr sdiv;
    sdiv.op = Opcode::SDivChk0;
    sdiv.result = 1;
    sdiv.operands.push_back(Value::temp(0));
    sdiv.operands.push_back(Value::constInt(2));

    entry.instructions.push_back(makeEhPush("handler"));
    entry.instructions.push_back(std::move(sdiv));
    entry.instructions.push_back(makeEhPop());
    entry.instructions.push_back(makeRet(1));
    entry.terminated = true;

    BasicBlock handler;
    handler.label = "handler";
    handler.instructions.push_back(makeRet(0));
    handler.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(handler));
    mod.functions.push_back(std::move(fn));

    bool changed = il::transform::ehOpt(mod);
    EXPECT_FALSE(changed);
}

TEST(EHOpt, NoChangeWhenNoPairs) {
    Module mod;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    {
        Param p;
        p.name = "x";
        p.type = Type(Type::Kind::I64);
        p.id = 0;
        entry.params.push_back(std::move(p));
    }
    entry.instructions.push_back(makeAdd(1, 0, 0));
    entry.instructions.push_back(makeRet(1));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    mod.functions.push_back(std::move(fn));

    bool changed = il::transform::ehOpt(mod);
    EXPECT_FALSE(changed);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
