//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/InteropThunkTests.cpp
// Purpose: Test boolean conversion thunk generation for cross-language interop.
// Key invariants:
//   - i1→i64 thunks use Zext1 (true=1, not -1).
//   - i64→i1 thunks use ICmpNe (any non-zero → true).
//   - No thunks generated when types already match.
// Ownership/Lifetime: Test-owned modules.
// Links: docs/adr/0003-il-linkage-and-module-linking.md
//
//===----------------------------------------------------------------------===//

#include "il/link/InteropThunks.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Linkage.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"

#include "tests/TestHarness.hpp"

using namespace il::core;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Function makeExportFunc(const std::string &name,
                               Type retType,
                               std::vector<Param> params = {})
{
    Function fn;
    fn.name = name;
    fn.retType = retType;
    fn.params = std::move(params);
    fn.linkage = Linkage::Export;

    BasicBlock entry;
    entry.label = "entry";
    Instr ret;
    ret.op = Opcode::Ret;
    if (retType.kind != Type::Kind::Void)
    {
        ret.type = retType;
        ret.operands.push_back(Value::constInt(1));
    }
    entry.instructions.push_back(ret);
    fn.blocks.push_back(std::move(entry));
    return fn;
}

static Function makeImportFunc(const std::string &name,
                               Type retType,
                               std::vector<Param> params = {})
{
    Function fn;
    fn.name = name;
    fn.retType = retType;
    fn.params = std::move(params);
    fn.linkage = Linkage::Import;
    return fn;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(InteropThunks, ReturnI1ToI64ThunkGenerated)
{
    // Export returns i1 (Zia), Import expects i64 (BASIC).
    Module exportMod;
    exportMod.functions.push_back(makeExportFunc("isReady", Type(Type::Kind::I1)));

    Module importMod;
    importMod.functions.push_back(makeImportFunc("isReady", Type(Type::Kind::I64)));

    auto thunks = il::link::generateBooleanThunks(importMod, exportMod);
    ASSERT_EQ(thunks.size(), 1u);
    EXPECT_EQ(thunks[0].targetName, "isReady");
    EXPECT_EQ(thunks[0].thunkName, "isReady$bool_thunk");

    // The thunk should return i64 (matching the import's expectation).
    EXPECT_EQ(thunks[0].thunk.retType.kind, Type::Kind::I64);

    // Thunk should have a body with Zext1 conversion.
    ASSERT_FALSE(thunks[0].thunk.blocks.empty());
    bool hasZext = false;
    for (const auto &instr : thunks[0].thunk.blocks[0].instructions)
    {
        if (instr.op == Opcode::Zext1)
            hasZext = true;
    }
    EXPECT_TRUE(hasZext);
}

TEST(InteropThunks, ReturnI64ToI1ThunkGenerated)
{
    // Export returns i64 (BASIC), Import expects i1 (Zia).
    Module exportMod;
    exportMod.functions.push_back(makeExportFunc("isValid", Type(Type::Kind::I64)));

    Module importMod;
    importMod.functions.push_back(makeImportFunc("isValid", Type(Type::Kind::I1)));

    auto thunks = il::link::generateBooleanThunks(importMod, exportMod);
    ASSERT_EQ(thunks.size(), 1u);
    EXPECT_EQ(thunks[0].thunk.retType.kind, Type::Kind::I1);

    // Thunk should have ICmpNe conversion.
    bool hasIcmp = false;
    for (const auto &instr : thunks[0].thunk.blocks[0].instructions)
    {
        if (instr.op == Opcode::ICmpNe)
            hasIcmp = true;
    }
    EXPECT_TRUE(hasIcmp);
}

TEST(InteropThunks, ParamI64ToI1ThunkGenerated)
{
    // Export expects i1 param (Zia), Import passes i64 param (BASIC).
    Module exportMod;
    exportMod.functions.push_back(makeExportFunc(
        "setFlag", Type(Type::Kind::Void), {Param{"flag", Type(Type::Kind::I1), 0}}));

    Module importMod;
    importMod.functions.push_back(makeImportFunc(
        "setFlag", Type(Type::Kind::Void), {Param{"flag", Type(Type::Kind::I64), 0}}));

    auto thunks = il::link::generateBooleanThunks(importMod, exportMod);
    ASSERT_EQ(thunks.size(), 1u);

    // Thunk should have ICmpNe to convert i64 param to i1.
    bool hasIcmp = false;
    for (const auto &instr : thunks[0].thunk.blocks[0].instructions)
    {
        if (instr.op == Opcode::ICmpNe)
            hasIcmp = true;
    }
    EXPECT_TRUE(hasIcmp);
}

TEST(InteropThunks, NoThunkWhenTypesMatch)
{
    // Both use i64 — no mismatch, no thunk needed.
    Module exportMod;
    exportMod.functions.push_back(makeExportFunc("compute", Type(Type::Kind::I64)));

    Module importMod;
    importMod.functions.push_back(makeImportFunc("compute", Type(Type::Kind::I64)));

    auto thunks = il::link::generateBooleanThunks(importMod, exportMod);
    EXPECT_TRUE(thunks.empty());
}

TEST(InteropThunks, NoThunkForNonImportFunctions)
{
    // Only Internal and Export functions — no thunks.
    Module exportMod;
    exportMod.functions.push_back(makeExportFunc("foo", Type(Type::Kind::I1)));

    Module importMod;
    importMod.functions.push_back(makeExportFunc("bar", Type(Type::Kind::I64)));

    auto thunks = il::link::generateBooleanThunks(importMod, exportMod);
    EXPECT_TRUE(thunks.empty());
}

int main()
{
    return viper_test::run_all_tests();
}
