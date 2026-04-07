//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_codegen_x86_64_allocator.cpp
// Purpose: Exercise the linear-scan allocator phase in isolation.
// Key invariants: Allocation assigns deterministic registers and rewrites
// Ownership/Lifetime: Tests mutate Machine IR in place and inspect the result.
// Links: src/codegen/x86_64/ra/Allocator.hpp
//
//===----------------------------------------------------------------------===//
#include "codegen/x86_64/RegAllocLinear.hpp"
#include "codegen/x86_64/TargetX64.hpp"
#include "codegen/x86_64/ra/Allocator.hpp"
#include "codegen/x86_64/ra/LiveIntervals.hpp"
#include "tests/TestHarness.hpp"

#include <iostream>

using namespace viper::codegen::x64;
using namespace viper::codegen::x64::ra;

namespace {

[[nodiscard]] MInstr makeMovImm(uint16_t id, int64_t value) {
    return MInstr::make(MOpcode::MOVri,
                        {makeVRegOperand(RegClass::GPR, id), makeImmOperand(value)});
}

[[nodiscard]] MInstr makeAdd(uint16_t lhs, uint16_t rhs) {
    return MInstr::make(MOpcode::ADDrr,
                        {makeVRegOperand(RegClass::GPR, lhs), makeVRegOperand(RegClass::GPR, rhs)});
}

[[nodiscard]] MInstr makeStoreIndexed(uint16_t base, uint16_t index, uint16_t src) {
    return MInstr::make(
        MOpcode::MOVrm,
        {makeMemOperand(makeVReg(RegClass::GPR, base), makeVReg(RegClass::GPR, index), 1, 0),
         makeVRegOperand(RegClass::GPR, src)});
}

} // namespace

TEST(Allocator, AssignsRegisters) {
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
    for (const auto &instr : rewritten) {
        for (const auto &operand : instr.operands) {
            if (const auto *reg = std::get_if<OpReg>(&operand); reg) {
                EXPECT_TRUE(reg->isPhys);
            }
        }
    }
}

TEST(Allocator, DoesNotSpillCurrentInstructionAddressOperands) {
    MFunction func{};
    MBasicBlock block{};
    block.label = "entry";

    for (uint16_t id = 1; id <= 16; ++id) {
        block.instructions.push_back(makeMovImm(id, static_cast<int64_t>(id)));
    }

    block.instructions.push_back(makeStoreIndexed(14, 15, 16));

    block.instructions.push_back(makeAdd(1, 2));
    block.instructions.push_back(makeAdd(3, 4));
    block.instructions.push_back(makeAdd(5, 6));
    block.instructions.push_back(makeAdd(7, 8));
    block.instructions.push_back(makeAdd(9, 10));
    block.instructions.push_back(makeAdd(11, 12));
    block.instructions.push_back(makeAdd(13, 14));
    block.instructions.push_back(makeAdd(15, 16));

    func.blocks.push_back(std::move(block));

    LiveIntervals intervals{};
    intervals.run(func);

    LinearScanAllocator allocator{func, sysvTarget(), intervals};
    [[maybe_unused]] auto allocResult = allocator.run();

    const auto &rewritten = func.blocks.front().instructions;
    ASSERT_GT(rewritten.size(), 16U);

    for (const auto &instr : rewritten) {
        std::cerr << toString(instr) << '\n';
    }

    const MInstr *store = nullptr;
    for (const auto &instr : rewritten) {
        if (instr.opcode != MOpcode::MOVrm || instr.operands.size() != 2U)
            continue;
        const auto *memCandidate = std::get_if<OpMem>(&instr.operands[0]);
        if (memCandidate && memCandidate->hasIndex) {
            store = &instr;
            break;
        }
    }
    ASSERT_NE(store, nullptr);

    const auto *mem = std::get_if<OpMem>(&store->operands[0]);
    const auto *src = std::get_if<OpReg>(&store->operands[1]);
    ASSERT_NE(mem, nullptr);
    ASSERT_NE(src, nullptr);
    ASSERT_TRUE(mem->base.isPhys);
    ASSERT_TRUE(mem->index.isPhys);
    ASSERT_TRUE(src->isPhys);
    EXPECT_NE(mem->base.idOrPhys, mem->index.idOrPhys);
    EXPECT_NE(mem->base.idOrPhys, src->idOrPhys);
}

TEST(Allocator, CarriesLiveValueIntoImmediateSinglePredecessorSuccessor) {
    MFunction func{};

    MBasicBlock entry{};
    entry.label = "entry";
    entry.instructions.push_back(makeMovImm(1, 42));
    entry.instructions.push_back(MInstr::make(MOpcode::JMP, {makeLabelOperand("next")}));

    MBasicBlock next{};
    next.label = "next";
    next.instructions.push_back(makeMovImm(2, 7));
    next.instructions.push_back(makeAdd(2, 1));
    next.instructions.push_back(MInstr::make(MOpcode::RET, {}));

    func.blocks.push_back(std::move(entry));
    func.blocks.push_back(std::move(next));

    auto result = allocate(func, sysvTarget());
    (void)result;

    for (const auto &block : func.blocks) {
        for (const auto &instr : block.instructions) {
            EXPECT_NE(instr.opcode, MOpcode::MOVmr);
            EXPECT_NE(instr.opcode, MOpcode::MOVrm);
        }
    }
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
