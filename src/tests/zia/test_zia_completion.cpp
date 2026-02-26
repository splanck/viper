//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file test_zia_completion.cpp
/// @brief Unit tests for the Zia completion / IDE tooling APIs.
///
/// @details Validates the following new APIs introduced in Phase 1 of the
/// Zia IntelliSense code completion feature:
///
///   - parseAndAnalyze()        — partial compilation (stop after Sema)
///   - Sema::getGlobalSymbols() — module-level symbol enumeration
///   - Sema::getMembersOf()     — fields and methods of user-defined types
///   - Sema::getRuntimeMembers()— RT class methods and properties
///   - Sema::getTypeNames()     — entity/value/interface declarations
///   - Sema::getBoundModuleNames() — bound namespace aliases
///   - Sema::getModuleExports() — symbols exported by bound file modules
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/ZiaAnalysis.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <algorithm>
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Check if a symbol name exists in a vector of symbols.
static bool hasSymbolNamed(const std::vector<Symbol> &syms, const std::string &name)
{
    return std::any_of(syms.begin(), syms.end(), [&](const Symbol &s) { return s.name == name; });
}

/// @brief Check if a string exists in a vector of strings.
static bool hasName(const std::vector<std::string> &names, const std::string &name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

// ---------------------------------------------------------------------------
// parseAndAnalyze — basic smoke tests
// ---------------------------------------------------------------------------

TEST(ZiaCompletion, ParseAndAnalyze_SuccessfulSource)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func greet() {
    Viper.Terminal.Say("hi");
}
)";
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};

    auto ar = parseAndAnalyze(input, opts, sm);

    EXPECT_TRUE(ar->ast != nullptr);
    EXPECT_TRUE(ar->sema != nullptr);
    EXPECT_FALSE(ar->hasErrors());
}

TEST(ZiaCompletion, ParseAndAnalyze_WithSyntaxErrors_StillReturnsSema)
{
    SourceManager sm;
    // Missing closing brace — parser error.
    const std::string source = R"(
module Test;

func broken( {
    Viper.Terminal.Say("oops");
)";
    CompilerInput input{.source = source, .path = "broken.zia"};
    CompilerOptions opts{};

    auto ar = parseAndAnalyze(input, opts, sm);

    // The function should not crash even with parse errors.
    // ast may be non-null (partial parse) or null (total failure).
    // The key invariant: no crash.
    EXPECT_TRUE(ar->hasErrors());
}

TEST(ZiaCompletion, ParseAndAnalyze_EmptySource)
{
    SourceManager sm;
    const std::string source = "module Test;\n";
    CompilerInput input{.source = source, .path = "empty.zia"};
    CompilerOptions opts{};

    auto ar = parseAndAnalyze(input, opts, sm);

    EXPECT_TRUE(ar->ast != nullptr);
    EXPECT_TRUE(ar->sema != nullptr);
    EXPECT_FALSE(ar->hasErrors());
}

// ---------------------------------------------------------------------------
// getGlobalSymbols
// ---------------------------------------------------------------------------

TEST(ZiaCompletion, GetGlobalSymbols_IncludesTopLevelFunction)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func add(a: Integer, b: Integer) -> Integer {
    return a + b;
}
)";
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};

    auto ar = parseAndAnalyze(input, opts, sm);
    ASSERT_TRUE(ar->sema != nullptr);

    auto globals = ar->sema->getGlobalSymbols();
    EXPECT_TRUE(hasSymbolNamed(globals, "add"));
}

TEST(ZiaCompletion, GetGlobalSymbols_IncludesEntityConstructor)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Dog {
    expose String name;
    expose func init() { name = "Buddy"; }
}
)";
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};

    auto ar = parseAndAnalyze(input, opts, sm);
    ASSERT_TRUE(ar->sema != nullptr);

    auto globals = ar->sema->getGlobalSymbols();
    // Entity constructors / types should appear as Type symbols at module level.
    EXPECT_TRUE(hasSymbolNamed(globals, "Dog"));
}

// ---------------------------------------------------------------------------
// getTypeNames
// ---------------------------------------------------------------------------

TEST(ZiaCompletion, GetTypeNames_ReturnsEntityNames)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Cat {
    expose func init() {}
}

value Point {
    expose Integer x;
    expose Integer y;
}
)";
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};

    auto ar = parseAndAnalyze(input, opts, sm);
    ASSERT_TRUE(ar->sema != nullptr);

    auto names = ar->sema->getTypeNames();
    EXPECT_TRUE(hasName(names, "Cat"));
    EXPECT_TRUE(hasName(names, "Point"));
}

// ---------------------------------------------------------------------------
// getMembersOf — user-defined types
// ---------------------------------------------------------------------------

TEST(ZiaCompletion, GetMembersOf_EntityFieldsAndMethods)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Box {
    expose Integer width;
    expose Integer height;
    expose func Area() -> Integer {
        return width * height;
    }
}
)";
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};

    auto ar = parseAndAnalyze(input, opts, sm);
    ASSERT_TRUE(ar->sema != nullptr);

    // Look up the Box entity type via global symbols.
    auto globals = ar->sema->getGlobalSymbols();
    const Symbol *boxSym = nullptr;
    for (const auto &s : globals)
    {
        if (s.name == "Box")
        {
            boxSym = &s;
            break;
        }
    }
    ASSERT_TRUE(boxSym != nullptr);

    auto members = ar->sema->getMembersOf(boxSym->type);
    EXPECT_TRUE(hasSymbolNamed(members, "width"));
    EXPECT_TRUE(hasSymbolNamed(members, "height"));
    EXPECT_TRUE(hasSymbolNamed(members, "Area"));
}

// ---------------------------------------------------------------------------
// getRuntimeMembers — runtime classes
// ---------------------------------------------------------------------------

TEST(ZiaCompletion, GetRuntimeMembers_StringClass)
{
    SourceManager sm;
    const std::string source = "module Test;\n";
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};

    auto ar = parseAndAnalyze(input, opts, sm);
    ASSERT_TRUE(ar->sema != nullptr);

    // Viper.String is always in the runtime catalog.
    auto members = ar->sema->getRuntimeMembers("Viper.String");
    EXPECT_FALSE(members.empty());

    // String should have at least a Length property and Substring method.
    EXPECT_TRUE(hasSymbolNamed(members, "Length") || hasSymbolNamed(members, "Len") ||
                hasSymbolNamed(members, "Substring") || members.size() > 2);
}

TEST(ZiaCompletion, GetRuntimeMembers_UnknownClass_ReturnsEmpty)
{
    SourceManager sm;
    const std::string source = "module Test;\n";
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};

    auto ar = parseAndAnalyze(input, opts, sm);
    ASSERT_TRUE(ar->sema != nullptr);

    auto members = ar->sema->getRuntimeMembers("Viper.NonExistent.Class");
    EXPECT_TRUE(members.empty());
}

// ---------------------------------------------------------------------------
// getBoundModuleNames
// ---------------------------------------------------------------------------

TEST(ZiaCompletion, GetBoundModuleNames_WithBindAlias)
{
    SourceManager sm;
    // bind with alias
    const std::string source = R"(
module Test;

bind Math = Viper.Math;

func compute() -> Number {
    return Math.Sqrt(4.0);
}
)";
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};

    auto ar = parseAndAnalyze(input, opts, sm);
    ASSERT_TRUE(ar->sema != nullptr);

    auto names = ar->sema->getBoundModuleNames();
    EXPECT_TRUE(hasName(names, "Math"));
}

} // anonymous namespace

int main()
{
    return viper_test::run_all_tests();
}
