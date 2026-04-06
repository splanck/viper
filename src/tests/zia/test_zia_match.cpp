//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia match expressions and statements.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

/// @brief Test that match statement works correctly.
TEST(ZiaMatch, MatchStatement) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var x: Integer = 5;
    match (x) {
        1 => { Viper.Terminal.Say("one"); }
        _ => { Viper.Terminal.Say("other"); }
    }
}
)";
    CompilerInput input{.source = source, .path = "match_stmt.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for MatchStatement:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundMatchBlock = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main") {
            for (const auto &block : fn.blocks) {
                if (block.label.find("match_arm") != std::string::npos)
                    foundMatchBlock = true;
            }
        }
    }
    EXPECT_TRUE(foundMatchBlock);
}

/// @brief Test that match expression (used as value) compiles.
TEST(ZiaMatch, MatchExpression) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var x: Integer = 2;
    var result: Integer = match (x) {
        1 => 10,
        2 => 20,
        _ => 0
    };
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "match_expr.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for MatchExpression:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundMatchBlock = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main") {
            for (const auto &block : fn.blocks) {
                if (block.label.find("match_arm") != std::string::npos)
                    foundMatchBlock = true;
            }
        }
    }
    EXPECT_TRUE(foundMatchBlock);
}

/// @brief Test that match expression with boolean subject and expression patterns works.
/// This tests the guard-style matching: match (true) { cond => value, ... }
TEST(ZiaMatch, MatchExpressionWithBooleanSubject) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func clamp(value: Integer, minVal: Integer, maxVal: Integer) -> Integer {    return match (true) {
        value < minVal => minVal,
        value > maxVal => maxVal,
        _ => value
    };
}

func start() {    var a: Integer = clamp(5, 0, 10);
    var negative: Integer = 0 - 5;
    var b: Integer = clamp(negative, 0, 10);
    var c: Integer = clamp(15, 0, 10);
    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
    Viper.Terminal.SayInt(c);
}
)";
    CompilerInput input{.source = source, .path = "match_bool.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for MatchExpressionWithBooleanSubject:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Verify clamp function has match blocks
    bool foundClampMatchArm = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "clamp") {
            for (const auto &block : fn.blocks) {
                if (block.label.find("match_arm") != std::string::npos)
                    foundClampMatchArm = true;
            }
        }
    }
    EXPECT_TRUE(foundClampMatchArm);

    // Verify comparison instruction is generated for expression patterns
    bool foundScmpLt = false;
    bool foundScmpGt = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "clamp") {
            for (const auto &block : fn.blocks) {
                for (const auto &instr : block.instructions) {
                    if (instr.op == il::core::Opcode::SCmpLT)
                        foundScmpLt = true;
                    if (instr.op == il::core::Opcode::SCmpGT)
                        foundScmpGt = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundScmpLt);
    EXPECT_TRUE(foundScmpGt);
}

/// @brief Test constructor, tuple, and optional patterns with guards.
TEST(ZiaMatch, MatchPatterns) {
    SourceManager sm;
    const std::string source = R"(
module Test;

struct Point {
    expose Integer x;
    expose Integer y;
}

func start() {    var p: Point = new Point(1, 7);
    var t: (Integer, Integer) = (1, 2);
    var maybe: Integer? = 5;

    var fromPoint: Integer = match (p) {
        Point(1, y) => y,
        _ => 0
    };

    var fromTuple: Integer = match (t) {
        (1, y) => y,
        _ => 0
    };

    var fromOpt: Integer? = match (maybe) {
        Some(v) if v > 0 => v,
        None => null,
        _ => null
    };

    var sum: Integer = fromPoint + fromTuple + (fromOpt ?? 0);
    Viper.Terminal.SayInt(sum);
}
)";
    CompilerInput input{.source = source, .path = "match_patterns.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for MatchPatterns:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundMatchArm = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main") {
            for (const auto &block : fn.blocks) {
                if (block.label.find("match_arm") != std::string::npos)
                    foundMatchArm = true;
            }
        }
    }
    EXPECT_TRUE(foundMatchArm);
}

} // namespace

int main() {
    return viper_test::run_all_tests();
}
