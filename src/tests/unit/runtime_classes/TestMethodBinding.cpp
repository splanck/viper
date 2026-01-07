//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime_classes/TestMethodBinding.cpp
// Purpose: Ensure method binding for Viper.String emits canonical externs with receiver.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//
#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view kSrc = R"BASIC(
10 PRINT ("abcd").Substring(2,2)
20 END
)BASIC";

[[nodiscard]] bool hasExtern(const il::core::Module &module, std::string_view name)
{
    const auto &externs = module.externs;
    return std::any_of(
        externs.begin(), externs.end(), [&](const il::core::Extern &e) { return e.name == name; });
}
} // namespace

TEST(RuntimeMethodBinding, EmitsViperStringSubstringExtern)
{
    il::support::SourceManager sm;
    il::frontends::basic::BasicCompilerOptions opts{};
    std::string source(kSrc);
    il::frontends::basic::BasicCompilerInput input{source, "method_substring.bas"};
    auto result = il::frontends::basic::compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasExtern(result.module, "Viper.String.Substring"));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
