// File: tests/unit/test_codegen_x86_64_live_intervals.cpp
// Purpose: Unit tests covering the live interval analysis used by the
//          linear-scan allocator.
// Key invariants: Analysis produces deterministic half-open instruction ranges.
// Ownership/Lifetime: Tests build Machine IR in place and inspect analysis
//                     results without transferring ownership.
// Links: src/codegen/x86_64/ra/LiveIntervals.hpp

#include "GTestStub.hpp"

#include "codegen/x86_64/MachineIR.hpp"
#include "codegen/x86_64/ra/LiveIntervals.hpp"

using namespace viper::codegen::x64;
using namespace viper::codegen::x64::ra;

namespace
{

[[nodiscard]] MInstr makeMovImm(uint16_t id, int64_t value)
{
    return MInstr::make(MOpcode::MOVri,
                        {makeVRegOperand(RegClass::GPR, id), makeImmOperand(value)});
}

[[nodiscard]] MInstr makeAdd(uint16_t lhs, uint16_t rhs)
{
    return MInstr::make(MOpcode::ADDrr,
                        {makeVRegOperand(RegClass::GPR, lhs), makeVRegOperand(RegClass::GPR, rhs)});
}

} // namespace

TEST(LiveIntervals, ComputesLocalRanges)
{
    MFunction func{};
    MBasicBlock block{};
    block.label = "entry";
    block.instructions.push_back(makeMovImm(1, 42));
    block.instructions.push_back(makeMovImm(2, 7));
    block.instructions.push_back(makeAdd(1, 2));
    func.blocks.push_back(std::move(block));

    LiveIntervals analysis{};
    analysis.run(func);

    const auto *interval1 = analysis.lookup(1);
    ASSERT_NE(interval1, nullptr);
    EXPECT_EQ(interval1->start, 0U);
    EXPECT_EQ(interval1->end, 3U);

    const auto *interval2 = analysis.lookup(2);
    ASSERT_NE(interval2, nullptr);
    EXPECT_EQ(interval2->start, 1U);
    EXPECT_EQ(interval2->end, 3U);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
