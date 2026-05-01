//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for the extended Mem2Reg pass — specifically the removal of the
// entry-block-only restriction.  An alloca in a non-entry block is now
// promotable when its defining block dominates all blocks containing uses.
//
// Test cases:
//   1. Single-block alloca in non-entry block — always promotable.
//   2. Multi-block alloca where defining block dominates all uses — promotable.
//   3. Multi-block alloca where defining block does NOT dominate a use — not promoted.
//   4. Loop-header block params remain deterministic and branch edges are repaired.
//   5. Cross-block alloca in a re-entered loop header — not promoted.
//   6. EH functions are skipped until mem2reg has an exception-aware CFG.
//
//===----------------------------------------------------------------------===//

#include "il/transform/Mem2Reg.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/Verifier.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace {

/// @brief Count opcode in function.
unsigned countOpcodeInFunction(const Function &fn, Opcode op) {
    unsigned count = 0;
    for (const auto &bb : fn.blocks)
        for (const auto &instr : bb.instructions)
            if (instr.op == op)
                ++count;
    return count;
}

const BasicBlock *findBlock(const Function &fn, const std::string &label) {
    for (const auto &block : fn.blocks)
        if (block.label == label)
            return &block;
    return nullptr;
}

/// Build a function with an alloca in a non-entry block (single-block use).
///
///   fn single_block_nonentry() -> i64:
///     entry:
///       br middle
///     middle:
///       t0 = alloca 8         ; alloca in non-entry block
///       store i64 42, t0
///       t1 = load i64, t0    ; use in same block -> singleBlock=true
///       ret t1
Module buildSingleBlockNonEntryAlloca() {
    Module module;
    Function fn;
    fn.name = "single_block_nonentry";
    fn.retType = Type(Type::Kind::I64);

    unsigned id = 0;

    // entry block
    BasicBlock entry;
    entry.label = "entry";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("middle");
        br.brArgs.push_back({});
        entry.instructions.push_back(std::move(br));
        entry.terminated = true;
    }

    // middle block — alloca + store + load + ret, all in same block
    BasicBlock middle;
    middle.label = "middle";
    {
        Instr alloca_;
        alloca_.result = id++;
        alloca_.op = Opcode::Alloca;
        alloca_.type = Type(Type::Kind::Ptr);
        alloca_.operands.push_back(Value::constInt(8));
        middle.instructions.push_back(std::move(alloca_)); // t0

        Instr store;
        store.op = Opcode::Store;
        store.type = Type(Type::Kind::I64);
        store.operands.push_back(Value::temp(0)); // ptr t0
        store.operands.push_back(Value::constInt(42));
        middle.instructions.push_back(std::move(store));

        Instr load;
        load.result = id++;
        load.op = Opcode::Load;
        load.type = Type(Type::Kind::I64);
        load.operands.push_back(Value::temp(0));        // ptr t0
        middle.instructions.push_back(std::move(load)); // t1

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(1));
        middle.instructions.push_back(std::move(ret));
        middle.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(middle));
    fn.valueNames.resize(id);
    fn.valueNames[0] = "ptr";
    fn.valueNames[1] = "val";
    module.functions.push_back(std::move(fn));
    return module;
}

Module buildEhFunctionWithHandlerStore() {
    Module module;
    Function fn;
    fn.name = "eh_store";
    fn.retType = Type(Type::Kind::I1);

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr alloca_;
        alloca_.result = 0;
        alloca_.op = Opcode::Alloca;
        alloca_.type = Type(Type::Kind::Ptr);
        alloca_.operands.push_back(Value::constInt(8));
        entry.instructions.push_back(std::move(alloca_));

        Instr init;
        init.op = Opcode::Store;
        init.type = Type(Type::Kind::I1);
        init.operands.push_back(Value::temp(0));
        init.operands.push_back(Value::constInt(0));
        entry.instructions.push_back(std::move(init));

        Instr push;
        push.op = Opcode::EhPush;
        push.type = Type(Type::Kind::Void);
        push.labels.push_back("handler");
        entry.instructions.push_back(std::move(push));

        Instr div;
        div.result = 1;
        div.op = Opcode::SDivChk0;
        div.type = Type(Type::Kind::I64);
        div.operands.push_back(Value::constInt(1));
        div.operands.push_back(Value::constInt(0));
        entry.instructions.push_back(std::move(div));

        Instr pop;
        pop.op = Opcode::EhPop;
        pop.type = Type(Type::Kind::Void);
        entry.instructions.push_back(std::move(pop));

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("done");
        br.brArgs.push_back({});
        entry.instructions.push_back(std::move(br));
        entry.terminated = true;
    }

    BasicBlock done;
    done.label = "done";
    {
        Instr load;
        load.result = 2;
        load.op = Opcode::Load;
        load.type = Type(Type::Kind::I1);
        load.operands.push_back(Value::temp(0));
        done.instructions.push_back(std::move(load));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(2));
        done.instructions.push_back(std::move(ret));
        done.terminated = true;
    }

    BasicBlock handler;
    handler.label = "handler";
    handler.params.push_back(Param{"err", Type(Type::Kind::Error), 3});
    handler.params.push_back(Param{"tok", Type(Type::Kind::ResumeTok), 4});
    {
        Instr marker;
        marker.op = Opcode::EhEntry;
        marker.type = Type(Type::Kind::Void);
        handler.instructions.push_back(std::move(marker));

        Instr caught;
        caught.op = Opcode::Store;
        caught.type = Type(Type::Kind::I1);
        caught.operands.push_back(Value::temp(0));
        caught.operands.push_back(Value::constInt(1));
        handler.instructions.push_back(std::move(caught));

        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.type = Type(Type::Kind::Void);
        resume.operands.push_back(Value::temp(4));
        resume.labels.push_back("done");
        resume.brArgs.push_back({});
        handler.instructions.push_back(std::move(resume));
        handler.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(done));
    fn.blocks.push_back(std::move(handler));
    fn.valueNames.resize(5);
    module.functions.push_back(std::move(fn));
    return module;
}

/// Build a function where an if-branch alloca dominates a successor's use.
///
///   fn dominating_nonentry() -> i64:
///     entry:
///       cbr 1, then, else
///     then:
///       t0 = alloca 8
///       store i64 7, t0
///       t1 = load i64, t0
///       br merge, t1
///     else:
///       br merge, i64 0
///     merge(x: i64):
///       ret x
///
/// The alloca is in "then" which dominates only the then→merge path — but all
/// uses of t0 are in "then", so singleBlock=true → promoted.
Module buildDominatingNonEntryAlloca() {
    Module module;
    Function fn;
    fn.name = "dominating_nonentry";
    fn.retType = Type(Type::Kind::I64);

    unsigned id = 0;

    // entry: unconditionally enter "then" for simplicity (cbr cond=1)
    BasicBlock entry;
    entry.label = "entry";
    {
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands.push_back(Value::constInt(1));
        cbr.labels.push_back("then");
        cbr.labels.push_back("els");
        cbr.brArgs.push_back({});
        cbr.brArgs.push_back({});
        entry.instructions.push_back(std::move(cbr));
        entry.terminated = true;
    }

    // then: alloca + store + load → br merge
    BasicBlock then_;
    then_.label = "then";
    {
        Instr alloca_;
        alloca_.result = id++; // t0
        alloca_.op = Opcode::Alloca;
        alloca_.type = Type(Type::Kind::Ptr);
        alloca_.operands.push_back(Value::constInt(8));
        then_.instructions.push_back(std::move(alloca_));

        Instr store;
        store.op = Opcode::Store;
        store.type = Type(Type::Kind::I64);
        store.operands.push_back(Value::temp(0));
        store.operands.push_back(Value::constInt(7));
        then_.instructions.push_back(std::move(store));

        Instr load;
        load.result = id++; // t1
        load.op = Opcode::Load;
        load.type = Type(Type::Kind::I64);
        load.operands.push_back(Value::temp(0));
        then_.instructions.push_back(std::move(load));

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("merge");
        br.brArgs.push_back({Value::temp(1)});
        then_.instructions.push_back(std::move(br));
        then_.terminated = true;
    }

    // else: br merge with constant 0
    BasicBlock else_;
    else_.label = "els";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("merge");
        br.brArgs.push_back({Value::constInt(0)});
        else_.instructions.push_back(std::move(br));
        else_.terminated = true;
    }

    // merge(x: i64): ret x
    BasicBlock merge;
    merge.label = "merge";
    {
        Param p;
        p.id = id++; // t2
        p.type = Type(Type::Kind::I64);
        p.name = "x";
        merge.params.push_back(std::move(p));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(2));
        merge.instructions.push_back(std::move(ret));
        merge.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(then_));
    fn.blocks.push_back(std::move(else_));
    fn.blocks.push_back(std::move(merge));
    fn.valueNames.resize(id);
    fn.valueNames[0] = "ptr";
    fn.valueNames[1] = "val";
    fn.valueNames[2] = "x";
    module.functions.push_back(std::move(fn));
    return module;
}

Module buildEntryLoadBeforeStoreAlloca() {
    Module module;
    Function fn;
    fn.name = "entry_default_load";
    fn.retType = Type(Type::Kind::I64);

    unsigned id = 0;

    BasicBlock entry;
    entry.label = "entry";

    Instr alloca_;
    alloca_.result = id++;
    alloca_.op = Opcode::Alloca;
    alloca_.type = Type(Type::Kind::Ptr);
    alloca_.operands.push_back(Value::constInt(8));
    entry.instructions.push_back(std::move(alloca_));

    Instr load;
    load.result = id++;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.operands.push_back(Value::temp(0));
    entry.instructions.push_back(std::move(load));

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::I64);
    store.operands.push_back(Value::temp(0));
    store.operands.push_back(Value::constInt(7));
    entry.instructions.push_back(std::move(store));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(1));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(id);
    fn.valueNames[0] = "ptr";
    fn.valueNames[1] = "val";
    module.functions.push_back(std::move(fn));
    return module;
}

Module buildLoopHeaderTwoPromotedValues() {
    Module module;
    Function fn;
    fn.name = "loop_header_two_values";
    fn.retType = Type(Type::Kind::I64);

    unsigned id = 0;

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr allocaX;
        allocaX.result = id++;
        allocaX.op = Opcode::Alloca;
        allocaX.type = Type(Type::Kind::Ptr);
        allocaX.operands.push_back(Value::constInt(8));
        entry.instructions.push_back(std::move(allocaX));

        Instr allocaY;
        allocaY.result = id++;
        allocaY.op = Opcode::Alloca;
        allocaY.type = Type(Type::Kind::Ptr);
        allocaY.operands.push_back(Value::constInt(8));
        entry.instructions.push_back(std::move(allocaY));

        Instr storeX;
        storeX.op = Opcode::Store;
        storeX.type = Type(Type::Kind::I64);
        storeX.operands = {Value::temp(0), Value::constInt(1)};
        entry.instructions.push_back(std::move(storeX));

        Instr storeY;
        storeY.op = Opcode::Store;
        storeY.type = Type(Type::Kind::I64);
        storeY.operands = {Value::temp(1), Value::constInt(2)};
        entry.instructions.push_back(std::move(storeY));

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels = {"header"};
        br.brArgs = {{}};
        entry.instructions.push_back(std::move(br));
        entry.terminated = true;
    }

    BasicBlock header;
    header.label = "header";
    {
        Instr loadX;
        loadX.result = id++;
        loadX.op = Opcode::Load;
        loadX.type = Type(Type::Kind::I64);
        loadX.operands = {Value::temp(0)};
        header.instructions.push_back(std::move(loadX));

        Instr loadY;
        loadY.result = id++;
        loadY.op = Opcode::Load;
        loadY.type = Type(Type::Kind::I64);
        loadY.operands = {Value::temp(1)};
        header.instructions.push_back(std::move(loadY));

        Instr sum;
        sum.result = id++;
        sum.op = Opcode::IAddOvf;
        sum.type = Type(Type::Kind::I64);
        sum.operands = {Value::temp(2), Value::temp(3)};
        header.instructions.push_back(std::move(sum));

        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands = {Value::constBool(true)};
        cbr.labels = {"exit", "latch"};
        cbr.brArgs = {{Value::temp(4)}, {}};
        header.instructions.push_back(std::move(cbr));
        header.terminated = true;
    }

    BasicBlock latch;
    latch.label = "latch";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels = {"header"};
        br.brArgs = {{}};
        latch.instructions.push_back(std::move(br));
        latch.terminated = true;
    }

    BasicBlock exit;
    exit.label = "exit";
    {
        Param result;
        result.id = id++;
        result.type = Type(Type::Kind::I64);
        result.name = "result";
        exit.params.push_back(result);

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(result.id)};
        exit.instructions.push_back(std::move(ret));
        exit.terminated = true;
    }

    fn.blocks = {std::move(entry), std::move(header), std::move(latch), std::move(exit)};
    fn.valueNames.resize(id);
    module.functions.push_back(std::move(fn));
    return module;
}

Module buildLoopHeaderDynamicAlloca() {
    Module module;
    Function fn;
    fn.name = "loop_header_dynamic_alloca";
    fn.retType = Type(Type::Kind::I64);

    unsigned id = 0;

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels = {"header"};
        br.brArgs = {{}};
        entry.instructions.push_back(std::move(br));
        entry.terminated = true;
    }

    BasicBlock header;
    header.label = "header";
    {
        Instr alloca_;
        alloca_.result = id++;
        alloca_.op = Opcode::Alloca;
        alloca_.type = Type(Type::Kind::Ptr);
        alloca_.operands = {Value::constInt(8)};
        header.instructions.push_back(std::move(alloca_));

        Instr store;
        store.op = Opcode::Store;
        store.type = Type(Type::Kind::I64);
        store.operands = {Value::temp(0), Value::constInt(0)};
        header.instructions.push_back(std::move(store));

        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands = {Value::constBool(true)};
        cbr.labels = {"body", "exit"};
        cbr.brArgs = {{}, {}};
        header.instructions.push_back(std::move(cbr));
        header.terminated = true;
    }

    BasicBlock body;
    body.label = "body";
    {
        Instr load;
        load.result = id++;
        load.op = Opcode::Load;
        load.type = Type(Type::Kind::I64);
        load.operands = {Value::temp(0)};
        body.instructions.push_back(std::move(load));

        Instr inc;
        inc.result = id++;
        inc.op = Opcode::IAddOvf;
        inc.type = Type(Type::Kind::I64);
        inc.operands = {Value::temp(1), Value::constInt(1)};
        body.instructions.push_back(std::move(inc));

        Instr store;
        store.op = Opcode::Store;
        store.type = Type(Type::Kind::I64);
        store.operands = {Value::temp(0), Value::temp(2)};
        body.instructions.push_back(std::move(store));

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels = {"header"};
        br.brArgs = {{}};
        body.instructions.push_back(std::move(br));
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

    fn.blocks = {std::move(entry), std::move(header), std::move(body), std::move(exit)};
    fn.valueNames.resize(id);
    module.functions.push_back(std::move(fn));
    return module;
}

} // namespace

// A single-block alloca in a non-entry block must be promoted.
// Previously this would be silently skipped (entry-block-only restriction).
TEST(Mem2RegNonEntry, SingleBlockNonEntryAllocaIsPromoted) {
    Module module = buildSingleBlockNonEntryAlloca();
    ASSERT_FALSE(module.functions.empty());

    // Before: 1 alloca, 1 store, 1 load
    unsigned allocasBefore = countOpcodeInFunction(module.functions[0], Opcode::Alloca);
    ASSERT_EQ(allocasBefore, 1u);

    viper::passes::Mem2RegStats stats{};
    viper::passes::mem2reg(module, &stats);

    // After: alloca and load must be removed (promoted to SSA)
    unsigned allocasAfter = countOpcodeInFunction(module.functions[0], Opcode::Alloca);
    unsigned loadsAfter = countOpcodeInFunction(module.functions[0], Opcode::Load);
    EXPECT_EQ(allocasAfter, 0u);
    EXPECT_EQ(loadsAfter, 0u);
}

// A non-entry-block alloca where all uses are in the same block (singleBlock=true)
// and the module has a conditional branch — must be promoted.
TEST(Mem2RegNonEntry, DominatingNonEntryAllocaIsPromoted) {
    Module module = buildDominatingNonEntryAlloca();
    ASSERT_FALSE(module.functions.empty());

    unsigned allocasBefore = countOpcodeInFunction(module.functions[0], Opcode::Alloca);
    ASSERT_EQ(allocasBefore, 1u);

    viper::passes::Mem2RegStats stats{};
    viper::passes::mem2reg(module, &stats);

    // Alloca and its load must be gone after promotion
    unsigned allocasAfter = countOpcodeInFunction(module.functions[0], Opcode::Alloca);
    unsigned loadsAfter = countOpcodeInFunction(module.functions[0], Opcode::Load);
    EXPECT_EQ(allocasAfter, 0u);
    EXPECT_EQ(loadsAfter, 0u);
}

TEST(Mem2RegNonEntry, EntryLoadBeforeStorePromotesToDefaultValueWithoutEntryPhi) {
    Module module = buildEntryLoadBeforeStoreAlloca();
    ASSERT_FALSE(module.functions.empty());

    viper::passes::Mem2RegStats stats{};
    viper::passes::mem2reg(module, &stats);

    const Function &fn = module.functions.front();
    ASSERT_EQ(countOpcodeInFunction(fn, Opcode::Alloca), 0u);
    ASSERT_EQ(countOpcodeInFunction(fn, Opcode::Load), 0u);
    ASSERT_FALSE(fn.blocks.empty());
    EXPECT_TRUE(fn.blocks.front().params.empty());

    const Instr &ret = fn.blocks.front().instructions.back();
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_EQ(ret.operands.size(), 1u);
    EXPECT_EQ(ret.operands[0].kind, Value::Kind::ConstInt);
    if (ret.operands[0].kind == Value::Kind::ConstInt)
        EXPECT_EQ(ret.operands[0].i64, 0);
}

TEST(Mem2RegNonEntry, LoopHeaderParamsAreDeterministicAndEdgesAreRepaired) {
    Module module = buildLoopHeaderTwoPromotedValues();

    viper::passes::Mem2RegStats stats{};
    viper::passes::mem2reg(module, &stats);

    ASSERT_TRUE(il::verify::Verifier::verify(module).hasValue());

    const Function &fn = module.functions.front();
    const BasicBlock *header = findBlock(fn, "header");
    ASSERT_NE(header, nullptr);
    ASSERT_EQ(header->params.size(), 2u);
    EXPECT_LT(header->params[0].id, header->params[1].id);

    const BasicBlock *entry = findBlock(fn, "entry");
    ASSERT_NE(entry, nullptr);
    const Instr &entryTerm = entry->instructions.back();
    ASSERT_EQ(entryTerm.brArgs.size(), 1u);
    ASSERT_EQ(entryTerm.brArgs[0].size(), 2u);
    EXPECT_EQ(entryTerm.brArgs[0][0].kind, Value::Kind::ConstInt);
    EXPECT_EQ(entryTerm.brArgs[0][1].kind, Value::Kind::ConstInt);
    if (entryTerm.brArgs[0][0].kind == Value::Kind::ConstInt)
        EXPECT_EQ(entryTerm.brArgs[0][0].i64, 1);
    if (entryTerm.brArgs[0][1].kind == Value::Kind::ConstInt)
        EXPECT_EQ(entryTerm.brArgs[0][1].i64, 2);

    const BasicBlock *latch = findBlock(fn, "latch");
    ASSERT_NE(latch, nullptr);
    const Instr &latchTerm = latch->instructions.back();
    ASSERT_EQ(latchTerm.brArgs.size(), 1u);
    ASSERT_EQ(latchTerm.brArgs[0].size(), 2u);
}

TEST(Mem2RegNonEntry, ReenteredNonEntryAllocaIsNotPromotedAcrossBlocks) {
    Module module = buildLoopHeaderDynamicAlloca();

    viper::passes::Mem2RegStats stats{};
    viper::passes::mem2reg(module, &stats);

    ASSERT_TRUE(il::verify::Verifier::verify(module).hasValue());
    const Function &fn = module.functions.front();
    EXPECT_EQ(stats.promotedVars, 0u);
    EXPECT_EQ(countOpcodeInFunction(fn, Opcode::Alloca), 1u);
    EXPECT_EQ(countOpcodeInFunction(fn, Opcode::Load), 1u);
    EXPECT_EQ(countOpcodeInFunction(fn, Opcode::Store), 2u);
}

TEST(Mem2RegNonEntry, ExceptionHandlingFunctionIsSkipped) {
    Module module = buildEhFunctionWithHandlerStore();
    ASSERT_TRUE(il::verify::Verifier::verify(module).hasValue());

    viper::passes::Mem2RegStats stats{};
    viper::passes::mem2reg(module, &stats);

    ASSERT_TRUE(il::verify::Verifier::verify(module).hasValue());
    const Function &fn = module.functions.front();
    EXPECT_EQ(stats.promotedVars, 0u);
    EXPECT_EQ(countOpcodeInFunction(fn, Opcode::Alloca), 1u);
    EXPECT_EQ(countOpcodeInFunction(fn, Opcode::Load), 1u);
    EXPECT_EQ(countOpcodeInFunction(fn, Opcode::Store), 2u);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
