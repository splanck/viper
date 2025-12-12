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

using il::frontends::basic::BasicType;
using il::frontends::basic::runtimeMethodIndex;

TEST(RuntimeMethodIndexBasic, StringSubstringTarget)
{
    // Seed from catalog explicitly
    const auto &cat = il::runtime::runtimeClassCatalog();
    runtimeMethodIndex().seed(cat);

    auto info = runtimeMethodIndex().find("Viper.String", "Substring", 2);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->target, std::string("Viper.String.Substring"));
    // Also assert ret/args types where helpful
    EXPECT_EQ(info->ret, BasicType::String);
    ASSERT_EQ(info->args.size(), 2u);
    EXPECT_EQ(info->args[0], BasicType::Int);
    EXPECT_EQ(info->args[1], BasicType::Int);
}

TEST(RuntimeMethodIndexBasic, ObjectMethodsTargets)
{
    // Seed from catalog explicitly
    const auto &cat = il::runtime::runtimeClassCatalog();
    runtimeMethodIndex().seed(cat);

    // Equals has arity 1
    auto eq = runtimeMethodIndex().find("Viper.Object", "Equals", 1);
    ASSERT_TRUE(eq.has_value());
    EXPECT_EQ(eq->target, std::string("Viper.Object.Equals"));

    // GetHashCode has arity 0
    auto hc = runtimeMethodIndex().find("Viper.Object", "GetHashCode", 0);
    ASSERT_TRUE(hc.has_value());
    EXPECT_EQ(hc->target, std::string("Viper.Object.GetHashCode"));

    // ToString has arity 0
    auto ts = runtimeMethodIndex().find("Viper.Object", "ToString", 0);
    ASSERT_TRUE(ts.has_value());
    EXPECT_EQ(ts->target, std::string("Viper.Object.ToString"));

    // ReferenceEquals is a static function, not an instance method.
    // It should NOT be found via the method index.
    auto re = runtimeMethodIndex().find("Viper.Object", "ReferenceEquals", 2);
    EXPECT_FALSE(re.has_value());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
