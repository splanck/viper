//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file TestRuntimeClassFileBinding.cpp
/// @brief Unit tests for Viper.IO.File runtime class method bindings.
///
/// @details This test file verifies that the RuntimeMethodIndex correctly
/// binds static method calls on the Viper.IO.File runtime class to their
/// canonical extern target names. These bindings are critical for the BASIC
/// frontend to generate correct extern calls to the runtime library.
///
/// ## Test Coverage
///
/// The tests verify that looking up File methods by name and arity returns
/// the correct extern target:
///
/// | Method Call           | Arity | Expected Target              |
/// |-----------------------|-------|------------------------------|
/// | File.Exists(path)     | 1     | Viper.IO.File.Exists         |
/// | File.ReadAllText(p)   | 1     | Viper.IO.File.ReadAllText    |
/// | File.WriteAllText(p,c)| 2     | Viper.IO.File.WriteAllText   |
/// | File.Delete(path)     | 1     | Viper.IO.File.Delete         |
///
/// ## RuntimeMethodIndex Integration
///
/// The test exercises the RuntimeMethodIndex's find() method which:
/// 1. Delegates to RuntimeRegistry::instance().findMethod()
/// 2. Converts the ParsedMethod result to RuntimeMethodInfo
/// 3. Returns the extern target name for IL code generation
///
/// This validates that the unified RuntimeRegistry correctly exposes
/// File class methods through the BASIC frontend's method index.
///
/// @see RuntimeMethodIndex - BASIC frontend method lookup
/// @see RuntimeRegistry - Unified signature registry
/// @see runtime.def - Source definition for Viper.IO.File
///
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"

/// @brief Test that File class methods bind to correct extern targets.
///
/// @details Verifies the RuntimeMethodIndex correctly resolves File static
/// methods to their canonical extern names. Each method is looked up by
/// class name, method name, and arity, then the target is verified.
///
TEST(RuntimeClassFileBinding, MethodIndexTargets)
{
    // Initialize the method index (now delegates to RuntimeRegistry)
    il::frontends::basic::runtimeMethodIndex().seed();
    auto &midx = il::frontends::basic::runtimeMethodIndex();

    // Test File.Exists(path: String) -> Boolean
    auto a = midx.find("Viper.IO.File", "Exists", 1);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->target, std::string("Viper.IO.File.Exists"));

    // Test File.ReadAllText(path: String) -> String
    auto b = midx.find("Viper.IO.File", "ReadAllText", 1);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->target, std::string("Viper.IO.File.ReadAllText"));

    // Test File.WriteAllText(path: String, contents: String) -> void
    auto c = midx.find("Viper.IO.File", "WriteAllText", 2);
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->target, std::string("Viper.IO.File.WriteAllText"));

    // Test File.Delete(path: String) -> void
    auto d = midx.find("Viper.IO.File", "Delete", 1);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->target, std::string("Viper.IO.File.Delete"));
}

/// @brief Test entry point.
int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
