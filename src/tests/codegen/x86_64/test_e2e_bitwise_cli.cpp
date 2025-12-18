//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_e2e_bitwise_cli.cpp
// Purpose: Exercise the ilc CLI end-to-end for bitwise IL snippets and ensure
// Key invariants: The parameterized scenarios describe IL programs that must
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
