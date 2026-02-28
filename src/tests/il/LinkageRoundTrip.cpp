//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/LinkageRoundTrip.cpp
// Purpose: Verify that Linkage annotations (Internal, Export, Import) survive
//          serialization and parsing round-trips correctly.
// Key invariants:
//   - Internal linkage is the default and is NOT printed (backwards compat).
//   - Export linkage is printed as "func export @name ...".
//   - Import linkage functions have no body.
// Ownership/Lifetime: Test-owned modules.
// Links: docs/adr/0003-il-linkage-and-module-linking.md
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Linkage.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "il/io/Serializer.hpp"
#include "viper/il/IO.hpp"

#include "tests/TestHarness.hpp"

#include <sstream>
#include <string>

using namespace il::core;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Build a minimal module with one function of the given linkage.
static Module makeModuleWithFunc(const std::string &name, Linkage linkage)
{
    Module m;
    Function fn;
    fn.name = name;
    fn.retType = Type(Type::Kind::I64);
    fn.params.push_back(Param{"x", Type(Type::Kind::I64), 0});
    fn.linkage = linkage;

    if (linkage != Linkage::Import)
    {
        // Give the function a trivial body so it's valid IL.
        BasicBlock entry;
        entry.label = "entry";

        Instr retInstr;
        retInstr.op = Opcode::Ret;
        retInstr.type = Type(Type::Kind::I64);
        retInstr.operands.push_back(Value::temp(0));
        entry.instructions.push_back(retInstr);

        fn.blocks.push_back(std::move(entry));
    }

    m.functions.push_back(std::move(fn));
    return m;
}

/// Serialize a module to its textual IL representation.
static std::string serialize(const Module &m)
{
    std::ostringstream oss;
    il::io::Serializer::write(m, oss);
    return oss.str();
}

/// Parse a textual IL module, returning success/failure.
static bool parseIL(const std::string &text, Module &out)
{
    std::istringstream iss(text);
    auto result = il::io::Parser::parse(iss, out);
    return result.hasValue();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(LinkageRoundTrip, InternalLinkageIsDefault)
{
    Module m = makeModuleWithFunc("myFunc", Linkage::Internal);
    std::string text = serialize(m);

    // Internal linkage should NOT produce any keyword (backwards compat).
    EXPECT_TRUE(text.find("func @myFunc(") != std::string::npos);
    EXPECT_TRUE(text.find("func export") == std::string::npos);
    EXPECT_TRUE(text.find("func import") == std::string::npos);

    // Round-trip: parse back and verify linkage.
    Module parsed;
    ASSERT_TRUE(parseIL(text, parsed));
    ASSERT_EQ(parsed.functions.size(), 1u);
    EXPECT_EQ(static_cast<int>(parsed.functions[0].linkage), static_cast<int>(Linkage::Internal));
}

TEST(LinkageRoundTrip, ExportLinkageSurvivesRoundTrip)
{
    Module m = makeModuleWithFunc("calcScore", Linkage::Export);
    std::string text = serialize(m);

    // Export keyword should appear.
    EXPECT_TRUE(text.find("func export @calcScore(") != std::string::npos);

    // Round-trip: parse back and verify linkage.
    Module parsed;
    ASSERT_TRUE(parseIL(text, parsed));
    ASSERT_EQ(parsed.functions.size(), 1u);
    EXPECT_EQ(static_cast<int>(parsed.functions[0].linkage), static_cast<int>(Linkage::Export));
    EXPECT_EQ(parsed.functions[0].name, "calcScore");
}

TEST(LinkageRoundTrip, ImportLinkageHasNoBody)
{
    Module m = makeModuleWithFunc("foreignHelper", Linkage::Import);
    std::string text = serialize(m);

    // Import keyword should appear, and there should be no opening brace.
    EXPECT_TRUE(text.find("func import @foreignHelper(") != std::string::npos);
    // Import declarations should NOT have a body brace on the same conceptual unit.
    // (The serializer emits just the prototype line with no '{')

    // Round-trip: parse back and verify linkage and empty blocks.
    Module parsed;
    ASSERT_TRUE(parseIL(text, parsed));
    ASSERT_EQ(parsed.functions.size(), 1u);
    EXPECT_EQ(static_cast<int>(parsed.functions[0].linkage), static_cast<int>(Linkage::Import));
    EXPECT_EQ(parsed.functions[0].name, "foreignHelper");
    EXPECT_TRUE(parsed.functions[0].blocks.empty());
}

TEST(LinkageRoundTrip, MixedLinkagesInOneModule)
{
    Module m;

    // Internal function
    {
        Function fn;
        fn.name = "helper";
        fn.retType = Type(Type::Kind::Void);
        fn.linkage = Linkage::Internal;
        BasicBlock entry;
        entry.label = "entry";
        Instr ret;
        ret.op = Opcode::Ret;
        entry.instructions.push_back(ret);
        fn.blocks.push_back(std::move(entry));
        m.functions.push_back(std::move(fn));
    }
    // Export function
    {
        Function fn;
        fn.name = "publicApi";
        fn.retType = Type(Type::Kind::I64);
        fn.linkage = Linkage::Export;
        BasicBlock entry;
        entry.label = "entry";
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::I64);
        ret.operands.push_back(Value::constInt(42));
        entry.instructions.push_back(ret);
        fn.blocks.push_back(std::move(entry));
        m.functions.push_back(std::move(fn));
    }
    // Import function
    {
        Function fn;
        fn.name = "foreignFunc";
        fn.retType = Type(Type::Kind::Str);
        fn.params.push_back(Param{"n", Type(Type::Kind::I64), 0});
        fn.linkage = Linkage::Import;
        // No blocks for import
        m.functions.push_back(std::move(fn));
    }

    std::string text = serialize(m);

    // Verify text
    EXPECT_TRUE(text.find("func @helper(") != std::string::npos);
    EXPECT_TRUE(text.find("func export @publicApi(") != std::string::npos);
    EXPECT_TRUE(text.find("func import @foreignFunc(") != std::string::npos);

    // Round-trip
    Module parsed;
    ASSERT_TRUE(parseIL(text, parsed));
    ASSERT_EQ(parsed.functions.size(), 3u);

    EXPECT_EQ(static_cast<int>(parsed.functions[0].linkage), static_cast<int>(Linkage::Internal));
    EXPECT_EQ(static_cast<int>(parsed.functions[1].linkage), static_cast<int>(Linkage::Export));
    EXPECT_EQ(static_cast<int>(parsed.functions[2].linkage), static_cast<int>(Linkage::Import));

    // Import has no body
    EXPECT_TRUE(parsed.functions[2].blocks.empty());
    // Others have bodies
    EXPECT_FALSE(parsed.functions[0].blocks.empty());
    EXPECT_FALSE(parsed.functions[1].blocks.empty());
}

TEST(LinkageRoundTrip, GlobalLinkageRoundTrips)
{
    Module m;
    m.globals.push_back({"str0", Type(Type::Kind::Str), "hello", Linkage::Internal});
    m.globals.push_back({"str1", Type(Type::Kind::Str), "world", Linkage::Export});

    std::string text = serialize(m);
    EXPECT_TRUE(text.find("global const str @str0") != std::string::npos);
    EXPECT_TRUE(text.find("global export const str @str1") != std::string::npos);

    Module parsed;
    ASSERT_TRUE(parseIL(text, parsed));
    ASSERT_EQ(parsed.globals.size(), 2u);
    EXPECT_EQ(static_cast<int>(parsed.globals[0].linkage), static_cast<int>(Linkage::Internal));
    EXPECT_EQ(static_cast<int>(parsed.globals[1].linkage), static_cast<int>(Linkage::Export));
}

int main()
{
    return viper_test::run_all_tests();
}
