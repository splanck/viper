//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/viperlang/test_viperlang_list_methods.cpp
// Purpose: Verify Bug #022 fix - List methods like remove(), insert(), find()
//          are properly lowered.
// Key invariants: All List methods should produce valid IL code.
// Ownership/Lifetime: Test file.
// Links: docs/bugs/sqldb_bugs.md
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Compiler.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <string>

using namespace il::frontends::viperlang;
using namespace il::support;

namespace
{

/// @brief Test that List.remove() compiles successfully.
TEST(ViperLangListMethods, RemoveMethod)
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
    CompilerInput input{.source = src, .path = "test.viper"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for RemoveMethod:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Verify the list remove function is called
    bool hasRemoveCall = false;
    for (const auto &fn : result.module.functions)
    {
        for (const auto &bb : fn.blocks)
        {
            for (const auto &in : bb.instructions)
            {
                if (in.op == il::core::Opcode::Call &&
                    in.callee.find("Remove") != std::string::npos)
                {
                    hasRemoveCall = true;
                }
            }
        }
    }
    EXPECT_TRUE(hasRemoveCall);
}

/// @brief Test that List.insert() compiles successfully.
TEST(ViperLangListMethods, InsertMethod)
{
    const std::string src = R"(
module Test;

func start() {
    List[Integer] items = new List[Integer]();
    items.add(10);
    items.add(30);
    items.insert(1, 20);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.viper"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for InsertMethod:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Verify the list insert function is called
    bool hasInsertCall = false;
    for (const auto &fn : result.module.functions)
    {
        for (const auto &bb : fn.blocks)
        {
            for (const auto &in : bb.instructions)
            {
                if (in.op == il::core::Opcode::Call &&
                    in.callee.find("Insert") != std::string::npos)
                {
                    hasInsertCall = true;
                }
            }
        }
    }
    EXPECT_TRUE(hasInsertCall);
}

/// @brief Test that List.find() compiles successfully.
TEST(ViperLangListMethods, FindMethod)
{
    const std::string src = R"(
module Test;

func start() {
    List[Integer] items = new List[Integer]();
    items.add(10);
    items.add(20);
    items.add(30);
    Integer idx = items.find(20);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.viper"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for FindMethod:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Verify the list find function is called
    bool hasFindCall = false;
    for (const auto &fn : result.module.functions)
    {
        for (const auto &bb : fn.blocks)
        {
            for (const auto &in : bb.instructions)
            {
                if (in.op == il::core::Opcode::Call &&
                    in.callee.find("Find") != std::string::npos)
                {
                    hasFindCall = true;
                }
            }
        }
    }
    EXPECT_TRUE(hasFindCall);
}

/// @brief Test that List.indexOf() (alias for find) compiles successfully.
TEST(ViperLangListMethods, IndexOfMethod)
{
    const std::string src = R"(
module Test;

func start() {
    List[Integer] items = new List[Integer]();
    items.add(10);
    items.add(20);
    Integer idx = items.indexOf(10);
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.viper"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for IndexOfMethod:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that accessing entity field through List.get() compiles.
/// This is the core of Bug #022.
TEST(ViperLangListMethods, GetEntityProperty)
{
    const std::string src = R"(
module Test;

entity Item {
    expose String name;

    func init(n: String) {
        name = n;
    }

    func getName() -> String {
        return name;
    }
}

func start() {
    List[Item] items = new List[Item]();
    Item item1 = Item("first");
    Item item2 = Item("second");
    items.add(item1);
    items.add(item2);

    // Access property through get() - this was causing Bug #022
    Item retrieved = items.get(0);
    String itemName = retrieved.name;
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "test.viper"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for GetEntityProperty:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Just verify that compilation succeeds - the main test is that
    // List.get() followed by field access doesn't cause a compilation error
    // or produce invalid IL that would cause a "null indirect callee" trap at runtime
}

} // namespace

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
