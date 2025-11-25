//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// File: tests/unit/runtime_classes/TestRuntimeClassListBinding.cpp
// Purpose: Ensure instance calls to Viper.System.Collections.List bind to externs.
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

TEST(RuntimeClassListBinding, EmitsListExterns)
{
    il::support::SourceManager sm;
    il::frontends::basic::BasicCompilerOptions opts{};
    const char *kSrc = R"BASIC(
10 DIM l AS Viper.System.Collections.List
20 l = NEW Viper.System.Collections.List()
30 l.Add(l)
40 PRINT l.Count
50 l.RemoveAt(0)
60 l.Clear()
70 PRINT l.get_Item(0)
80 l.set_Item(0, l)
90 END
)BASIC";
    std::string source(kSrc);
    il::frontends::basic::BasicCompilerInput input{source, "list_binding.bas"};
    auto result = il::frontends::basic::compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasExtern(result.module, "Viper.System.Collections.List.New"));
    EXPECT_TRUE(hasExtern(result.module, "Viper.System.Collections.List.Add"));
    EXPECT_TRUE(hasExtern(result.module, "Viper.System.Collections.List.get_Count"));
    EXPECT_TRUE(hasExtern(result.module, "Viper.System.Collections.List.RemoveAt"));
    EXPECT_TRUE(hasExtern(result.module, "Viper.System.Collections.List.Clear"));
    EXPECT_TRUE(hasExtern(result.module, "Viper.System.Collections.List.get_Item"));
    EXPECT_TRUE(hasExtern(result.module, "Viper.System.Collections.List.set_Item"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
