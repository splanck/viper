//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file TestConvertBinding.cpp
/// @brief Unit tests for conversion and parse runtime class bindings.
///
/// @details This test file verifies that the canonical Zanna.Core.Convert and
/// Zanna.Core.Parse runtime classes are registered in the catalog and that the
/// public runtime surface exposes only the canonical conversion and parse names.
///
/// ## Test Coverage
///
/// ### Catalog Registration Tests
///
/// Verifies that Zanna.Core.Convert exists in the runtime class catalog with
/// the expected conversion methods:
/// - ToInt64(str) - Parse string to 64-bit integer
/// - ToDouble(str) - Parse string to 64-bit float
/// - NumToInt(f64) - Truncate/clamp float to 64-bit integer
/// - ToStringInt(i64) - Format integer as string
/// - ToStringDouble(f64) - Format float as string
///
/// ### Method Index Tests
///
/// Verifies that conversion methods resolve to correct extern targets:
///
/// | Method            | Arity | Expected Target              |
/// |-------------------|-------|------------------------------|
/// | ToInt64(str)      | 1     | Zanna.Core.Convert.ToInt64     |
/// | ToDouble(str)     | 1     | Zanna.Core.Convert.ToDouble  |
/// | NumToInt(f64)     | 1     | Zanna.Core.Convert.NumToInt  |
/// | ToStringInt(i64) | 1     | Zanna.Core.Convert.ToStringInt |
/// | ToStringDouble(f64)| 1   | Zanna.Core.Convert.ToStringDouble |
///
/// ## Conversion Architecture
///
/// The Zanna.Core.Convert class provides bidirectional type conversion:
///
/// **String → Numeric:**
/// - ToInt64: Parses decimal string to i64
/// - ToDouble: Parses string to f64 (handles scientific notation)
/// - NumToInt: Converts f64 to i64
///
/// **Numeric → String:**
/// - ToStringInt: Formats i64 as decimal string
/// - ToStringDouble: Formats f64 with appropriate precision
///
/// Note: The ToString variants delegate to Zanna.Strings functions for
/// implementation efficiency.
///
/// @see RuntimeMethodIndex - Method lookup interface
/// @see runtimeClassCatalog - Raw class metadata
/// @see runtime.def - Source definition for Zanna.Core.Convert
///
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/RuntimeNameMap.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <string>

/// @brief Test that Zanna.Core.Convert exists in the catalog with expected methods.
///
/// @details Searches the runtime class catalog for Zanna.Core.Convert and verifies
/// it contains all the expected conversion methods.
///
TEST(RuntimeClassConvertBinding, CatalogContainsConvert) {
    const auto &cat = il::runtime::runtimeClassCatalog();

    auto requireConvertClass = [&](const char *qname) {
        auto it = std::find_if(
            cat.begin(), cat.end(), [&](const auto &c) { return std::string(c.qname) == qname; });
        ASSERT_NE(it, cat.end());
        (void)qname;

        // Verify expected conversion methods are present
        bool hasToInt64 = false;
        bool hasToDouble = false;
        bool hasNumToInt = false;
        bool hasToStringInt = false;
        bool hasToStringDouble = false;
        bool hasCanonicalToStringInt = false;
        bool hasCanonicalToStringDouble = false;
        for (const auto &m : it->methods) {
            hasToInt64 = hasToInt64 || std::string(m.name) == "ToInt64";
            hasToDouble = hasToDouble || std::string(m.name) == "ToDouble";
            hasNumToInt = hasNumToInt || std::string(m.name) == "NumToInt";
            // Underscore compatibility spellings were removed in the pre-alpha
            // sweep; only the canonical names may appear.
            hasToStringInt = hasToStringInt || std::string(m.name) == "ToString_Int";
            hasToStringDouble = hasToStringDouble || std::string(m.name) == "ToString_Double";
            hasCanonicalToStringInt =
                hasCanonicalToStringInt || std::string(m.name) == "ToStringInt";
            hasCanonicalToStringDouble =
                hasCanonicalToStringDouble || std::string(m.name) == "ToStringDouble";
            EXPECT_NE(std::string(m.name), "ToInt");
        }
        EXPECT_TRUE(hasToInt64);
        EXPECT_TRUE(hasToDouble);
        EXPECT_TRUE(hasNumToInt);
        EXPECT_TRUE(!hasToStringInt);
        EXPECT_TRUE(!hasToStringDouble);
        EXPECT_TRUE(hasCanonicalToStringInt);
        EXPECT_TRUE(hasCanonicalToStringDouble);
    };

    requireConvertClass("Zanna.Core.Convert");
}

/// @brief Test that Convert methods resolve to correct extern targets.
///
/// @details Verifies the RuntimeMethodIndex correctly maps Convert method
/// lookups to their canonical extern names for IL code generation.
/// Note that ToString variants delegate to Zanna.Strings functions.
///
TEST(RuntimeClassConvertBinding, MethodIndexTargets) {
    // Initialize the method index
    il::frontends::basic::runtimeMethodIndex().seed();
    auto &midx = il::frontends::basic::runtimeMethodIndex();

    // Test Convert.ToInt64(str: String) -> Int.
    auto oldToInt = midx.find("Zanna.Core.Convert", "ToInt", 1);
    EXPECT_FALSE(oldToInt.has_value());

    auto ti = midx.find("Zanna.Core.Convert", "ToInt64", 1);
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->target, std::string("Zanna.Core.Convert.ToInt64"));

    // Test Convert.ToDouble(str: String) -> Float
    auto td = midx.find("Zanna.Core.Convert", "ToDouble", 1);
    ASSERT_TRUE(td.has_value());
    EXPECT_EQ(td->target, std::string("Zanna.Core.Convert.ToDouble"));

    // Test Convert.NumToInt(f: Float) -> Int
    auto nti = midx.find("Zanna.Core.Convert", "NumToInt", 1);
    ASSERT_TRUE(nti.has_value());
    EXPECT_EQ(nti->target, std::string("Zanna.Core.Convert.NumToInt"));

    // The underscore compatibility spelling is gone; only the canonical
    // ToStringInt remains.
    auto tsi = midx.find("Zanna.Core.Convert", "ToString_Int", 1);
    EXPECT_FALSE(tsi.has_value());
    auto tsiCanon = midx.find("Zanna.Core.Convert", "ToStringInt", 1);
    ASSERT_TRUE(tsiCanon.has_value());
    EXPECT_EQ(tsiCanon->target, std::string("Zanna.Core.Convert.ToStringInt"));

    // Test Convert.ToStringDouble(f: Float) -> String
    auto tsd = midx.find("Zanna.Core.Convert", "ToString_Double", 1);
    EXPECT_FALSE(tsd.has_value());

    auto canonicalTsi = midx.find("Zanna.Core.Convert", "ToStringInt", 1);
    ASSERT_TRUE(canonicalTsi.has_value());
    EXPECT_EQ(canonicalTsi->target, std::string("Zanna.Core.Convert.ToStringInt"));

    auto canonicalTsd = midx.find("Zanna.Core.Convert", "ToStringDouble", 1);
    ASSERT_TRUE(canonicalTsd.has_value());
    EXPECT_EQ(canonicalTsd->target, std::string("Zanna.Core.Convert.ToStringDouble"));
}

TEST(RuntimeClassParseBinding, CatalogContainsCanonicalParseMethods) {
    const auto &cat = il::runtime::runtimeClassCatalog();

    auto requireParseClass = [&](const char *qname) {
        auto it = std::find_if(
            cat.begin(), cat.end(), [&](const auto &c) { return std::string(c.qname) == qname; });
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
        bool hasLegacyInt64 = false;
        bool hasLegacyDouble = false;
        for (const auto &m : it->methods) {
            hasTryInt = hasTryInt || std::string(m.name) == "TryInt";
            hasTryNum = hasTryNum || std::string(m.name) == "TryNum";
            hasTryBool = hasTryBool || std::string(m.name) == "TryBool";
            hasIntOr = hasIntOr || std::string(m.name) == "IntOr";
            hasNumOr = hasNumOr || std::string(m.name) == "DoubleOr";
            hasBoolOr = hasBoolOr || std::string(m.name) == "BoolOr";
            hasIsInt = hasIsInt || std::string(m.name) == "IsInt";
            hasIsNum = hasIsNum || std::string(m.name) == "IsNum";
            hasIntRadix = hasIntRadix || std::string(m.name) == "IntRadix";
            hasLegacyInt64 = hasLegacyInt64 || std::string(m.name) == "Int64";
            hasLegacyDouble = hasLegacyDouble || std::string(m.name) == "Double";
        }
        EXPECT_TRUE(hasTryInt);
        EXPECT_TRUE(!hasTryNum);
        EXPECT_TRUE(hasTryBool);
        EXPECT_TRUE(hasIntOr);
        EXPECT_TRUE(hasNumOr);
        EXPECT_TRUE(hasBoolOr);
        EXPECT_TRUE(hasIsInt);
        EXPECT_TRUE(hasIsNum);
        EXPECT_TRUE(hasIntRadix);
        EXPECT_FALSE(hasLegacyInt64);
        EXPECT_FALSE(hasLegacyDouble);
    };

    requireParseClass("Zanna.Core.Parse");
}

TEST(RuntimeClassParseBinding, MethodIndexTargets) {
    il::frontends::basic::runtimeMethodIndex().seed();
    auto &midx = il::frontends::basic::runtimeMethodIndex();

    auto intOr = midx.find("Zanna.Core.Parse", "IntOr", 2);
    ASSERT_TRUE(intOr.has_value());
    EXPECT_EQ(intOr->target, std::string("Zanna.Core.Parse.IntOr"));

    auto radix = midx.find("Zanna.Core.Parse", "IntRadix", 3);
    ASSERT_TRUE(radix.has_value());
    EXPECT_EQ(radix->target, std::string("Zanna.Core.Parse.IntRadix"));

    // The TryNum spelling was removed; TryDouble is canonical.
    EXPECT_FALSE(midx.find("Zanna.Core.Parse", "TryNum", 1).has_value());
    auto tryDouble = midx.find("Zanna.Core.Parse", "TryDouble", 1);
    ASSERT_TRUE(tryDouble.has_value());
    EXPECT_EQ(tryDouble->target, std::string("Zanna.Core.Parse.TryDouble"));
}

TEST(RuntimeUtilityBindings, DirectCanonicalFunctionsResolve) {
    auto convert = il::runtime::mapCanonicalRuntimeName("Zanna.Core.Convert.ToInt64");
    ASSERT_TRUE(convert.has_value());
    EXPECT_EQ(*convert, std::string_view("rt_to_int"));

    auto parse = il::runtime::mapCanonicalRuntimeName("Zanna.Core.Parse.IntRadix");
    ASSERT_TRUE(parse.has_value());
    EXPECT_EQ(*parse, std::string_view("rt_parse_int_radix"));
}

/// @brief Test entry point.
int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
