//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/Basic_Timer_Lowering.cpp
// Purpose: Ensure TIMER() in BASIC lowers to a call to rt_timer_ms.
// Key invariants: TIMER() produces i64 result via rt_timer_ms call.
// Ownership/Lifetime: Test harness constructs and inspects lowered IL.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//
#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include "viper/il/IO.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
[[nodiscard]] bool hasExtern(const il::core::Module &module, std::string_view name)
{
    const auto &externs = module.externs;
    return std::any_of(externs.begin(),
                       externs.end(),
                       [&](const il::core::Extern &ext) { return ext.name == name; });
}
} // namespace

TEST(BasicTimerLowering, DeclaresTimerExtern)
{
    constexpr std::string_view kSrc = R"BASIC(
DIM t AS LONG
t = TIMER()
)BASIC";

    il::support::SourceManager sourceManager;
    il::frontends::basic::BasicCompilerInput input{kSrc, "timer.bas"};
    il::frontends::basic::BasicCompilerOptions options{};

    auto result = il::frontends::basic::compileBasic(input, options, sourceManager);
    if (!result.succeeded())
    {
        if (result.emitter)
        {
            result.emitter->printAll(std::cerr);
        }
    }
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;
    EXPECT_TRUE(hasExtern(module, "rt_timer_ms"));

    const std::string ilText = il::io::Serializer::toString(result.module);
    EXPECT_NE(ilText.find("extern @rt_timer_ms"), std::string::npos);
    EXPECT_NE(ilText.find("call @rt_timer_ms"), std::string::npos);
}

TEST(BasicTimerLowering, TimerInExpression)
{
    constexpr std::string_view kSrc = R"BASIC(
DIM elapsed AS LONG
elapsed = TIMER() - TIMER()
)BASIC";

    il::support::SourceManager sourceManager;
    il::frontends::basic::BasicCompilerInput input{kSrc, "timer_expr.bas"};
    il::frontends::basic::BasicCompilerOptions options{};

    auto result = il::frontends::basic::compileBasic(input, options, sourceManager);
    if (!result.succeeded())
    {
        if (result.emitter)
        {
            result.emitter->printAll(std::cerr);
        }
    }
    ASSERT_TRUE(result.succeeded());

    const std::string ilText = il::io::Serializer::toString(result.module);

    // Count occurrences of "call @rt_timer_ms" (should be 2)
    size_t count = 0;
    size_t pos = 0;
    const std::string pattern = "call @rt_timer_ms";
    while ((pos = ilText.find(pattern, pos)) != std::string::npos)
    {
        ++count;
        pos += pattern.length();
    }

    EXPECT_EQ(count, 2);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
