//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file TestMethodIndex.cpp
/// @brief Unit tests for RuntimeMethodIndex method lookup functionality.
///
/// @details This test file verifies that the RuntimeMethodIndex correctly
/// looks up runtime class methods and returns accurate signature information.
/// It tests the integration between the BASIC frontend's method index and
/// the IL-layer RuntimeRegistry.
///
/// ## Test Coverage
///
/// ### String.Substring Tests
///
/// Verifies that String.Substring(start, length) is correctly resolved with:
/// - Target: "Viper.String.Substring"
/// - Return type: String
/// - Parameter types: [Int, Int]
///
/// ### Object Method Tests
///
/// Verifies standard Object methods from the runtime library:
///
/// | Method         | Arity | Expected Target                 | Return |
/// |----------------|-------|---------------------------------|--------|
/// | Equals(obj)    | 1     | Viper.Object.Equals             | Bool   |
/// | GetHashCode()  | 0     | Viper.Object.GetHashCode        | Int    |
/// | ToString()     | 0     | Viper.Object.ToString           | String |
/// | ReferenceEquals| 2     | NOT FOUND (static function)     | N/A    |
///
/// ## RuntimeMethodIndex Architecture
///
/// The RuntimeMethodIndex now delegates to the unified RuntimeRegistry:
///
/// ```
/// runtimeMethodIndex().find(class, method, arity)
///         │
///         ▼
/// RuntimeRegistry::instance().findMethod(class, method, arity)
///         │
///         ▼
/// ParsedMethod (IL types)
///         │
///         ▼
/// toBasicType() conversion
///         │
///         ▼
/// RuntimeMethodInfo (BASIC types)
/// ```
///
/// This ensures signature information is consistent across all frontends.
///
/// @see RuntimeMethodIndex - BASIC frontend method lookup interface
/// @see RuntimeRegistry - Unified signature registry
/// @see runtime.def - Source definitions for runtime methods
///
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"

using il::frontends::basic::BasicType;
using il::frontends::basic::runtimeMethodIndex;

/// @brief Test String.Substring lookup returns correct target and types.
///
/// @details Verifies that looking up String.Substring with arity 2 returns:
/// - Correct extern target name for IL code generation
/// - Correct return type (String)
/// - Correct parameter types (Int, Int)
///
TEST(RuntimeMethodIndexBasic, StringSubstringTarget)
{
    // Seed the index (delegates to RuntimeRegistry internally)
    runtimeMethodIndex().seed();

    // Look up String.Substring(start: Int, length: Int) -> String
    auto info = runtimeMethodIndex().find("Viper.String", "Substring", 2);
    ASSERT_TRUE(info.has_value());

    // Verify extern target name
    EXPECT_EQ(info->target, std::string("Viper.String.Substring"));

    // Verify return type is String
    EXPECT_EQ(info->ret, BasicType::String);

    // Verify parameter types are [Int, Int]
    ASSERT_EQ(info->args.size(), 2u);
    EXPECT_EQ(info->args[0], BasicType::Int);
    EXPECT_EQ(info->args[1], BasicType::Int);
}

/// @brief Test Object class method lookups.
///
/// @details Verifies that standard Object methods are correctly resolved.
/// Also tests that static functions (like ReferenceEquals) are NOT found
/// through the instance method index.
///
TEST(RuntimeMethodIndexBasic, ObjectMethodsTargets)
{
    // Seed the index (delegates to RuntimeRegistry internally)
    runtimeMethodIndex().seed();

    // Test Object.Equals(other: Object) -> Boolean
    auto eq = runtimeMethodIndex().find("Viper.Object", "Equals", 1);
    ASSERT_TRUE(eq.has_value());
    EXPECT_EQ(eq->target, std::string("Viper.Object.Equals"));

    // Test Object.GetHashCode() -> Int
    auto hc = runtimeMethodIndex().find("Viper.Object", "GetHashCode", 0);
    ASSERT_TRUE(hc.has_value());
    EXPECT_EQ(hc->target, std::string("Viper.Object.GetHashCode"));

    // Test Object.ToString() -> String
    auto ts = runtimeMethodIndex().find("Viper.Object", "ToString", 0);
    ASSERT_TRUE(ts.has_value());
    EXPECT_EQ(ts->target, std::string("Viper.Object.ToString"));

    // ReferenceEquals is a static function, not an instance method.
    // It should NOT be found via the method index (which is for instance methods).
    auto re = runtimeMethodIndex().find("Viper.Object", "ReferenceEquals", 2);
    EXPECT_FALSE(re.has_value());
}

/// @brief Test entry point.
int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
