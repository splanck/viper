//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia visibility enforcement.
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

/// @brief Test that visibility enforcement works (private members are rejected).
TEST(ZiaVisibility, VisibilityEnforcement)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Person {
    Integer secretAge;
    expose Integer publicAge;
}

func start() {
    Person p = new Person(30, 25);
    Integer age = p.secretAge;
}
)";
    CompilerInput input{.source = source, .path = "visibility.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // This should FAIL because secretAge is private
    EXPECT_FALSE(result.succeeded());

    bool foundVisibilityError = false;
    for (const auto &d : result.diagnostics.diagnostics())
    {
        if (d.message.find("private") != std::string::npos)
        {
            foundVisibilityError = true;
            EXPECT_EQ(d.code, "V3000");
        }
    }
    EXPECT_TRUE(foundVisibilityError);
}

/// @brief Test that visibility works correctly with exposed members.
TEST(ZiaVisibility, VisibilityExposed)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Person {
    expose Integer age;
}

func start() {
    Person p = new Person(30);
    Integer age = p.age;
    Viper.Terminal.SayInt(age);
}
)";
    CompilerInput input{.source = source, .path = "visibility_exposed.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for VisibilityExposed:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
