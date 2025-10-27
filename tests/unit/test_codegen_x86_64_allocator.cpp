// File: tests/unit/test_codegen_x86_64_allocator.cpp
// Purpose: Exercise the linear-scan allocator phase in isolation.
// Key invariants: Allocation assigns deterministic registers and rewrites
//                 operands to their physical counterparts.
// Ownership/Lifetime: Tests mutate Machine IR in place and inspect the result.
// Links: src/codegen/x86_64/ra/Allocator.hpp

#include "GTestStub.hpp"

#include "codegen/x86_64/RegAllocLinear.hpp"
#include "codegen/x86_64/TargetX64.hpp"
#include "codegen/x86_64/ra/Allocator.hpp"
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

TEST(Allocator, AssignsRegisters)
{
    MFunction func{};
    MBasicBlock block{};
    block.label = "entry";
    block.instructions.push_back(makeMovImm(1, 42));
    block.instructions.push_back(makeMovImm(2, 7));
    block.instructions.push_back(makeAdd(1, 2));
    func.blocks.push_back(std::move(block));

    LiveIntervals intervals{};
    intervals.run(func);

    LinearScanAllocator allocator{func, sysvTarget(), intervals};
    auto result = allocator.run();

    ASSERT_EQ(result.vregToPhys.size(), 2U);
    EXPECT_EQ(result.vregToPhys[1], PhysReg::RAX);
    EXPECT_EQ(result.vregToPhys[2], PhysReg::RDI);

    const auto &rewritten = func.blocks.front().instructions;
    ASSERT_EQ(rewritten.size(), 3U);
    for (const auto &instr : rewritten)
    {
        for (const auto &operand : instr.operands)
        {
            if (const auto *reg = std::get_if<OpReg>(&operand); reg)
            {
                EXPECT_TRUE(reg->isPhys);
            }
        }
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
