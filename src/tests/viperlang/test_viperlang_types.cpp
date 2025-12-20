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

    bool foundRtAlloc = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == "rt_alloc")
                        foundRtAlloc = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundRtAlloc);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
