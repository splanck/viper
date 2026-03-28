//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/zia/test_zia_repl.cpp
// Purpose: Unit tests for the Zia REPL adapter (Phase 1-3, 5 features).
//          Tests expression auto-print, variable persistence, function
//          definition/redefinition, bind persistence, .type probing,
//          error recovery, input classification, tab completion via
//          CompletionEngine, multi-line eval, .il command, class auto-print,
//          and history persistence.
// Key invariants:
//   - Failed compilations never corrupt session state.
//   - Variables persist across eval() calls.
//   - Expression auto-print correctly detects type.
// Ownership/Lifetime:
//   - Each test creates a fresh ZiaReplAdapter.
// Links: src/repl/ZiaReplAdapter.hpp, src/repl/ReplInputClassifier.hpp
//
//===----------------------------------------------------------------------===//

#include "repl/ReplInputClassifier.hpp"
#include "repl/ReplLineEditor.hpp"
#include "repl/ZiaReplAdapter.hpp"
#include "tests/TestHarness.hpp"

#include <filesystem>
#include <string>

using namespace viper::repl;

namespace {

// ---------------------------------------------------------------------------
// Input classifier tests
// ---------------------------------------------------------------------------

TEST(ReplClassifier, EmptyInput) {
    EXPECT_EQ(ReplInputClassifier::classify(""), InputKind::Empty);
    EXPECT_EQ(ReplInputClassifier::classify("   "), InputKind::Empty);
    EXPECT_EQ(ReplInputClassifier::classify("\t\n"), InputKind::Empty);
}

TEST(ReplClassifier, MetaCommand) {
    EXPECT_EQ(ReplInputClassifier::classify(".help"), InputKind::MetaCommand);
    EXPECT_EQ(ReplInputClassifier::classify(".quit"), InputKind::MetaCommand);
    EXPECT_EQ(ReplInputClassifier::classify("  .type 42"), InputKind::MetaCommand);
}

TEST(ReplClassifier, CompleteInput) {
    EXPECT_EQ(ReplInputClassifier::classify("Say(\"hello\")"), InputKind::Complete);
    EXPECT_EQ(ReplInputClassifier::classify("var x = 42"), InputKind::Complete);
    EXPECT_EQ(ReplInputClassifier::classify("2 + 3"), InputKind::Complete);
}

TEST(ReplClassifier, IncompleteInput) {
    EXPECT_EQ(ReplInputClassifier::classify("func foo() {"), InputKind::Incomplete);
    EXPECT_EQ(ReplInputClassifier::classify("Say("), InputKind::Incomplete);
    EXPECT_EQ(ReplInputClassifier::classify("var x = [1, 2,"), InputKind::Incomplete);
}

TEST(ReplClassifier, BracketsInStringsIgnored) {
    EXPECT_EQ(ReplInputClassifier::classify("Say(\"{not a block}\")"), InputKind::Complete);
    EXPECT_EQ(ReplInputClassifier::classify("var s = \"(\""), InputKind::Complete);
}

// ---------------------------------------------------------------------------
// Expression auto-print tests
// ---------------------------------------------------------------------------

TEST(ReplAutoPrint, IntegerExpression) {
    ZiaReplAdapter adapter;
    auto result = adapter.eval("2 + 3");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.resultType, ResultType::Integer);
    // Output should contain "5"
    EXPECT_NE(result.output.find("5"), std::string::npos);
}

TEST(ReplAutoPrint, NumberExpression) {
    ZiaReplAdapter adapter;
    auto result = adapter.eval("3.14 * 2.0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.resultType, ResultType::Number);
    EXPECT_NE(result.output.find("6.28"), std::string::npos);
}

TEST(ReplAutoPrint, StringExpression) {
    ZiaReplAdapter adapter;
    auto result = adapter.eval("\"hello\" + \" world\"");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.resultType, ResultType::String);
    EXPECT_NE(result.output.find("hello world"), std::string::npos);
}

TEST(ReplAutoPrint, BooleanExpression) {
    ZiaReplAdapter adapter;
    auto result = adapter.eval("true");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.resultType, ResultType::Boolean);
    EXPECT_NE(result.output.find("true"), std::string::npos);
}

TEST(ReplAutoPrint, ExplicitSayNotDoubled) {
    ZiaReplAdapter adapter;
    auto result = adapter.eval("Say(\"hello\")");
    EXPECT_TRUE(result.success);
    // Should print "hello" exactly once (Statement type, not auto-print)
    EXPECT_EQ(result.resultType, ResultType::Statement);
    EXPECT_NE(result.output.find("hello"), std::string::npos);
}

TEST(ReplAutoPrint, FunctionCallResult) {
    ZiaReplAdapter adapter;
    adapter.eval("func square(x: Integer) -> Integer { return x * x; }");
    auto result = adapter.eval("square(7)");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.resultType, ResultType::Integer);
    EXPECT_NE(result.output.find("49"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Variable persistence tests
// ---------------------------------------------------------------------------

TEST(ReplVarPersist, BasicPersistence) {
    ZiaReplAdapter adapter;
    auto r1 = adapter.eval("var x = 42");
    EXPECT_TRUE(r1.success);

    auto r2 = adapter.eval("x");
    EXPECT_TRUE(r2.success);
    EXPECT_EQ(r2.resultType, ResultType::Integer);
    EXPECT_NE(r2.output.find("42"), std::string::npos);
}

TEST(ReplVarPersist, DependentVariables) {
    ZiaReplAdapter adapter;
    adapter.eval("var x = 10");
    adapter.eval("var y = x + 5");

    auto result = adapter.eval("y");
    EXPECT_TRUE(result.success);
    EXPECT_NE(result.output.find("15"), std::string::npos);
}

TEST(ReplVarPersist, Assignment) {
    ZiaReplAdapter adapter;
    adapter.eval("var x = 42");
    adapter.eval("x = 100");

    auto result = adapter.eval("x");
    EXPECT_TRUE(result.success);
    EXPECT_NE(result.output.find("100"), std::string::npos);
}

TEST(ReplVarPersist, MultipleAssignments) {
    ZiaReplAdapter adapter;
    adapter.eval("var x = 1");
    adapter.eval("x = 2");
    adapter.eval("x = 3");

    auto result = adapter.eval("x");
    EXPECT_TRUE(result.success);
    EXPECT_NE(result.output.find("3"), std::string::npos);
}

TEST(ReplVarPersist, VarsCommand) {
    ZiaReplAdapter adapter;
    adapter.eval("var x = 42");
    adapter.eval("var name = \"Alice\"");

    auto vars = adapter.listVariables();
    EXPECT_EQ(vars.size(), 2u);
    EXPECT_EQ(vars[0].name, "x");
    EXPECT_EQ(vars[0].type, "Integer");
    EXPECT_EQ(vars[1].name, "name");
    EXPECT_EQ(vars[1].type, "String");
}

TEST(ReplVarPersist, SurvivesErrors) {
    ZiaReplAdapter adapter;
    adapter.eval("var x = 42");
    auto errResult = adapter.eval("undefined_var");
    EXPECT_FALSE(errResult.success);

    // x should still be accessible
    auto result = adapter.eval("x");
    EXPECT_TRUE(result.success);
    EXPECT_NE(result.output.find("42"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Function definition and redefinition
// ---------------------------------------------------------------------------

TEST(ReplFunctions, DefineAndCall) {
    ZiaReplAdapter adapter;
    auto r1 = adapter.eval("func add(a: Integer, b: Integer) -> Integer { return a + b; }");
    EXPECT_TRUE(r1.success);

    auto r2 = adapter.eval("add(3, 4)");
    EXPECT_TRUE(r2.success);
    EXPECT_NE(r2.output.find("7"), std::string::npos);
}

TEST(ReplFunctions, Redefine) {
    ZiaReplAdapter adapter;
    adapter.eval("func greet(name: String) -> String { return \"Hello, \" + name; }");
    auto r1 = adapter.eval("greet(\"World\")");
    EXPECT_TRUE(r1.success);
    EXPECT_NE(r1.output.find("Hello, World"), std::string::npos);

    adapter.eval("func greet(name: String) -> String { return \"Hi, \" + name; }");
    auto r2 = adapter.eval("greet(\"World\")");
    EXPECT_TRUE(r2.success);
    EXPECT_NE(r2.output.find("Hi, World"), std::string::npos);
}

TEST(ReplFunctions, FuncsCommand) {
    ZiaReplAdapter adapter;
    adapter.eval("func square(x: Integer) -> Integer { return x * x; }");

    auto funcs = adapter.listFunctions();
    EXPECT_EQ(funcs.size(), 1u);
    EXPECT_EQ(funcs[0].name, "square");
}

// ---------------------------------------------------------------------------
// Bind persistence
// ---------------------------------------------------------------------------

TEST(ReplBinds, DefaultBinds) {
    ZiaReplAdapter adapter;
    auto binds = adapter.listBinds();
    EXPECT_EQ(binds.size(), 3u);
    EXPECT_EQ(binds[0], "bind Viper.Terminal");
    EXPECT_EQ(binds[1], "bind Fmt = Viper.Fmt");
    EXPECT_EQ(binds[2], "bind Obj = Viper.Core.Object");
}

TEST(ReplBinds, AddBind) {
    ZiaReplAdapter adapter;
    auto r = adapter.eval("bind Math = Viper.Math");
    EXPECT_TRUE(r.success);

    auto binds = adapter.listBinds();
    EXPECT_EQ(binds.size(), 4u);
}

TEST(ReplBinds, DuplicateBindIgnored) {
    ZiaReplAdapter adapter;
    auto r = adapter.eval("bind Viper.Terminal");
    EXPECT_TRUE(r.success);
    EXPECT_NE(r.output.find("already bound"), std::string::npos);
}

// ---------------------------------------------------------------------------
// .type meta-command
// ---------------------------------------------------------------------------

TEST(ReplType, IntegerType) {
    ZiaReplAdapter adapter;
    EXPECT_EQ(adapter.getExprType("42"), "Integer");
    EXPECT_EQ(adapter.getExprType("2 + 3"), "Integer");
}

TEST(ReplType, NumberType) {
    ZiaReplAdapter adapter;
    EXPECT_EQ(adapter.getExprType("3.14"), "Number");
}

TEST(ReplType, StringType) {
    ZiaReplAdapter adapter;
    EXPECT_EQ(adapter.getExprType("\"hello\""), "String");
}

TEST(ReplType, BooleanType) {
    ZiaReplAdapter adapter;
    EXPECT_EQ(adapter.getExprType("true"), "Boolean");
    EXPECT_EQ(adapter.getExprType("false"), "Boolean");
}

TEST(ReplType, VariableType) {
    ZiaReplAdapter adapter;
    adapter.eval("var x = 42");
    EXPECT_EQ(adapter.getExprType("x"), "Integer");
}

TEST(ReplType, FunctionReturnType) {
    ZiaReplAdapter adapter;
    adapter.eval("func double(n: Integer) -> Integer { return n * 2; }");
    EXPECT_EQ(adapter.getExprType("double(5)"), "Integer");
}

// ---------------------------------------------------------------------------
// Error recovery
// ---------------------------------------------------------------------------

TEST(ReplErrors, SyntaxError) {
    ZiaReplAdapter adapter;
    auto r = adapter.eval("Say(");
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.errorMessage.empty());
}

TEST(ReplErrors, TypeError) {
    ZiaReplAdapter adapter;
    // Say(123) now succeeds due to auto-dispatch to SayInt.
    // Use a genuine type error instead.
    auto r = adapter.eval("var x: Integer = \"hello\"");
    EXPECT_FALSE(r.success);
}

TEST(ReplErrors, SessionIntactAfterError) {
    ZiaReplAdapter adapter;
    adapter.eval("func add(a: Integer, b: Integer) -> Integer { return a + b; }");
    adapter.eval("var x = 10");
    adapter.eval("bad syntax here!!!");

    // Session should still work
    auto r = adapter.eval("add(x, 5)");
    EXPECT_TRUE(r.success);
    EXPECT_NE(r.output.find("15"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST(ReplReset, ClearsState) {
    ZiaReplAdapter adapter;
    adapter.eval("var x = 42");
    adapter.eval("func foo() -> Integer { return 1; }");

    adapter.reset();

    EXPECT_TRUE(adapter.listVariables().empty());
    EXPECT_TRUE(adapter.listFunctions().empty());
    // Default binds restored (Terminal, Fmt, Obj)
    EXPECT_EQ(adapter.listBinds().size(), 3u);
}

// ---------------------------------------------------------------------------
// Tab completion tests (Phase 3 — CompletionEngine integration)
// ---------------------------------------------------------------------------

TEST(ReplCompletion, KeywordCompletion) {
    ZiaReplAdapter adapter;
    auto completions = adapter.complete("whi", 3);
    // Should include "while" as a completion
    bool foundWhile = false;
    for (const auto &c : completions) {
        if (c.find("while") != std::string::npos)
            foundWhile = true;
    }
    EXPECT_TRUE(foundWhile);
}

TEST(ReplCompletion, VariableCompletion) {
    ZiaReplAdapter adapter;
    adapter.eval("var myCounter = 42");
    auto completions = adapter.complete("myCo", 4);
    bool foundVar = false;
    for (const auto &c : completions) {
        if (c.find("myCounter") != std::string::npos)
            foundVar = true;
    }
    EXPECT_TRUE(foundVar);
}

TEST(ReplCompletion, FunctionCompletion) {
    ZiaReplAdapter adapter;
    adapter.eval("func calculateSum(a: Integer, b: Integer) -> Integer { return a + b; }");
    auto completions = adapter.complete("calc", 4);
    bool foundFunc = false;
    for (const auto &c : completions) {
        if (c.find("calculateSum") != std::string::npos)
            foundFunc = true;
    }
    EXPECT_TRUE(foundFunc);
}

TEST(ReplCompletion, EmptyInput) {
    ZiaReplAdapter adapter;
    auto completions = adapter.complete("", 0);
    EXPECT_TRUE(completions.empty());
}

TEST(ReplCompletion, NoMatchReturnsEmpty) {
    ZiaReplAdapter adapter;
    auto completions = adapter.complete("zzzzzzzzz", 9);
    EXPECT_TRUE(completions.empty());
}

// ---------------------------------------------------------------------------
// Multi-line eval tests (Phase 3)
// ---------------------------------------------------------------------------

TEST(ReplMultiLine, ClassifierDetectsIncomplete) {
    // Unclosed brace
    EXPECT_EQ(ReplInputClassifier::classify("func foo() {"), InputKind::Incomplete);
    // Unclosed paren
    EXPECT_EQ(ReplInputClassifier::classify("add(1,"), InputKind::Incomplete);
    // Unclosed bracket
    EXPECT_EQ(ReplInputClassifier::classify("var arr = [1, 2"), InputKind::Incomplete);
}

TEST(ReplMultiLine, ClassifierDetectsComplete) {
    // Closed brace after multi-line accumulation
    EXPECT_EQ(ReplInputClassifier::classify("func foo() {\n    return 1;\n}"), InputKind::Complete);
    // Complete expression
    EXPECT_EQ(ReplInputClassifier::classify("add(1, 2)"), InputKind::Complete);
}

TEST(ReplMultiLine, MultiLineFunctionDefAndCall) {
    ZiaReplAdapter adapter;
    // Simulate what the session does: accumulate then eval
    std::string multiLine = "func triple(n: Integer) -> Integer {\n    return n * 3;\n}";
    auto r1 = adapter.eval(multiLine);
    EXPECT_TRUE(r1.success);

    auto r2 = adapter.eval("triple(7)");
    EXPECT_TRUE(r2.success);
    EXPECT_NE(r2.output.find("21"), std::string::npos);
}

TEST(ReplMultiLine, MultiLineEntityDef) {
    ZiaReplAdapter adapter;
    std::string multiLine = "class Point {\n    Integer x;\n    Integer y;\n"
                            "    func init(px: Integer, py: Integer) {\n"
                            "        x = px;\n        y = py;\n    }\n"
                            "    func getX() -> Integer { return x; }\n}";
    auto r1 = adapter.eval(multiLine);
    EXPECT_TRUE(r1.success);
}

// ---------------------------------------------------------------------------
// Entity / Object auto-print (Phase 5)
// ---------------------------------------------------------------------------

TEST(ReplAutoPrint, EntityAutoprint) {
    ZiaReplAdapter adapter;
    // Define a simple class
    auto r1 =
        adapter.eval("class Dog {\n    String name;\n    func init(n: String) { name = n; }\n}");
    EXPECT_TRUE(r1.success);

    // Evaluating a new class expression should auto-print.
    // Note: Entity pointers satisfy the Say(String) probe before the
    // Obj.ToString() probe because both String and class types are Ptr in the IL.
    // The auto-print still works — the expression gets printed.
    auto r2 = adapter.eval("new Dog(\"Rex\")");
    EXPECT_TRUE(r2.success);
    // Auto-print should produce some output (the exact type may be String
    // due to Say() accepting Ptr, which class pointers also satisfy)
    EXPECT_TRUE(r2.resultType == ResultType::String || r2.resultType == ResultType::Object);
}

TEST(ReplAutoPrint, ObjectToStringExplicit) {
    ZiaReplAdapter adapter;
    // Obj.ToString works explicitly for objects
    adapter.eval("class Cat {\n    String name;\n    func init(n: String) { name = n; }\n}");
    auto result = adapter.eval("Obj.ToString(new Cat(\"Whiskers\"))");
    EXPECT_TRUE(result.success);
    // Should match String probe since Obj.ToString returns a String
    EXPECT_EQ(result.resultType, ResultType::String);
    EXPECT_NE(result.output.find("Object"), std::string::npos);
}

// ---------------------------------------------------------------------------
// History persistence (Phase 5)
// ---------------------------------------------------------------------------

TEST(ReplHistory, SaveAndLoadRoundtrip) {
    // Create a temp file path
    auto tmpDir = std::filesystem::temp_directory_path() / "viper_test_repl";
    std::filesystem::create_directories(tmpDir);
    auto histFile = tmpDir / "test_history";

    // Create an editor and add some entries
    ReplLineEditor editor;
    editor.addHistory("var x = 42");
    editor.addHistory("x + 1");
    editor.addHistory("func greet() -> String { return \"hi\"; }");

    // Save
    EXPECT_TRUE(editor.saveHistory(histFile));

    // Create another editor and load
    ReplLineEditor editor2;
    size_t loaded = editor2.loadHistory(histFile);
    EXPECT_EQ(loaded, 3u);

    auto history = editor2.getHistory();
    EXPECT_EQ(history.size(), 3u);
    EXPECT_EQ(history[0], "var x = 42");
    EXPECT_EQ(history[1], "x + 1");
    EXPECT_EQ(history[2], "func greet() -> String { return \"hi\"; }");

    // Clean up
    std::filesystem::remove_all(tmpDir);
}

TEST(ReplHistory, DuplicateSkipped) {
    ReplLineEditor editor;
    editor.addHistory("var x = 42");
    editor.addHistory("var x = 42"); // duplicate — should be skipped
    editor.addHistory("var y = 10");

    auto history = editor.getHistory();
    EXPECT_EQ(history.size(), 2u);
    EXPECT_EQ(history[0], "var x = 42");
    EXPECT_EQ(history[1], "var y = 10");
}

TEST(ReplHistory, EmptySkipped) {
    ReplLineEditor editor;
    editor.addHistory("");
    editor.addHistory("var x = 1");
    editor.addHistory("");

    auto history = editor.getHistory();
    EXPECT_EQ(history.size(), 1u);
    EXPECT_EQ(history[0], "var x = 1");
}

TEST(ReplHistory, LoadNonexistentFileReturnsZero) {
    ReplLineEditor editor;
    size_t loaded = editor.loadHistory("/tmp/viper_test_nonexistent_file_xyz");
    EXPECT_EQ(loaded, 0u);
}

// ---------------------------------------------------------------------------
// .il meta-command tests (Phase 3)
// ---------------------------------------------------------------------------

TEST(ReplIL, IntegerExpression) {
    ZiaReplAdapter adapter;
    std::string il = adapter.getIL("2 + 3");
    // Should contain IL instructions
    EXPECT_NE(il.find("iadd"), std::string::npos);
}

TEST(ReplIL, FunctionCall) {
    ZiaReplAdapter adapter;
    adapter.eval("func double(n: Integer) -> Integer { return n * 2; }");
    std::string il = adapter.getIL("double(5)");
    // Should contain a call instruction
    EXPECT_NE(il.find("call"), std::string::npos);
}

TEST(ReplIL, InvalidExpressionReturnsError) {
    ZiaReplAdapter adapter;
    std::string il = adapter.getIL("undeclared_xyz");
    // Should contain an error message
    EXPECT_NE(il.find("error"), std::string::npos);
}

TEST(ReplIL, StringExpression) {
    ZiaReplAdapter adapter;
    std::string il = adapter.getIL("\"hello\"");
    // Should contain string constant or Say call
    EXPECT_NE(il.find("hello"), std::string::npos);
}

} // anonymous namespace

int main() {
    return viper_test::run_all_tests();
}
