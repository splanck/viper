//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/Basic_Sleep_Lowering.cpp
// Purpose: Verify SLEEP lowers to a call to rt_sleep_ms and declares extern. 
// Key invariants: Module contains extern @rt_sleep_ms when SLEEP is present.
// Ownership/Lifetime: Compiles a tiny BASIC snippet and inspects the module and IL text.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "GTestStub.hpp"
#endif

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "viper/il/IO.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view kSrc = R"BASIC(
10 SLEEP 100
)BASIC";

[[nodiscard]] bool hasExtern(const il::core::Module &module, std::string_view name)
{
    const auto &externs = module.externs;
    return std::any_of(externs.begin(),
                       externs.end(),
                       [&](const il::core::Extern &ext) { return ext.name == name; });
}
} // namespace

TEST(BasicSleepLowering, DeclaresSleepExtern)
{
    il::support::SourceManager sourceManager;
    il::frontends::basic::BasicCompilerInput input{kSrc, "sleep.bas"};
    il::frontends::basic::BasicCompilerOptions options{};

    auto result = il::frontends::basic::compileBasic(input, options, sourceManager);
    ASSERT_TRUE(result.succeeded());

    const il::core::Module &module = result.module;
    EXPECT_TRUE(hasExtern(module, "rt_sleep_ms"));

    const std::string ilText = il::io::Serializer::toString(result.module);
    EXPECT_NE(ilText.find("extern @rt_sleep_ms"), std::string::npos);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
