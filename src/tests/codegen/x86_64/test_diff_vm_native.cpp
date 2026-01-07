//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_diff_vm_native.cpp
// Purpose: Ensure the ilc VM runner and native backend produce identical
// Key invariants: Shared codegen fixture handles filesystem orchestration while
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

const std::array<CliScenario, 2> kScenarios = {{{"BranchPrint",
                                                 {R"(il 0.1.2
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void

func @main() -> i32 {
entry:
  %condition = scmp_gt 5, 3
  cbr %condition, greater, smaller
greater:
  call @rt_print_i64(42)
  call @rt_print_f64(3.5)
  br exit
smaller:
  call @rt_print_i64(0)
  call @rt_print_f64(0.0)
  br exit
exit:
  ret 7
}
)",
                                                  "branch_print.il",
                                                  {},
                                                  {}},
                                                 {false, std::nullopt}},
                                                {"BranchPrintSpecialChar",
                                                 {R"(il 0.1.2
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void

func @main() -> i32 {
entry:
  %condition = scmp_gt 5, 3
  cbr %condition, greater, smaller
greater:
  call @rt_print_i64(42)
  call @rt_print_f64(3.5)
  br exit
smaller:
  call @rt_print_i64(0)
  call @rt_print_f64(0.0)
  br exit
exit:
  ret 7
}
)",
                                                  "branch_print$literal.il",
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
