//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// File: tests/unit/runtime_classes/TestConsoleBinding.cpp
// Purpose: Ensure Viper.Console methods are registered in the catalog.
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

TEST(RuntimeClassConsoleBinding, CatalogContainsConsole)
{
    const auto &cat = il::runtime::runtimeClassCatalog();
    auto it = std::find_if(cat.begin(),
                           cat.end(),
                           [](const auto &c) { return std::string(c.qname) == "Viper.Console"; });
    ASSERT_NE(it, cat.end());
    // Should have WriteLine and ReadLine methods
    bool hasWriteLine = false;
    bool hasReadLine = false;
    for (const auto &m : it->methods)
    {
        hasWriteLine = hasWriteLine || std::string(m.name) == "WriteLine";
        hasReadLine = hasReadLine || std::string(m.name) == "ReadLine";
    }
    EXPECT_TRUE(hasWriteLine);
    EXPECT_TRUE(hasReadLine);
}

TEST(RuntimeClassConsoleBinding, MethodIndexTargets)
{
    const auto &cat = il::runtime::runtimeClassCatalog();
    il::frontends::basic::runtimeMethodIndex().seed(cat);
    auto &midx = il::frontends::basic::runtimeMethodIndex();

    // WriteLine(str) -> Viper.Console.PrintStr
    auto wl = midx.find("Viper.Console", "WriteLine", 1);
    ASSERT_TRUE(wl.has_value());
    EXPECT_EQ(wl->target, std::string("Viper.Console.PrintStr"));

    // ReadLine() -> Viper.Console.ReadLine
    auto rl = midx.find("Viper.Console", "ReadLine", 0);
    ASSERT_TRUE(rl.has_value());
    EXPECT_EQ(rl->target, std::string("Viper.Console.ReadLine"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
