//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_codegen_x86_64_spiller.cpp
// Purpose: Validate spill slot management and load/store helpers.
// Key invariants: Spill slots grow monotonically and spill insertion releases
// Ownership/Lifetime: Tests manipulate VirtualAllocation records directly.
// Links: src/codegen/x86_64/ra/Spiller.hpp
//
//===----------------------------------------------------------------------===//
#include "codegen/x86_64/RegAllocLinear.hpp"
#include "codegen/x86_64/ra/Allocator.hpp"
#include "codegen/x86_64/ra/Spiller.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <deque>

using namespace viper::codegen::x64;
using namespace viper::codegen::x64::ra;

TEST(Spiller, AllocatesSlotsPerClass)
{
    Spiller spiller{};
    SpillPlan plan{};
    spiller.ensureSpillSlot(RegClass::GPR, plan);
    EXPECT_TRUE(plan.needsSpill);
    EXPECT_EQ(plan.slot, 0);
    EXPECT_EQ(spiller.gprSlots(), 1);

    SpillPlan xmmPlan{};
    spiller.ensureSpillSlot(RegClass::XMM, xmmPlan);
    EXPECT_TRUE(xmmPlan.needsSpill);
    EXPECT_EQ(xmmPlan.slot, 0);
    EXPECT_EQ(spiller.xmmSlots(), 1);
}

TEST(Spiller, EmitsLoadStore)
{
    Spiller spiller{};
    SpillPlan plan{true, 3};
    auto load = spiller.makeLoad(RegClass::GPR, PhysReg::RAX, plan);
    EXPECT_EQ(load.opcode, MOpcode::MOVmr);
    ASSERT_EQ(load.operands.size(), 2U);
    const auto *dst = std::get_if<OpReg>(&load.operands[0]);
    ASSERT_NE(dst, nullptr);
    EXPECT_TRUE(dst->isPhys);
    EXPECT_EQ(dst->idOrPhys, static_cast<uint16_t>(PhysReg::RAX));

    auto store = spiller.makeStore(RegClass::GPR, plan, PhysReg::RDI);
    EXPECT_EQ(store.opcode, MOpcode::MOVrm);
    ASSERT_EQ(store.operands.size(), 2U);
    const auto *src = std::get_if<OpReg>(&store.operands[1]);
    ASSERT_NE(src, nullptr);
    EXPECT_TRUE(src->isPhys);
    EXPECT_EQ(src->idOrPhys, static_cast<uint16_t>(PhysReg::RDI));
}

TEST(Spiller, SpillsActiveValue)
{
    Spiller spiller{};
    VirtualAllocation alloc{};
    alloc.cls = RegClass::GPR;
    alloc.hasPhys = true;
    alloc.phys = PhysReg::RAX;
    AllocationResult result{};
    result.vregToPhys.emplace(static_cast<uint16_t>(7), PhysReg::RAX);

    std::deque<PhysReg> pool{};
    std::vector<MInstr> prefix{};
    spiller.spillValue(RegClass::GPR, 7, alloc, pool, prefix, result);

    EXPECT_FALSE(alloc.hasPhys);
    EXPECT_TRUE(alloc.spill.needsSpill);
    EXPECT_EQ(alloc.spill.slot, 0);
    EXPECT_EQ(spiller.gprSlots(), 1);
    EXPECT_TRUE(pool.end() != std::find(pool.begin(), pool.end(), PhysReg::RAX));
    EXPECT_TRUE(prefix.size() == 1U);
    EXPECT_EQ(prefix.front().opcode, MOpcode::MOVrm);
    EXPECT_TRUE(result.vregToPhys.empty());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
