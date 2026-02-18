//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_frame_spill_reuse.cpp
// Purpose: Verify AArch64 FrameBuilder spill slot reuse correctness.
//
// Tests that ensureSpillWithReuse() recycles dead slots for new vregs when
// their live ranges do not overlap, and conservatively allocates fresh slots
// when ranges do overlap.
//
// Key invariants:
//   1. A slot whose lastUseIdx < currentInstrIdx is dead and reusable.
//   2. A slot that is still live (lastUseIdx >= currentInstrIdx) must NOT
//      be recycled — doing so would corrupt the live value on the stack.
//   3. Multiple sequential non-overlapping vregs share one slot.
//   4. Total frame size decreases when reuse is possible.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/FrameBuilder.hpp"
#include "codegen/aarch64/MachineIR.hpp"

#include <unordered_set>

using namespace viper::codegen::aarch64;

// -------------------------------------------------------------------------
// Test 1: Two non-overlapping vregs share one spill slot.
//
//   vreg 0: [2, 5]  — spilled at 2, last use 5
//   vreg 1: [6, 10] — spilled at 6 (vreg 0 is dead: lastUse=5 < 6)
//
// Expected: both vregs return the SAME FP-relative offset.
// -------------------------------------------------------------------------
TEST(AArch64SpillReuse, NonOverlappingSharesSlot)
{
    MFunction mf{};
    FrameBuilder fb{mf};

    const int off0 = fb.ensureSpillWithReuse(/*vreg=*/0, /*lastUse=*/5, /*current=*/2);
    const int off1 = fb.ensureSpillWithReuse(/*vreg=*/1, /*lastUse=*/10, /*current=*/6);

    // vreg 0 is dead at instr 6 (lastUse=5 < 6), so vreg 1 must reuse the slot.
    EXPECT_EQ(off0, off1);
}

// -------------------------------------------------------------------------
// Test 2: Overlapping vregs must NOT share a slot.
//
//   vreg 0: [2, 8]  — spilled at 2, last use 8
//   vreg 1: [5, 10] — spilled at 5 (vreg 0 still live: lastUse=8 >= 5)
//
// Expected: vregs get DIFFERENT FP-relative offsets.
// -------------------------------------------------------------------------
TEST(AArch64SpillReuse, OverlappingAllocatesSeparateSlots)
{
    MFunction mf{};
    FrameBuilder fb{mf};

    const int off0 = fb.ensureSpillWithReuse(/*vreg=*/0, /*lastUse=*/8, /*current=*/2);
    const int off1 = fb.ensureSpillWithReuse(/*vreg=*/1, /*lastUse=*/10, /*current=*/5);

    EXPECT_NE(off0, off1);
}

// -------------------------------------------------------------------------
// Test 3: Three sequential non-overlapping vregs all share one slot.
//
//   vreg 0: [1, 3]
//   vreg 1: [4, 6]
//   vreg 2: [7, 9]
//
// Expected: all three get the SAME offset; exactly one distinct slot offset.
// -------------------------------------------------------------------------
TEST(AArch64SpillReuse, ThreeSequentialVregsShareOneSlot)
{
    MFunction mf{};
    FrameBuilder fb{mf};

    const int off0 = fb.ensureSpillWithReuse(0, /*lastUse=*/3, /*current=*/1);
    const int off1 = fb.ensureSpillWithReuse(1, /*lastUse=*/6, /*current=*/4);
    const int off2 = fb.ensureSpillWithReuse(2, /*lastUse=*/9, /*current=*/7);

    EXPECT_EQ(off0, off1);
    EXPECT_EQ(off1, off2);

    std::unordered_set<int> distinct;
    for (const auto &s : mf.frame.spills)
        distinct.insert(s.offset);
    EXPECT_EQ(static_cast<int>(distinct.size()), 1);
}

// -------------------------------------------------------------------------
// Test 4: Repeated call for the same vreg returns the same offset (idempotent).
// -------------------------------------------------------------------------
TEST(AArch64SpillReuse, SameVregReturnsSameOffset)
{
    MFunction mf{};
    FrameBuilder fb{mf};

    const int off0 = fb.ensureSpillWithReuse(42, /*lastUse=*/10, /*current=*/2);
    const int off1 = fb.ensureSpillWithReuse(42, /*lastUse=*/10, /*current=*/3);

    EXPECT_EQ(off0, off1);
}

// -------------------------------------------------------------------------
// Test 5: Frame size with reuse is smaller than without reuse.
//
//   Non-overlapping: 4 vregs → one 8-byte slot → 16 bytes (16-byte aligned).
//   Overlapping:     4 vregs → four 8-byte slots → 32 bytes.
// -------------------------------------------------------------------------
TEST(AArch64SpillReuse, ReusedFrameSmallerThanUniqueSlots)
{
    // Scenario A: 4 non-overlapping vregs — all reuse one slot.
    {
        MFunction mf{};
        FrameBuilder fb{mf};
        fb.ensureSpillWithReuse(0, /*lastUse=*/3, /*current=*/1);
        fb.ensureSpillWithReuse(1, /*lastUse=*/6, /*current=*/4);
        fb.ensureSpillWithReuse(2, /*lastUse=*/9, /*current=*/7);
        fb.ensureSpillWithReuse(3, /*lastUse=*/12, /*current=*/10);
        fb.finalize();
        EXPECT_EQ(mf.frame.totalBytes, 16); // one slot, 16-byte aligned
    }

    // Scenario B: 4 simultaneously-live vregs — need 4 unique slots.
    {
        MFunction mf{};
        FrameBuilder fb{mf};
        fb.ensureSpillWithReuse(0, /*lastUse=*/20, /*current=*/1);
        fb.ensureSpillWithReuse(1, /*lastUse=*/20, /*current=*/2);
        fb.ensureSpillWithReuse(2, /*lastUse=*/20, /*current=*/3);
        fb.ensureSpillWithReuse(3, /*lastUse=*/20, /*current=*/4);
        fb.finalize();
        EXPECT_EQ(mf.frame.totalBytes, 32); // four slots × 8 bytes
    }
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
