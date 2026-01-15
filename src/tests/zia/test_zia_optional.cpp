//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia optional types.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

/// @brief Test that optional types and coalesce operator work correctly.
TEST(ZiaOptional, OptionalAndCoalesce)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Person {
    expose Integer age;
}

func start() {
    Person? p1 = new Person(30);
    Person? p2 = null;

    Person result1 = p1 ?? new Person(99);
    Person result2 = p2 ?? new Person(88);

    Integer age1 = result1.age;
    Integer age2 = result2.age;

    Viper.Terminal.SayInt(age1);
    Viper.Terminal.SayInt(age2);
}
)";
    CompilerInput input{.source = source, .path = "optional.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for OptionalAndCoalesce:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundCoalesceBlock = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("coalesce") != std::string::npos)
                {
                    foundCoalesceBlock = true;
                    break;
                }
            }
        }
    }
    EXPECT_TRUE(foundCoalesceBlock);
}

/// @brief Test that optional chaining and optional returns lower correctly.
TEST(ZiaOptional, OptionalChainAndReturnWrap)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Person {
    expose Integer age;
}

func maybeAge(Boolean flag) -> Integer? {
    if (flag) {
        return 7;
    }
    return null;
}

func maybePerson(Boolean flag) -> Person? {
    if (flag) {
        return new Person(42);
    }
    return null;
}

func start() {
    Person? p = maybePerson(true);
    Integer? age = p?.age;
    Integer resolved = age ?? 0;
    Integer? wrapped = maybeAge(true);
    Viper.Terminal.SayInt(resolved);
    Viper.Terminal.SayInt(wrapped ?? 0);
}
)";
    CompilerInput input{.source = source, .path = "optional_chain.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for OptionalChainAndReturnWrap:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundBox = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "maybeAge")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == "Viper.Box.I64")
                    {
                        foundBox = true;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundBox);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
