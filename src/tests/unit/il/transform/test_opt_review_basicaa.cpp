//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for BasicAA fixes from the IL optimization review:
// - computeCalleeEffect priority cascade (module-level is authoritative)
// - queryRuntimeEffect cached hash map lookup
// - ModRef classification correctness
//
//===----------------------------------------------------------------------===//

#include "il/analysis/BasicAA.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;
using zanna::analysis::AliasResult;
using zanna::analysis::BasicAA;
using zanna::analysis::ModRefResult;

namespace {

/// @brief Make alloca.
Instr makeAlloca(unsigned id) {
    Instr instr;
    instr.result = id;
    instr.op = Opcode::Alloca;
    instr.type = Type(Type::Kind::Ptr);
    instr.operands.push_back(Value::constInt(8));
    return instr;
}

/// @brief Make call.
Instr makeCall(std::string callee) {
    Instr instr;
    instr.op = Opcode::Call;
    instr.callee = std::move(callee);
    return instr;
}

} // namespace

// Test that module-level function attributes take priority over runtime
// signatures. The fix changed computeCalleeEffect from OR-merging to
// a priority cascade where module definitions are authoritative.
TEST(BasicAA, ModuleFunctionOverridesRuntimeSignature) {
    Module module;
    il::build::IRBuilder builder(module);

    // Create a function marked as NOT pure, NOT readonly in the module
    Function &callee = builder.startFunction("my_callee", Type(Type::Kind::Void), {});
    builder.createBlock(callee, "entry");
    BasicBlock &calleeEntry = callee.blocks.front();
    builder.setInsertPoint(calleeEntry);
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    calleeEntry.instructions.push_back(std::move(ret));
    calleeEntry.terminated = true;

    // Explicitly NOT pure and NOT readonly
    callee.attrs().pure = false;
    callee.attrs().readonly = false;

    // Create a caller function
    Function &caller = builder.startFunction("caller", Type(Type::Kind::Void), {});
    builder.createBlock(caller, "entry");
    BasicBlock &callerEntry = caller.blocks.front();
    builder.setInsertPoint(callerEntry);

    unsigned allocaId = builder.reserveTempId();
    callerEntry.instructions.push_back(makeAlloca(allocaId));

    Instr retCaller;
    retCaller.op = Opcode::Ret;
    retCaller.type = Type(Type::Kind::Void);
    callerEntry.instructions.push_back(std::move(retCaller));
    callerEntry.terminated = true;

    BasicAA aa(module, caller);

    // Call to module-defined function that is NOT pure/readonly
    Instr call = makeCall("my_callee");
    ModRefResult result = aa.modRef(call);

    // Should be ModRef because the module says it's not pure/readonly,
    // regardless of what a runtime signature might say
    EXPECT_EQ(result, ModRefResult::ModRef);
}

// Test that when a module function is marked pure, modRef returns NoModRef
TEST(BasicAA, ModulePureFunctionReturnsNoModRef) {
    Module module;
    il::build::IRBuilder builder(module);

    Function &callee = builder.startFunction("pure_callee", Type(Type::Kind::Void), {});
    builder.createBlock(callee, "entry");
    BasicBlock &calleeEntry = callee.blocks.front();
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    calleeEntry.instructions.push_back(std::move(ret));
    calleeEntry.terminated = true;
    callee.attrs().pure = true;

    Function &caller = builder.startFunction("caller", Type(Type::Kind::Void), {});
    builder.createBlock(caller, "entry");
    BasicBlock &callerEntry = caller.blocks.front();
    Instr retCaller;
    retCaller.op = Opcode::Ret;
    retCaller.type = Type(Type::Kind::Void);
    callerEntry.instructions.push_back(std::move(retCaller));
    callerEntry.terminated = true;

    BasicAA aa(module, caller);

    Instr call = makeCall("pure_callee");
    EXPECT_EQ(aa.modRef(call), ModRefResult::NoModRef);
}

// Test that a module function marked readonly returns Ref
TEST(BasicAA, ModuleReadonlyFunctionReturnsRef) {
    Module module;
    il::build::IRBuilder builder(module);

    Function &callee = builder.startFunction("ro_callee", Type(Type::Kind::Void), {});
    builder.createBlock(callee, "entry");
    BasicBlock &calleeEntry = callee.blocks.front();
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    calleeEntry.instructions.push_back(std::move(ret));
    calleeEntry.terminated = true;
    callee.attrs().readonly = true;

    Function &caller = builder.startFunction("caller", Type(Type::Kind::Void), {});
    builder.createBlock(caller, "entry");
    BasicBlock &callerEntry = caller.blocks.front();
    Instr retCaller;
    retCaller.op = Opcode::Ret;
    retCaller.type = Type(Type::Kind::Void);
    callerEntry.instructions.push_back(std::move(retCaller));
    callerEntry.terminated = true;

    BasicAA aa(module, caller);

    Instr call = makeCall("ro_callee");
    EXPECT_EQ(aa.modRef(call), ModRefResult::Ref);
}

TEST(BasicAA, UnknownInstrPureAttrIsConservative) {
    Module module;
    Function fn;
    fn.name = "test_fn";
    fn.retType = Type(Type::Kind::Void);
    BasicBlock entry;
    entry.label = "entry";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;
    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));

    BasicAA aa(module, module.functions.front());

    Instr call = makeCall("unknown_fn");
    call.CallAttr.pure = true;
    EXPECT_EQ(aa.modRef(call), ModRefResult::ModRef);
}

// Test that non-call instructions return ModRef (conservative)
TEST(BasicAA, NonCallReturnsModRef) {
    Module module;
    Function fn;
    fn.name = "test_fn";
    fn.retType = Type(Type::Kind::Void);
    BasicBlock entry;
    entry.label = "entry";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;
    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));

    BasicAA aa(module, module.functions.front());

    Instr load;
    load.op = Opcode::Load;
    EXPECT_EQ(aa.modRef(load), ModRefResult::ModRef);
}

// Test distinct allocas are NoAlias
TEST(BasicAA, DistinctAllocasNoAlias) {
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test", Type(Type::Kind::Void), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks.front();
    builder.setInsertPoint(entry);

    unsigned idA = builder.reserveTempId();
    entry.instructions.push_back(makeAlloca(idA));
    unsigned idB = builder.reserveTempId();
    entry.instructions.push_back(makeAlloca(idB));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    BasicAA aa(module, fn);

    EXPECT_EQ(aa.alias(Value::temp(idA), Value::temp(idB)), AliasResult::NoAlias);
    EXPECT_EQ(aa.alias(Value::temp(idA), Value::temp(idA)), AliasResult::MustAlias);
}

// Test alloca vs global is NoAlias
TEST(BasicAA, AllocaVsGlobalNoAlias) {
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test", Type(Type::Kind::Void), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks.front();
    builder.setInsertPoint(entry);

    unsigned idA = builder.reserveTempId();
    entry.instructions.push_back(makeAlloca(idA));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    BasicAA aa(module, fn);

    EXPECT_EQ(aa.alias(Value::temp(idA), Value::global("some_global")), AliasResult::NoAlias);
}

TEST(BasicAA, NoAliasParamDoesNotDisambiguateGlobals) {
    Module module;
    Function fn;
    fn.name = "test_fn";
    fn.retType = Type(Type::Kind::Void);
    Param ptr{"p", Type(Type::Kind::Ptr), 0};
    ptr.setNoAlias(true);
    fn.params.push_back(ptr);

    BasicBlock entry;
    entry.label = "entry";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;
    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));

    BasicAA aa(module, module.functions.front());
    EXPECT_EQ(aa.alias(Value::temp(ptr.id), Value::global("g")), AliasResult::MayAlias);
}

TEST(BasicAA, BranchArgsPropagateAllocaEscapes) {
    Module module;
    Function fn;
    fn.name = "branch_escape";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";
    Instr alloca;
    alloca.result = 0;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(8));
    Instr br;
    br.op = Opcode::Br;
    br.type = Type(Type::Kind::Void);
    br.labels = {"sink_block"};
    br.brArgs = {{Value::temp(0)}};
    entry.instructions = {alloca, br};
    entry.terminated = true;

    BasicBlock sinkBlock;
    sinkBlock.label = "sink_block";
    Param p{"p", Type(Type::Kind::Ptr), 1};
    sinkBlock.params.push_back(p);
    Instr call = makeCall("unknown_sink");
    call.operands.push_back(Value::temp(p.id));
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    sinkBlock.instructions = {call, ret};
    sinkBlock.terminated = true;

    fn.blocks = {entry, sinkBlock};
    module.functions.push_back(std::move(fn));

    BasicAA aa(module, module.functions.front());
    EXPECT_FALSE(aa.isNonEscapingAlloca(0));
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
