// File: tests/codegen/x86_64/test_e2e_bitwise_cli.cpp
// Purpose: Exercise the ilc CLI end-to-end for bitwise IL snippets and ensure
//          VM/native parity using the shared codegen fixture.
// Key invariants: The parameterized scenarios describe IL programs that must
//                 yield identical exit codes and stdout for both execution
//                 paths.

#include "tests/common/CodegenFixture.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#define VIPER_HAS_GTEST 1
#else
#define VIPER_HAS_GTEST 0
#endif

namespace
{
using viper::tests::CodegenComparisonOptions;
using viper::tests::CodegenComparisonResult;
using viper::tests::CodegenFixture;
using viper::tests::CodegenRunConfig;

struct CliScenario
{
    const char *name;
    CodegenRunConfig config;
    CodegenComparisonOptions options;
};

const std::array<CliScenario, 1> kScenarios = {{{"Bitwise",
                                                 {R"(il 0.1.2
func @main() -> i64 {
entry:
  %a = iconst.i64 0xFF00FF00
  %b = iconst.i64 0x00000100
  %c = and.i64 %a, %b
  %d = or.i64 %c, 0x2
  %e = xor.i64 %d, 0x5
  ret %e
}
)",
                                                  "bitwise.il",
                                                  {},
                                                  {}},
                                                 {false, std::nullopt}}}};

CodegenComparisonResult runScenario(CodegenFixture &fixture, const CliScenario &scenario)
{
    return fixture.compareVmAndNative(scenario.config, scenario.options);
}

} // namespace

#if VIPER_HAS_GTEST

class CodegenBitwiseCliTest : public ::testing::TestWithParam<CliScenario>
{
  protected:
    CodegenFixture fixture_;
};

TEST_P(CodegenBitwiseCliTest, VmAndNativeOutputsMatch)
{
    ASSERT_TRUE(fixture_.isReady()) << fixture_.setupError();
    const CodegenComparisonResult result = runScenario(fixture_, GetParam());
    ASSERT_TRUE(result.success) << result.message;
}

INSTANTIATE_TEST_SUITE_P(BitwiseCli,
                         CodegenBitwiseCliTest,
                         ::testing::ValuesIn(kScenarios),
                         [](const ::testing::TestParamInfo<CliScenario> &info)
                         { return info.param.name; });

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#else

int main()
{
    CodegenFixture fixture;
    if (!fixture.isReady())
    {
        std::cerr << fixture.setupError();
        return EXIT_FAILURE;
    }

    for (const CliScenario &scenario : kScenarios)
    {
        const CodegenComparisonResult result = runScenario(fixture, scenario);
        if (!result.success)
        {
            std::cerr << result.message;
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

#endif
