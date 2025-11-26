//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// File: tests/unit/runtime_classes/TestRuntimeClassFileBinding.cpp
// Purpose: Ensure static calls to Viper.IO.File bind to canonical externs.
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../GTestStub.hpp"
#endif

#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

TEST(RuntimeClassFileBinding, MethodIndexTargets)
{
    const auto &cat = il::runtime::runtimeClassCatalog();
    il::frontends::basic::runtimeMethodIndex().seed(cat);
    auto &midx = il::frontends::basic::runtimeMethodIndex();
    // Exists(path)
    auto a = midx.find("Viper.IO.File", "Exists", 1);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->target, std::string("Viper.IO.File.Exists"));
    // ReadAllText(path)
    auto b = midx.find("Viper.IO.File", "ReadAllText", 1);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->target, std::string("Viper.IO.File.ReadAllText"));
    // WriteAllText(path,contents)
    auto c = midx.find("Viper.IO.File", "WriteAllText", 2);
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->target, std::string("Viper.IO.File.WriteAllText"));
    // Delete(path)
    auto d = midx.find("Viper.IO.File", "Delete", 1);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->target, std::string("Viper.IO.File.Delete"));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
