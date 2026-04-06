//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia/test_zia_final_enforcement.cpp
// Purpose: Verify that the `final` keyword prevents variable reassignment.
// Key invariants:
//   - Reassigning a final variable must produce a compile-time error.
//   - Compound assignments (+=, -=, etc.) on final variables must also error.
//   - For-in loop variables (implicitly final) must not be reassignable.
//   - Mutable variables (var) must remain freely reassignable.
// Ownership/Lifetime: Test-only; not linked into the compiler.
// Links: src/frontends/zia/Sema_Expr_Ops.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

CompilerResult compileSource(const std::string &source) {
    SourceManager sm;
    CompilerInput input{.source = source, .path = "final_test.zia"};
    CompilerOptions opts{};
    return compile(input, opts, sm);
}

bool hasDiagContaining(const DiagnosticEngine &diag, const std::string &needle) {
    for (const auto &d : diag.diagnostics()) {
        if (d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

// ============================================================================
// Negative tests: these should all fail to compile
// ============================================================================

TEST(ZiaFinalEnforcement, SimpleReassignment) {
    auto result = compileSource(R"(
module Test;
func start() {    final x = 10;
    x = 20;
}
)");
    EXPECT_TRUE(!result.succeeded());
    EXPECT_TRUE(hasDiagContaining(result.diagnostics, "Cannot reassign final variable"));
}

TEST(ZiaFinalEnforcement, CompoundAssignment) {
    auto result = compileSource(R"(
module Test;
func start() {    final x = 10;
    x += 5;
}
)");
    EXPECT_TRUE(!result.succeeded());
    EXPECT_TRUE(hasDiagContaining(result.diagnostics, "Cannot reassign final variable"));
}

TEST(ZiaFinalEnforcement, ForLoopVariable) {
    auto result = compileSource(R"(
module Test;
func start() {    for i in 0..10 {
        i = 5;
    }
}
)");
    EXPECT_TRUE(!result.succeeded());
    EXPECT_TRUE(hasDiagContaining(result.diagnostics, "Cannot reassign final variable"));
}

TEST(ZiaFinalEnforcement, AllCompoundOps) {
    // Test all compound assignment operators on a final variable
    for (const char *op : {"+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "^="}) {
        std::string source = std::string(R"(
module Test;
func start() {    final x = 100;
    x )") + op + R"( 1;
}
)";
        auto result = compileSource(source);
        EXPECT_TRUE(!result.succeeded());
    }
}

// ============================================================================
// Positive tests: these should compile and run correctly
// ============================================================================

TEST(ZiaFinalEnforcement, MutableReassignmentOk) {
    auto result = compileSource(R"(
module Test;
func start() {    var x = 10;
    x = 20;
    x += 10;
}
)");
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaFinalEnforcement, FinalReadOk) {
    auto result = compileSource(R"(
module Test;
func start() {    final x = 42;
    Viper.Terminal.SayInt(x);
}
)");
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaFinalEnforcement, FinalInExpressionOk) {
    auto result = compileSource(R"(
module Test;
func start() {    final x = 10;
    var y = x + 5;
    Viper.Terminal.SayInt(y);
}
)");
    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
