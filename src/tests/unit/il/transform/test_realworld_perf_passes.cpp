//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/il/transform/test_realworld_perf_passes.cpp
// Purpose: Regression coverage for real-world O2 performance-enabling passes.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/runtime/RuntimeOwnership.hpp"
#include "il/transform/PassManager.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace il::core;

namespace {

Instr makeCall(std::string callee,
               Type type,
               std::vector<Value> operands,
               std::optional<unsigned> result = std::nullopt) {
    Instr instr;
    instr.op = Opcode::Call;
    instr.type = type;
    instr.result = result;
    instr.callee = std::move(callee);
    instr.operands = std::move(operands);
    return instr;
}

Instr makeRet(Value value) {
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {std::move(value)};
    return ret;
}

Function makeFunction(std::string name, Type retType) {
    Function fn;
    fn.name = std::move(name);
    fn.retType = retType;
    return fn;
}

void runPass(Module &module, const std::string &passId) {
    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false);
    ASSERT_TRUE(pm.run(module, {passId}));
}

bool hasCallNamed(const Function &fn, const std::string &callee) {
    for (const auto &block : fn.blocks)
        for (const auto &instr : block.instructions)
            if (instr.op == Opcode::Call && instr.callee == callee)
                return true;
    return false;
}

} // namespace

TEST(RealWorldPerfPasses, RuntimeOwnershipClassifiesArrayAndObjectHelpers) {
    const auto objNew = il::runtime::classifyRuntimeOwnership("rt_obj_new_i64");
    EXPECT_TRUE(objNew.returnsOwned);
    EXPECT_TRUE(objNew.mayAllocate);

    const auto arrRelease = il::runtime::classifyRuntimeOwnership("rt_arr_i64_release");
    EXPECT_TRUE(arrRelease.consumesArg(0));

    const auto retainKnown = il::runtime::classifyRuntimeOwnership("rt_obj_retain_known");
    EXPECT_TRUE(retainKnown.retainsArg(0));

    const auto releaseKnown = il::runtime::classifyRuntimeOwnership("rt_obj_release_known_check0");
    EXPECT_TRUE(releaseKnown.consumesArg(0));

    const auto memoryRetain = il::runtime::classifyRuntimeOwnership("Viper.Memory.Retain");
    EXPECT_TRUE(memoryRetain.retainsArg(0));

    const auto boxStr = il::runtime::classifyRuntimeOwnership("Viper.Core.Box.Str");
    EXPECT_TRUE(boxStr.retainsArg(0));
    EXPECT_TRUE(boxStr.returnsOwned);

    const auto msgSub = il::runtime::classifyRuntimeOwnership("Viper.Core.MessageBus.Subscribe");
    EXPECT_TRUE(msgSub.retainsArg(1));
    EXPECT_TRUE(msgSub.retainsArg(2));
}

TEST(RealWorldPerfPasses, OwnershipOptRemovesLocalRetainReleasePair) {
    Module module;
    Function fn = makeFunction("retain_release", Type(Type::Kind::I64));
    Param strParam;
    strParam.name = "s";
    strParam.type = Type(Type::Kind::Str);
    strParam.id = 0;
    fn.params.push_back(strParam);

    BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back(
        makeCall("rt_str_retain_maybe", Type(Type::Kind::Void), {Value::temp(0)}));

    Instr add;
    add.op = Opcode::Add;
    add.result = 1;
    add.type = Type(Type::Kind::I64);
    add.operands = {Value::constInt(3), Value::constInt(4)};
    entry.instructions.push_back(std::move(add));

    entry.instructions.push_back(
        makeCall("rt_str_release_maybe", Type(Type::Kind::Void), {Value::temp(0)}));
    entry.instructions.push_back(makeRet(Value::temp(1)));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(2);
    module.functions.push_back(std::move(fn));

    runPass(module, "ownership-opt");

    const Function &out = module.functions.front();
    EXPECT_FALSE(hasCallNamed(out, "rt_str_retain_maybe"));
    EXPECT_FALSE(hasCallNamed(out, "rt_str_release_maybe"));
}

TEST(RealWorldPerfPasses, ArrayFastPathRewritesAfterIdxCheck) {
    Module module;
    Function fn = makeFunction("array_fastpath", Type(Type::Kind::I64));
    fn.params.push_back(Param{"arr", Type(Type::Kind::Ptr), 0});
    fn.params.push_back(Param{"idx", Type(Type::Kind::I64), 1});

    BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back(
        makeCall("rt_arr_i64_len", Type(Type::Kind::I64), {Value::temp(0)}, 2));

    Instr chk;
    chk.op = Opcode::IdxChk;
    chk.result = 3;
    chk.type = Type(Type::Kind::I64);
    chk.operands = {Value::temp(1), Value::constInt(0), Value::temp(2)};
    entry.instructions.push_back(std::move(chk));

    entry.instructions.push_back(
        makeCall("rt_arr_i64_get", Type(Type::Kind::I64), {Value::temp(0), Value::temp(1)}, 4));
    entry.instructions.push_back(makeRet(Value::temp(4)));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(5);
    module.functions.push_back(std::move(fn));

    runPass(module, "array-fastpath");

    EXPECT_TRUE(hasCallNamed(module.functions.front(), "rt_arr_i64_get_fast"));
    EXPECT_FALSE(hasCallNamed(module.functions.front(), "rt_arr_i64_get"));
}

TEST(RealWorldPerfPasses, ArrayFastPathDoesNotCrossUnknownMemoryEffects) {
    Module module;
    Function fn = makeFunction("array_fastpath_blocked", Type(Type::Kind::I64));
    fn.params.push_back(Param{"arr", Type(Type::Kind::Ptr), 0});
    fn.params.push_back(Param{"idx", Type(Type::Kind::I64), 1});

    BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back(
        makeCall("rt_arr_i64_len", Type(Type::Kind::I64), {Value::temp(0)}, 2));

    Instr chk;
    chk.op = Opcode::IdxChk;
    chk.result = 3;
    chk.type = Type(Type::Kind::I64);
    chk.operands = {Value::temp(1), Value::constInt(0), Value::temp(2)};
    entry.instructions.push_back(std::move(chk));

    entry.instructions.push_back(
        makeCall("unknown_may_resize", Type(Type::Kind::Void), {Value::temp(0)}));
    entry.instructions.push_back(
        makeCall("rt_arr_i64_get", Type(Type::Kind::I64), {Value::temp(0), Value::temp(1)}, 4));
    entry.instructions.push_back(makeRet(Value::temp(4)));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(5);
    module.functions.push_back(std::move(fn));

    runPass(module, "array-fastpath");

    EXPECT_TRUE(hasCallNamed(module.functions.front(), "rt_arr_i64_get"));
    EXPECT_FALSE(hasCallNamed(module.functions.front(), "rt_arr_i64_get_fast"));
}

TEST(RealWorldPerfPasses, RuntimeFastPathRewritesKnownObjectReferenceCounting) {
    Module module;
    Function fn = makeFunction("known_object_rc", Type(Type::Kind::I1));

    BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back(
        makeCall("rt_obj_new_i64", Type(Type::Kind::Ptr), {Value::constInt(7), Value::constInt(16)}, 0));
    entry.instructions.push_back(
        makeCall("rt_obj_retain_maybe", Type(Type::Kind::Void), {Value::temp(0)}));
    entry.instructions.push_back(
        makeCall("rt_obj_release_check0", Type(Type::Kind::I1), {Value::temp(0)}, 1));
    entry.instructions.push_back(makeRet(Value::temp(1)));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(2);
    module.functions.push_back(std::move(fn));

    runPass(module, "runtime-fastpath");

    EXPECT_TRUE(hasCallNamed(module.functions.front(), "rt_obj_retain_known"));
    EXPECT_TRUE(hasCallNamed(module.functions.front(), "rt_obj_release_known_check0"));
    EXPECT_FALSE(hasCallNamed(module.functions.front(), "rt_obj_retain_maybe"));
    EXPECT_FALSE(hasCallNamed(module.functions.front(), "rt_obj_release_check0"));
}

TEST(RealWorldPerfPasses, DevirtualizeRewritesConstantFunctionPointerCall) {
    Module module;
    Function fn = makeFunction("devirt", Type(Type::Kind::I64));

    BasicBlock entry;
    entry.label = "entry";
    Instr gaddr;
    gaddr.op = Opcode::GAddr;
    gaddr.result = 0;
    gaddr.type = Type(Type::Kind::Ptr);
    gaddr.operands = {Value::global("target_fn")};
    entry.instructions.push_back(std::move(gaddr));

    Instr call;
    call.op = Opcode::CallIndirect;
    call.result = 1;
    call.type = Type(Type::Kind::I64);
    call.operands = {Value::temp(0), Value::constInt(42)};
    call.hasIndirectSignature = true;
    call.indirectRetType = Type(Type::Kind::I64);
    call.indirectParamTypes = {Type(Type::Kind::I64)};
    entry.instructions.push_back(std::move(call));
    entry.instructions.push_back(makeRet(Value::temp(1)));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(2);
    module.functions.push_back(std::move(fn));

    runPass(module, "devirt");

    const auto &rewritten = module.functions.front().blocks.front().instructions[1];
    ASSERT_EQ(rewritten.op, Opcode::Call);
    EXPECT_EQ(rewritten.callee, "target_fn");
    ASSERT_EQ(rewritten.operands.size(), 1u);
    EXPECT_EQ(rewritten.operands.front().kind, Value::Kind::ConstInt);
    EXPECT_FALSE(rewritten.hasIndirectSignature);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
