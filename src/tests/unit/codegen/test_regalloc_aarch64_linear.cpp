//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_regalloc_aarch64_linear.cpp
// Purpose: Validate AArch64 linear allocation, spills, ABI constraints, and
//          cross-block value restoration.
// Key invariants: Every vreg is physicalized, live values survive pressure and
//                 CFG edges, and ABI registers are protected when required.
// Ownership/Lifetime: Tests own ephemeral machine functions and frame layouts.
// Links: codegen/aarch64/ra/Allocator.hpp, docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/FrameBuilder.hpp"
#include "codegen/aarch64/LowerOvf.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/RegAllocLinear.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/aarch64/ra/RegPools.hpp"
#include <cstdio>

using namespace zanna::codegen::aarch64;

TEST(Arm64RegAlloc, SpillsAndCalleeSaved) {
    auto &ti = darwinTarget();
    MFunction fn{};
    fn.name = "ra_test";
    fn.blocks.push_back(MBasicBlock{});
    auto &bb = fn.blocks.back();
    bb.name = "entry";

    // Reserve a local alloca (one i64)
    FrameBuilder fb{fn};
    fb.addLocal(/*tempId*/ 1, /*size*/ 8, /*align*/ 8);

    // Create many virtual temporaries to exceed the available caller-saved pool
    // and force usage of callee-saved + spills. We define 40 vregs sequentially.
    const int N = 40;
    for (int i = 0; i < N; ++i) {
        MOperand dst = MOperand::vregOp(RegClass::GPR, static_cast<uint16_t>(i));
        bb.instrs.push_back(MInstr{MOpcode::MovRI, {dst, MOperand::immOp(i)}});
    }
    // Use every temporary after all definitions so the live range overlap
    // exceeds the available register pool and forces actual spill/reload code.
    uint16_t acc = 0;
    uint16_t next = static_cast<uint16_t>(N);
    for (int i = 1; i < N; ++i) {
        const uint16_t dstId = next++;
        bb.instrs.push_back(MInstr{MOpcode::AddRRR,
                                   {MOperand::vregOp(RegClass::GPR, dstId),
                                    MOperand::vregOp(RegClass::GPR, acc),
                                    MOperand::vregOp(RegClass::GPR, static_cast<uint16_t>(i))}});
        acc = dstId;
    }
    // Move result to x0 to make output deterministic
    bb.instrs.push_back(MInstr{
        MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, acc)}});

    // Run allocator
    auto result = allocate(fn, ti);
    // Expect that we used at least one callee-saved register
    bool usedCallee = false;
    for (auto r : fn.savedGPRs) {
        if (std::find(ti.calleeSavedGPR.begin(), ti.calleeSavedGPR.end(), r) !=
            ti.calleeSavedGPR.end()) {
            usedCallee = true;
        }
    }
    EXPECT_TRUE(usedCallee);

    // Emit to text and look for spills/loads and prologue shape
    AsmEmitter emit{ti};
    std::ostringstream os;
    emit.emitFunction(os, fn);
    const std::string asmText = os.str();
    // Spill stores and reload loads should appear
    EXPECT_NE(asmText.find("str x"), std::string::npos);
    EXPECT_NE(asmText.find("ldr x"), std::string::npos);

    // Prologue must adjust sp by total frame size
    const int frameSize = fn.frame.totalBytes;
    if (frameSize > 0) {
        const std::string sub = "sub sp, sp, #" + std::to_string(frameSize);
        EXPECT_NE(asmText.find(sub), std::string::npos);
    }
    // Callee-saved used in RA must be saved in prologue
    for (auto r : fn.savedGPRs) {
        const char *name = regName(r);
        const std::string needle = std::string(name) + ",";
        EXPECT_NE(asmText.find(needle), std::string::npos);
    }
}

TEST(Arm64RegAlloc, LiveOutSpillsStayAfterInternalOverflowBranch) {
    auto &ti = darwinTarget();
    MFunction fn{};
    fn.name = "ra_ovf_liveout";
    fn.blocks.resize(4);
    fn.blocks[0].name = "entry";
    fn.blocks[1].name = "then";
    fn.blocks[2].name = "exit";
    fn.blocks[3].name = "trap";

    auto &entry = fn.blocks[0];
    entry.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, 1), MOperand::immOp(40)}});
    entry.instrs.push_back(MInstr{MOpcode::AddOvfRI,
                                  {MOperand::vregOp(RegClass::GPR, 2),
                                   MOperand::vregOp(RegClass::GPR, 1),
                                   MOperand::immOp(2)}});
    entry.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, 3), MOperand::immOp(7)}});
    entry.instrs.push_back(
        MInstr{MOpcode::CmpRI, {MOperand::vregOp(RegClass::GPR, 1), MOperand::immOp(0)}});
    entry.instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp("then")}});
    entry.instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("exit")}});

    fn.blocks[1].instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, 2)}});
    fn.blocks[1].instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks[2].instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, 3)}});
    fn.blocks[2].instrs.push_back(MInstr{MOpcode::Ret, {}});
    fn.blocks[3].instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("rt_trap_ovf")}});

    lowerOverflowOps(fn);
    auto result = allocate(fn, ti);
    (void)result;

    const auto &rewritten = fn.blocks[0].instrs;
    auto addIt = std::find_if(rewritten.begin(), rewritten.end(), [](const MInstr &instr) {
        return instr.opc == MOpcode::AddsRI;
    });
    ASSERT_NE(addIt, rewritten.end());
    auto afterAdd = std::next(addIt);
    ASSERT_NE(afterAdd, rewritten.end());
    EXPECT_EQ(afterAdd->opc, MOpcode::BCond);

    const auto trailingBr =
        std::find_if(rewritten.begin(), rewritten.end(), [](const MInstr &instr) {
            return instr.opc == MOpcode::Br;
        });
    ASSERT_NE(trailingBr, rewritten.end());
    const auto firstTrailingTerm = std::prev(trailingBr);
    EXPECT_EQ(firstTrailingTerm->opc, MOpcode::BCond);
    const bool hasSpillBeforeTrailingTerm =
        std::any_of(std::next(addIt), firstTrailingTerm, [](const MInstr &instr) {
            return instr.opc == MOpcode::StrRegFpImm || instr.opc == MOpcode::StrFprFpImm;
        });
    EXPECT_TRUE(hasSpillBeforeTrailingTerm);
}

TEST(Arm64RegAlloc, SpilledVregReloadsOnceAndIsCached) {
    // A spilled vreg used by several later instructions must be reloaded into
    // an adopted home register once and reused, not re-loaded through scratch
    // at every use. Build pressure with 40 long-lived defs, then read v0 three
    // times in a row.
    auto &ti = darwinTarget();
    MFunction fn{};
    fn.name = "ra_reload_cache";
    fn.blocks.push_back(MBasicBlock{});
    auto &bb = fn.blocks.back();
    bb.name = "entry";

    const int N = 40;
    for (int i = 0; i < N; ++i) {
        bb.instrs.push_back(MInstr{
            MOpcode::MovRI,
            {MOperand::vregOp(RegClass::GPR, static_cast<uint16_t>(i)), MOperand::immOp(i)}});
    }

    // Consume v1..v39 first so v0's next use is the furthest and pressure
    // evicts it to its spill slot.
    uint16_t next = static_cast<uint16_t>(N);
    uint16_t acc = 1;
    for (int i = 2; i < N; ++i) {
        const uint16_t dst = next++;
        bb.instrs.push_back(MInstr{MOpcode::AddRRR,
                                   {MOperand::vregOp(RegClass::GPR, dst),
                                    MOperand::vregOp(RegClass::GPR, acc),
                                    MOperand::vregOp(RegClass::GPR, static_cast<uint16_t>(i))}});
        acc = dst;
    }
    // Three consecutive reads of the (by now spilled) v0.
    for (int k = 0; k < 3; ++k) {
        const uint16_t dst = next++;
        bb.instrs.push_back(MInstr{MOpcode::AddRRR,
                                   {MOperand::vregOp(RegClass::GPR, dst),
                                    MOperand::vregOp(RegClass::GPR, 0),
                                    MOperand::vregOp(RegClass::GPR, 0)}});
    }
    bb.instrs.push_back(MInstr{
        MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, acc)}});

    (void)allocate(fn, ti);

    // Count frame reloads feeding the three v0 reads: locate the AddRRR run
    // whose source operands are equal registers, then count LdrRegFpImm into
    // that register. Exactly one reload of v0 must remain (the adopted-home
    // load); the historical behaviour reloaded per use (three loads).
    int selfAddCount = 0;
    PhysReg selfAddSrc{};
    for (const auto &mi : fn.blocks[0].instrs) {
        if (mi.opc == MOpcode::AddRRR && mi.ops.size() == 3 &&
            mi.ops[1].kind == MOperand::Kind::Reg && mi.ops[2].kind == MOperand::Kind::Reg &&
            mi.ops[1].reg.isPhys && mi.ops[2].reg.isPhys &&
            mi.ops[1].reg.idOrPhys == mi.ops[2].reg.idOrPhys) {
            ++selfAddCount;
            selfAddSrc = static_cast<PhysReg>(mi.ops[1].reg.idOrPhys);
        }
    }
    ASSERT_EQ(selfAddCount, 3);

    // Between the first and last self-add there must be NO reloads of the
    // source register: the single adopted-home load happens before the first
    // read and is reused by the following two. (The historical behaviour
    // emitted one scratch reload per read.)
    std::size_t firstSelfAdd = 0;
    std::size_t lastSelfAdd = 0;
    bool seenSelfAdd = false;
    const auto &instrs = fn.blocks[0].instrs;
    for (std::size_t i = 0; i < instrs.size(); ++i) {
        const auto &mi = instrs[i];
        if (mi.opc == MOpcode::AddRRR && mi.ops.size() == 3 &&
            mi.ops[1].kind == MOperand::Kind::Reg && mi.ops[2].kind == MOperand::Kind::Reg &&
            mi.ops[1].reg.isPhys && mi.ops[2].reg.isPhys &&
            mi.ops[1].reg.idOrPhys == mi.ops[2].reg.idOrPhys) {
            if (!seenSelfAdd)
                firstSelfAdd = i;
            lastSelfAdd = i;
            seenSelfAdd = true;
        }
    }
    ASSERT_TRUE(seenSelfAdd);

    int reloadsBetweenReads = 0;
    for (std::size_t i = firstSelfAdd; i <= lastSelfAdd; ++i) {
        if (instrs[i].opc == MOpcode::LdrRegFpImm)
            ++reloadsBetweenReads;
    }
    EXPECT_EQ(reloadsBetweenReads, 0);

    // And at least one load into the adopted register feeds the first read
    // (the same physical register may have served other reloads earlier).
    int loadsBeforeFirstRead = 0;
    for (std::size_t i = 0; i < firstSelfAdd; ++i) {
        const auto &mi = instrs[i];
        if (mi.opc == MOpcode::LdrRegFpImm && !mi.ops.empty() &&
            mi.ops[0].kind == MOperand::Kind::Reg && mi.ops[0].reg.isPhys &&
            static_cast<PhysReg>(mi.ops[0].reg.idOrPhys) == selfAddSrc) {
            ++loadsBeforeFirstRead;
        }
    }
    EXPECT_GE(loadsBeforeFirstRead, 1);
}

TEST(Arm64RegAlloc, PhysDefEvictsResidentVreg) {
    // An instruction that writes a physical register must evict any vreg the
    // allocator parked there: spill it when still needed, and reload it for
    // later uses. Target the first pool register so the test is independent
    // of pool ordering details.
    auto &ti = darwinTarget();

    // Determine the first register the pool will hand out.
    ra::RegPools probe;
    probe.build(ti);
    const PhysReg firstPool = probe.takeGPR();

    MFunction fn{};
    fn.name = "phys_def_evict";
    fn.blocks.push_back(MBasicBlock{});
    auto &bb = fn.blocks.back();
    bb.name = "entry";

    // v1 := 7  (lands in firstPool)
    bb.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, 1), MOperand::immOp(7)}});
    // firstPool := 99 (explicit physical def — clobbers v1's home)
    bb.instrs.push_back(MInstr{MOpcode::MovRI, {MOperand::regOp(firstPool), MOperand::immOp(99)}});
    // v2 := v1 + v1 (v1 must have been preserved across the clobber)
    bb.instrs.push_back(MInstr{MOpcode::AddRRR,
                               {MOperand::vregOp(RegClass::GPR, 2),
                                MOperand::vregOp(RegClass::GPR, 1),
                                MOperand::vregOp(RegClass::GPR, 1)}});
    bb.instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, 2)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    (void)allocate(fn, ti);

    // After allocation no instruction may read firstPool between the explicit
    // def and a store/reload pair: concretely, the AddRRR's sources must not
    // be firstPool, OR a spill store of firstPool must precede the clobber.
    const auto &instrs = fn.blocks[0].instrs;
    std::size_t clobberIdx = instrs.size();
    for (std::size_t i = 0; i < instrs.size(); ++i) {
        const auto &mi = instrs[i];
        if (mi.opc == MOpcode::MovRI && mi.ops.size() == 2 &&
            mi.ops[0].kind == MOperand::Kind::Reg && mi.ops[0].reg.isPhys &&
            static_cast<PhysReg>(mi.ops[0].reg.idOrPhys) == firstPool &&
            mi.ops[1].kind == MOperand::Kind::Imm && mi.ops[1].imm == 99) {
            clobberIdx = i;
            break;
        }
    }
    ASSERT_LT(clobberIdx, instrs.size());

    bool spilledBeforeClobber = false;
    for (std::size_t i = 0; i < clobberIdx; ++i) {
        if (instrs[i].opc == MOpcode::StrRegFpImm && !instrs[i].ops.empty() &&
            instrs[i].ops[0].kind == MOperand::Kind::Reg &&
            static_cast<PhysReg>(instrs[i].ops[0].reg.idOrPhys) == firstPool)
            spilledBeforeClobber = true;
    }

    bool addReadsClobberedReg = false;
    for (std::size_t i = clobberIdx + 1; i < instrs.size(); ++i) {
        const auto &mi = instrs[i];
        if (mi.opc != MOpcode::AddRRR || mi.ops.size() != 3)
            continue;
        // A reload may legitimately re-adopt firstPool AFTER loading the
        // spilled value back, so only flag reads without a preceding reload.
        bool reloadedBetween = false;
        for (std::size_t j = clobberIdx + 1; j < i; ++j) {
            if (instrs[j].opc == MOpcode::LdrRegFpImm && !instrs[j].ops.empty() &&
                instrs[j].ops[0].kind == MOperand::Kind::Reg &&
                static_cast<PhysReg>(instrs[j].ops[0].reg.idOrPhys) ==
                    static_cast<PhysReg>(mi.ops[1].reg.idOrPhys))
                reloadedBetween = true;
        }
        if (!reloadedBetween && static_cast<PhysReg>(mi.ops[1].reg.idOrPhys) == firstPool)
            addReadsClobberedReg = true;
    }

    EXPECT_TRUE(spilledBeforeClobber);
    EXPECT_FALSE(addReadsClobberedReg);
}

TEST(Arm64RegAlloc, MarshalledArgRegisterNotReassignedBeforeCall) {
    // Once call marshalling writes an argument register, fresh allocations
    // must not receive that register until the call has consumed it.
    auto &ti = darwinTarget();

    MFunction fn{};
    fn.name = "arg_reserved";
    fn.isLeaf = false;
    fn.blocks.push_back(MBasicBlock{});
    auto &bb = fn.blocks.back();
    bb.name = "entry";

    bb.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, 1), MOperand::immOp(7)}});
    // Marshal the argument: x0 := v1
    bb.instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, 1)}});
    // Fresh def between marshalling and the call must not take x0.
    bb.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, 2), MOperand::immOp(5)}});
    bb.instrs.push_back(MInstr{MOpcode::Bl, {MOperand::labelOp("callee")}});
    // Keep v2 alive past the call.
    bb.instrs.push_back(
        MInstr{MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, 2)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    (void)allocate(fn, ti);

    // Find the rewritten "v2 := 5" def (immediate 5) between marshalling and
    // the call and verify it did not land in x0.
    const auto &instrs = fn.blocks[0].instrs;
    bool checked = false;
    for (std::size_t i = 0; i < instrs.size(); ++i) {
        const auto &mi = instrs[i];
        if (mi.opc == MOpcode::Bl)
            break;
        if (mi.opc == MOpcode::MovRI && mi.ops.size() == 2 &&
            mi.ops[1].kind == MOperand::Kind::Imm && mi.ops[1].imm == 5 &&
            mi.ops[0].kind == MOperand::Kind::Reg && mi.ops[0].reg.isPhys) {
            EXPECT_TRUE(static_cast<PhysReg>(mi.ops[0].reg.idOrPhys) != PhysReg::X0);
            checked = true;
        }
    }
    EXPECT_TRUE(checked);
}

TEST(Arm64RegAlloc, AbiLiveInArgRegisterIsNeverAllocated) {
    // A function whose entry reads x0 before any def (parameter spill) must
    // keep x0 out of the pools for the whole function, while other argument
    // registers remain allocatable.
    auto &ti = darwinTarget();

    MFunction fn{};
    fn.name = "live_in_excluded";
    fn.blocks.push_back(MBasicBlock{});
    auto &bb = fn.blocks.back();
    bb.name = "entry";

    FrameBuilder fb{fn};
    fb.addLocal(1, 8, 8);

    // Entry parameter spill: read of live-in x0.
    bb.instrs.push_back(
        MInstr{MOpcode::StrRegFpImm, {MOperand::regOp(PhysReg::X0), MOperand::immOp(-8)}});
    // Plenty of fresh vregs afterwards.
    for (uint16_t v = 1; v <= 20; ++v) {
        bb.instrs.push_back(
            MInstr{MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, v), MOperand::immOp(v)}});
    }
    uint16_t acc = 1;
    uint16_t next = 21;
    for (uint16_t v = 2; v <= 20; ++v) {
        bb.instrs.push_back(MInstr{MOpcode::AddRRR,
                                   {MOperand::vregOp(RegClass::GPR, next),
                                    MOperand::vregOp(RegClass::GPR, acc),
                                    MOperand::vregOp(RegClass::GPR, v)}});
        acc = next++;
    }
    bb.instrs.push_back(MInstr{
        MOpcode::MovRR, {MOperand::regOp(PhysReg::X0), MOperand::vregOp(RegClass::GPR, acc)}});
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    (void)allocate(fn, ti);

    // No vreg def (MovRI with positive immediate) may have been assigned x0;
    // x1 (not a live-in here) should appear as some def's destination.
    bool x0Assigned = false;
    bool x1Assigned = false;
    for (const auto &mi : fn.blocks[0].instrs) {
        if (mi.opc != MOpcode::MovRI || mi.ops.size() != 2)
            continue;
        if (mi.ops[0].kind != MOperand::Kind::Reg || !mi.ops[0].reg.isPhys)
            continue;
        if (mi.ops[1].kind != MOperand::Kind::Imm || mi.ops[1].imm < 1 || mi.ops[1].imm > 20)
            continue;
        const auto dst = static_cast<PhysReg>(mi.ops[0].reg.idOrPhys);
        x0Assigned = x0Assigned || dst == PhysReg::X0;
        x1Assigned = x1Assigned || dst == PhysReg::X1;
    }
    EXPECT_FALSE(x0Assigned);
    EXPECT_TRUE(x1Assigned);
}

TEST(Arm64RegAlloc, SiblingSuccessorsRestoreSpilledLiveInsIndependently) {
    // Both successors read the same high-pressure live-out set. Allocating the
    // first successor must not retire the predecessor spills needed by the
    // second successor.
    auto &ti = darwinTarget();
    MFunction fn{};
    fn.name = "sibling_liveins";
    fn.blocks.resize(3);
    fn.blocks[0].name = "entry";
    fn.blocks[1].name = "true_edge";
    fn.blocks[2].name = "false_edge";

    constexpr uint16_t kValueCount = 40;
    for (uint16_t value = 1; value <= kValueCount; ++value) {
        fn.blocks[0].instrs.push_back(MInstr{
            MOpcode::MovRI, {MOperand::vregOp(RegClass::GPR, value), MOperand::immOp(value)}});
    }
    fn.blocks[0].instrs.push_back(
        MInstr{MOpcode::CmpRI, {MOperand::vregOp(RegClass::GPR, 1), MOperand::immOp(0)}});
    fn.blocks[0].instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp("true_edge")}});
    fn.blocks[0].instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("false_edge")}});

    FrameBuilder destinationSlots{fn};
    std::vector<int> offsets;
    offsets.reserve(kValueCount);
    for (uint16_t value = 1; value <= kValueCount; ++value)
        offsets.push_back(destinationSlots.ensureSpill(60000u + value));

    for (std::size_t blockIndex = 1; blockIndex <= 2; ++blockIndex) {
        auto &block = fn.blocks[blockIndex];
        for (uint16_t value = 1; value <= kValueCount; ++value) {
            block.instrs.push_back(MInstr{
                MOpcode::PhiStoreGPR,
                {MOperand::vregOp(RegClass::GPR, value), MOperand::immOp(offsets[value - 1])}});
        }
        block.instrs.push_back(MInstr{MOpcode::Ret, {}});
    }

    (void)allocate(fn, ti);

    const auto reloadCount = [](const MBasicBlock &block) {
        return static_cast<int>(
            std::count_if(block.instrs.begin(), block.instrs.end(), [](const MInstr &instr) {
                return instr.opc == MOpcode::LdrRegFpImm;
            }));
    };
    const int trueReloads = reloadCount(fn.blocks[1]);
    const int falseReloads = reloadCount(fn.blocks[2]);
    EXPECT_GT(trueReloads, 0);
    EXPECT_EQ(falseReloads, trueReloads);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
