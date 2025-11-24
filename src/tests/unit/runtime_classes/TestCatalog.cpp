//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime_classes/TestCatalog.cpp
// Purpose: Validate runtimeClassCatalog() contains expected System.* entries and members. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
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
    // Properties include Length and IsEmpty
    bool hasLenProp = false;
    bool hasIsEmpty = false;
    for (const auto &p : it->properties)
    {
        hasLenProp = hasLenProp || std::string(p.name) == "Length";
        hasIsEmpty = hasIsEmpty || std::string(p.name) == "IsEmpty";
    }
    EXPECT_TRUE(hasLenProp);
    EXPECT_TRUE(hasIsEmpty);
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
    // Properties include Length and Capacity
    bool hasLenProp = false;
    bool hasCapProp = false;
    for (const auto &p : it->properties)
    {
        hasLenProp = hasLenProp || std::string(p.name) == "Length";
        hasCapProp = hasCapProp || std::string(p.name) == "Capacity";
    }
    EXPECT_TRUE(hasLenProp);
    EXPECT_TRUE(hasCapProp);
    // Methods include Append
    bool hasAppend = false;
    for (const auto &m : it->methods)
        hasAppend = hasAppend || std::string(m.name) == "Append";
    EXPECT_TRUE(hasAppend);
}

TEST(RuntimeClassCatalogBasic, ContainsAdditionalSystemTypes)
{
    const auto &cat = runtimeClassCatalog();
    auto hasQ = [&](const char *qname) {
        return std::find_if(cat.begin(), cat.end(), [&](const auto &c) {
                   return std::string(c.qname) == qname;
               }) != cat.end();
    };
    EXPECT_TRUE(hasQ("Viper.System.Object"));
    EXPECT_TRUE(hasQ("Viper.System.IO.File"));
    EXPECT_TRUE(hasQ("Viper.System.Collections.List"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
