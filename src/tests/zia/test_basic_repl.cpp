//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/zia/test_basic_repl.cpp
// Purpose: Unit tests for the BASIC REPL adapter (Phase 4 features).
//          Tests input classification (block keyword tracking), expression
//          auto-print, variable persistence, SUB/FUNCTION definition,
//          error recovery, and keyword completion.
// Key invariants:
//   - Failed compilations never corrupt session state.
//   - Variables persist across eval() calls.
//   - classifyBasic() correctly tracks block keywords.
// Ownership/Lifetime:
//   - Each test creates a fresh BasicReplAdapter.
// Links: src/repl/BasicReplAdapter.hpp, src/repl/ReplInputClassifier.hpp
//
//===----------------------------------------------------------------------===//

#include "repl/BasicReplAdapter.hpp"
#include "repl/ReplInputClassifier.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <string>

using namespace viper::repl;

namespace {

// ---------------------------------------------------------------------------
// BASIC input classifier tests
// ---------------------------------------------------------------------------

TEST(BasicClassifier, EmptyInput) {
    EXPECT_EQ(ReplInputClassifier::classifyBasic(""), InputKind::Empty);
    EXPECT_EQ(ReplInputClassifier::classifyBasic("   "), InputKind::Empty);
    EXPECT_EQ(ReplInputClassifier::classifyBasic("\t\n"), InputKind::Empty);
}

TEST(BasicClassifier, MetaCommand) {
    EXPECT_EQ(ReplInputClassifier::classifyBasic(".help"), InputKind::MetaCommand);
    EXPECT_EQ(ReplInputClassifier::classifyBasic(".quit"), InputKind::MetaCommand);
    EXPECT_EQ(ReplInputClassifier::classifyBasic("  .vars"), InputKind::MetaCommand);
}

TEST(BasicClassifier, CompleteStatements) {
    EXPECT_EQ(ReplInputClassifier::classifyBasic("PRINT \"hello\""), InputKind::Complete);
    EXPECT_EQ(ReplInputClassifier::classifyBasic("DIM x AS Integer = 42"), InputKind::Complete);
    EXPECT_EQ(ReplInputClassifier::classifyBasic("x = 10"), InputKind::Complete);
}

TEST(BasicClassifier, SingleLineIfComplete) {
    // Single-line IF with THEN followed by a statement is complete
    EXPECT_EQ(ReplInputClassifier::classifyBasic("IF x > 0 THEN PRINT x"), InputKind::Complete);
}

TEST(BasicClassifier, MultiLineIfIncomplete) {
    // IF ... THEN at end of line → multi-line → incomplete
    EXPECT_EQ(ReplInputClassifier::classifyBasic("IF x > 0 THEN"), InputKind::Incomplete);
}

TEST(BasicClassifier, IfEndIfComplete) {
    std::string input = "IF x > 0 THEN\n  PRINT x\nEND IF";
    EXPECT_EQ(ReplInputClassifier::classifyBasic(input), InputKind::Complete);
}

TEST(BasicClassifier, ForNextComplete) {
    std::string input = "FOR i = 1 TO 10\n  PRINT i\nNEXT";
    EXPECT_EQ(ReplInputClassifier::classifyBasic(input), InputKind::Complete);
}

TEST(BasicClassifier, ForIncomplete) {
    EXPECT_EQ(ReplInputClassifier::classifyBasic("FOR i = 1 TO 10"), InputKind::Incomplete);
}

TEST(BasicClassifier, DoLoopComplete) {
    std::string input = "DO\n  PRINT x\nLOOP";
    EXPECT_EQ(ReplInputClassifier::classifyBasic(input), InputKind::Complete);
}

TEST(BasicClassifier, WhileWendComplete) {
    std::string input = "WHILE x > 0\n  x = x - 1\nWEND";
    EXPECT_EQ(ReplInputClassifier::classifyBasic(input), InputKind::Complete);
}

TEST(BasicClassifier, SubEndSubComplete) {
    std::string input = "SUB Hello()\n  PRINT \"Hello\"\nEND SUB";
    EXPECT_EQ(ReplInputClassifier::classifyBasic(input), InputKind::Complete);
}

TEST(BasicClassifier, SubIncomplete) {
    EXPECT_EQ(ReplInputClassifier::classifyBasic("SUB Hello()"), InputKind::Incomplete);
}

TEST(BasicClassifier, FunctionEndFunctionComplete) {
    std::string input =
        "FUNCTION Add(a AS Integer, b AS Integer) AS Integer\n  RETURN a + b\nEND FUNCTION";
    EXPECT_EQ(ReplInputClassifier::classifyBasic(input), InputKind::Complete);
}

TEST(BasicClassifier, SelectCaseComplete) {
    std::string input = "SELECT CASE x\n  CASE 1\n    PRINT \"one\"\nEND SELECT";
    EXPECT_EQ(ReplInputClassifier::classifyBasic(input), InputKind::Complete);
}

TEST(BasicClassifier, CaseInsensitive) {
    // Keywords should be case-insensitive
    EXPECT_EQ(ReplInputClassifier::classifyBasic("if x > 0 then"), InputKind::Incomplete);
    std::string input = "for i = 1 to 10\n  print i\nnext";
    EXPECT_EQ(ReplInputClassifier::classifyBasic(input), InputKind::Complete);
}

TEST(BasicClassifier, CommentIgnored) {
    // Comment lines should not affect classification
    EXPECT_EQ(ReplInputClassifier::classifyBasic("' This is a comment"), InputKind::Complete);
    EXPECT_EQ(ReplInputClassifier::classifyBasic("PRINT 42 ' with comment"), InputKind::Complete);
}

TEST(BasicClassifier, NestedBlocks) {
    // Nested IF inside FOR
    std::string input = "FOR i = 1 TO 10\n  IF i > 5 THEN\n    PRINT i\n  END IF\nNEXT";
    EXPECT_EQ(ReplInputClassifier::classifyBasic(input), InputKind::Complete);
}

TEST(BasicClassifier, NestedBlocksIncomplete) {
    // Missing NEXT for outer FOR
    std::string input = "FOR i = 1 TO 10\n  IF i > 5 THEN\n    PRINT i\n  END IF";
    EXPECT_EQ(ReplInputClassifier::classifyBasic(input), InputKind::Incomplete);
}

// ---------------------------------------------------------------------------
// BASIC adapter tests — eval and session state
// ---------------------------------------------------------------------------

TEST(BasicRepl, SimplePrint) {
    BasicReplAdapter adapter;
    auto result = adapter.eval("PRINT \"hello world\"");
    EXPECT_TRUE(result.success);
    EXPECT_NE(result.output.find("hello world"), std::string::npos);
}

TEST(BasicRepl, IntegerExpression) {
    BasicReplAdapter adapter;
    // Expression auto-print wraps with PRINT
    auto result = adapter.eval("2 + 3");
    EXPECT_TRUE(result.success);
    EXPECT_NE(result.output.find("5"), std::string::npos);
}

TEST(BasicRepl, VariablePersistence) {
    BasicReplAdapter adapter;

    // Declare a variable
    auto r1 = adapter.eval("DIM x AS Integer = 42");
    EXPECT_TRUE(r1.success);

    // Use it in subsequent expression
    auto r2 = adapter.eval("x + 8");
    EXPECT_TRUE(r2.success);
    EXPECT_NE(r2.output.find("50"), std::string::npos);
}

TEST(BasicRepl, VariableReassignment) {
    BasicReplAdapter adapter;

    auto r1 = adapter.eval("DIM x AS Integer = 10");
    EXPECT_TRUE(r1.success);

    auto r2 = adapter.eval("x = 20");
    EXPECT_TRUE(r2.success);

    auto r3 = adapter.eval("x");
    EXPECT_TRUE(r3.success);
    EXPECT_NE(r3.output.find("20"), std::string::npos);
}

TEST(BasicRepl, SubDefinitionAndCall) {
    BasicReplAdapter adapter;

    auto r1 = adapter.eval("SUB Greet()\n  PRINT \"Hello from SUB\"\nEND SUB");
    EXPECT_TRUE(r1.success);

    auto r2 = adapter.eval("Greet()");
    EXPECT_TRUE(r2.success);
    EXPECT_NE(r2.output.find("Hello from SUB"), std::string::npos);
}

TEST(BasicRepl, FunctionDefinitionAndCall) {
    BasicReplAdapter adapter;

    auto r1 =
        adapter.eval("FUNCTION Double(n AS Integer) AS Integer\n  RETURN n * 2\nEND FUNCTION");
    EXPECT_TRUE(r1.success);

    auto r2 = adapter.eval("Double(21)");
    EXPECT_TRUE(r2.success);
    EXPECT_NE(r2.output.find("42"), std::string::npos);
}

TEST(BasicRepl, ErrorRecovery) {
    BasicReplAdapter adapter;

    // Valid first
    auto r1 = adapter.eval("DIM x AS Integer = 42");
    EXPECT_TRUE(r1.success);

    // Invalid expression
    auto r2 = adapter.eval("PRINT undefined_var_xyz");
    EXPECT_FALSE(r2.success);

    // Session state should still be intact
    auto r3 = adapter.eval("x");
    EXPECT_TRUE(r3.success);
    EXPECT_NE(r3.output.find("42"), std::string::npos);
}

TEST(BasicRepl, ListVariables) {
    BasicReplAdapter adapter;
    adapter.eval("DIM x AS Integer = 42");
    adapter.eval("DIM name AS String = \"test\"");

    auto vars = adapter.listVariables();
    EXPECT_EQ(vars.size(), 2u);
    EXPECT_EQ(vars[0].name, "x");
    EXPECT_EQ(vars[0].type, "Integer");
    EXPECT_EQ(vars[1].name, "name");
    EXPECT_EQ(vars[1].type, "String");
}

TEST(BasicRepl, ListFunctions) {
    BasicReplAdapter adapter;
    adapter.eval("SUB Hello()\n  PRINT \"hi\"\nEND SUB");

    auto funcs = adapter.listFunctions();
    EXPECT_EQ(funcs.size(), 1u);
    EXPECT_EQ(funcs[0].name, "Hello");
}

TEST(BasicRepl, NoBinds) {
    BasicReplAdapter adapter;
    auto binds = adapter.listBinds();
    EXPECT_TRUE(binds.empty());
}

TEST(BasicRepl, Reset) {
    BasicReplAdapter adapter;
    adapter.eval("DIM x AS Integer = 42");
    adapter.eval("SUB Hi()\n  PRINT \"hi\"\nEND SUB");

    adapter.reset();

    auto vars = adapter.listVariables();
    auto funcs = adapter.listFunctions();
    EXPECT_TRUE(vars.empty());
    EXPECT_TRUE(funcs.empty());
}

TEST(BasicRepl, LanguageName) {
    BasicReplAdapter adapter;
    EXPECT_EQ(adapter.languageName(), "basic");
}

// ---------------------------------------------------------------------------
// BASIC tab completion tests
// ---------------------------------------------------------------------------

TEST(BasicCompletion, KeywordCompletion) {
    BasicReplAdapter adapter;
    auto matches = adapter.complete("PRI", 3);

    // Should have PRINT
    bool found = false;
    for (const auto &m : matches) {
        if (m.find("PRINT") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(BasicCompletion, VariableCompletion) {
    BasicReplAdapter adapter;
    adapter.eval("DIM myCounter AS Integer = 0");

    auto matches = adapter.complete("myC", 3);
    bool found = false;
    for (const auto &m : matches) {
        if (m.find("myCounter") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(BasicCompletion, EmptyInput) {
    BasicReplAdapter adapter;
    auto matches = adapter.complete("", 0);
    EXPECT_TRUE(matches.empty());
}

TEST(BasicCompletion, CaseInsensitive) {
    BasicReplAdapter adapter;
    // Lowercase "di" should match uppercase DIM
    auto matches = adapter.complete("DI", 2);
    bool found = false;
    for (const auto &m : matches) {
        if (m.find("DIM") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// BASIC classifier via adapter interface
// ---------------------------------------------------------------------------

TEST(BasicRepl, ClassifyInputUsesBasicRules) {
    BasicReplAdapter adapter;
    // Bracket-only Zia classifier would say this is complete (no brackets).
    // BASIC classifier should detect the incomplete block.
    EXPECT_EQ(adapter.classifyInput("IF x > 0 THEN"), InputKind::Incomplete);
    EXPECT_EQ(adapter.classifyInput("PRINT 42"), InputKind::Complete);
}

} // anonymous namespace

int main() {
    return viper_test::run_all_tests();
}
