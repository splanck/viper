//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia try/catch/finally and throw statements.
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

// ============================================================================
// Helpers
// ============================================================================

static bool hasFunction(const il::core::Module &mod, const std::string &fnName) {
    for (const auto &fn : mod.functions) {
        if (fn.name == fnName)
            return true;
    }
    return false;
}

/// @brief Check if a function contains an instruction with the given opcode.
static bool hasOpcode(const il::core::Module &mod, const std::string &fnName, il::core::Opcode op) {
    for (const auto &fn : mod.functions) {
        if (fn.name == fnName) {
            for (const auto &block : fn.blocks) {
                for (const auto &instr : block.instructions) {
                    if (instr.op == op)
                        return true;
                }
            }
        }
    }
    return false;
}

/// @brief Count blocks in a specific function.
static size_t blockCount(const il::core::Module &mod, const std::string &fnName) {
    for (const auto &fn : mod.functions) {
        if (fn.name == fnName)
            return fn.blocks.size();
    }
    return 0;
}

static bool hasErrorContaining(const CompilerResult &result, const std::string &needle) {
    for (const auto &d : result.diagnostics.diagnostics()) {
        if (d.severity == Severity::Error && d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

// ============================================================================
// Tests
// ============================================================================

/// @brief Test basic try/catch compiles and produces EH opcodes.
TEST(ZiaTryCatch, BasicTryCatch) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    try {
        var x = 1;
    } catch(e) {
        var y = 2;
    }
}
)";

    CompilerInput input{.source = source, .path = "test_try_basic.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());

    // Should have EhPush and EhPop opcodes
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhPush));
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhPop));

    // Should have EhEntry in handler block
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhEntry));

    // Should have ResumeLabel to resume from handler
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::ResumeLabel));
}

/// @brief Test try/finally (no catch clause).
TEST(ZiaTryCatch, TryFinally) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    try {
        var x = 1;
    } finally {
        var cleanup = 0;
    }
}
)";

    CompilerInput input{.source = source, .path = "test_try_finally.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());

    // EH opcodes should be present
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhPush));
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhPop));

    // Multiple blocks for: entry, handler, finally_normal, after_try
    EXPECT_TRUE(blockCount(result.module, "main") >= 4);
}

/// @brief Test try/catch/finally (all three clauses).
TEST(ZiaTryCatch, TryCatchFinally) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    try {
        var x = 1;
    } catch(e) {
        var y = 2;
    } finally {
        var z = 3;
    }
}
)";

    CompilerInput input{.source = source, .path = "test_try_catch_finally.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());

    // All EH opcodes should be present
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhPush));
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhPop));
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhEntry));
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::ResumeLabel));
}

/// @brief Test throw statement compiles and emits a RuntimeError trap.
TEST(ZiaTryCatch, ThrowStatement) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    throw 42;
}
)";

    CompilerInput input{.source = source, .path = "test_throw.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());

    // Zia throw is a language-level RuntimeError, emitted through trap.from_err.
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::TrapFromErr));
    EXPECT_FALSE(hasOpcode(result.module, "main", il::core::Opcode::Trap));
}

/// @brief Named catch bindings are initialized before the catch body is analyzed.
TEST(ZiaTryCatch, CatchBindingCanBeRead) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    try {
        throw "boom";
    } catch(e) {
        Viper.Terminal.Say(e.message);
    }
}
)";

    CompilerInput input{.source = source, .path = "test_catch_binding_read.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());
}

/// @brief Test catch without variable binding.
TEST(ZiaTryCatch, CatchWithoutVariable) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    try {
        var x = 1;
    } catch {
        var y = 2;
    }
}
)";

    CompilerInput input{.source = source, .path = "test_catch_no_var.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());

    // Should compile successfully even without catch variable
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhPush));
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhEntry));
}

/// @brief Typed catch should compose with finally without verifier failures.
TEST(ZiaTryCatch, TypedCatchFinally) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    try {
        throw 1;
    } catch(e: RuntimeError) {
        Viper.Terminal.Say("caught");
    } finally {
        Viper.Terminal.Say("done");
    }
}
)";

    CompilerInput input{.source = source, .path = "test_typed_catch_finally.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhPush));
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::EhEntry));
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::ResumeLabel));
}

TEST(ZiaTryCatch, GuardElseThrowSatisfiesExitRequirement) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var ok: Boolean = false;
    guard ok else {
        throw "not ok";
    }
}
)";

    CompilerInput input{.source = source, .path = "test_guard_throw.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaTryCatch, TryExpressionPropagatesOptionalNull) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func maybeValue(flag: Boolean) -> Integer? {
    if flag {
        return 7;
    }
    return null;
}

func pass(flag: Boolean) -> Integer? {
    var value: Integer = maybeValue(flag)?;
    return value;
}

func start() {
    var result: Integer? = pass(true);
}
)";

    CompilerInput input{.source = source, .path = "test_try_expr_optional.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaTryCatch, TryExpressionRequiresOptionalOperand) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x: Integer = 1;
    var y: Integer = x?;
}
)";

    CompilerInput input{.source = source, .path = "test_try_expr_non_optional.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "requires an optional operand"));
}

TEST(ZiaTryCatch, TryExpressionRequiresOptionalReturn) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func bad() -> Integer {
    var x: Integer? = 1;
    return x?;
}

func start() {
    Viper.Terminal.SayInt(bad());
}
)";

    CompilerInput input{.source = source, .path = "test_try_expr_bad_return.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "function returning an optional type"));
}

} // anonymous namespace

int main() {
    return viper_test::run_all_tests();
}
