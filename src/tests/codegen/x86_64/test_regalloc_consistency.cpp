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

[[nodiscard]] MInstr makeAddAll(uint16_t dst, const std::vector<uint16_t> &srcs)
{
    // Create an instruction that uses all source vregs - ADD dst, src
    // We'll add them one at a time to create dependencies
    return MInstr::make(MOpcode::ADDrr,
                        {makeVRegOperand(RegClass::GPR, dst), makeVRegOperand(RegClass::GPR, srcs[0])});
}

void addPressureFunction(MFunction &func)
{
    MBasicBlock block{};
    block.label = "pressure";
    // Define 15 vregs
    for (uint16_t id = 1; id <= 15; ++id)
    {
        block.instructions.push_back(makeMovImm(id, static_cast<int64_t>(id)));
    }
    // Add instructions that use all 15 vregs simultaneously to force them all to be live
    // Sum them all: result = v1 + v2 + v3 + ... + v15
    for (uint16_t id = 2; id <= 15; ++id)
    {
        block.instructions.push_back(makeAdd(1, id)); // v1 += v<id>
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

    // Pressure test: 15 vregs that are all live simultaneously should cause spilling
    // With 14 allocatable GPRs, at least 1 spill is expected
    MFunction pressure{};
    addPressureFunction(pressure);
    auto pressureResult = allocate(pressure, target);

    // Verify allocation succeeded and at least 1 spill occurred (15 vregs > 14 GPRs)
    if (pressureResult.spillSlotsGPR < 1)
    {
        std::cerr << "Pressure allocation: expected at least 1 spill slot, got "
                  << pressureResult.spillSlotsGPR << "\n";
        std::cerr << "  vregToPhys.size() = " << pressureResult.vregToPhys.size() << "\n";
        return EXIT_FAILURE;
    }

    // Verify spill stores were actually emitted
    const auto &pressureBlock = pressure.blocks.front().instructions;
    const bool hasSpillStore =
        std::any_of(pressureBlock.begin(),
                    pressureBlock.end(),
                    [](const MInstr &instr)
                    {
                        return instr.opcode == MOpcode::MOVrm && instr.operands.size() == 2 &&
                               std::holds_alternative<OpMem>(instr.operands[0]);
                    });
    if (!hasSpillStore)
    {
        std::cerr << "Pressure allocation: expected spill store\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
