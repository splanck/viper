//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for ViperLang type system (value types, entity types).
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

/// @brief Test that value types parse correctly.
TEST(ViperLangTypes, ValueTypeDeclaration)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Point {
    Integer x;
    Integer y;
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "value.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ValueTypeDeclaration:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that entity types with new keyword work correctly.
TEST(ViperLangTypes, EntityTypeWithNew)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Person {
    expose Integer age;
    expose Integer score;

    expose func getAge() -> Integer {
        return age;
    }
}

func start() {
    Person p = new Person(30, 100);
    Integer age = p.age;
    Integer method_age = p.getAge();
    Viper.Terminal.SayInt(age);
    Viper.Terminal.SayInt(method_age);
}
)";
    CompilerInput input{.source = source, .path = "entity.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EntityTypeWithNew:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

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

/// @brief Test Bug #16 fix: Entity type as function parameter compiles correctly.
/// Previously caused an infinite loop in the parser.
TEST(ViperLangTypes, EntityAsParameter)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Frog {
    expose Integer x;
}

func useFrog(Frog f) {
    Viper.Terminal.SayInt(f.x);
}

func start() {
    var f = new Frog();
    f.x = 42;
    useFrog(f);
}
)";
    CompilerInput input{.source = source, .path = "entity_param.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EntityAsParameter:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check that the useFrog function exists and takes a parameter
    bool foundUseFrogFunc = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "Test.useFrog" || fn.name == "useFrog")
        {
            foundUseFrogFunc = true;
            EXPECT_EQ(fn.params.size(), 1u);
            break;
        }
    }
    EXPECT_TRUE(foundUseFrogFunc);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
