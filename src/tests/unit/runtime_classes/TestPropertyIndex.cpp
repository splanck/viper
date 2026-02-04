//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime_classes/TestPropertyIndex.cpp
// Purpose: Verify RuntimePropertyIndex lookup for System.String.Length maps to Viper.String.Len.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//
#include "frontends/basic/sem/RuntimePropertyIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"

using il::frontends::basic::runtimePropertyIndex;

TEST(RuntimePropertyIndexBasic, StringLengthGetter)
{
    // Seed from catalog explicitly to avoid test-order coupling
    const auto &cat = il::runtime::runtimeClassCatalog();
    runtimePropertyIndex().seed(cat);

    auto info = runtimePropertyIndex().find("Viper.String", "Length");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->getter, std::string("Viper.String.get_Length"));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
