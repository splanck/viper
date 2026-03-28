//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia/test_zia_guard_clause_narrowing.cpp
// Purpose: Verify guard-clause null narrowing: after `if (x == null) return;`,
//          the variable x should be narrowed to its non-optional type.
// Key invariants:
//   - Guard clause pattern compiles without errors.
//   - Non-guard patterns (no early return) do NOT narrow.
// Links: src/frontends/zia/Sema_Stmt.cpp (analyzeBlockStmt)
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

/// @brief Guard clause with return narrows optional to non-optional.
TEST(ZiaGuardClause, NullCheckReturnNarrows) {
    const std::string src = R"(
module Test;

class Person {
    expose String name;

    expose func init(n: String) {
        name = n;
    }
}

func greet(p: Person?) {
    if (p == null) {
        return;
    }
    // After guard clause, p should be narrowed to Person
    String name = p.name;
    Viper.Terminal.Say(name);
}

func start() {
    greet(new Person("Alice"));
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "guard_clause.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for NullCheckReturnNarrows:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Multiple guard clauses in sequence.
TEST(ZiaGuardClause, MultipleGuardClauses) {
    const std::string src = R"(
module Test;

class Person {
    expose String name;

    expose func init(n: String) {
        name = n;
    }
}

class Item {
    expose String label;

    expose func init(l: String) {
        label = l;
    }
}

func process(p: Person?, item: Item?) {
    if (p == null) {
        return;
    }
    if (item == null) {
        return;
    }
    // Both p and item should be narrowed
    Viper.Terminal.Say(p.name);
    Viper.Terminal.Say(item.label);
}

func start() {
    process(new Person("Bob"), new Item("sword"));
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "multi_guard.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for MultipleGuardClauses:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Guard-clause narrowing must also lower optional primitives as non-optional values.
TEST(ZiaGuardClause, PrimitiveOptionalGuardNarrowsForLowering) {
    const std::string src = R"(
module Test;

func emitValue(x: Integer?) {
    if (x == null) {
        return;
    }

    Integer narrowed = x;
    Viper.Terminal.SayInt(x);
    Viper.Terminal.SayInt(narrowed);
}

func start() {
    emitValue(42);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "guard_clause_integer.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for PrimitiveOptionalGuardNarrowsForLowering:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
