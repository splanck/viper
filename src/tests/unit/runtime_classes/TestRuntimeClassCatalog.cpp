//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime_classes/TestRuntimeClassCatalog.cpp
// Purpose: Verify runtime class catalog ingestion and TypeRegistry seeding. 
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
#include "frontends/basic/sem/TypeRegistry.hpp"

using il::runtime::runtimeClassCatalog;
using il::frontends::basic::runtimeTypeRegistry;
using il::frontends::basic::TypeKind;

TEST(RuntimeClassCatalog, ContainsViperString)
{
    const auto &cat = runtimeClassCatalog();
    ASSERT_FALSE(cat.empty());
    const auto &cls = cat.front();
    EXPECT_TRUE(std::string(cls.qname) == "Viper.String");
    EXPECT_TRUE(cls.properties.size() >= 2u);
    EXPECT_TRUE(std::string(cls.properties[0].name) == "Length");
    EXPECT_TRUE(std::string(cls.properties[1].name) == "IsEmpty");
    EXPECT_TRUE(cls.methods.size() >= 2u);
    EXPECT_TRUE(std::string(cls.methods[0].name) == "Substring");
    EXPECT_TRUE(std::string(cls.methods[1].name) == "Concat");
}

TEST(RuntimeClassCatalog, TypeRegistryResolvesBuiltinExternal)
{
    // Ensure singleton is seeded for this process
    auto &tyreg = runtimeTypeRegistry();
    tyreg.seedRuntimeClasses(runtimeClassCatalog());
    EXPECT_EQ(tyreg.kindOf("Viper.String"), TypeKind::BuiltinExternalType);
    // BASIC alias STRING should also map
    EXPECT_EQ(tyreg.kindOf("STRING"), TypeKind::BuiltinExternalType);
}

TEST(RuntimeClassCatalog, ContainsViperSystemString)
{
    const auto &cat = runtimeClassCatalog();
    EXPECT_TRUE(cat.size() >= 2u);

    const auto it = std::find_if(cat.begin(), cat.end(), [](const auto &c) {
        return std::string(c.qname) == "Viper.System.String";
    });
    ASSERT_NE(it, cat.end());

    // Check a couple of members
    const auto &cls = *it;
    // Expect at least Length property
    bool hasLen = false;
    for (const auto &p : cls.properties)
        hasLen = hasLen || std::string(p.name) == "Length";
    EXPECT_TRUE(hasLen);

    // Expect Substring method
    bool hasSubstr = false;
    for (const auto &m : cls.methods)
        hasSubstr = hasSubstr || std::string(m.name) == "Substring";
    EXPECT_TRUE(hasSubstr);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
