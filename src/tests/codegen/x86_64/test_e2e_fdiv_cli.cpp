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
