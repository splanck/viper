//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file
/// @brief Regression tests for stable IL core storage and identifier symbols.
///
/// @details These tests exercise the structural guarantees that compiler
///          analyses depend on: instruction and block addresses must survive
///          unrelated container mutation, module-owned symbol sidecars must
///          mirror string identifiers, and branch/call mutators must keep
///          metadata internally consistent.
///
//===----------------------------------------------------------------------===//

#include "il/analysis/CFG.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/StableList.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tests/TestHarness.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using il::build::IRBuilder;
using il::core::BasicBlock;
using il::core::Extern;
using il::core::Function;
using il::core::Global;
using il::core::Instr;
using il::core::Module;
using il::core::Opcode;
using il::core::StableList;
using il::core::Type;
using il::core::Value;

namespace {

struct PoolProbe {
    static int live;
    int value{0};

    explicit PoolProbe(int input = 0) : value(input) {
        ++live;
    }
    PoolProbe(const PoolProbe &other) : value(other.value) {
        ++live;
    }
    PoolProbe(PoolProbe &&other) noexcept : value(other.value) {
        ++live;
    }
    ~PoolProbe() {
        --live;
    }
};

int PoolProbe::live = 0;

/// @brief Create a representative arithmetic instruction for storage tests.
/// @param op Opcode to place in the instruction.
/// @param result SSA temporary produced by the instruction.
/// @return Instruction with two constant integer operands and an i64 result.
[[nodiscard]] Instr makeArithmeticInstr(Opcode op, unsigned result) {
    Instr instr;
    instr.result = result;
    instr.op = op;
    instr.type = Type(Type::Kind::I64);
    instr.operands = {Value::constInt(static_cast<long long>(result)),
                      Value::constInt(static_cast<long long>(result + 1U))};
    return instr;
}

/// @brief Create a labelled block used by stable block-storage tests.
/// @param label Block label to assign.
/// @return Basic block containing no instructions.
[[nodiscard]] BasicBlock makeBlock(std::string label) {
    BasicBlock block;
    block.label = std::move(label);
    return block;
}

/// @brief Convert a module-owned symbol to an owning string for assertions.
/// @param module Module that owns the symbol table.
/// @param symbol Symbol to look up.
/// @return String copy of the interned identifier, or empty when invalid.
[[nodiscard]] std::string symbolText(const Module &module, il::support::Symbol symbol) {
    return std::string(module.lookupIdentifier(symbol));
}

/// @brief Create a module with direct calls and branches built through raw fields.
/// @return Module whose identifier sidecars are intentionally unpopulated until
///         Module::internOwnedIdentifiers() is called.
[[nodiscard]] Module makeUninternedModule() {
    Module module;

    Extern ext;
    ext.name = "callee";
    ext.retType = Type(Type::Kind::I64);
    ext.params = {Type(Type::Kind::I64)};
    module.externs.push_back(std::move(ext));

    Global global;
    global.name = "global_value";
    global.type = Type(Type::Kind::I64);
    module.globals.push_back(std::move(global));

    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry = makeBlock("entry");

    Instr call;
    call.result = 0U;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.callee = "callee";
    call.operands = {Value::constInt(7)};
    entry.instructions.push_back(std::move(call));

    Instr branch;
    branch.op = Opcode::Br;
    branch.type = Type(Type::Kind::Void);
    branch.labels = {"exit"};
    branch.brArgs = {{}};
    entry.instructions.push_back(std::move(branch));
    entry.terminated = true;

    BasicBlock exit = makeBlock("exit");
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    exit.instructions.push_back(std::move(ret));
    exit.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(exit));
    module.functions.push_back(std::move(fn));
    return module;
}

} // namespace

TEST(IRCoreStructures, InstructionPointersSurviveUnrelatedBlockMutation) {
    BasicBlock block = makeBlock("entry");
    block.instructions.push_back(makeArithmeticInstr(Opcode::Add, 0U));
    Instr *firstInstr = &block.instructions.front();

    for (unsigned i = 1; i < 96; ++i)
        block.instructions.push_back(makeArithmeticInstr(Opcode::Sub, i));

    block.instructions.insert(block.instructions.begin() + 1,
                              makeArithmeticInstr(Opcode::Mul, 1000U));
    block.instructions.erase(block.instructions.begin() + 1);

    ASSERT_EQ(firstInstr, &block.instructions.front());
    ASSERT_TRUE(firstInstr->result.has_value());
    EXPECT_EQ(*firstInstr->result, 0U);
    EXPECT_EQ(firstInstr->op, Opcode::Add);
}

TEST(IRCoreStructures, BlockPointersSurviveUnrelatedFunctionMutation) {
    Function fn;
    fn.name = "stable_blocks";
    fn.retType = Type(Type::Kind::Void);
    fn.blocks.push_back(makeBlock("entry"));
    BasicBlock *entry = &fn.blocks.front();

    for (unsigned i = 0; i < 64; ++i)
        fn.blocks.push_back(makeBlock("block_" + std::to_string(i)));

    fn.blocks.insert(fn.blocks.begin() + 1, makeBlock("inserted"));
    fn.blocks.erase(fn.blocks.begin() + 1);

    ASSERT_EQ(entry, &fn.blocks.front());
    EXPECT_EQ(entry->label, std::string("entry"));
}

TEST(IRCoreStructures, ModuleInterningPopulatesDeclarationAndInstructionSidecars) {
    Module module = makeUninternedModule();
    module.internOwnedIdentifiers();

    ASSERT_TRUE(static_cast<bool>(module.externs.front().nameSymbol));
    ASSERT_TRUE(static_cast<bool>(module.globals.front().nameSymbol));
    ASSERT_TRUE(static_cast<bool>(module.functions.front().nameSymbol));

    EXPECT_EQ(symbolText(module, module.externs.front().nameSymbol), std::string("callee"));
    EXPECT_EQ(symbolText(module, module.globals.front().nameSymbol), std::string("global_value"));
    EXPECT_EQ(symbolText(module, module.functions.front().nameSymbol), std::string("main"));

    auto &entry = module.functions.front().blocks.front();
    auto &exit = module.functions.front().blocks[1];
    ASSERT_TRUE(static_cast<bool>(entry.labelSymbol));
    ASSERT_TRUE(static_cast<bool>(exit.labelSymbol));
    EXPECT_EQ(symbolText(module, entry.labelSymbol), std::string("entry"));
    EXPECT_EQ(symbolText(module, exit.labelSymbol), std::string("exit"));

    const Instr &call = entry.instructions.front();
    ASSERT_TRUE(static_cast<bool>(call.calleeSymbol));
    EXPECT_EQ(symbolText(module, call.calleeSymbol), std::string("callee"));

    const Instr &branch = entry.instructions.back();
    ASSERT_EQ(branch.labelSymbols.size(), 1U);
    ASSERT_TRUE(branch.labelSymbols.front() == exit.labelSymbol);
}

TEST(IRCoreStructures, IRBuilderMaintainsSymbolsWhileConstructingControlFlow) {
    Module module;
    IRBuilder builder(module);
    builder.addExtern("callee", Type(Type::Kind::Void), {});
    Function &fn = builder.startFunction("main", Type(Type::Kind::Void), {});
    BasicBlock &entry = builder.createBlock(fn, "entry");
    BasicBlock &exit = builder.createBlock(fn, "exit");

    builder.setInsertPoint(entry);
    builder.emitCall("callee", {}, std::nullopt, {});
    builder.br(exit);

    builder.setInsertPoint(exit);
    builder.emitRet(std::nullopt, {});

    const Instr &call = entry.instructions.front();
    ASSERT_TRUE(static_cast<bool>(call.calleeSymbol));
    EXPECT_EQ(symbolText(module, call.calleeSymbol), std::string("callee"));

    const Instr &branch = entry.instructions.back();
    ASSERT_EQ(branch.labelSymbols.size(), 1U);
    ASSERT_TRUE(branch.labelSymbols.front() == exit.labelSymbol);
}

TEST(IRCoreStructures, InstrMutatorsValidateAndRefreshMetadata) {
    Module module;

    Instr call;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::Void);
    call.setDirectCallee(module, "callee");
    ASSERT_TRUE(static_cast<bool>(call.calleeSymbol));
    EXPECT_EQ(symbolText(module, call.calleeSymbol), std::string("callee"));
    call.clearDirectCallee();
    EXPECT_TRUE(call.callee.empty());
    EXPECT_FALSE(static_cast<bool>(call.calleeSymbol));

    Instr branch;
    branch.op = Opcode::CBr;
    branch.type = Type(Type::Kind::Void);
    branch.operands = {Value::constBool(true)};
    branch.setBranchTargets(module, {"then", "else"}, {{}, {Value::constInt(42)}});
    ASSERT_EQ(branch.labels.size(), 2U);
    ASSERT_EQ(branch.labelSymbols.size(), 2U);
    EXPECT_EQ(symbolText(module, branch.labelSymbols[0]), std::string("then"));
    EXPECT_EQ(symbolText(module, branch.labelSymbols[1]), std::string("else"));
    EXPECT_THROWS(branch.setBranchTargets({"only_one"}, {{}, {}}), std::invalid_argument);

    Instr indirect;
    indirect.op = Opcode::CallIndirect;
    indirect.type = Type(Type::Kind::I64);
    indirect.setIndirectSignature(Type(Type::Kind::I64), {Type(Type::Kind::I64)}, true);
    EXPECT_TRUE(indirect.hasIndirectCallSignature());
    ASSERT_EQ(indirect.indirectParamTypes.size(), 1U);
    indirect.clearIndirectSignature();
    EXPECT_FALSE(indirect.hasIndirectCallSignature());
    EXPECT_TRUE(indirect.indirectParamTypes.empty());
}

TEST(IRCoreStructures, CFGContextIndexesInternedBlockLabels) {
    Module module = makeUninternedModule();
    module.internOwnedIdentifiers();

    zanna::analysis::CFGContext cfg(module);
    Function &fn = module.functions.front();
    BasicBlock &entry = fn.blocks.front();
    BasicBlock &exit = fn.blocks[1];

    const auto fnIt = cfg.functionLabelSymbolToBlock.find(&fn);
    ASSERT_TRUE(fnIt != cfg.functionLabelSymbolToBlock.end());
    ASSERT_TRUE(fnIt->second.contains(entry.labelSymbol));
    ASSERT_TRUE(fnIt->second.contains(exit.labelSymbol));
    ASSERT_EQ(fnIt->second.at(exit.labelSymbol), &exit);

    const auto &succ = zanna::analysis::successors(cfg, entry);
    ASSERT_EQ(succ.size(), 1U);
    EXPECT_EQ(succ.front(), &exit);
}

TEST(IRCoreStructures, CFGContextDoesNotInternUnownedIdentifierSidecars) {
    Module module = makeUninternedModule();
    Function &fn = module.functions.front();
    ASSERT_FALSE(fn.nameSymbol);
    ASSERT_FALSE(fn.blocks.front().labelSymbol);
    ASSERT_TRUE(fn.blocks.front().instructions.front().labelSymbols.empty());

    zanna::analysis::CFGContext cfg(module);

    EXPECT_TRUE(cfg.valid());
    EXPECT_FALSE(fn.nameSymbol);
    EXPECT_FALSE(fn.blocks.front().labelSymbol);
    EXPECT_TRUE(fn.blocks.front().instructions.front().labelSymbols.empty());
    const auto &succ = zanna::analysis::successors(cfg, fn.blocks.front());
    ASSERT_EQ(succ.size(), 1U);
    EXPECT_EQ(succ.front(), &fn.blocks[1]);
}

TEST(IRCoreStructures, StableListPoolPreservesAddressesAndReusesErasedSlots) {
    ASSERT_EQ(PoolProbe::live, 0);
    {
        StableList<PoolProbe> values;
        values.reserve(130);
        for (int value = 0; value < 100; ++value)
            values.emplace_back(value);

        PoolProbe *first = &values.front();
        PoolProbe *erased = &values[40];
        values.erase(values.begin() + 40);
        ASSERT_EQ(PoolProbe::live, 99);
        values.emplace_back(999);
        EXPECT_EQ(&values.back(), erased);
        EXPECT_EQ(&values.front(), first);

        StableList<PoolProbe> copy = values;
        ASSERT_EQ(copy.size(), values.size());
        EXPECT_NE(&copy.front(), &values.front());
        EXPECT_EQ(copy.front().value, values.front().value);

        PoolProbe *movedFirst = &copy.front();
        StableList<PoolProbe> moved = std::move(copy);
        EXPECT_EQ(&moved.front(), movedFirst);
        EXPECT_TRUE(copy.empty());
    }
    EXPECT_EQ(PoolProbe::live, 0);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
