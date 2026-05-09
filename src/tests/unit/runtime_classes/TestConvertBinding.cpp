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
/// - ToInt(str) - Parse string to 64-bit integer
/// - ToInt64(str) - Parse string to 64-bit integer
/// - ToDouble(str) - Parse string to 64-bit float
/// - NumToInt(f64) - Truncate/clamp float to 64-bit integer
/// - ToString_Int(i64) - Format integer as string
/// - ToString_Double(f64) - Format float as string
///
/// ### Method Index Tests
///
/// Verifies that conversion methods resolve to correct extern targets:
///
/// | Method            | Arity | Expected Target              |
/// |-------------------|-------|------------------------------|
/// | ToInt(str)        | 1     | Viper.Core.Convert.ToInt     |
/// | ToInt64(str)      | 1     | Viper.Core.Convert.ToInt     |
/// | ToDouble(str)     | 1     | Viper.Core.Convert.ToDouble  |
/// | NumToInt(f64)     | 1     | Viper.Core.Convert.NumToInt  |
/// | ToString_Int(i64) | 1     | Viper.Core.Convert.ToString_Int |
/// | ToString_Double(f64)| 1   | Viper.Core.Convert.ToString_Double |
///
/// ## Conversion Architecture
///
/// The Viper.Convert class provides bidirectional type conversion:
///
/// **String → Numeric:**
/// - ToInt: Parses string to i64
/// - ToInt64: Parses decimal string to i64
/// - ToDouble: Parses string to f64 (handles scientific notation)
/// - NumToInt: Converts f64 to i64
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
#include "il/runtime/RuntimeNameMap.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <string>

/// @brief Test that Viper.Convert exists in the catalog with expected methods.
///
/// @details Searches the runtime class catalog for Viper.Convert and verifies
/// it contains all the expected conversion methods.
///
TEST(RuntimeClassConvertBinding, CatalogContainsConvert) {
    const auto &cat = il::runtime::runtimeClassCatalog();

    auto requireConvertClass = [&](const char *qname) {
        auto it = std::find_if(cat.begin(), cat.end(), [&](const auto &c) {
            return std::string(c.qname) == qname;
        });
        ASSERT_NE(it, cat.end());
        (void)qname;

        // Verify expected conversion methods are present
        bool hasToInt = false;
        bool hasToInt64 = false;
        bool hasToDouble = false;
        bool hasNumToInt = false;
        bool hasToStringInt = false;
        bool hasToStringDouble = false;
        for (const auto &m : it->methods) {
            hasToInt = hasToInt || std::string(m.name) == "ToInt";
            hasToInt64 = hasToInt64 || std::string(m.name) == "ToInt64";
            hasToDouble = hasToDouble || std::string(m.name) == "ToDouble";
            hasNumToInt = hasNumToInt || std::string(m.name) == "NumToInt";
            hasToStringInt = hasToStringInt || std::string(m.name) == "ToString_Int";
            hasToStringDouble = hasToStringDouble || std::string(m.name) == "ToString_Double";
        }
        EXPECT_TRUE(hasToInt);
        EXPECT_TRUE(hasToInt64);
        EXPECT_TRUE(hasToDouble);
        EXPECT_TRUE(hasNumToInt);
        EXPECT_TRUE(hasToStringInt);
        EXPECT_TRUE(hasToStringDouble);
    };

    requireConvertClass("Viper.Core.Convert");
    requireConvertClass("Viper.Convert");
}

/// @brief Test that Convert methods resolve to correct extern targets.
///
/// @details Verifies the RuntimeMethodIndex correctly maps Convert method
/// lookups to their canonical extern names for IL code generation.
/// Note that ToString variants delegate to Viper.Strings functions.
///
TEST(RuntimeClassConvertBinding, MethodIndexTargets) {
    // Initialize the method index
    il::frontends::basic::runtimeMethodIndex().seed();
    auto &midx = il::frontends::basic::runtimeMethodIndex();

    // Test Convert.ToInt64(str: String) -> Int
    auto ti_alias = midx.find("Viper.Core.Convert", "ToInt", 1);
    ASSERT_TRUE(ti_alias.has_value());
    EXPECT_EQ(ti_alias->target, std::string("Viper.Core.Convert.ToInt"));

    auto ti = midx.find("Viper.Core.Convert", "ToInt64", 1);
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->target, std::string("Viper.Core.Convert.ToInt"));

    // Test Convert.ToDouble(str: String) -> Float
    auto td = midx.find("Viper.Core.Convert", "ToDouble", 1);
    ASSERT_TRUE(td.has_value());
    EXPECT_EQ(td->target, std::string("Viper.Core.Convert.ToDouble"));

    // Test Convert.NumToInt(f: Float) -> Int
    auto nti = midx.find("Viper.Core.Convert", "NumToInt", 1);
    ASSERT_TRUE(nti.has_value());
    EXPECT_EQ(nti->target, std::string("Viper.Core.Convert.NumToInt"));

    // Test Convert.ToString_Int(i: Int) -> String
    auto tsi = midx.find("Viper.Core.Convert", "ToString_Int", 1);
    ASSERT_TRUE(tsi.has_value());
    EXPECT_EQ(tsi->target, std::string("Viper.Core.Convert.ToString_Int"));

    // Test Convert.ToString_Double(f: Float) -> String
    auto tsd = midx.find("Viper.Core.Convert", "ToString_Double", 1);
    ASSERT_TRUE(tsd.has_value());
    EXPECT_EQ(tsd->target, std::string("Viper.Core.Convert.ToString_Double"));

    auto public_ti = midx.find("Viper.Convert", "ToInt", 1);
    ASSERT_TRUE(public_ti.has_value());
    EXPECT_EQ(public_ti->target, std::string("Viper.Core.Convert.ToInt"));

    auto public_nti = midx.find("Viper.Convert", "NumToInt", 1);
    ASSERT_TRUE(public_nti.has_value());
    EXPECT_EQ(public_nti->target, std::string("Viper.Core.Convert.NumToInt"));
}

TEST(RuntimeClassParseBinding, CatalogContainsParseAliases) {
    const auto &cat = il::runtime::runtimeClassCatalog();

    auto requireParseClass = [&](const char *qname) {
        auto it = std::find_if(cat.begin(), cat.end(), [&](const auto &c) {
            return std::string(c.qname) == qname;
        });
        ASSERT_NE(it, cat.end());
        (void)qname;

        bool hasTryInt = false;
        bool hasTryNum = false;
        bool hasTryBool = false;
        bool hasIntOr = false;
        bool hasNumOr = false;
        bool hasBoolOr = false;
        bool hasIsInt = false;
        bool hasIsNum = false;
        bool hasIntRadix = false;
        bool hasInt64 = false;
        bool hasDouble = false;
        for (const auto &m : it->methods) {
            hasTryInt = hasTryInt || std::string(m.name) == "TryInt";
            hasTryNum = hasTryNum || std::string(m.name) == "TryNum";
            hasTryBool = hasTryBool || std::string(m.name) == "TryBool";
            hasIntOr = hasIntOr || std::string(m.name) == "IntOr";
            hasNumOr = hasNumOr || std::string(m.name) == "NumOr";
            hasBoolOr = hasBoolOr || std::string(m.name) == "BoolOr";
            hasIsInt = hasIsInt || std::string(m.name) == "IsInt";
            hasIsNum = hasIsNum || std::string(m.name) == "IsNum";
            hasIntRadix = hasIntRadix || std::string(m.name) == "IntRadix";
            if (std::string(m.name) == "Int64") {
                hasInt64 = true;
                EXPECT_EQ(std::string(m.signature), std::string("i32(str,ptr)"));
            }
            if (std::string(m.name) == "Double") {
                hasDouble = true;
                EXPECT_EQ(std::string(m.signature), std::string("i32(str,ptr)"));
            }
        }
        EXPECT_TRUE(hasTryInt);
        EXPECT_TRUE(hasTryNum);
        EXPECT_TRUE(hasTryBool);
        EXPECT_TRUE(hasIntOr);
        EXPECT_TRUE(hasNumOr);
        EXPECT_TRUE(hasBoolOr);
        EXPECT_TRUE(hasIsInt);
        EXPECT_TRUE(hasIsNum);
        EXPECT_TRUE(hasIntRadix);
        EXPECT_TRUE(hasInt64);
        EXPECT_TRUE(hasDouble);
    };

    requireParseClass("Viper.Core.Parse");
    requireParseClass("Viper.Parse");
}

TEST(RuntimeClassParseBinding, MethodIndexTargets) {
    il::frontends::basic::runtimeMethodIndex().seed();
    auto &midx = il::frontends::basic::runtimeMethodIndex();

    auto intOr = midx.find("Viper.Parse", "IntOr", 2);
    ASSERT_TRUE(intOr.has_value());
    EXPECT_EQ(intOr->target, std::string("Viper.Core.Parse.IntOr"));

    auto radix = midx.find("Viper.Parse", "IntRadix", 3);
    ASSERT_TRUE(radix.has_value());
    EXPECT_EQ(radix->target, std::string("Viper.Core.Parse.IntRadix"));

    auto tryNum = midx.find("Viper.Core.Parse", "TryNum", 2);
    ASSERT_TRUE(tryNum.has_value());
    EXPECT_EQ(tryNum->target, std::string("Viper.Core.Parse.TryNum"));
}

TEST(RuntimeUtilityAliases, DirectFunctionAliasesResolve) {
    auto convert = il::runtime::mapCanonicalRuntimeName("Viper.Convert.ToInt");
    ASSERT_TRUE(convert.has_value());
    EXPECT_EQ(*convert, std::string_view("rt_to_int"));

    auto parse = il::runtime::mapCanonicalRuntimeName("Viper.Parse.IntRadix");
    ASSERT_TRUE(parse.has_value());
    EXPECT_EQ(*parse, std::string_view("rt_parse_int_radix"));
}

/// @brief Test entry point.
int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
