//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/InteropIntegrationTests.cpp
// Purpose: Integration tests for cross-language interop — builds IL modules
//          with export/import linkage, generates boolean thunks, links them,
//          and verifies the merged module is structurally correct.
// Key invariants:
//   - Linked modules resolve all imports.
//   - Boolean thunks insert correct conversion opcodes.
//   - No function name collisions after linking.
// Ownership/Lifetime: Test-owned modules.
// Links: docs/adr/0003-il-linkage-and-module-linking.md
//
//===----------------------------------------------------------------------===//

#include "il/link/InteropThunks.hpp"
#include "il/link/ModuleLinker.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Linkage.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"

#include "tests/TestHarness.hpp"

#include <algorithm>
#include <string>

using namespace il::core;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Create a function with the given linkage. Export/Internal get a body; Import does not.
static Function makeFunc(const std::string &name,
                         Type retType,
                         std::vector<Param> params,
                         Linkage linkage)
{
    Function fn;
    fn.name = name;
    fn.retType = retType;
    fn.params = std::move(params);
    fn.linkage = linkage;

    if (linkage != Linkage::Import)
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr ret;
        ret.op = Opcode::Ret;
        if (retType.kind != Type::Kind::Void)
        {
            ret.type = retType;
            ret.operands.push_back(Value::constInt(42));
        }
        entry.instructions.push_back(ret);
        fn.blocks.push_back(std::move(entry));
    }

    return fn;
}

/// Create a main function that calls another function and returns the result.
static Function makeMainCalling(const std::string &callee, Type calleeRetType)
{
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);
    fn.linkage = Linkage::Internal;

    BasicBlock entry;
    entry.label = "entry";

    // %0 = call @callee()
    Instr call;
    call.op = Opcode::Call;
    call.callee = callee;
    call.type = calleeRetType;
    call.result = 0;
    entry.instructions.push_back(std::move(call));

    // ret i64 %0
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::I64);
    ret.operands.push_back(Value::temp(0));
    entry.instructions.push_back(std::move(ret));

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(1);
    return fn;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(InteropIntegration, LinkTwoModulesWithExportImport)
{
    // Module A (entry): has main that calls "helper", imports "helper".
    Module modA;
    modA.functions.push_back(makeMainCalling("helper", Type(Type::Kind::I64)));
    modA.functions.push_back(makeFunc("helper", Type(Type::Kind::I64), {}, Linkage::Import));

    // Module B (library): exports "helper".
    Module modB;
    modB.functions.push_back(makeFunc("helper", Type(Type::Kind::I64), {}, Linkage::Export));

    std::vector<Module> modules;
    modules.push_back(std::move(modA));
    modules.push_back(std::move(modB));

    auto result = il::link::linkModules(std::move(modules));
    ASSERT_TRUE(result.succeeded());

    // The merged module should have "main" and "helper" (no import stubs).
    bool hasMain = false;
    bool hasHelper = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
            hasMain = true;
        if (fn.name == "helper")
            hasHelper = true;
        // No import-linkage functions should remain.
        EXPECT_NE(fn.linkage, Linkage::Import);
    }
    EXPECT_TRUE(hasMain);
    EXPECT_TRUE(hasHelper);
}

TEST(InteropIntegration, BooleanThunksInsertedDuringLink)
{
    // Zia exports isReady() -> i1, BASIC imports it expecting i64.
    Module ziaMod;
    ziaMod.functions.push_back(makeFunc("isReady", Type(Type::Kind::I1), {}, Linkage::Export));

    Module basicMod;
    basicMod.functions.push_back(makeMainCalling("isReady", Type(Type::Kind::I64)));
    basicMod.functions.push_back(makeFunc("isReady", Type(Type::Kind::I64), {}, Linkage::Import));

    // Generate thunks from the import/export mismatch.
    auto thunks = il::link::generateBooleanThunks(basicMod, ziaMod);
    ASSERT_EQ(thunks.size(), 1u);
    EXPECT_EQ(thunks[0].thunkName, "isReady$bool_thunk");

    // Verify the thunk returns i64 (matching the importer's expectation).
    EXPECT_EQ(thunks[0].thunk.retType.kind, Type::Kind::I64);

    // Verify the thunk contains a Zext1 instruction.
    bool hasZext = false;
    for (const auto &block : thunks[0].thunk.blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == Opcode::Zext1)
                hasZext = true;
        }
    }
    EXPECT_TRUE(hasZext);
}

TEST(InteropIntegration, MergedModuleHasNoImportFunctions)
{
    // Two modules with matching export/import pair.
    Module modA;
    modA.functions.push_back(makeMainCalling("compute", Type(Type::Kind::I64)));
    modA.functions.push_back(makeFunc("compute", Type(Type::Kind::I64), {}, Linkage::Import));

    Module modB;
    modB.functions.push_back(makeFunc("compute", Type(Type::Kind::I64), {}, Linkage::Export));

    std::vector<Module> modules;
    modules.push_back(std::move(modA));
    modules.push_back(std::move(modB));

    auto result = il::link::linkModules(std::move(modules));
    ASSERT_TRUE(result.succeeded());

    // Verify no Import-linkage functions remain.
    for (const auto &fn : result.module.functions)
    {
        EXPECT_NE(fn.linkage, Linkage::Import);
    }
}

TEST(InteropIntegration, InternalNameCollisionsResolved)
{
    // Both modules have an Internal function named "helper".
    Module modA;
    modA.functions.push_back(makeMainCalling("helper", Type(Type::Kind::I64)));
    modA.functions.push_back(makeFunc("helper", Type(Type::Kind::I64), {}, Linkage::Internal));

    Module modB;
    modB.functions.push_back(makeFunc("helper", Type(Type::Kind::I64), {}, Linkage::Internal));

    std::vector<Module> modules;
    modules.push_back(std::move(modA));
    modules.push_back(std::move(modB));

    auto result = il::link::linkModules(std::move(modules));
    ASSERT_TRUE(result.succeeded());

    // The entry module's "helper" should keep its name, while the
    // non-entry module's "helper" should be prefixed.
    bool hasOriginal = false;
    bool hasPrefixed = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "helper")
            hasOriginal = true;
        if (fn.name.find("$helper") != std::string::npos)
            hasPrefixed = true;
    }
    EXPECT_TRUE(hasOriginal);
    EXPECT_TRUE(hasPrefixed);
}

TEST(InteropIntegration, ParamBooleanThunkGenerated)
{
    // Export expects i1 param, Import passes i64 param.
    Module ziaMod;
    ziaMod.functions.push_back(makeFunc("setFlag",
                                        Type(Type::Kind::Void),
                                        {Param{"flag", Type(Type::Kind::I1), 0}},
                                        Linkage::Export));

    Module basicMod;
    basicMod.functions.push_back(makeFunc("setFlag",
                                          Type(Type::Kind::Void),
                                          {Param{"flag", Type(Type::Kind::I64), 0}},
                                          Linkage::Import));

    auto thunks = il::link::generateBooleanThunks(basicMod, ziaMod);
    ASSERT_EQ(thunks.size(), 1u);

    // Thunk should accept i64 (matching the import) and contain ICmpNe.
    EXPECT_EQ(thunks[0].thunk.params.size(), 1u);
    EXPECT_EQ(thunks[0].thunk.params[0].type.kind, Type::Kind::I64);

    bool hasIcmp = false;
    for (const auto &block : thunks[0].thunk.blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == Opcode::ICmpNe)
                hasIcmp = true;
        }
    }
    EXPECT_TRUE(hasIcmp);
}

TEST(InteropIntegration, ExternsMergedCorrectly)
{
    // Both modules declare the same extern.
    Module modA;
    modA.functions.push_back(makeMainCalling("helper", Type(Type::Kind::I64)));
    modA.functions.push_back(makeFunc("helper", Type(Type::Kind::I64), {}, Linkage::Import));
    modA.externs.push_back(
        Extern{"Viper.Terminal.Say", Type(Type::Kind::Void), {Type(Type::Kind::Str)}});

    Module modB;
    modB.functions.push_back(makeFunc("helper", Type(Type::Kind::I64), {}, Linkage::Export));
    modB.externs.push_back(
        Extern{"Viper.Terminal.Say", Type(Type::Kind::Void), {Type(Type::Kind::Str)}});

    std::vector<Module> modules;
    modules.push_back(std::move(modA));
    modules.push_back(std::move(modB));

    auto result = il::link::linkModules(std::move(modules));
    ASSERT_TRUE(result.succeeded());

    // The shared extern should appear exactly once (deduplicated).
    int sayCount = 0;
    for (const auto &ext : result.module.externs)
    {
        if (ext.name == "Viper.Terminal.Say")
            sayCount++;
    }
    EXPECT_EQ(sayCount, 1);
}

int main()
{
    return viper_test::run_all_tests();
}
