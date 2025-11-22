//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_e2e_fdiv_cli.cpp
// Purpose: Validate VM/native parity for floating-point division IL programs 
// Key invariants: Floating-point outputs are compared within a configurable
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "common/CodegenFixture.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#if __has_include(<gtest/gtest.h>)
#ifdef VIPER_HAS_GTEST
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

const std::array<CliScenario, 1> kScenarios = {{{"FDiv",
                                                 {R"(il 0.1.2
extern @rt_print_f64(f64) -> void
func @main() -> i64 {
entry:
  %x = fconst.f64 6.0
  %y = fconst.f64 2.0
  %z = fdiv.f64 %x, %y
  call @rt_print_f64(%z)
  ret 0
}
)",
                                                  "fdiv.il",
                                                  {},
                                                  {}},
                                                 {true, 1e-12}}}};

CodegenComparisonResult runScenario(CodegenFixture &fixture, const CliScenario &scenario)
{
    return fixture.compareVmAndNative(scenario.config, scenario.options);
}

} // namespace

#if VIPER_HAS_GTEST

class CodegenFdivCliTest : public ::testing::TestWithParam<CliScenario>
{
  protected:
    CodegenFixture fixture_;
};

TEST_P(CodegenFdivCliTest, VmAndNativeOutputsMatch)
{
    ASSERT_TRUE(fixture_.isReady()) << fixture_.setupError();
    const CodegenComparisonResult result = runScenario(fixture_, GetParam());
    ASSERT_TRUE(result.success) << result.message;
}

INSTANTIATE_TEST_SUITE_P(FdivCli,
                         CodegenFdivCliTest,
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
