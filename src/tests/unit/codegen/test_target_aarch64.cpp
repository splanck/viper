//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_target_aarch64.cpp
// Purpose: Smoke test for AArch64 target descriptor: register naming, ABI sets, and arg orders.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include "codegen/aarch64/TargetAArch64.hpp"

using namespace viper::codegen::aarch64;

TEST(AArch64Target, RegNamesAndABI)
{
    auto &ti = darwinTarget();

    // Basic name sanity
    EXPECT_NE(std::string(regName(PhysReg::X0)).find('x'), std::string::npos);
    EXPECT_NE(std::string(regName(PhysReg::V0)).find('v'), std::string::npos);

    // Int arg order is x0..x7
    EXPECT_EQ(ti.intArgOrder[0], PhysReg::X0);
    EXPECT_EQ(ti.intArgOrder[7], PhysReg::X7);

    // FP arg order is v0..v7
    EXPECT_EQ(ti.f64ArgOrder[0], PhysReg::V0);
    EXPECT_EQ(ti.f64ArgOrder[7], PhysReg::V7);

    // Return registers
    EXPECT_EQ(ti.intReturnReg, PhysReg::X0);
    EXPECT_EQ(ti.f64ReturnReg, PhysReg::V0);

    // Stack alignment
    EXPECT_EQ(ti.stackAlignment, 16U);

    // Classification helpers
    EXPECT_TRUE(isGPR(PhysReg::X10));
    EXPECT_FALSE(isGPR(PhysReg::V10));
    EXPECT_TRUE(isFPR(PhysReg::V31));
    EXPECT_FALSE(isFPR(PhysReg::SP));
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
