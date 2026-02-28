//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/ModuleLinkerTests.cpp
// Purpose: Test the IL module linker — export/import resolution, name collision
//          prefixing, extern merging, and init function injection.
// Key invariants: Linked modules must resolve all imports and have no duplicates.
// Ownership/Lifetime: Test-owned modules.
// Links: docs/adr/0003-il-linkage-and-module-linking.md
//
//===----------------------------------------------------------------------===//

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

/// Create a trivial function with a single entry block that returns void.
static Function makeVoidFunc(const std::string &name, Linkage linkage)
{
    Function fn;
    fn.name = name;
    fn.retType = Type(Type::Kind::Void);
    fn.linkage = linkage;

    if (linkage != Linkage::Import)
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr ret;
        ret.op = Opcode::Ret;
        entry.instructions.push_back(ret);
        fn.blocks.push_back(std::move(entry));
    }

    return fn;
}

/// Create a function that returns i64 with a trivial body.
static Function makeI64Func(const std::string &name, Linkage linkage, long long retVal = 0)
{
    Function fn;
    fn.name = name;
    fn.retType = Type(Type::Kind::I64);
    fn.linkage = linkage;

    if (linkage != Linkage::Import)
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::I64);
        ret.operands.push_back(Value::constInt(retVal));
        entry.instructions.push_back(ret);
        fn.blocks.push_back(std::move(entry));
    }

    return fn;
}

/// Create a function with parameters.
static Function makeI64FuncWithParams(const std::string &name,
                                      Linkage linkage,
                                      std::vector<Param> params)
{
    Function fn;
    fn.name = name;
    fn.retType = Type(Type::Kind::I64);
    fn.params = std::move(params);
    fn.linkage = linkage;
    // No body for import
    return fn;
}

/// Check if a function with the given name exists in the module.
static bool hasFunction(const Module &m, const std::string &name)
{
    return std::any_of(
        m.functions.begin(), m.functions.end(), [&](const Function &f) { return f.name == name; });
}

/// Count functions in the module.
static size_t countFunctions(const Module &m)
{
    return m.functions.size();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(ModuleLinker, SingleModulePassthrough)
{
    Module m;
    m.functions.push_back(makeI64Func("main", Linkage::Internal));

    std::vector<Module> modules;
    modules.push_back(std::move(m));

    auto result = il::link::linkModules(std::move(modules));
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunction(result.module, "main"));
}

TEST(ModuleLinker, TwoModulesExportImportResolved)
{
    // Module A: entry module with main, imports "helper"
    Module a;
    a.functions.push_back(makeI64Func("main", Linkage::Internal));
    a.functions.push_back(makeI64Func("helper", Linkage::Import));

    // Module B: library with exported "helper"
    Module b;
    b.functions.push_back(makeI64Func("helper", Linkage::Export, 42));

    std::vector<Module> modules;
    modules.push_back(std::move(a));
    modules.push_back(std::move(b));

    auto result = il::link::linkModules(std::move(modules));
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunction(result.module, "main"));
    EXPECT_TRUE(hasFunction(result.module, "helper"));

    // The import stub should be dropped, so we have exactly 2 functions.
    EXPECT_EQ(countFunctions(result.module), 2u);
}

TEST(ModuleLinker, UnresolvedImportFails)
{
    Module a;
    a.functions.push_back(makeI64Func("main", Linkage::Internal));
    a.functions.push_back(makeI64Func("missing", Linkage::Import));

    Module b;
    b.functions.push_back(makeVoidFunc("unrelated", Linkage::Export));

    std::vector<Module> modules;
    modules.push_back(std::move(a));
    modules.push_back(std::move(b));

    auto result = il::link::linkModules(std::move(modules));
    EXPECT_FALSE(result.succeeded());
    ASSERT_FALSE(result.errors.empty());
    EXPECT_TRUE(result.errors[0].find("missing") != std::string::npos);
}

TEST(ModuleLinker, DuplicateMainFails)
{
    Module a;
    a.functions.push_back(makeI64Func("main", Linkage::Internal));

    Module b;
    b.functions.push_back(makeI64Func("main", Linkage::Internal));

    std::vector<Module> modules;
    modules.push_back(std::move(a));
    modules.push_back(std::move(b));

    auto result = il::link::linkModules(std::move(modules));
    EXPECT_FALSE(result.succeeded());
    ASSERT_FALSE(result.errors.empty());
    EXPECT_TRUE(result.errors[0].find("multiple modules define 'main'") != std::string::npos);
}

TEST(ModuleLinker, InternalNameCollisionPrefixed)
{
    // Both modules have an Internal "helper" — non-entry one gets prefixed.
    Module a;
    a.functions.push_back(makeI64Func("main", Linkage::Internal));
    a.functions.push_back(makeVoidFunc("helper", Linkage::Internal));

    Module b;
    b.functions.push_back(makeVoidFunc("helper", Linkage::Internal));
    b.functions.push_back(makeI64Func("compute", Linkage::Export, 99));

    std::vector<Module> modules;
    modules.push_back(std::move(a));
    modules.push_back(std::move(b));

    auto result = il::link::linkModules(std::move(modules));
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunction(result.module, "helper"));    // Entry module's version
    EXPECT_TRUE(hasFunction(result.module, "m1$helper")); // Non-entry gets prefix
    EXPECT_TRUE(hasFunction(result.module, "compute"));
}

TEST(ModuleLinker, ExternsMergedAndDeduplicated)
{
    Module a;
    a.functions.push_back(makeI64Func("main", Linkage::Internal));
    a.externs.push_back({"Viper.Terminal.Say", Type(Type::Kind::Void), {Type(Type::Kind::Str)}});

    Module b;
    b.functions.push_back(makeVoidFunc("lib", Linkage::Export));
    b.externs.push_back({"Viper.Terminal.Say", Type(Type::Kind::Void), {Type(Type::Kind::Str)}});

    std::vector<Module> modules;
    modules.push_back(std::move(a));
    modules.push_back(std::move(b));

    auto result = il::link::linkModules(std::move(modules));
    ASSERT_TRUE(result.succeeded());

    // Should be deduplicated to one extern.
    EXPECT_EQ(result.module.externs.size(), 1u);
}

TEST(ModuleLinker, ExternSignatureMismatchFails)
{
    Module a;
    a.functions.push_back(makeI64Func("main", Linkage::Internal));
    a.externs.push_back({"Viper.Foo", Type(Type::Kind::Void), {Type(Type::Kind::I64)}});

    Module b;
    b.functions.push_back(makeVoidFunc("lib", Linkage::Export));
    // Same name but different return type
    b.externs.push_back({"Viper.Foo", Type(Type::Kind::I64), {Type(Type::Kind::I64)}});

    std::vector<Module> modules;
    modules.push_back(std::move(a));
    modules.push_back(std::move(b));

    auto result = il::link::linkModules(std::move(modules));
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.errors[0].find("extern signature mismatch") != std::string::npos);
}

TEST(ModuleLinker, GlobalsMerged)
{
    Module a;
    a.functions.push_back(makeI64Func("main", Linkage::Internal));
    a.globals.push_back({"str0", Type(Type::Kind::Str), "hello"});

    Module b;
    b.functions.push_back(makeVoidFunc("lib", Linkage::Export));
    b.globals.push_back({"str1", Type(Type::Kind::Str), "world"});

    std::vector<Module> modules;
    modules.push_back(std::move(a));
    modules.push_back(std::move(b));

    auto result = il::link::linkModules(std::move(modules));
    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.module.globals.size(), 2u);
}

TEST(ModuleLinker, EmptyModuleListFails)
{
    std::vector<Module> modules;
    auto result = il::link::linkModules(std::move(modules));
    EXPECT_FALSE(result.succeeded());
}

TEST(ModuleLinker, NoMainFails)
{
    Module a;
    a.functions.push_back(makeVoidFunc("notMain", Linkage::Export));

    Module b;
    b.functions.push_back(makeVoidFunc("alsoNotMain", Linkage::Export));

    std::vector<Module> modules;
    modules.push_back(std::move(a));
    modules.push_back(std::move(b));

    auto result = il::link::linkModules(std::move(modules));
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.errors[0].find("no module defines 'main'") != std::string::npos);
}

int main()
{
    return viper_test::run_all_tests();
}
