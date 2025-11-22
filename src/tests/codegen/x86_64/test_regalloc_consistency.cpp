//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/x86_64/test_regalloc_consistency.cpp
// Purpose: Integration tests validating register allocation outputs against 
// Key invariants: Allocation results remain deterministic for representative
// Ownership/Lifetime: Tests construct Machine IR on the stack and run the
// Links: src/codegen/x86_64/RegAllocLinear.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/RegAllocLinear.hpp"
#include "codegen/x86_64/TargetX64.hpp"

#if __has_include(<gtest/gtest.h>)
#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#define VIPER_HAS_GTEST 1
#else
#include <cstdlib>
#include <iostream>
#define VIPER_HAS_GTEST 0
#endif

#include <algorithm>

using namespace viper::codegen::x64;

namespace
{

[[nodiscard]] MInstr makeMovImm(uint16_t id, int64_t value)
{
    return MInstr::make(MOpcode::MOVri,
                        {makeVRegOperand(RegClass::GPR, id), makeImmOperand(value)});
}

[[nodiscard]] MInstr makeAdd(uint16_t dst, uint16_t rhs)
{
    return MInstr::make(MOpcode::ADDrr,
                        {makeVRegOperand(RegClass::GPR, dst), makeVRegOperand(RegClass::GPR, rhs)});
}

void addSimpleFunction(MFunction &func)
{
    MBasicBlock block{};
    block.label = "simple";
    block.instructions.push_back(makeMovImm(1, 10));
    block.instructions.push_back(makeMovImm(2, 20));
    block.instructions.push_back(makeAdd(1, 2));
    func.blocks.push_back(std::move(block));
}

void addPressureFunction(MFunction &func)
{
    MBasicBlock block{};
    block.label = "pressure";
    for (uint16_t id = 1; id <= 15; ++id)
    {
        block.instructions.push_back(makeMovImm(id, static_cast<int64_t>(id)));
    }
    func.blocks.push_back(std::move(block));
}

} // namespace

#if VIPER_HAS_GTEST

TEST(RegAllocConsistency, MatchesExpectedAssignments)
{
    TargetInfo &target = sysvTarget();

    MFunction simple{};
    addSimpleFunction(simple);
    auto simpleResult = allocate(simple, target);
    ASSERT_EQ(simpleResult.vregToPhys.size(), 2U);
    EXPECT_EQ(simpleResult.vregToPhys[1], PhysReg::RAX);
    EXPECT_EQ(simpleResult.vregToPhys[2], PhysReg::RDI);
    EXPECT_EQ(simpleResult.spillSlotsGPR, 0);

    MFunction pressure{};
    addPressureFunction(pressure);
    auto pressureResult = allocate(pressure, target);
    EXPECT_EQ(pressureResult.spillSlotsGPR, 1);
    ASSERT_EQ(pressureResult.vregToPhys.size(), 14U);
    EXPECT_EQ(pressureResult.vregToPhys.at(2), PhysReg::RDI);
    EXPECT_EQ(pressureResult.vregToPhys.at(3), PhysReg::RSI);
    EXPECT_EQ(pressureResult.vregToPhys.at(4), PhysReg::RDX);
    EXPECT_EQ(pressureResult.vregToPhys.at(5), PhysReg::RCX);
    EXPECT_EQ(pressureResult.vregToPhys.at(6), PhysReg::R8);
    EXPECT_EQ(pressureResult.vregToPhys.at(7), PhysReg::R9);
    EXPECT_EQ(pressureResult.vregToPhys.at(8), PhysReg::R10);
    EXPECT_EQ(pressureResult.vregToPhys.at(9), PhysReg::R11);
    EXPECT_EQ(pressureResult.vregToPhys.at(10), PhysReg::RBX);
    EXPECT_EQ(pressureResult.vregToPhys.at(11), PhysReg::R12);
    EXPECT_EQ(pressureResult.vregToPhys.at(12), PhysReg::R13);
    EXPECT_EQ(pressureResult.vregToPhys.at(13), PhysReg::R14);
    EXPECT_EQ(pressureResult.vregToPhys.at(14), PhysReg::R15);
    EXPECT_EQ(pressureResult.vregToPhys.at(15), PhysReg::RAX);

    const auto &pressureBlock = pressure.blocks.front().instructions;
    const bool hasSpillStore =
        std::any_of(pressureBlock.begin(),
                    pressureBlock.end(),
                    [](const MInstr &instr)
                    {
                        return instr.opcode == MOpcode::MOVrr && instr.operands.size() == 2 &&
                               std::holds_alternative<OpMem>(instr.operands[0]);
                    });
    EXPECT_TRUE(hasSpillStore);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#else

int main()
{
    TargetInfo &target = sysvTarget();

    MFunction simple{};
    addSimpleFunction(simple);
    auto simpleResult = allocate(simple, target);
    if (simpleResult.vregToPhys.size() != 2U || simpleResult.vregToPhys[1] != PhysReg::RAX ||
        simpleResult.vregToPhys[2] != PhysReg::RDI || simpleResult.spillSlotsGPR != 0)
    {
        std::cerr << "Simple allocation mismatch";
        return EXIT_FAILURE;
    }

    MFunction pressure{};
    addPressureFunction(pressure);
    auto pressureResult = allocate(pressure, target);
    if (pressureResult.spillSlotsGPR != 1 || pressureResult.vregToPhys.size() != 14U)
    {
        std::cerr << "Pressure allocation summary mismatch";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#endif
