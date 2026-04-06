//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia/test_zia_parser_errors.cpp
// Purpose: Negative tests for the Zia parser and lexer.
//          Verifies that malformed input produces diagnostics rather than
//          crashes or silently incorrect ASTs.
// Key invariants:
//   - Every test here must FAIL compilation (result.succeeded() == false).
//   - No test should crash or hang.
// Ownership/Lifetime: Test-only; not linked into the compiler.
// Links: src/frontends/zia/Parser_Expr.cpp, src/frontends/zia/Lexer.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

/// @brief Compile Zia source and return the result.
CompilerResult compileSource(const std::string &source) {
    SourceManager sm;
    CompilerInput input{.source = source, .path = "error_test.zia"};
    CompilerOptions opts{};
    return compile(input, opts, sm);
}

/// @brief Check whether any diagnostic message contains @p needle.
bool hasDiagContaining(const DiagnosticEngine &diag, const std::string &needle) {
    for (const auto &d : diag.diagnostics()) {
        if (d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

//===----------------------------------------------------------------------===//
// Missing tokens
//===----------------------------------------------------------------------===//

TEST(ZiaParserErrors, MissingSemicolon) {
    auto result = compileSource(R"(
module Test;
func start() {    var x = 5
    var y = 10
}
)");
    // Should either fail or gracefully recover
    // (semicolons are not required in Zia, so this may succeed)
}

TEST(ZiaParserErrors, MissingClosingBrace) {
    auto result = compileSource(R"(
module Test;
func start() {    var x = 5
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, MissingClosingParen) {
    auto result = compileSource(R"(
module Test;
func start() {    var x = (1 + 2
}
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, MissingFuncBody) {
    auto result = compileSource(R"(
module Test;
func start()
)");
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Invalid expressions
//===----------------------------------------------------------------------===//

TEST(ZiaParserErrors, DoubleOperator) {
    auto result = compileSource(R"(
module Test;
func start() {    var x = 1 ++ 2
}
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, TrailingOperator) {
    auto result = compileSource(R"(
module Test;
func start() {    var x = 1 +
}
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, EmptyParens) {
    auto result = compileSource(R"(
module Test;
func start() {    var x = ()
}
)");
    // Empty parens could be valid (unit/void) or error depending on grammar
}

TEST(ZiaParserErrors, InvalidAssignmentTarget) {
    auto result = compileSource(R"(
module Test;
func start() {    5 = 10
}
)");
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Module declaration errors
//===----------------------------------------------------------------------===//

TEST(ZiaParserErrors, MissingModuleDecl) {
    auto result = compileSource(R"(
func start() {}
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, DuplicateModuleDecl) {
    auto result = compileSource(R"(
module Test;
module Other;
func start() {}
)");
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Type annotation errors
//===----------------------------------------------------------------------===//

TEST(ZiaParserErrors, InvalidTypeAnnotation) {
    auto result = compileSource(R"(
module Test;
func start() {    var x: 123 = 5
}
)");
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Control flow errors
//===----------------------------------------------------------------------===//

TEST(ZiaParserErrors, IfWithoutCondition) {
    auto result = compileSource(R"(
module Test;
func start() {    if {
        var x = 1
    }
}
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, WhileWithoutCondition) {
    auto result = compileSource(R"(
module Test;
func start() {    while {
        var x = 1
    }
}
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, ForInWithoutIterable) {
    auto result = compileSource(R"(
module Test;
func start() {    for x in {
    }
}
)");
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// String / literal edge cases
//===----------------------------------------------------------------------===//

TEST(ZiaParserErrors, UnterminatedString) {
    auto result = compileSource("module Test;\nfunc start() {\n    var x = \"hello\n}\n");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, UnterminatedStringInterpolation) {
    auto result = compileSource("module Test;\nfunc start() {\n    var x = \"hello \\(\n}\n");
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Function declaration errors
//===----------------------------------------------------------------------===//

TEST(ZiaParserErrors, DuplicateParamNames) {
    auto result = compileSource(R"(
module Test;
func add(x: Integer, x: Integer) -> Integer {    return x + x
}
func start() {})");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, MissingReturnType) {
    auto result = compileSource(R"(
module Test;
func add(x: Integer) -> {    return x
}
func start() {})");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, RejectsJavaStyleParameters) {
    auto result = compileSource(R"(
module Test;
func add(Integer a, Integer b) -> Integer { return a + b; }
)");
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasDiagContaining(result.diagnostics, "expected ':' after parameter name"));
}

TEST(ZiaParserErrors, RejectsColonReturnTypeSyntax) {
    auto result = compileSource(R"(
module Test;
func add(x: Integer): Integer { return x; }
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, RejectsTypedVariableDeclarationWithoutVar) {
    auto result = compileSource(R"(
module Test;
func start() {
    Integer x = 5;
}
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, RejectsBindAliasAssignmentSyntax) {
    auto result = compileSource(R"(
module Test;
bind Math = Viper.Math;
func start() {}
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, RejectsTupleDestructuringVarDecl) {
    auto result = compileSource(R"(
module Test;
func start() {
    var (x, y) = (1, 2);
}
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, RejectsBareTypeCallConstruction) {
    auto result = compileSource(R"(
module Test;
class Box {
    expose func init(value: Integer) {}
}
func start() {
    var box = Box(42);
}
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, RejectsUntypedSingleParameterLambda) {
    auto result = compileSource(R"(
module Test;
func start() {
    var inc = (x) => x + 1;
}
)");
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(
        hasDiagContaining(result.diagnostics, "lambda parameters require explicit type annotations"));
}

TEST(ZiaParserErrors, RejectsUntypedMultiParameterLambda) {
    auto result = compileSource(R"(
module Test;
func start() {
    var add = (a, b) => a + b;
}
)");
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(
        hasDiagContaining(result.diagnostics, "lambda parameters require explicit type annotations"));
}

TEST(ZiaParserErrors, RejectsFuncStyleLambdaSyntax) {
    auto result = compileSource(R"(
module Test;
func start() {
    var work = func(x: Integer) -> Integer { return x + 1; };
}
)");
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Match expression errors
//===----------------------------------------------------------------------===//

TEST(ZiaParserErrors, MatchWithoutSubject) {
    auto result = compileSource(R"(
module Test;
func start() {    match {
        case 1 => print("one")
    }
}
)");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, MatchCaseWithoutArrow) {
    auto result = compileSource(R"(
module Test;
func start() {    var x = 1
    match x {
        case 1 print("one")
    }
}
)");
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Entity declaration errors
//===----------------------------------------------------------------------===//

TEST(ZiaParserErrors, EntityMissingName) {
    auto result = compileSource(R"(
module Test;
class {
    var x: Integer
}
func start() {})");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, EntityMissingBrace) {
    auto result = compileSource(R"(
module Test;
class Foo
    var x: Integer
func start() {})");
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Empty / minimal input
//===----------------------------------------------------------------------===//

TEST(ZiaParserErrors, EmptyInput) {
    auto result = compileSource("");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, WhitespaceOnly) {
    auto result = compileSource("   \n\n\t\t\n   ");
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaParserErrors, CommentOnly) {
    auto result = compileSource("// just a comment\n");
    EXPECT_FALSE(result.succeeded());
}

} // namespace

int main() {
    return viper_test::run_all_tests();
}
