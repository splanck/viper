//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for ViperLang lambda expressions.
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

/// @brief Test that lambda with block body compiles.
TEST(ViperLangLambda, LambdaWithBlockBody)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var greet = () => {
        Viper.Terminal.Say("Hello");
    };
}
)";
    CompilerInput input{.source = source, .path = "lambda_block.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for LambdaWithBlockBody:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundLambdaFunc = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name.find("lambda") != std::string::npos)
        {
            foundLambdaFunc = true;
            break;
        }
    }
    EXPECT_TRUE(foundLambdaFunc);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
