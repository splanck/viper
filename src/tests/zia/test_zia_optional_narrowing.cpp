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

namespace
{

/// @brief Test that type narrowing works after "x != null" check.
TEST(ZiaOptionalNarrowing, NarrowingAfterNotNullCheck)
{
    const std::string src = R"(
module Test;

entity Person {
    expose String name;
}

func start() {
    Person? maybePerson = new Person("Alice");

    if (maybePerson != null) {
        // Inside this branch, maybePerson should be narrowed to Person
        String name = maybePerson.name;
        Viper.Terminal.Say(name);
    }
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for NarrowingAfterNotNullCheck:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that type narrowing works in else branch after "x == null" check.
TEST(ZiaOptionalNarrowing, NarrowingInElseBranchAfterNullCheck)
{
    const std::string src = R"(
module Test;

entity Person {
    expose String name;
}

func start() {
    Person? maybePerson = new Person("Bob");

    if (maybePerson == null) {
        Viper.Terminal.Say("No person");
    } else {
        // Inside else branch, maybePerson should be narrowed to Person
        String name = maybePerson.name;
        Viper.Terminal.Say(name);
    }
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for NarrowingInElseBranchAfterNullCheck:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that type narrowing works with reversed null check (null != x).
TEST(ZiaOptionalNarrowing, NarrowingWithReversedNullCheck)
{
    const std::string src = R"(
module Test;

entity Person {
    expose String name;
}

func start() {
    Person? maybePerson = new Person("Charlie");

    if (null != maybePerson) {
        // Inside this branch, maybePerson should be narrowed to Person
        String name = maybePerson.name;
        Viper.Terminal.Say(name);
    }
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for NarrowingWithReversedNullCheck:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that narrowed type allows method calls.
TEST(ZiaOptionalNarrowing, NarrowedTypeAllowsMethodCalls)
{
    const std::string src = R"(
module Test;

entity Person {
    expose String name;

    expose func greet() -> String {
        return "Hello, " + self.name;
    }
}

func start() {
    Person? maybePerson = new Person("Eve");

    if (maybePerson != null) {
        // Inside this branch, can call methods on the narrowed type
        String greeting = maybePerson.greet();
        Viper.Terminal.Say(greeting);
    }
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for NarrowedTypeAllowsMethodCalls:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
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
TEST(ZiaForceUnwrap, ForceUnwrapEntity)
{
    const std::string src = R"(
module Test;

entity Person {
    expose String name;
}

func start() {
    Person? maybePerson = new Person("Alice");
    Person person = maybePerson!;
    Viper.Terminal.Say(person.name);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ForceUnwrapEntity:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that force-unwrap works in function call arguments.
TEST(ZiaForceUnwrap, ForceUnwrapInCallArg)
{
    const std::string src = R"(
module Test;

entity Item {
    expose String label;
}

func useItem(item: Item) {
    Viper.Terminal.Say(item.label);
}

func start() {
    Item? maybeItem = new Item("sword");
    useItem(maybeItem!);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ForceUnwrapInCallArg:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that force-unwrap on non-optional produces an error.
TEST(ZiaForceUnwrap, ForceUnwrapNonOptionalError)
{
    const std::string src = R"(
module Test;

func start() {
    Integer x = 42;
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
TEST(ZiaForceUnwrap, ForceUnwrapThenFieldAccess)
{
    const std::string src = R"(
module Test;

entity Node {
    expose String value;
}

func start() {
    Node? maybeNode = new Node("hello");
    String val = maybeNode!.value;
    Viper.Terminal.Say(val);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ForceUnwrapThenFieldAccess:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
