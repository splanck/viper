//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia/test_zia_list_remove_at.cpp
// Purpose: Test List.removeAt() method and type checking for List.remove()
// Key invariants: removeAt should compile; remove with wrong type should error
// Links: bugs/sqlzia_bugs.md BUG-002
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

/// @brief Test that List.removeAt() compiles successfully.
TEST(ZiaListRemoveAt, RemoveAtMethod)
{
    const std::string src = R"(
module Test;

func start() {
    List[String] items = new List[String]();
    items.add("a");
    items.add("b");
    items.add("c");
    items.removeAt(1);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for RemoveAtMethod:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Verify the list removeAt function is called
    bool hasRemoveAtCall = false;
    for (const auto &fn : result.module.functions)
    {
        for (const auto &bb : fn.blocks)
        {
            for (const auto &in : bb.instructions)
            {
                if (in.op == il::core::Opcode::Call &&
                    in.callee.find("RemoveAt") != std::string::npos)
                {
                    hasRemoveAtCall = true;
                }
            }
        }
    }
    EXPECT_TRUE(hasRemoveAtCall);
}

/// @brief Test that List.remove(integer) on non-integer list produces error.
TEST(ZiaListRemoveAt, RemoveTypeMismatchError)
{
    const std::string src = R"(
module Test;

func start() {
    List[String] items = new List[String]();
    items.add("a");
    items.add("b");
    items.add("c");
    items.remove(1);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    // Should produce an error about type mismatch
    EXPECT_FALSE(result.succeeded());

    // Check that the error message mentions removeAt
    bool hasHelpfulError = false;
    for (const auto &d : result.diagnostics.diagnostics())
    {
        if (d.message.find("removeAt") != std::string::npos)
        {
            hasHelpfulError = true;
        }
    }
    EXPECT_TRUE(hasHelpfulError);
}

/// @brief Test that List.remove(value) with matching type works.
TEST(ZiaListRemoveAt, RemoveMatchingTypeWorks)
{
    const std::string src = R"(
module Test;

func start() {
    List[Integer] items = new List[Integer]();
    items.add(10);
    items.add(20);
    items.add(30);
    Boolean removed = items.remove(20);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for RemoveMatchingTypeWorks:\n";
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
