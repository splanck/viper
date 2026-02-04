//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file TestConvertBinding.cpp
/// @brief Unit tests for Viper.Convert runtime class bindings.
///
/// @details This test file verifies that the Viper.Convert runtime class
/// is correctly registered in the catalog and that its type conversion
/// methods can be looked up through the RuntimeMethodIndex.
///
/// ## Test Coverage
///
/// ### Catalog Registration Tests
///
/// Verifies that Viper.Convert exists in the runtime class catalog with
/// the expected conversion methods:
/// - ToInt64(str) - Parse string to 64-bit integer
/// - ToDouble(str) - Parse string to 64-bit float
/// - ToString_Int(i64) - Format integer as string
/// - ToString_Double(f64) - Format float as string
///
/// ### Method Index Tests
///
/// Verifies that conversion methods resolve to correct extern targets:
///
/// | Method            | Arity | Expected Target              |
/// |-------------------|-------|------------------------------|
/// | ToInt64(str)      | 1     | Viper.Convert.ToInt          |
/// | ToDouble(str)     | 1     | Viper.Convert.ToDouble       |
/// | ToString_Int(i64) | 1     | Viper.String.FromInt        |
/// | ToString_Double(f64)| 1   | Viper.String.FromDouble     |
///
/// ## Conversion Architecture
///
/// The Viper.Convert class provides bidirectional type conversion:
///
/// **String → Numeric:**
/// - ToInt64: Parses string to i64 (handles decimal, hex, octal)
/// - ToDouble: Parses string to f64 (handles scientific notation)
///
/// **Numeric → String:**
/// - ToString_Int: Formats i64 as decimal string
/// - ToString_Double: Formats f64 with appropriate precision
///
/// Note: The ToString variants delegate to Viper.Strings functions for
/// implementation efficiency.
///
/// @see RuntimeMethodIndex - Method lookup interface
/// @see runtimeClassCatalog - Raw class metadata
/// @see runtime.def - Source definition for Viper.Convert
///
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <string>

/// @brief Test that Viper.Convert exists in the catalog with expected methods.
///
/// @details Searches the runtime class catalog for Viper.Convert and verifies
/// it contains all the expected conversion methods.
///
TEST(RuntimeClassConvertBinding, CatalogContainsConvert)
{
    const auto &cat = il::runtime::runtimeClassCatalog();

    // Find Viper.Convert in the catalog
    auto it = std::find_if(cat.begin(),
                           cat.end(),
                           [](const auto &c) { return std::string(c.qname) == "Viper.Convert"; });
    ASSERT_NE(it, cat.end());

    // Verify expected conversion methods are present
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

/// @brief Test that Convert methods resolve to correct extern targets.
///
/// @details Verifies the RuntimeMethodIndex correctly maps Convert method
/// lookups to their canonical extern names for IL code generation.
/// Note that ToString variants delegate to Viper.Strings functions.
///
TEST(RuntimeClassConvertBinding, MethodIndexTargets)
{
    // Initialize the method index
    il::frontends::basic::runtimeMethodIndex().seed();
    auto &midx = il::frontends::basic::runtimeMethodIndex();

    // Test Convert.ToInt64(str: String) -> Int
    auto ti = midx.find("Viper.Convert", "ToInt64", 1);
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->target, std::string("Viper.Convert.ToInt"));

    // Test Convert.ToDouble(str: String) -> Float
    auto td = midx.find("Viper.Convert", "ToDouble", 1);
    ASSERT_TRUE(td.has_value());
    EXPECT_EQ(td->target, std::string("Viper.Convert.ToDouble"));

    // Test Convert.ToString_Int(i: Int) -> String
    auto tsi = midx.find("Viper.Convert", "ToString_Int", 1);
    ASSERT_TRUE(tsi.has_value());
    EXPECT_EQ(tsi->target, std::string("Viper.Convert.ToString_Int"));

    // Test Convert.ToString_Double(f: Float) -> String
    auto tsd = midx.find("Viper.Convert", "ToString_Double", 1);
    ASSERT_TRUE(tsd.has_value());
    EXPECT_EQ(tsd->target, std::string("Viper.Convert.ToString_Double"));
}

/// @brief Test entry point.
int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
