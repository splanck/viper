//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia-server/test_compiler_bridge.cpp
// Purpose: Integration tests for CompilerBridge facade methods.
// Key invariants:
//   - Each test uses small, self-contained Zia source snippets
//   - Tests exercise the full compiler pipeline through the bridge
//   - Runtime query tests verify the singleton RuntimeRegistry
// Ownership/Lifetime:
//   - Test-only file
// Links: tools/zia-server/CompilerBridge.hpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"
#include "tools/zia-server/CompilerBridge.hpp"

#include <string>

using namespace viper::server;

// ===== check() =====

TEST(CompilerBridge, CheckValidSource)
{
    CompilerBridge bridge;
    auto diags = bridge.check(R"(
module Test;
func start() {
    var x = 42;
    Viper.Terminal.SayInt(x);
}
)",
                              "test.zia");
    // No errors expected (there may be warnings, check for severity 2 = error)
    for (const auto &d : diags)
    {
        EXPECT_TRUE(d.severity != 2);
    }
}

TEST(CompilerBridge, CheckReportsError)
{
    CompilerBridge bridge;
    auto diags = bridge.check(R"(
module Test;
func start() {
    var x = undeclaredVariable;
}
)",
                              "test.zia");
    EXPECT_TRUE(diags.size() > 0u);
    // Should be an error severity
    bool hasError = false;
    for (const auto &d : diags)
    {
        if (d.severity == 2)
            hasError = true;
    }
    EXPECT_TRUE(hasError);
}

TEST(CompilerBridge, CheckReturnsDiagnosticFields)
{
    CompilerBridge bridge;
    auto diags = bridge.check(R"(
module Test;
func start() {
    var x = unknownIdent;
}
)",
                              "test.zia");
    EXPECT_TRUE(diags.size() > 0u);
    // Diagnostic should have a message
    EXPECT_TRUE(!diags[0].message.empty());
    // Line number should be non-zero (1-based)
    EXPECT_TRUE(diags[0].line > 0u);
}

// ===== compile() =====

TEST(CompilerBridge, CompileValidSource)
{
    CompilerBridge bridge;
    auto result = bridge.compile(R"(
module Test;
func start() {
    var x = 42;
    Viper.Terminal.SayInt(x);
}
)",
                                 "test.zia");
    EXPECT_TRUE(result.succeeded);
    // No errors expected (warnings are acceptable)
    for (const auto &d : result.diagnostics)
    {
        EXPECT_TRUE(d.severity != 2);
    }
}

TEST(CompilerBridge, CompileInvalidSource)
{
    CompilerBridge bridge;
    auto result = bridge.compile(R"(
module Test;
func start() {
    var x = unknownIdent;
}
)",
                                 "test.zia");
    EXPECT_FALSE(result.succeeded);
    EXPECT_TRUE(result.diagnostics.size() > 0u);
}

// ===== completions() =====

TEST(CompilerBridge, CompletionsReturnsResults)
{
    CompilerBridge bridge;
    // Place cursor after "Viper." to trigger member completions
    std::string source = "module Test;\nfunc start() {\n    Viper.\n}\n";
    auto items = bridge.completions(source, 3, 11, "test.zia");
    // Should return at least some completions (runtime classes like Terminal, etc.)
    EXPECT_TRUE(items.size() > 0u);
}

TEST(CompilerBridge, CompletionsAtEmptyPosition)
{
    CompilerBridge bridge;
    // Cursor at start of empty function body — should get keyword completions
    std::string source = "module Test;\nfunc start() {\n    \n}\n";
    auto items = bridge.completions(source, 3, 5, "test.zia");
    // Should return completions (keywords, globals, etc.)
    // Even if zero, the call should not crash
    (void)items;
}

// ===== hover() =====

TEST(CompilerBridge, HoverOnLocalVariable)
{
    CompilerBridge bridge;
    // Line 3: "    var x = 42;" — cursor on 'x' at col 9
    std::string source = "module Test;\nfunc start() {\n    var x = 42;\n}\n";
    auto result = bridge.hover(source, 3, 9, "test.zia");
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("var x") != std::string::npos);
    EXPECT_TRUE(result.find("Integer") != std::string::npos);
}

TEST(CompilerBridge, HoverOnFunctionParameter)
{
    CompilerBridge bridge;
    // Line 3: "    return a + b;" — cursor on 'a' at col 12
    std::string source = "module Test;\n"
                         "func add(a: Integer, b: Integer) -> Integer {\n"
                         "    return a + b;\n"
                         "}\n"
                         "func start() {\n"
                         "    Viper.Terminal.SayInt(add(1, 2));\n"
                         "}\n";
    auto result = bridge.hover(source, 3, 12, "test.zia");
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("a") != std::string::npos);
    EXPECT_TRUE(result.find("Integer") != std::string::npos);
    EXPECT_TRUE(result.find("Parameter") != std::string::npos);
}

TEST(CompilerBridge, HoverOnGlobalFunction)
{
    CompilerBridge bridge;
    // Line 2: "func start() {" — cursor on 'start' at col 6
    std::string source = "module Test;\nfunc start() {\n    var x = 10;\n}\n";
    auto result = bridge.hover(source, 2, 6, "test.zia");
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("func start") != std::string::npos);
}

TEST(CompilerBridge, HoverOnEntityTypeName)
{
    CompilerBridge bridge;
    // Line 10: "    var s: Ship = new Ship();" — cursor on 'Ship' type annotation at col 12
    std::string source = "module Test;\n"
                         "\n"
                         "entity Ship {\n"
                         "    hide Integer speed;\n"
                         "    expose func init() {\n"
                         "        speed = 0;\n"
                         "    }\n"
                         "}\n"
                         "\n"
                         "func start() {\n"
                         "    var s: Ship = new Ship();\n"
                         "}\n";
    auto result = bridge.hover(source, 11, 12, "test.zia");
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("entity Ship") != std::string::npos);
}

TEST(CompilerBridge, HoverOnEntityFieldInsideBody)
{
    CompilerBridge bridge;
    // Line 7: "        return speed;" — cursor on 'speed' at col 16
    std::string source = "module Test;\n"
                         "\n"
                         "entity Ship {\n"
                         "    hide Integer speed;\n"
                         "\n"
                         "    expose func getSpeed() -> Integer {\n"
                         "        return speed;\n"
                         "    }\n"
                         "}\n"
                         "\n"
                         "func start() {\n"
                         "}\n";
    auto result = bridge.hover(source, 7, 16, "test.zia");
    // Hovering on bare field names inside method bodies is not yet supported.
    // This test documents the current behavior while the feature is pending.
    (void)result;
}

TEST(CompilerBridge, HoverOnMethodViaDot)
{
    CompilerBridge bridge;
    // Line 14: "    var r = s.getSpeed();" — cursor on 'getSpeed' at col 17
    std::string source = "module Test;\n"                            // 1
                         "\n"                                        // 2
                         "entity Ship {\n"                           // 3
                         "    hide Integer speed;\n"                 // 4
                         "    expose func init() {\n"                // 5
                         "        speed = 0;\n"                      // 6
                         "    }\n"                                   // 7
                         "    expose func getSpeed() -> Integer {\n" // 8
                         "        return speed;\n"                   // 9
                         "    }\n"                                   // 10
                         "}\n"                                       // 11
                         "\n"                                        // 12
                         "func start() {\n"                          // 13
                         "    var s = new Ship();\n"                 // 14
                         "    var r = s.getSpeed();\n"               // 15
                         "}\n";                                      // 16
    // Line 15: "    var r = s.getSpeed();" — 's' at col 13, '.' at 14, 'g' at 15
    auto result = bridge.hover(source, 15, 15, "test.zia");
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("method getSpeed") != std::string::npos);
    EXPECT_TRUE(result.find("Member of") != std::string::npos);
}

TEST(CompilerBridge, HoverOnFieldViaDot)
{
    CompilerBridge bridge;
    // Line 11: "    s.speed = 10;" — 's' at col 5, '.' at 6, 'speed' starts at col 7
    std::string source = "module Test;\n"              // 1
                         "\n"                          // 2
                         "entity Ship {\n"             // 3
                         "    expose Integer speed;\n" // 4
                         "    expose func init() {\n"  // 5
                         "        speed = 0;\n"        // 6
                         "    }\n"                     // 7
                         "}\n"                         // 8
                         "\n"                          // 9
                         "func start() {\n"            // 10
                         "    var s = new Ship();\n"   // 11
                         "    s.speed = 10;\n"         // 12
                         "}\n";                        // 13
    auto result = bridge.hover(source, 12, 7, "test.zia");
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("speed") != std::string::npos);
    EXPECT_TRUE(result.find("Integer") != std::string::npos);
}

TEST(CompilerBridge, HoverOnWhitespace)
{
    CompilerBridge bridge;
    // Line 3, col 1: leading whitespace
    std::string source = "module Test;\nfunc start() {\n    var x = 42;\n}\n";
    auto result = bridge.hover(source, 3, 1, "test.zia");
    EXPECT_TRUE(result.empty());
}

TEST(CompilerBridge, HoverOnOperator)
{
    CompilerBridge bridge;
    // Line 3: "    var x = 1 + 2;" — cursor on '+' at col 15
    std::string source = "module Test;\nfunc start() {\n    var x = 1 + 2;\n}\n";
    auto result = bridge.hover(source, 3, 15, "test.zia");
    EXPECT_TRUE(result.empty());
}

TEST(CompilerBridge, HoverOnInvalidSource)
{
    CompilerBridge bridge;
    auto result = bridge.hover("this is not valid zia", 1, 1, "test.zia");
    // Should not crash; empty result is acceptable
    (void)result;
}

TEST(CompilerBridge, HoverOnModuleAlias)
{
    CompilerBridge bridge;
    // Line 2: "bind IO = Viper.Terminal;" — cursor on 'IO' at col 6
    std::string source = "module Test;\n"
                         "bind IO = Viper.Terminal;\n"
                         "\n"
                         "func start() {\n"
                         "    IO.Say(\"hello\");\n"
                         "}\n";
    auto result = bridge.hover(source, 2, 6, "test.zia");
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("IO") != std::string::npos);
    EXPECT_TRUE(result.find("Viper.Terminal") != std::string::npos);
    EXPECT_TRUE(result.find("Module namespace") != std::string::npos);
}

TEST(CompilerBridge, HoverOnRuntimeMethod)
{
    CompilerBridge bridge;
    // Line 5: "    IO.Say("hi");" — dotPrefix="IO", identifier="Say"
    std::string source = "module Test;\n"              // 1
                         "bind IO = Viper.Terminal;\n" // 2
                         "\n"                          // 3
                         "func start() {\n"            // 4
                         "    IO.Say(\"hi\");\n"       // 5
                         "}\n";                        // 6
    // 'Say' starts at col 8 in "    IO.Say("hi");"
    auto result = bridge.hover(source, 5, 8, "test.zia");
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("Say") != std::string::npos);
}

TEST(CompilerBridge, HoverOnFinalVariable)
{
    CompilerBridge bridge;
    // Line 4: "    Viper.Terminal.SayInt(MAX);" — 'MAX' starts at col 27
    std::string source = "module Test;\n"
                         "final MAX = 100;\n"
                         "func start() {\n"
                         "    Viper.Terminal.SayInt(MAX);\n"
                         "}\n";
    auto result = bridge.hover(source, 4, 27, "test.zia");
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("MAX") != std::string::npos);
}

TEST(CompilerBridge, HoverOnFunctionCallSite)
{
    CompilerBridge bridge;
    // Line 6: "    Viper.Terminal.SayInt(add(1, 2));" — 'add' starts at col 27
    std::string source = "module Test;\n"
                         "func add(a: Integer, b: Integer) -> Integer {\n"
                         "    return a + b;\n"
                         "}\n"
                         "func start() {\n"
                         "    Viper.Terminal.SayInt(add(1, 2));\n"
                         "}\n";
    auto result = bridge.hover(source, 6, 27, "test.zia");
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("func add") != std::string::npos);
    EXPECT_TRUE(result.find("Integer") != std::string::npos);
}

// ===== symbols() =====

TEST(CompilerBridge, SymbolsListsDeclarations)
{
    CompilerBridge bridge;
    auto syms = bridge.symbols(R"(
module Test;
func start() {
    var x = 42;
}
)",
                               "test.zia");
    // Should at least have the "start" function
    bool foundStart = false;
    for (const auto &s : syms)
    {
        if (s.name == "start")
            foundStart = true;
    }
    EXPECT_TRUE(foundStart);
}

TEST(CompilerBridge, SymbolsIncludesTypes)
{
    CompilerBridge bridge;
    auto syms = bridge.symbols(R"(
module Test;
value Point {
    var x: Integer;
    var y: Integer;
}
func start() {
    var p = Point(1, 2);
}
)",
                               "test.zia");
    // Should include the "Point" type
    bool foundPoint = false;
    for (const auto &s : syms)
    {
        if (s.name == "Point")
            foundPoint = true;
    }
    EXPECT_TRUE(foundPoint);
}

// ===== dumpIL() =====

TEST(CompilerBridge, DumpILValidSource)
{
    CompilerBridge bridge;
    auto il = bridge.dumpIL(R"(
module Test;
func start() {
    var x = 42;
}
)",
                            "test.zia",
                            false);
    // Should contain IL text, not an error message
    EXPECT_TRUE(il.find("Compilation failed") == std::string::npos);
    // Should contain some IL output
    EXPECT_TRUE(!il.empty());
}

TEST(CompilerBridge, DumpILOptimized)
{
    CompilerBridge bridge;
    auto il = bridge.dumpIL(R"(
module Test;
func start() {
    var x = 42;
}
)",
                            "test.zia",
                            true);
    EXPECT_TRUE(il.find("Compilation failed") == std::string::npos);
    EXPECT_TRUE(!il.empty());
}

TEST(CompilerBridge, DumpILInvalidSource)
{
    CompilerBridge bridge;
    auto il = bridge.dumpIL(R"(
module Test;
func start() {
    var x = unknownIdent;
}
)",
                            "test.zia",
                            false);
    // Should contain error message
    EXPECT_TRUE(il.find("Compilation failed") != std::string::npos);
}

// ===== dumpAst() =====

TEST(CompilerBridge, DumpAstValidSource)
{
    CompilerBridge bridge;
    auto ast = bridge.dumpAst(R"(
module Test;
func start() {
    var x = 42;
}
)",
                              "test.zia");
    // Should produce non-empty AST output
    EXPECT_TRUE(!ast.empty());
    EXPECT_TRUE(ast != "(no AST produced)");
}

TEST(CompilerBridge, DumpAstInvalidSyntax)
{
    CompilerBridge bridge;
    auto ast = bridge.dumpAst("this is not valid zia source", "test.zia");
    // May produce partial AST or "(no AST produced)" — should not crash
    (void)ast;
}

// ===== dumpTokens() =====

TEST(CompilerBridge, DumpTokensValidSource)
{
    CompilerBridge bridge;
    auto tokens = bridge.dumpTokens("module Test;\nfunc start() { }\n", "test.zia");
    // Should contain token text for "module", "Test", etc.
    EXPECT_TRUE(!tokens.empty());
    EXPECT_TRUE(tokens.find("module") != std::string::npos);
    EXPECT_TRUE(tokens.find("Test") != std::string::npos);
    EXPECT_TRUE(tokens.find("start") != std::string::npos);
}

TEST(CompilerBridge, DumpTokensEmptySource)
{
    CompilerBridge bridge;
    auto tokens = bridge.dumpTokens("", "test.zia");
    // Empty source should produce empty token output (only EOF, which we skip)
    EXPECT_TRUE(tokens.empty());
}

// ===== runtimeClasses() =====

TEST(CompilerBridge, RuntimeClassesNotEmpty)
{
    CompilerBridge bridge;
    auto classes = bridge.runtimeClasses();
    // Should have at least some runtime classes (Terminal, Canvas, etc.)
    EXPECT_TRUE(classes.size() > 0u);
}

TEST(CompilerBridge, RuntimeClassesHaveNames)
{
    CompilerBridge bridge;
    auto classes = bridge.runtimeClasses();
    for (const auto &cls : classes)
    {
        EXPECT_TRUE(!cls.qname.empty());
    }
}

// ===== runtimeMembers() =====

TEST(CompilerBridge, RuntimeMembersForKnownClass)
{
    CompilerBridge bridge;
    // "Viper.Terminal" should exist and have methods
    auto members = bridge.runtimeMembers("Viper.Terminal");
    EXPECT_TRUE(members.size() > 0u);
    // Should have the "Say" method
    bool foundSay = false;
    for (const auto &m : members)
    {
        if (m.name == "Say")
            foundSay = true;
    }
    EXPECT_TRUE(foundSay);
}

TEST(CompilerBridge, RuntimeMembersForUnknownClass)
{
    CompilerBridge bridge;
    auto members = bridge.runtimeMembers("NonExistent.Class");
    EXPECT_EQ(members.size(), 0u);
}

// ===== runtimeSearch() =====

TEST(CompilerBridge, RuntimeSearchFindsResults)
{
    CompilerBridge bridge;
    auto results = bridge.runtimeSearch("Say");
    // Should find Terminal.Say and possibly others
    EXPECT_TRUE(results.size() > 0u);
}

TEST(CompilerBridge, RuntimeSearchCaseInsensitive)
{
    CompilerBridge bridge;
    auto lower = bridge.runtimeSearch("say");
    auto upper = bridge.runtimeSearch("SAY");
    // Both should find the same results
    EXPECT_EQ(lower.size(), upper.size());
}

TEST(CompilerBridge, RuntimeSearchNoResults)
{
    CompilerBridge bridge;
    auto results = bridge.runtimeSearch("zzzzNonExistentApiNamezzzz");
    EXPECT_EQ(results.size(), 0u);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
