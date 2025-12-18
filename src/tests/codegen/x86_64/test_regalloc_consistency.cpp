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

#include <algorithm>
#include <cstdlib>
#include <iostream>

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

int main()
{
    TargetInfo &target = sysvTarget();

    MFunction simple{};
    addSimpleFunction(simple);
    auto simpleResult = allocate(simple, target);
    if (simpleResult.vregToPhys.size() != 2U)
    {
        std::cerr << "Simple allocation: expected 2 vregs\n";
        return EXIT_FAILURE;
    }
    if (simpleResult.vregToPhys[1] != PhysReg::RAX || simpleResult.vregToPhys[2] != PhysReg::RDI)
    {
        std::cerr << "Simple allocation: unexpected vreg assignments\n";
        return EXIT_FAILURE;
    }
    if (simpleResult.spillSlotsGPR != 0)
    {
        std::cerr << "Simple allocation: expected 0 spill slots\n";
        return EXIT_FAILURE;
    }

    MFunction pressure{};
    addPressureFunction(pressure);
    auto pressureResult = allocate(pressure, target);
    if (pressureResult.spillSlotsGPR != 1)
    {
        std::cerr << "Pressure allocation: expected 1 spill slot\n";
        return EXIT_FAILURE;
    }
    if (pressureResult.vregToPhys.size() != 14U)
    {
        std::cerr << "Pressure allocation: expected 14 assigned vregs\n";
        return EXIT_FAILURE;
    }

    struct Expect
    {
        uint16_t vreg;
        PhysReg phys;
    };

    constexpr Expect kExpected[] = {
        {2, PhysReg::RDI},
        {3, PhysReg::RSI},
        {4, PhysReg::RDX},
        {5, PhysReg::RCX},
        {6, PhysReg::R8},
        {7, PhysReg::R9},
        {8, PhysReg::R10},
        {9, PhysReg::R11},
        {10, PhysReg::RBX},
        {11, PhysReg::R12},
        {12, PhysReg::R13},
        {13, PhysReg::R14},
        {14, PhysReg::R15},
        {15, PhysReg::RAX},
    };
    for (const auto &expect : kExpected)
    {
        auto it = pressureResult.vregToPhys.find(expect.vreg);
        if (it == pressureResult.vregToPhys.end() || it->second != expect.phys)
        {
            std::cerr << "Pressure allocation: unexpected mapping for vreg " << expect.vreg << "\n";
            return EXIT_FAILURE;
        }
    }

    const auto &pressureBlock = pressure.blocks.front().instructions;
    const bool hasSpillStore =
        std::any_of(pressureBlock.begin(),
                    pressureBlock.end(),
                    [](const MInstr &instr)
                    {
                        return instr.opcode == MOpcode::MOVrr && instr.operands.size() == 2 &&
                               std::holds_alternative<OpMem>(instr.operands[0]);
                    });
    if (!hasSpillStore)
    {
        std::cerr << "Pressure allocation: expected spill store\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
