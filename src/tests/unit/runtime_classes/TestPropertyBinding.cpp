// File: tests/unit/runtime_classes/TestPropertyBinding.cpp
// Purpose: Ensure member property binding for Viper.String emits canonical externs.

#if __has_include(<gtest/gtest.h>)
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
#include <string_view>

namespace
{
constexpr std::string_view kSrc = R"BASIC(
10 DIM s AS Viper.String
20 PRINT s.Length
30 END
)BASIC";

[[nodiscard]] bool hasExtern(const il::core::Module &module, std::string_view name)
{
    const auto &externs = module.externs;
    return std::any_of(externs.begin(), externs.end(), [&](const il::core::Extern &e) {
        return e.name == name;
    });
}
} // namespace

TEST(RuntimePropertyBinding, EmitsViperStringGetterExtern)
{
    il::support::SourceManager sm;
    il::frontends::basic::BasicCompilerOptions opts{};
    std::string source(kSrc);
    il::frontends::basic::BasicCompilerInput input{source, "prop_len.bas"};
    auto result = il::frontends::basic::compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasExtern(result.module, "Viper.String.get_Length"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
