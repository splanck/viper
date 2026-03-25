//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
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

static bool hasCallee(const il::core::Module &mod, const std::string &callee)
{
    for (const auto &fn : mod.functions)
    {
        for (const auto &bb : fn.blocks)
        {
            for (const auto &in : bb.instructions)
            {
                if (in.op == il::core::Opcode::Call && in.callee == callee)
                    return true;
            }
        }
    }
    return false;
}

TEST(ZiaStringInstanceMethods, ResolvesAndLowersRuntimeStringCalls)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var trimmed = "  hello  ".Trim();
    var upper = trimmed.ToUpper();
    var lower = upper.ToLower();
    var left = "  hi".TrimStart();
    var right = "hi  ".TrimEnd();
    var middle = "abcdef".Substring(1, 3);

    Viper.Terminal.Say(lower);
    Viper.Terminal.Say(left);
    Viper.Terminal.Say(right);
    Viper.Terminal.Say(middle);
}
)";

    CompilerInput input{.source = source, .path = "test_string_instance_methods.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasCallee(result.module, "Viper.String.Trim"));
    EXPECT_TRUE(hasCallee(result.module, "Viper.String.ToUpper"));
    EXPECT_TRUE(hasCallee(result.module, "Viper.String.ToLower"));
    EXPECT_TRUE(hasCallee(result.module, "Viper.String.TrimStart"));
    EXPECT_TRUE(hasCallee(result.module, "Viper.String.TrimEnd"));
    EXPECT_TRUE(hasCallee(result.module, "Viper.String.Substring"));
}

} // namespace

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
