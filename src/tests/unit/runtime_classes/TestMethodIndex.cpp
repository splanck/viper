//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime_classes/TestMethodIndex.cpp
// Purpose: Verify RuntimeMethodIndex lookup for System.String.Substring maps to Viper.Strings.Mid. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

using il::frontends::basic::runtimeMethodIndex;
using il::frontends::basic::BasicType;

TEST(RuntimeMethodIndexBasic, SystemStringSubstringTarget)
{
    // Seed from catalog explicitly
    const auto &cat = il::runtime::runtimeClassCatalog();
    runtimeMethodIndex().seed(cat);

    auto info = runtimeMethodIndex().find("Viper.System.String", "Substring", 2);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->target, std::string("Viper.Strings.Mid"));
    // Also assert ret/args types where helpful
    EXPECT_EQ(info->ret, BasicType::String);
    ASSERT_EQ(info->args.size(), 2u);
    EXPECT_EQ(info->args[0], BasicType::Int);
    EXPECT_EQ(info->args[1], BasicType::Int);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

