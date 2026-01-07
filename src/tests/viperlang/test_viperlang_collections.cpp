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
                        if (instr.callee == "Viper.Collections.Map.Set")
                            foundMapSet = true;
                        if (instr.callee == "Viper.Collections.Map.Get")
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
    Map[String, String] names = new Map[String, String]();
    names["one"] = "One";
    names["two"] = "Two";
    String name = names["one"];
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
                        if (instr.callee == "Viper.Collections.Map.Set")
                            foundMapSet = true;
                        if (instr.callee == "Viper.Collections.Map.Get")
                            foundMapGet = true;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundMapSet);
    EXPECT_TRUE(foundMapGet);
}

/// @brief Test that Map helpers like getOr and setIfMissing lower correctly.
TEST(ViperLangCollections, MapHelpers)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Map[String, Integer] ages = new Map[String, Integer]();
    Integer initial = ages.getOr("Alice", 0);
    Boolean inserted = ages.setIfMissing("Alice", 42);
    Boolean hasAlice = ages.has("Alice");
    Viper.Terminal.SayInt(initial);
    Viper.Terminal.SayInt(inserted ? 1 : 0);
    Viper.Terminal.SayInt(hasAlice ? 1 : 0);
}
)";
    CompilerInput input{.source = source, .path = "map_helpers.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MapHelpers:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundGetOr = false;
    bool foundSetIfMissing = false;
    bool foundHas = false;
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
                        if (instr.callee == "Viper.Collections.Map.GetOr")
                            foundGetOr = true;
                        if (instr.callee == "Viper.Collections.Map.SetIfMissing")
                            foundSetIfMissing = true;
                        if (instr.callee == "Viper.Collections.Map.Has")
                            foundHas = true;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundGetOr);
    EXPECT_TRUE(foundSetIfMissing);
    EXPECT_TRUE(foundHas);
}

/// @brief Test that Map key types are enforced as String.
TEST(ViperLangCollections, MapKeyTypeEnforced)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Map[Integer, String] names = new Map[Integer, String]();
    names[1] = "One";
}
)";
    CompilerInput input{.source = source, .path = "map_key_type.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_FALSE(result.succeeded());
    bool foundKeyError = false;
    for (const auto &d : result.diagnostics.diagnostics())
    {
        if (d.message.find("Map keys must be String") != std::string::npos)
            foundKeyError = true;
    }
    EXPECT_TRUE(foundKeyError);
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
