// File: tests/unit/runtime_classes/TestCatalog.cpp
// Purpose: Validate runtimeClassCatalog() contains expected System.* entries and members.

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "il/runtime/classes/RuntimeClasses.hpp"

#include <algorithm>
#include <string>

using il::runtime::runtimeClassCatalog;

TEST(RuntimeClassCatalogBasic, ContainsSystemStringMembers)
{
    const auto &cat = runtimeClassCatalog();
    auto it = std::find_if(cat.begin(), cat.end(), [](const auto &c) {
        return std::string(c.qname) == "Viper.System.String";
    });
    ASSERT_NE(it, cat.end());
    // Properties include Length
    bool hasLenProp = false;
    for (const auto &p : it->properties)
        hasLenProp = hasLenProp || std::string(p.name) == "Length";
    EXPECT_TRUE(hasLenProp);
    // Methods include Substring
    bool hasSubstr = false;
    for (const auto &m : it->methods)
        hasSubstr = hasSubstr || std::string(m.name) == "Substring";
    EXPECT_TRUE(hasSubstr);
}

TEST(RuntimeClassCatalogBasic, ContainsSystemTextStringBuilderMembers)
{
    const auto &cat = runtimeClassCatalog();
    auto it = std::find_if(cat.begin(), cat.end(), [](const auto &c) {
        return std::string(c.qname) == "Viper.System.Text.StringBuilder";
    });
    ASSERT_NE(it, cat.end());
    // Properties include Length (aliased via Viper.Strings.Builder.Length)
    bool hasLenProp = false;
    for (const auto &p : it->properties)
        hasLenProp = hasLenProp || std::string(p.name) == "Length";
    EXPECT_TRUE(hasLenProp);
    // Methods include Append
    bool hasAppend = false;
    for (const auto &m : it->methods)
        hasAppend = hasAppend || std::string(m.name) == "Append";
    EXPECT_TRUE(hasAppend);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

