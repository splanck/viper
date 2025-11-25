//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime_classes/TestStringBuilderBinding.cpp
// Purpose: Ensure System.Text.StringBuilder property/method bindings emit
//          canonical externs.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"

#include <algorithm>
#include <string>

namespace
{
[[nodiscard]] bool hasExtern(const il::core::Module &module, std::string_view name)
{
    const auto &externs = module.externs;
    return std::any_of(
        externs.begin(), externs.end(), [&](const il::core::Extern &e) { return e.name == name; });
}
} // namespace

TEST(RuntimeClassBinding, EmitsStringBuilderCapacityAndCtorExterns)
{
    il::support::SourceManager sm;
    il::frontends::basic::BasicCompilerOptions opts{};
    const char *kSrc = R"BASIC(
10 DIM sb AS Viper.System.Text.StringBuilder
20 sb = NEW Viper.System.Text.StringBuilder()
30 PRINT sb.Capacity
40 END
)BASIC";
    std::string source(kSrc);
    il::frontends::basic::BasicCompilerInput input{source, "sb_capacity.bas"};
    auto result = il::frontends::basic::compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasExtern(result.module, "Viper.System.Text.StringBuilder.New"));
    EXPECT_TRUE(hasExtern(result.module, "Viper.Text.StringBuilder.get_Capacity"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
