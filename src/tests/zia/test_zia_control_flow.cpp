//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia control flow (if, while, for).
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

/// @brief Test that if statements compile correctly.
TEST(ZiaControlFlow, IfStatement) {
    SourceManager sm;
    // Use a runtime condition (not constant) so peephole doesn't optimize away the CBr
    const std::string source = R"(
module Test;

func start() {    var x = 1;
    if (x > 0) {
        Zanna.Terminal.Say("yes");
    } else {
        Zanna.Terminal.Say("no");
    }
}
)";
    CompilerInput input{.source = source, .path = "if.zia"};
    // Use O0 to test IL generation without optimization
    CompilerOptions opts{.optLevel = OptLevel::O0};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    bool foundCBr = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main") {
            for (const auto &block : fn.blocks) {
                for (const auto &instr : block.instructions) {
                    if (instr.op == il::core::Opcode::CBr) {
                        foundCBr = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundCBr);
}

/// @brief Test that while loops compile correctly.
TEST(ZiaControlFlow, WhileLoop) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var i: Integer = 0;
    while (i < 10) {
        i = i + 1;
    }
}
)";
    CompilerInput input{.source = source, .path = "while.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    bool foundCmp = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main") {
            for (const auto &block : fn.blocks) {
                for (const auto &instr : block.instructions) {
                    if (instr.op == il::core::Opcode::SCmpLT) {
                        foundCmp = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundCmp);
}

/// @brief Test that for-in loops with ranges work correctly.
TEST(ZiaControlFlow, ForInLoop) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var sum: Integer = 0;
    for (i in 0..5) {
        sum = sum + i;
    }
    Zanna.Terminal.SayInt(sum);
}
)";
    CompilerInput input{.source = source, .path = "forin.zia"};
    // Use O0 to test IL generation without optimization
    CompilerOptions opts{.optLevel = OptLevel::O0};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for ForInLoop:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundForInCond = false;
    bool foundAlloca = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main") {
            for (const auto &block : fn.blocks) {
                if (block.label.find("forin_cond") != std::string::npos)
                    foundForInCond = true;
                for (const auto &instr : block.instructions) {
                    if (instr.op == il::core::Opcode::Alloca)
                        foundAlloca = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundForInCond);
    EXPECT_TRUE(foundAlloca);
}

/// @brief Test that for-in loops over lists and maps compile.
TEST(ZiaControlFlow, ForInCollections) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var numbers: List[Integer] = [1, 2, 3];
    var sum: Integer = 0;
    for (n in numbers) {
        sum = sum + n;
    }

    var ages: Map[String, Integer] = new Map[String, Integer]();
    ages.set("Alice", 30);
    ages.set("Bob", 25);
    for ((name, age) in ages) {
        sum = sum + age;
    }

    Zanna.Terminal.SayInt(sum);
}
)";
    CompilerInput input{.source = source, .path = "forin_collections.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for ForInCollections:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundListLoop = false;
    bool foundMapLoop = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main") {
            for (const auto &block : fn.blocks) {
                if (block.label.find("forin_list") != std::string::npos)
                    foundListLoop = true;
                if (block.label.find("forin_map") != std::string::npos)
                    foundMapLoop = true;
            }
        }
    }
    EXPECT_TRUE(foundListLoop);
    EXPECT_TRUE(foundMapLoop);
}

/// @brief Regression: a string-typed for-in loop variable must be
///        null-initialized before the loop.
/// @details Each iteration stores the new element with releaseDisplaced=true,
///          which frees the slot's previous value; on the first iteration that
///          value is the freshly-allocated (uninitialized) slot. The VM
///          zero-inits allocas so rt_str_release_maybe(null) was a safe no-op and
///          this was invisible to interpreter-run tests — but native codegen
///          leaves stack garbage there, so the release trapped "invalid string
///          handle". This crashed the IDE's native build on Open Folder
///          (for dir in Dir.Dirs(...)). The fix emits a null store to the loop
///          variable's string slot before the loop on every backend.
TEST(ZiaControlFlow, ForInStringLoopVarNullInitialized) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var out: Integer = 0;
    var names: List[String] = ["a", "b"];
    for (n in names) {
        out = out + 1;
    }
    Zanna.Terminal.SayInt(out);
}
)";
    CompilerInput input{.source = source, .path = "forin_str_init.zia"};
    CompilerOptions opts{.optLevel = OptLevel::O0};

    auto result = compile(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // The only string slot in this program is the loop variable `n`; a Store with
    // a null-pointer value operand is the loop-var zero-init the fix adds. Without
    // the fix no null store exists (the element store is the actual string, the
    // integer/list stores are non-null), so this fails if the init is dropped.
    bool foundNullStore = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name != "main")
            continue;
        for (const auto &block : fn.blocks) {
            for (const auto &instr : block.instructions) {
                if (instr.op != il::core::Opcode::Store)
                    continue;
                for (const auto &op : instr.operands) {
                    if (op.kind == il::core::Value::Kind::NullPtr)
                        foundNullStore = true;
                }
            }
        }
    }
    // Failure means the string for-in loop variable slot was not
    // null-initialized before the loop.
    EXPECT_TRUE(foundNullStore);
}

/// @brief Bug #28: Guard statement should work without parentheses.
/// Swift-style guard syntax should be supported in class methods.
TEST(ZiaControlFlow, GuardStatementWithoutParens) {
    SourceManager sm;
    const std::string source = R"(
module Test;

class Player {
    expose Integer state;

    expose func moveUp() {        guard state != 0 else { return; }
        state = state + 1;
    }

    expose func moveDown() {        guard (state != 0) else { return; }
        state = state - 1;
    }
}

func start() {    var p: Player = new Player();
    p.state = 1;
    p.moveUp();
    p.moveDown();
}
)";
    CompilerInput input{.source = source, .path = "guard.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for GuardStatementWithoutParens:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded()); // Bug #28: Guard without parens should parse correctly
}

} // namespace

int main() {
    return zanna_test::run_all_tests();
}
