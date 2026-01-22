//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// File: tests/unit/runtime_classes/TestConvertBinding.cpp
// Purpose: Ensure Viper.Convert methods are registered in the catalog.
//===----------------------------------------------------------------------===//
#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <string>

TEST(RuntimeClassConvertBinding, CatalogContainsConvert)
{
    const auto &cat = il::runtime::runtimeClassCatalog();
    auto it = std::find_if(cat.begin(),
                           cat.end(),
                           [](const auto &c) { return std::string(c.qname) == "Viper.Convert"; });
    ASSERT_NE(it, cat.end());
    // Should have conversion methods
    bool hasToInt64 = false;
    bool hasToDouble = false;
    bool hasToStringInt = false;
    bool hasToStringDouble = false;
    for (const auto &m : it->methods)
    {
        hasToInt64 = hasToInt64 || std::string(m.name) == "ToInt64";
        hasToDouble = hasToDouble || std::string(m.name) == "ToDouble";
        hasToStringInt = hasToStringInt || std::string(m.name) == "ToString_Int";
        hasToStringDouble = hasToStringDouble || std::string(m.name) == "ToString_Double";
    }
    EXPECT_TRUE(hasToInt64);
    EXPECT_TRUE(hasToDouble);
    EXPECT_TRUE(hasToStringInt);
    EXPECT_TRUE(hasToStringDouble);
}

TEST(RuntimeClassConvertBinding, MethodIndexTargets)
{
    il::frontends::basic::runtimeMethodIndex().seed();
    auto &midx = il::frontends::basic::runtimeMethodIndex();

    // ToInt64(str) -> Viper.Convert.ToInt
    auto ti = midx.find("Viper.Convert", "ToInt64", 1);
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->target, std::string("Viper.Convert.ToInt"));

    // ToDouble(str) -> Viper.Convert.ToDouble
    auto td = midx.find("Viper.Convert", "ToDouble", 1);
    ASSERT_TRUE(td.has_value());
    EXPECT_EQ(td->target, std::string("Viper.Convert.ToDouble"));

    // ToString_Int(i64) -> Viper.Strings.FromInt
    auto tsi = midx.find("Viper.Convert", "ToString_Int", 1);
    ASSERT_TRUE(tsi.has_value());
    EXPECT_EQ(tsi->target, std::string("Viper.Strings.FromInt"));

    // ToString_Double(f64) -> Viper.Strings.FromDouble
    auto tsd = midx.find("Viper.Convert", "ToString_Double", 1);
    ASSERT_TRUE(tsd.has_value());
    EXPECT_EQ(tsd->target, std::string("Viper.Strings.FromDouble"));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
