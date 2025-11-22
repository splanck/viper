//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_codegen_x86_64_coalescer.cpp
// Purpose: Ensure PX_COPY lowering emits deterministic move sequences. 
// Key invariants: Coalescing removes PX_COPY pseudos and releases scratch
// Ownership/Lifetime: Tests construct Machine IR locally and run the allocator.
// Links: src/codegen/x86_64/ra/Coalescer.hpp
//
//===----------------------------------------------------------------------===//

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

} // namespace

TEST(Coalescer, LowersParallelCopy)
{
    MFunction func{};
    MBasicBlock block{};
    block.label = "entry";
    block.instructions.push_back(makeMovImm(1, 1));
    block.instructions.push_back(makeMovImm(2, 2));
    block.instructions.push_back(MInstr::make(MOpcode::PX_COPY,
                                              {makeVRegOperand(RegClass::GPR, 1),
                                               makeVRegOperand(RegClass::GPR, 2),
                                               makeVRegOperand(RegClass::GPR, 2),
                                               makeVRegOperand(RegClass::GPR, 1)}));
    func.blocks.push_back(std::move(block));

    LiveIntervals intervals{};
    intervals.run(func);

    LinearScanAllocator allocator{func, sysvTarget(), intervals};
    auto result = allocator.run();
    EXPECT_EQ(result.spillSlotsGPR, 0);

    const auto &rewritten = func.blocks.front().instructions;
    EXPECT_TRUE(rewritten.size() > 3U);
    for (const auto &instr : rewritten)
    {
        EXPECT_NE(instr.opcode, MOpcode::PX_COPY);
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
