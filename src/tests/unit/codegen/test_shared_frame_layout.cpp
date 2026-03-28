//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_shared_frame_layout.cpp
// Purpose: Verify that the shared FrameLayout interface works correctly with
//          AArch64's FrameBuilder implementation. Tests local allocation,
//          spill slot management, and frame finalization through the abstract
//          interface.
//
// Key invariants:
//   - FrameBuilder can be used through a FrameLayout pointer
//   - Local offsets are negative (below frame pointer)
//   - Spill slot offsets are negative
//   - totalBytes() is valid after finalize()
//
// Ownership/Lifetime:
//   - Standalone test binary.
//
// Links: src/codegen/common/FrameLayout.hpp,
//        src/codegen/aarch64/FrameBuilder.hpp,
//        plans/audit-01-backend-abstraction.md
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/FrameBuilder.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/common/CallLoweringPlan.hpp"
#include "codegen/common/FrameLayout.hpp"

using namespace viper::codegen::aarch64;
using namespace viper::codegen::common;

// ===========================================================================
// FrameLayout via AArch64 FrameBuilder
// ===========================================================================

TEST(SharedFrameLayout, FrameBuilderImplementsInterface) {
    MFunction fn{};
    fn.name = "test_func";
    FrameBuilder builder(fn);

    // FrameBuilder IS-A FrameLayout
    FrameLayout *layout = &builder;
    (void)layout; // verify assignment compiles
    EXPECT_TRUE(true);
}

TEST(SharedFrameLayout, AddLocalViaInterface) {
    MFunction fn{};
    fn.name = "test_func";
    FrameBuilder builder(fn);
    FrameLayout &layout = builder;

    layout.addLocal(0, 8, 8);
    int offset = layout.localOffset(0);
    EXPECT_LT(offset, 0); // Negative offset (below FP)
}

TEST(SharedFrameLayout, SpillViaInterface) {
    MFunction fn{};
    fn.name = "test_func";
    FrameBuilder builder(fn);
    FrameLayout &layout = builder;

    int spillOff = layout.ensureSpill(42, 8, 8);
    EXPECT_LT(spillOff, 0); // Negative offset

    // Same vreg returns same offset
    int spillOff2 = layout.ensureSpill(42, 8, 8);
    EXPECT_EQ(spillOff, spillOff2);
}

TEST(SharedFrameLayout, FinalizeViaInterface) {
    MFunction fn{};
    fn.name = "test_func";
    FrameBuilder builder(fn);
    FrameLayout &layout = builder;

    layout.addLocal(0, 8, 8);
    layout.addLocal(1, 8, 8);
    layout.ensureSpill(10, 8, 8);
    layout.setMaxOutgoing(0);
    layout.finalize();

    int total = layout.totalBytes();
    EXPECT_GT(total, 0);
    // 3 slots × 8 bytes = 24 → aligned to 16 = 32
    EXPECT_GE(total, 24);
}

TEST(SharedFrameLayout, DifferentLocalsGetDifferentOffsets) {
    MFunction fn{};
    fn.name = "test_func";
    FrameBuilder builder(fn);
    FrameLayout &layout = builder;

    layout.addLocal(0, 8, 8);
    layout.addLocal(1, 8, 8);

    int off0 = layout.localOffset(0);
    int off1 = layout.localOffset(1);
    EXPECT_NE(off0, off1);
}

// ===========================================================================
// CallLoweringPlan shared struct
// ===========================================================================

TEST(SharedCallPlan, BasicConstruction) {
    CallLoweringPlan plan{};
    plan.callee = "rt_print_i64";
    plan.args.push_back({CallArgClass::GPR, 1, false, 0});
    plan.returnsF64 = false;
    plan.isVarArg = false;

    EXPECT_EQ(plan.callee, "rt_print_i64");
    EXPECT_EQ(plan.args.size(), 1u);
    EXPECT_EQ(plan.args[0].cls, CallArgClass::GPR);
}

TEST(SharedCallPlan, VarArgPlan) {
    CallLoweringPlan plan{};
    plan.callee = "rt_snprintf";
    plan.isVarArg = true;
    plan.numNamedArgs = 3;                                 // buf, size, fmt
    plan.args.push_back({CallArgClass::GPR, 1, false, 0}); // buf
    plan.args.push_back({CallArgClass::GPR, 2, false, 0}); // size
    plan.args.push_back({CallArgClass::GPR, 3, false, 0}); // fmt
    plan.args.push_back({CallArgClass::GPR, 4, false, 0}); // variadic arg 1

    EXPECT_TRUE(plan.isVarArg);
    EXPECT_EQ(plan.numNamedArgs, 3u);
    EXPECT_EQ(plan.args.size(), 4u);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
