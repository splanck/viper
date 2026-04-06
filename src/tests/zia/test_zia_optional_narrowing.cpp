//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia/test_zia_optional_narrowing.cpp
// Purpose: Test optional type narrowing after null checks
// Key invariants: After null check, type should be narrowed in the appropriate branch
// Links: bugs/sqlzia_bugs.md BUG-003
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

/// @brief Test that type narrowing works after "x != null" check.
TEST(ZiaOptionalNarrowing, NarrowingAfterNotNullCheck) {
    const std::string src = R"(
module Test;

class Person {
    expose String name;

    expose func init(n: String) {        name = n;
    }
}

func start() {    var maybePerson: Person? = new Person("Alice");

    if (maybePerson != null) {
        // Inside this branch, maybePerson should be narrowed to Person
        var name: String = maybePerson.name;
        Viper.Terminal.Say(name);
    }
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for NarrowingAfterNotNullCheck:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that type narrowing works in else branch after "x == null" check.
TEST(ZiaOptionalNarrowing, NarrowingInElseBranchAfterNullCheck) {
    const std::string src = R"(
module Test;

class Person {
    expose String name;

    expose func init(n: String) {        name = n;
    }
}

func start() {    var maybePerson: Person? = new Person("Bob");

    if (maybePerson == null) {
        Viper.Terminal.Say("No person");
    } else {
        // Inside else branch, maybePerson should be narrowed to Person
        var name: String = maybePerson.name;
        Viper.Terminal.Say(name);
    }
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for NarrowingInElseBranchAfterNullCheck:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that type narrowing works with reversed null check (null != x).
TEST(ZiaOptionalNarrowing, NarrowingWithReversedNullCheck) {
    const std::string src = R"(
module Test;

class Person {
    expose String name;

    expose func init(n: String) {        name = n;
    }
}

func start() {    var maybePerson: Person? = new Person("Charlie");

    if (null != maybePerson) {
        // Inside this branch, maybePerson should be narrowed to Person
        var name: String = maybePerson.name;
        Viper.Terminal.Say(name);
    }
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for NarrowingWithReversedNullCheck:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that narrowed type allows method calls.
TEST(ZiaOptionalNarrowing, NarrowedTypeAllowsMethodCalls) {
    const std::string src = R"(
module Test;

class Person {
    expose String name;

    expose func init(n: String) {        name = n;
    }

    expose func greet() -> String {        return "Hello, " + self.name;
    }
}

func start() {    var maybePerson: Person? = new Person("Eve");

    if (maybePerson != null) {
        // Inside this branch, can call methods on the narrowed type
        var greeting: String = maybePerson.greet();
        Viper.Terminal.Say(greeting);
    }
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for NarrowedTypeAllowsMethodCalls:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

//=============================================================================
// Force-Unwrap Operator Tests
//=============================================================================

/// @brief Test that force-unwrap converts Optional[Entity] to Entity.
TEST(ZiaForceUnwrap, ForceUnwrapEntity) {
    const std::string src = R"(
module Test;

class Person {
    expose String name;

    expose func init(n: String) {        name = n;
    }
}

func start() {    var maybePerson: Person? = new Person("Alice");
    var person: Person = maybePerson!;
    Viper.Terminal.Say(person.name);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for ForceUnwrapEntity:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that force-unwrap works in function call arguments.
TEST(ZiaForceUnwrap, ForceUnwrapInCallArg) {
    const std::string src = R"(
module Test;

class Item {
    expose String label;

    expose func init(l: String) {        label = l;
    }
}

func useItem(item: Item) {    Viper.Terminal.Say(item.label);
}

func start() {    var maybeItem: Item? = new Item("sword");
    useItem(maybeItem!);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for ForceUnwrapInCallArg:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that force-unwrap on non-optional produces an error.
TEST(ZiaForceUnwrap, ForceUnwrapNonOptionalError) {
    const std::string src = R"(
module Test;

func start() {    var x: Integer = 42;
    var y = x!;
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    // Should fail — force-unwrapping a non-optional is an error
    EXPECT_FALSE(result.succeeded());
}

/// @brief Test force-unwrap chains with field access.
TEST(ZiaForceUnwrap, ForceUnwrapThenFieldAccess) {
    const std::string src = R"(
module Test;

class Node {
    expose String value;

    expose func init(v: String) {        value = v;
    }
}

func start() {    var maybeNode: Node? = new Node("hello");
    var val: String = maybeNode!.value;
    Viper.Terminal.Say(val);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for ForceUnwrapThenFieldAccess:\n";
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
