//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_codegen_x86_64_passes.cpp
// Purpose: Unit tests for the x86-64 codegen pass manager and individual passes.
// Key invariants: Passes respect prerequisite state and report diagnostics accordingly.
// Ownership/Lifetime: Tests construct Module and Diagnostics instances on the stack.
// Links: src/codegen/x86_64/passes
//
//===----------------------------------------------------------------------===//
#include "codegen/x86_64/MachineIR.hpp"
#include "codegen/x86_64/Scheduler.hpp"
#include "codegen/x86_64/passes/BinaryEmitPass.hpp"
#include "codegen/x86_64/passes/EmitPass.hpp"
#include "codegen/x86_64/passes/LegalizePass.hpp"
#include "codegen/x86_64/passes/LoweringPass.hpp"
#include "codegen/x86_64/passes/PassManager.hpp"
#include "codegen/x86_64/passes/PeepholePass.hpp"
#include "codegen/x86_64/passes/RegAllocPass.hpp"
#include "codegen/x86_64/passes/SchedulerPass.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <memory>
#include <string>

using namespace viper::codegen::x64::passes;

namespace {

il::core::Module makeRetConstModule(long long value) {
    il::core::Module module{};

    il::core::Function fn;
    fn.name = "main";
    fn.retType = il::core::Type(il::core::Type::Kind::I64);

    il::core::BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    il::core::Instr ret;
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    ret.operands.push_back(il::core::Value::constInt(value));
    entry.instructions.push_back(ret);

    fn.blocks.push_back(entry);
    module.functions.push_back(fn);
    return module;
}

il::core::Module makeMalformedVoidCallModule() {
    il::core::Module module{};

    il::core::Function fn;
    fn.name = "main";
    fn.retType = il::core::Type(il::core::Type::Kind::I64);

    il::core::BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    il::core::Instr call;
    call.op = il::core::Opcode::Call;
    call.type = il::core::Type(il::core::Type::Kind::Void);
    call.callee = "broken";
    call.result = 1;
    entry.instructions.push_back(call);

    il::core::Instr ret;
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    ret.operands.push_back(il::core::Value::constInt(0));
    entry.instructions.push_back(ret);

    fn.blocks.push_back(entry);
    module.functions.push_back(fn);
    return module;
}

il::core::Module makeStoreF64ImmediateModule() {
    il::core::Module module{};

    il::core::Function fn;
    fn.name = "main";
    fn.retType = il::core::Type(il::core::Type::Kind::Void);

    il::core::BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    il::core::Instr alloca;
    alloca.op = il::core::Opcode::Alloca;
    alloca.type = il::core::Type(il::core::Type::Kind::Ptr);
    alloca.result = 1;
    alloca.operands.push_back(il::core::Value::constInt(8));
    entry.instructions.push_back(alloca);

    il::core::Instr store;
    store.op = il::core::Opcode::Store;
    store.type = il::core::Type(il::core::Type::Kind::F64);
    store.operands.push_back(il::core::Value::temp(1));
    store.operands.push_back(il::core::Value::constFloat(50.0));
    entry.instructions.push_back(store);

    il::core::Instr ret;
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    entry.instructions.push_back(ret);

    fn.blocks.push_back(entry);
    module.functions.push_back(fn);
    return module;
}

il::core::Module makeStoreFunctionAddressModule() {
    il::core::Module module{};

    il::core::Function callee;
    callee.name = "callee";
    callee.retType = il::core::Type(il::core::Type::Kind::Void);

    il::core::BasicBlock calleeEntry;
    calleeEntry.label = "entry";
    calleeEntry.terminated = true;

    il::core::Instr calleeRet;
    calleeRet.op = il::core::Opcode::Ret;
    calleeRet.type = il::core::Type(il::core::Type::Kind::Void);
    calleeEntry.instructions.push_back(calleeRet);
    callee.blocks.push_back(calleeEntry);
    module.functions.push_back(callee);

    il::core::Function main;
    main.name = "main";
    main.retType = il::core::Type(il::core::Type::Kind::Void);

    il::core::BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    il::core::Instr alloca;
    alloca.op = il::core::Opcode::Alloca;
    alloca.type = il::core::Type(il::core::Type::Kind::Ptr);
    alloca.result = 1;
    alloca.operands.push_back(il::core::Value::constInt(8));
    entry.instructions.push_back(alloca);

    il::core::Instr store;
    store.op = il::core::Opcode::Store;
    store.type = il::core::Type(il::core::Type::Kind::Ptr);
    store.operands.push_back(il::core::Value::temp(1));
    store.operands.push_back(il::core::Value::global("callee"));
    entry.instructions.push_back(store);

    il::core::Instr ret;
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    entry.instructions.push_back(ret);

    main.blocks.push_back(entry);
    module.functions.push_back(main);
    return module;
}

il::core::Module makeErrGetMsgModule() {
    il::core::Module module{};

    il::core::Function fn;
    fn.name = "main";
    fn.retType = il::core::Type(il::core::Type::Kind::Str);

    il::core::BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    il::core::Instr getMsg;
    getMsg.op = il::core::Opcode::ErrGetMsg;
    getMsg.type = il::core::Type(il::core::Type::Kind::Str);
    getMsg.result = 1;
    entry.instructions.push_back(getMsg);

    il::core::Instr ret;
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    ret.operands.push_back(il::core::Value::temp(1));
    entry.instructions.push_back(ret);

    fn.blocks.push_back(entry);
    module.functions.push_back(fn);
    return module;
}

il::core::Module makeNarrowI16Module() {
    il::core::Module module{};

    il::core::Function fn;
    fn.name = "main";
    fn.retType = il::core::Type(il::core::Type::Kind::I16);
    fn.params.push_back({"x", il::core::Type(il::core::Type::Kind::I64), 0});

    il::core::BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    entry.params.push_back({"x", il::core::Type(il::core::Type::Kind::I64), 0});

    il::core::Instr narrow;
    narrow.op = il::core::Opcode::CastSiNarrowChk;
    narrow.type = il::core::Type(il::core::Type::Kind::I16);
    narrow.result = 1;
    narrow.operands.push_back(il::core::Value::temp(0));
    entry.instructions.push_back(narrow);

    il::core::Instr ret;
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    ret.operands.push_back(il::core::Value::temp(1));
    entry.instructions.push_back(ret);

    fn.blocks.push_back(entry);
    module.functions.push_back(fn);
    return module;
}

il::core::Module makeOutOfOrderBlockParamUseModule() {
    using il::core::BasicBlock;
    using il::core::Function;
    using il::core::Instr;
    using il::core::Module;
    using il::core::Opcode;
    using il::core::Param;
    using il::core::Type;
    using il::core::Value;

    Module module{};

    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);
    fn.valueNames.resize(6);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    Instr entryBr;
    entryBr.op = Opcode::Br;
    entryBr.type = Type(Type::Kind::Void);
    entryBr.labels.push_back("loop");
    entryBr.brArgs.push_back({Value::constInt(0)});
    entry.instructions.push_back(entryBr);

    BasicBlock loop;
    loop.label = "loop";
    loop.params.push_back(Param{"i", Type(Type::Kind::I64), 1});
    loop.terminated = true;
    Instr cmp;
    cmp.op = Opcode::SCmpLT;
    cmp.type = Type(Type::Kind::I1);
    cmp.result = 3;
    cmp.operands.push_back(Value::temp(1));
    cmp.operands.push_back(Value::constInt(10));
    loop.instructions.push_back(cmp);
    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.type = Type(Type::Kind::Void);
    cbr.operands.push_back(Value::temp(3));
    cbr.labels = {"body", "exit"};
    cbr.brArgs.push_back({Value::temp(1)});
    cbr.brArgs.push_back({});
    loop.instructions.push_back(cbr);

    // Textually before "body", but it uses body's block parameter. This mirrors
    // optimized BASIC for-loop shapes such as Scoreboard.GetRank.
    BasicBlock inc;
    inc.label = "inc";
    inc.terminated = true;
    Instr add;
    add.op = Opcode::IAddOvf;
    add.type = Type(Type::Kind::I64);
    add.result = 5;
    add.operands.push_back(Value::temp(2));
    add.operands.push_back(Value::constInt(1));
    inc.instructions.push_back(add);
    Instr back;
    back.op = Opcode::Br;
    back.type = Type(Type::Kind::Void);
    back.labels.push_back("loop");
    back.brArgs.push_back({Value::temp(5)});
    inc.instructions.push_back(back);

    BasicBlock body;
    body.label = "body";
    body.params.push_back(Param{"body_i", Type(Type::Kind::I64), 2});
    body.terminated = true;
    Instr toInc;
    toInc.op = Opcode::Br;
    toInc.type = Type(Type::Kind::Void);
    toInc.labels.push_back("inc");
    toInc.brArgs.push_back({});
    body.instructions.push_back(toInc);

    BasicBlock exit;
    exit.label = "exit";
    exit.terminated = true;
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(0));
    exit.instructions.push_back(ret);

    fn.blocks = {entry, loop, inc, body, exit};
    module.functions.push_back(fn);
    return module;
}

il::core::Module makeOutOfOrderInstructionResultUseModule() {
    using il::core::BasicBlock;
    using il::core::Function;
    using il::core::Instr;
    using il::core::Module;
    using il::core::Opcode;
    using il::core::Type;
    using il::core::Value;

    Module module{};

    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);
    fn.valueNames.resize(4);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    Instr entryBr;
    entryBr.op = Opcode::Br;
    entryBr.type = Type(Type::Kind::Void);
    entryBr.labels.push_back("def");
    entry.instructions.push_back(entryBr);

    // Textually before "def", but "def" dominates this block and produces
    // %t2. Optimized for-in lowering can create this shape on Windows x64.
    BasicBlock use;
    use.label = "use";
    use.terminated = true;
    Instr useAdd;
    useAdd.op = Opcode::IAddOvf;
    useAdd.type = Type(Type::Kind::I64);
    useAdd.result = 3;
    useAdd.operands.push_back(Value::temp(2));
    useAdd.operands.push_back(Value::constInt(1));
    use.instructions.push_back(useAdd);
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(3));
    use.instructions.push_back(ret);

    BasicBlock def;
    def.label = "def";
    def.terminated = true;
    Instr defAdd;
    defAdd.op = Opcode::IAddOvf;
    defAdd.type = Type(Type::Kind::I64);
    defAdd.result = 2;
    defAdd.operands.push_back(Value::constInt(41));
    defAdd.operands.push_back(Value::constInt(0));
    def.instructions.push_back(defAdd);
    Instr toUse;
    toUse.op = Opcode::Br;
    toUse.type = Type(Type::Kind::Void);
    toUse.labels.push_back("use");
    def.instructions.push_back(toUse);

    fn.blocks = {entry, use, def};
    module.functions.push_back(fn);
    return module;
}

il::core::Module makeOutOfOrderEdgeArgumentUseModule() {
    using il::core::BasicBlock;
    using il::core::Function;
    using il::core::Instr;
    using il::core::Module;
    using il::core::Opcode;
    using il::core::Param;
    using il::core::Type;
    using il::core::Value;

    Module module{};

    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);
    fn.valueNames.resize(5);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    Instr entryBr;
    entryBr.op = Opcode::Br;
    entryBr.type = Type(Type::Kind::Void);
    entryBr.labels.push_back("def");
    entry.instructions.push_back(entryBr);

    // Textually before "def", but the only path reaches this block after
    // "def" creates %t2. Its branch passes %t2 to a block parameter.
    BasicBlock update;
    update.label = "update";
    update.terminated = true;
    Instr toJoin;
    toJoin.op = Opcode::Br;
    toJoin.type = Type(Type::Kind::Void);
    toJoin.labels.push_back("join");
    toJoin.brArgs.push_back({Value::temp(2)});
    update.instructions.push_back(toJoin);

    BasicBlock join;
    join.label = "join";
    join.params.push_back(Param{"x", Type(Type::Kind::I64), 4});
    join.terminated = true;
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(4));
    join.instructions.push_back(ret);

    BasicBlock def;
    def.label = "def";
    def.terminated = true;
    Instr defAdd;
    defAdd.op = Opcode::IAddOvf;
    defAdd.type = Type(Type::Kind::I64);
    defAdd.result = 2;
    defAdd.operands.push_back(Value::constInt(41));
    defAdd.operands.push_back(Value::constInt(1));
    def.instructions.push_back(defAdd);
    Instr toUpdate;
    toUpdate.op = Opcode::Br;
    toUpdate.type = Type(Type::Kind::Void);
    toUpdate.labels.push_back("update");
    def.instructions.push_back(toUpdate);

    fn.blocks = {entry, update, join, def};
    module.functions.push_back(fn);
    return module;
}

std::size_t binarySizeForOptLevel(int optimizeLevel) {
    Module module{};
    module.il = makeRetConstModule(0);
    module.options.optimizeLevel = optimizeLevel;
    Diagnostics diags{};

    PassManager pm{};
    pm.addPass(std::make_unique<LoweringPass>());
    pm.addPass(std::make_unique<LegalizePass>());
    pm.addPass(std::make_unique<RegAllocPass>());
    pm.addPass(std::make_unique<SchedulerPass>());
    pm.addPass(std::make_unique<PeepholePass>());

    viper::codegen::x64::CodegenOptions opts{};
    opts.optimizeLevel = optimizeLevel;
    pm.addPass(std::make_unique<BinaryEmitPass>(opts));

    if (!pm.run(module, diags))
        return 0;

    std::size_t size = 0;
    for (const auto &section : module.binaryTextSections)
        size += section.bytes().size();
    if (size != 0 || !module.binaryTextSections.empty())
        return size;
    return module.binaryText ? module.binaryText->bytes().size() : 0;
}

bool runThroughRegAlloc(Module &module, Diagnostics &diags) {
    PassManager pm{};
    pm.addPass(std::make_unique<LoweringPass>());
    pm.addPass(std::make_unique<LegalizePass>());
    pm.addPass(std::make_unique<RegAllocPass>());
    return pm.run(module, diags);
}

viper::codegen::x64::Operand physGpr(viper::codegen::x64::PhysReg reg) {
    return viper::codegen::x64::OpReg{
        true, viper::codegen::x64::RegClass::GPR, static_cast<uint16_t>(reg)};
}

viper::codegen::x64::Operand physXmm(viper::codegen::x64::PhysReg reg) {
    return viper::codegen::x64::OpReg{
        true, viper::codegen::x64::RegClass::XMM, static_cast<uint16_t>(reg)};
}

viper::codegen::x64::Operand rbpMem(int disp) {
    viper::codegen::x64::OpMem mem{};
    mem.base = viper::codegen::x64::OpReg{true,
                                          viper::codegen::x64::RegClass::GPR,
                                          static_cast<uint16_t>(viper::codegen::x64::PhysReg::RBP)};
    mem.disp = disp;
    return mem;
}

} // namespace

TEST(LoweringPass, HandlesEmptyModule) {
    Module module{};
    Diagnostics diags{};
    LoweringPass pass{};
    EXPECT_TRUE(pass.run(module, diags));
    EXPECT_TRUE(module.lowered.has_value());
    EXPECT_EQ(module.lowered->funcs.size(), 0U);
    EXPECT_FALSE(diags.hasErrors());
}

TEST(LoweringPass, RejectsVoidCallWithResultId) {
    Module module{};
    module.il = makeMalformedVoidCallModule();
    Diagnostics diags{};
    LoweringPass pass{};
    EXPECT_FALSE(pass.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
}

TEST(LoweringPass, PreservesF64ImmediateStoreOperandKind) {
    Module module{};
    module.il = makeStoreF64ImmediateModule();

    Diagnostics diags{};
    LoweringPass pass{};
    ASSERT_TRUE(pass.run(module, diags));
    ASSERT_FALSE(diags.hasErrors());
    ASSERT_TRUE(module.lowered.has_value());
    ASSERT_EQ(module.lowered->funcs.size(), 1U);
    ASSERT_EQ(module.lowered->funcs[0].blocks.size(), 1U);

    const auto &instrs = module.lowered->funcs[0].blocks[0].instrs;
    const auto it = std::find_if(
        instrs.begin(), instrs.end(), [](const auto &instr) { return instr.opcode == "store"; });
    ASSERT_TRUE(it != instrs.end());
    ASSERT_GE(it->ops.size(), 2U);
    EXPECT_EQ(it->ops[0].kind, viper::codegen::x64::ILValue::Kind::PTR);
    EXPECT_EQ(it->ops[1].kind, viper::codegen::x64::ILValue::Kind::F64);
    EXPECT_EQ(it->ops[1].id, -1);
    EXPECT_NEAR(it->ops[1].f64, 50.0, 1e-12);
}

TEST(LoweringPass, PreservesFunctionAddressStoreOperandKind) {
    Module module{};
    module.il = makeStoreFunctionAddressModule();

    Diagnostics diags{};
    LoweringPass pass{};
    ASSERT_TRUE(pass.run(module, diags));
    ASSERT_FALSE(diags.hasErrors());
    ASSERT_TRUE(module.lowered.has_value());
    ASSERT_EQ(module.lowered->funcs.size(), 2U);
    ASSERT_EQ(module.lowered->funcs[1].blocks.size(), 1U);

    const auto &instrs = module.lowered->funcs[1].blocks[0].instrs;
    const auto it = std::find_if(
        instrs.begin(), instrs.end(), [](const auto &instr) { return instr.opcode == "store"; });
    ASSERT_TRUE(it != instrs.end());
    ASSERT_GE(it->ops.size(), 2U);
    EXPECT_EQ(it->ops[0].kind, viper::codegen::x64::ILValue::Kind::PTR);
    EXPECT_EQ(it->ops[1].kind, viper::codegen::x64::ILValue::Kind::LABEL);
    EXPECT_EQ(it->ops[1].label, "callee");
}

TEST(LoweringPass, LowersErrGetMsgToRuntimeStringCall) {
    Module module{};
    module.il = makeErrGetMsgModule();

    Diagnostics diags{};
    LoweringPass pass{};
    ASSERT_TRUE(pass.run(module, diags));
    ASSERT_FALSE(diags.hasErrors());
    ASSERT_TRUE(module.lowered.has_value());

    const auto &instrs = module.lowered->funcs[0].blocks[0].instrs;
    const auto it = std::find_if(instrs.begin(), instrs.end(), [](const auto &instr) {
        return instr.opcode == "call" && !instr.ops.empty() &&
               instr.ops[0].kind == viper::codegen::x64::ILValue::Kind::LABEL &&
               instr.ops[0].label == "rt_throw_msg_get";
    });
    ASSERT_TRUE(it != instrs.end());
    EXPECT_EQ(it->resultKind, viper::codegen::x64::ILValue::Kind::STR);
}

TEST(LoweringPass, PreservesCheckedNarrowResultWidth) {
    Module module{};
    module.il = makeNarrowI16Module();

    Diagnostics diags{};
    LoweringPass pass{};
    ASSERT_TRUE(pass.run(module, diags));
    ASSERT_FALSE(diags.hasErrors());

    const auto &instrs = module.lowered->funcs[0].blocks[0].instrs;
    const auto it = std::find_if(instrs.begin(), instrs.end(), [](const auto &instr) {
        return instr.opcode == "si_narrow_chk";
    });
    ASSERT_TRUE(it != instrs.end());
    EXPECT_EQ(it->resultBits, 16);
}

TEST(LoweringPass, PreRegistersBlockParamsBeforeTextualUse) {
    Module module{};
    module.il = makeOutOfOrderBlockParamUseModule();

    Diagnostics diags{};
    LoweringPass pass{};
    ASSERT_TRUE(pass.run(module, diags));
    ASSERT_FALSE(diags.hasErrors());
    ASSERT_TRUE(module.lowered.has_value());
    ASSERT_EQ(module.lowered->funcs[0].blocks.size(), 5U);

    const auto &incInstrs = module.lowered->funcs[0].blocks[2].instrs;
    ASSERT_FALSE(incInstrs.empty());
    EXPECT_EQ(incInstrs[0].opcode, "iadd.ovf");
    ASSERT_FALSE(incInstrs[0].ops.empty());
    EXPECT_EQ(incInstrs[0].ops[0].id, 2);
    EXPECT_EQ(incInstrs[0].ops[0].kind, viper::codegen::x64::ILValue::Kind::I64);
}

TEST(LoweringPass, PreRegistersInstructionResultsBeforeTextualUse) {
    Module module{};
    module.il = makeOutOfOrderInstructionResultUseModule();

    Diagnostics diags{};
    LoweringPass pass{};
    ASSERT_TRUE(pass.run(module, diags));
    ASSERT_FALSE(diags.hasErrors());
    ASSERT_TRUE(module.lowered.has_value());
    ASSERT_EQ(module.lowered->funcs[0].blocks.size(), 3U);

    const auto &useInstrs = module.lowered->funcs[0].blocks[1].instrs;
    ASSERT_FALSE(useInstrs.empty());
    EXPECT_EQ(useInstrs[0].opcode, "iadd.ovf");
    ASSERT_FALSE(useInstrs[0].ops.empty());
    EXPECT_EQ(useInstrs[0].ops[0].id, 2);
    EXPECT_EQ(useInstrs[0].ops[0].kind, viper::codegen::x64::ILValue::Kind::I64);
}

TEST(LegalizePass, FailsWhenLoweringMissing) {
    Module module{};
    Diagnostics diags{};
    LegalizePass pass{};
    EXPECT_FALSE(pass.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
    EXPECT_FALSE(module.legalised);
}

TEST(LegalizePass, MarksModuleWhenLoweringReady) {
    Module module{};
    module.il = makeRetConstModule(7);
    LoweringPass lower{};
    Diagnostics lowerDiags{};
    ASSERT_TRUE(lower.run(module, lowerDiags));
    Diagnostics diags{};
    LegalizePass pass{};
    EXPECT_TRUE(pass.run(module, diags));
    EXPECT_TRUE(module.legalised);
    EXPECT_EQ(module.mir.size(), 1U);
    EXPECT_EQ(module.frames.size(), 1U);
    EXPECT_FALSE(diags.hasErrors());
}

TEST(LegalizePass, ResolvesOutOfOrderInstructionResultEdgeArgument) {
    Module module{};
    module.il = makeOutOfOrderEdgeArgumentUseModule();

    LoweringPass lower{};
    Diagnostics lowerDiags{};
    ASSERT_TRUE(lower.run(module, lowerDiags));
    ASSERT_FALSE(lowerDiags.hasErrors());

    Diagnostics diags{};
    LegalizePass pass{};
    EXPECT_TRUE(pass.run(module, diags));
    EXPECT_FALSE(diags.hasErrors());
    EXPECT_TRUE(module.legalised);
    EXPECT_EQ(module.mir.size(), 1U);
}

TEST(RegAllocPass, RequiresLegalize) {
    Module module{};
    Diagnostics diags{};
    RegAllocPass pass{};
    EXPECT_FALSE(pass.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
    module.il = makeRetConstModule(11);
    Diagnostics diagsSuccess{};
    ASSERT_TRUE(runThroughRegAlloc(module, diagsSuccess));
    EXPECT_TRUE(module.registersAllocated);
}

TEST(SchedulerPass, RequiresRegAlloc) {
    Module module{};
    Diagnostics diags{};
    SchedulerPass pass{};
    EXPECT_FALSE(pass.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
}

TEST(SchedulerPass, PreservesWin64FrameSetupBeforeCalleeSaves) {
    using namespace viper::codegen::x64;

    MBasicBlock entry{};
    entry.label = "entry";
    entry.instructions.push_back(MInstr::make(MOpcode::PUSH, {physGpr(PhysReg::RBP)}));
    entry.instructions.push_back(
        MInstr::make(MOpcode::MOVrr, {physGpr(PhysReg::RBP), physGpr(PhysReg::RSP)}));
    entry.instructions.push_back(MInstr::make(MOpcode::ADDri, {physGpr(PhysReg::RSP), OpImm{-96}}));
    entry.instructions.push_back(MInstr::make(MOpcode::MOVrm, {rbpMem(-8), physGpr(PhysReg::RBX)}));
    entry.instructions.push_back(
        MInstr::make(MOpcode::MOVrm, {rbpMem(-16), physGpr(PhysReg::RDI)}));
    entry.instructions.push_back(
        MInstr::make(MOpcode::MOVUPSrm, {rbpMem(-32), physXmm(PhysReg::XMM6)}));
    entry.instructions.push_back(
        MInstr::make(MOpcode::MOVmr, {physGpr(PhysReg::RAX), rbpMem(-64)}));
    entry.instructions.push_back(
        MInstr::make(MOpcode::ADDrr, {physGpr(PhysReg::RAX), physGpr(PhysReg::RDX)}));

    MFunction fn{};
    fn.name = "prologue_order";
    fn.blocks.push_back(std::move(entry));

    scheduleFunction(fn);

    const auto &instrs = fn.blocks[0].instructions;
    ASSERT_EQ(instrs.size(), 8U);
    EXPECT_EQ(instrs[0].opcode, MOpcode::PUSH);
    EXPECT_EQ(instrs[1].opcode, MOpcode::MOVrr);
    EXPECT_EQ(instrs[2].opcode, MOpcode::ADDri);
    EXPECT_EQ(instrs[3].opcode, MOpcode::MOVrm);
    EXPECT_EQ(instrs[4].opcode, MOpcode::MOVrm);
    EXPECT_EQ(instrs[5].opcode, MOpcode::MOVUPSrm);
    EXPECT_EQ(instrs[6].opcode, MOpcode::MOVmr);
    EXPECT_EQ(instrs[7].opcode, MOpcode::ADDrr);
}

TEST(EmitPass, ProducesAssembly) {
    Module module{};
    module.il = makeRetConstModule(42);
    Diagnostics diags{};
    ASSERT_TRUE(runThroughRegAlloc(module, diags));
    EmitPass pass{viper::codegen::x64::CodegenOptions{}};
    EXPECT_TRUE(pass.run(module, diags));
    EXPECT_TRUE(module.codegenResult.has_value());
    EXPECT_TRUE(module.codegenResult->asmText.find("ret") != std::string::npos);
    EXPECT_FALSE(diags.hasErrors());
}

TEST(CodegenOptions, OptimizeLevelDefaultsToOne) {
    viper::codegen::x64::CodegenOptions opts{};
    EXPECT_EQ(opts.optimizeLevel, 1);
}

TEST(CodegenOptions, OptimizeLevelZeroIsValid) {
    viper::codegen::x64::CodegenOptions opts{};
    opts.optimizeLevel = 0;
    EXPECT_EQ(opts.optimizeLevel, 0);
}

TEST(BinaryEmitPass, HonorsOptimizeLevel) {
    const std::size_t o0Size = binarySizeForOptLevel(0);
    const std::size_t o1Size = binarySizeForOptLevel(1);
    EXPECT_TRUE(o0Size > 0U);
    EXPECT_TRUE(o1Size > 0U);
}

TEST(BinaryEmitPass, ReportsEncoderValidationFailures) {
    Module module{};
    module.target = &viper::codegen::x64::hostTarget();
    module.registersAllocated = true;

    viper::codegen::x64::MFunction fn{};
    fn.name = "bad_mem_index";

    viper::codegen::x64::MBasicBlock bb{};
    bb.label = ".Lbad_mem_index";

    viper::codegen::x64::OpMem badMem{};
    badMem.base =
        viper::codegen::x64::makePhysReg(viper::codegen::x64::RegClass::GPR,
                                         static_cast<uint16_t>(viper::codegen::x64::PhysReg::RAX));
    badMem.index =
        viper::codegen::x64::makePhysReg(viper::codegen::x64::RegClass::GPR,
                                         static_cast<uint16_t>(viper::codegen::x64::PhysReg::RSP));
    badMem.scale = 1;
    badMem.hasIndex = true;

    bb.append(viper::codegen::x64::MInstr::make(
        viper::codegen::x64::MOpcode::MOVmr,
        {viper::codegen::x64::makePhysRegOperand(
             viper::codegen::x64::RegClass::GPR,
             static_cast<uint16_t>(viper::codegen::x64::PhysReg::RAX)),
         viper::codegen::x64::Operand{badMem}}));
    fn.addBlock(std::move(bb));

    module.mir.push_back(std::move(fn));
    module.frames.push_back(viper::codegen::x64::FrameInfo{});

    Diagnostics diags{};
    viper::codegen::x64::CodegenOptions opts{};
    BinaryEmitPass pass{opts};
    EXPECT_FALSE(pass.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
}

TEST(PassManager, ShortCircuitsOnFailure) {
    Module module{};
    Diagnostics diags{};
    PassManager pm{};
    pm.addPass(std::make_unique<LegalizePass>());
    pm.addPass(std::make_unique<RegAllocPass>());
    pm.addPass(std::make_unique<EmitPass>(viper::codegen::x64::CodegenOptions{}));
    EXPECT_FALSE(pm.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
    EXPECT_FALSE(module.registersAllocated);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
