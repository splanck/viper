// File: tests/unit/runtime_classes/TestPropertyIndex.cpp
// Purpose: Verify RuntimePropertyIndex lookup for System.String.Length maps to Viper.Strings.Len.

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "frontends/basic/sem/RuntimePropertyIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

using il::frontends::basic::runtimePropertyIndex;

TEST(RuntimePropertyIndexBasic, SystemStringLengthGetter)
{
    // Seed from catalog explicitly to avoid test-order coupling
    const auto &cat = il::runtime::runtimeClassCatalog();
    runtimePropertyIndex().seed(cat);

    auto info = runtimePropertyIndex().find("Viper.System.String", "Length");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->getter, std::string("Viper.Strings.Len"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

