//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// File: tests/unit/runtime_classes/TestConsoleBinding.cpp
// Purpose: Ensure Viper.Terminal I/O methods are registered in the catalog.
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <algorithm>
#include <string>

TEST(RuntimeClassTerminalBinding, CatalogContainsTerminal)
{
    const auto &cat = il::runtime::runtimeClassCatalog();
    auto it = std::find_if(cat.begin(),
                           cat.end(),
                           [](const auto &c) { return std::string(c.qname) == "Viper.Terminal"; });
    ASSERT_NE(it, cat.end());
    // Should have Say and ReadLine methods (consolidated I/O)
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

TEST(RuntimeClassTerminalBinding, MethodIndexTargets)
{
    const auto &cat = il::runtime::runtimeClassCatalog();
    il::frontends::basic::runtimeMethodIndex().seed(cat);
    auto &midx = il::frontends::basic::runtimeMethodIndex();

    // Say(str) -> Viper.Terminal.Say
    auto say = midx.find("Viper.Terminal", "Say", 1);
    ASSERT_TRUE(say.has_value());
    EXPECT_EQ(say->target, std::string("Viper.Terminal.Say"));

    // ReadLine() -> Viper.Terminal.ReadLine
    auto rl = midx.find("Viper.Terminal", "ReadLine", 0);
    ASSERT_TRUE(rl.has_value());
    EXPECT_EQ(rl->target, std::string("Viper.Terminal.ReadLine"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
