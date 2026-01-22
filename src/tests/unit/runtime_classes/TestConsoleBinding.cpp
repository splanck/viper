//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file TestConsoleBinding.cpp
/// @brief Unit tests for Viper.Terminal runtime class bindings.
///
/// @details This test file verifies that the Viper.Terminal runtime class
/// is correctly registered in the catalog and that its methods can be looked
/// up through the RuntimeMethodIndex.
///
/// ## Test Coverage
///
/// ### Catalog Registration Tests
///
/// Verifies that Viper.Terminal exists in the runtime class catalog with
/// the expected methods:
/// - Say(message) - Output text to the terminal
/// - ReadLine() - Read a line of text from the terminal
///
/// ### Method Index Tests
///
/// Verifies that terminal methods resolve to correct extern targets:
///
/// | Method        | Arity | Expected Target           |
/// |---------------|-------|---------------------------|
/// | Say(str)      | 1     | Viper.Terminal.Say        |
/// | ReadLine()    | 0     | Viper.Terminal.ReadLine   |
///
/// ## Terminal I/O Architecture
///
/// The Viper.Terminal class provides consolidated I/O operations:
/// - Say() is the primary output function (replaces PRINT)
/// - ReadLine() is the primary input function (replaces INPUT)
///
/// These methods map directly to runtime library functions that handle
/// platform-specific terminal I/O.
///
/// @see RuntimeMethodIndex - Method lookup interface
/// @see runtimeClassCatalog - Raw class metadata
/// @see runtime.def - Source definition for Viper.Terminal
///
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <string>

/// @brief Test that Viper.Terminal exists in the catalog with expected methods.
///
/// @details Searches the runtime class catalog for Viper.Terminal and verifies
/// it contains the Say and ReadLine methods.
///
TEST(RuntimeClassTerminalBinding, CatalogContainsTerminal)
{
    const auto &cat = il::runtime::runtimeClassCatalog();

    // Find Viper.Terminal in the catalog
    auto it = std::find_if(cat.begin(),
                           cat.end(),
                           [](const auto &c) { return std::string(c.qname) == "Viper.Terminal"; });
    ASSERT_NE(it, cat.end());

    // Verify expected methods are present
    bool hasSay = false;
    bool hasReadLine = false;
    for (const auto &m : it->methods)
    {
        hasSay = hasSay || std::string(m.name) == "Say";
        hasReadLine = hasReadLine || std::string(m.name) == "ReadLine";
    }
    EXPECT_TRUE(hasSay);
    EXPECT_TRUE(hasReadLine);
}

/// @brief Test that Terminal methods resolve to correct extern targets.
///
/// @details Verifies the RuntimeMethodIndex correctly maps Terminal method
/// lookups to their canonical extern names for IL code generation.
///
TEST(RuntimeClassTerminalBinding, MethodIndexTargets)
{
    // Initialize the method index
    il::frontends::basic::runtimeMethodIndex().seed();
    auto &midx = il::frontends::basic::runtimeMethodIndex();

    // Test Terminal.Say(message: String) -> void
    auto say = midx.find("Viper.Terminal", "Say", 1);
    ASSERT_TRUE(say.has_value());
    EXPECT_EQ(say->target, std::string("Viper.Terminal.Say"));

    // Test Terminal.ReadLine() -> String
    auto rl = midx.find("Viper.Terminal", "ReadLine", 0);
    ASSERT_TRUE(rl.has_value());
    EXPECT_EQ(rl->target, std::string("Viper.Terminal.ReadLine"));
}

/// @brief Test entry point.
int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
