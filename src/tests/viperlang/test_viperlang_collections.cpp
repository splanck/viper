//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for ViperLang collections (Map, List).
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::viperlang;
using namespace il::support;

namespace
{

/// @brief Test that Map collections compile correctly.
TEST(ViperLangCollections, MapCollection)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Map[String, Integer] ages = new Map[String, Integer]();
    ages.set("Alice", 30);
    ages.set("Bob", 25);
    Integer aliceAge = ages.get("Alice");
    Integer count = ages.count();
    Viper.Terminal.SayInt(aliceAge);
    Viper.Terminal.SayInt(count);
}
)";
    CompilerInput input{.source = source, .path = "map.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MapCollection:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundMapNew = false;
    bool foundMapSet = false;
    bool foundMapGet = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call)
                    {
                        if (instr.callee == "Viper.Collections.Map.New")
                            foundMapNew = true;
                        if (instr.callee == "Viper.Collections.Map.set_Item")
                            foundMapSet = true;
                        if (instr.callee == "Viper.Collections.Map.get_Item")
                            foundMapGet = true;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundMapNew);
    EXPECT_TRUE(foundMapSet);
    EXPECT_TRUE(foundMapGet);
}

/// @brief Test that Map index access and assignment work correctly.
TEST(ViperLangCollections, MapIndexAccess)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Map[Integer, String] names = new Map[Integer, String]();
    names[1] = "One";
    names[2] = "Two";
    String name = names[1];
    Viper.Terminal.Say(name);
}
)";
    CompilerInput input{.source = source, .path = "mapindex.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MapIndexAccess:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundMapSet = false;
    bool foundMapGet = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call)
                    {
                        if (instr.callee == "Viper.Collections.Map.set_Item")
                            foundMapSet = true;
                        if (instr.callee == "Viper.Collections.Map.get_Item")
                            foundMapGet = true;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundMapSet);
    EXPECT_TRUE(foundMapGet);
}

/// @brief Test that empty list type inference works.
TEST(ViperLangCollections, EmptyListTypeInference)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    List[Integer] numbers = [];
    numbers.add(42);
    Integer first = numbers.get(0);
    Viper.Terminal.SayInt(first);
}
)";
    CompilerInput input{.source = source, .path = "emptylist.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EmptyListTypeInference:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test Bug #17 fix: List[Entity] compiles correctly.
/// Previously caused a runtime assertion failure when adding entities to lists.
TEST(ViperLangCollections, ListOfEntities)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Frog {
    expose Integer x;
}

func start() {
    List[Frog] frogs = [];
    var f = new Frog();
    f.x = 5;
    frogs.add(f);
    Integer count = frogs.count();
    Viper.Terminal.SayInt(count);
}
)";
    CompilerInput input{.source = source, .path = "list_entity.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ListOfEntities:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check that rt_obj_new_i64 is used for entity allocation (not rt_alloc)
    // This ensures entities have proper heap headers for reference counting
    bool foundRtObjNew = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == "rt_obj_new_i64")
                        foundRtObjNew = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundRtObjNew);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
