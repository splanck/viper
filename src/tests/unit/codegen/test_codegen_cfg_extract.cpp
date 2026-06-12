//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_cfg_extract.cpp
// Purpose: Unit tests for shared MIR CFG extraction (CfgExtract.hpp) and its
//          use by both backend liveness analyses. The key regression covered:
//          a block containing several conditional branches before its final
//          unconditional jump (switch compare cascades) must contribute an
//          edge for EVERY conditional branch, not just the one nearest the
//          terminator — otherwise values used only in an early case block are
//          dropped from liveOut and the allocator never preserves them.
//
// Key invariants:
//   - Every JCC / BCond / Cbz / Cbnz target is a successor.
//   - RET / UD2 / no-return calls end a block with no successors.
//   - Blocks without unconditional terminators fall through to the next
//     layout block.
//
// Ownership/Lifetime:
//   - Standalone test binary.
//
// Links: src/codegen/common/ra/CfgExtract.hpp,
//        src/codegen/x86_64/ra/Liveness.cpp,
//        src/codegen/aarch64/ra/Liveness.cpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/ra/Liveness.hpp"
#include "codegen/common/ra/CfgExtract.hpp"
#include "codegen/x86_64/MachineIR.hpp"
#include "codegen/x86_64/ra/Liveness.hpp"

#include <algorithm>
#include <vector>

namespace {

bool contains(const std::vector<std::size_t> &haystack, std::size_t needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

} // namespace

// ---------------------------------------------------------------------------
// x86-64: switch-style compare cascade — every JCC contributes an edge.
// ---------------------------------------------------------------------------
TEST(CfgExtract, X64SwitchCascadeKeepsAllCaseSuccessors) {
    using namespace viper::codegen::x64;

    MFunction fn{};
    fn.name = "cascade";

    // entry:
    //   v1 = 42                      (value used only in case "one")
    //   v2 = 7                       (value used only in case "two")
    //   CMP v3, $1 ; JCC one
    //   CMP v3, $2 ; JCC two
    //   JMP def
    MBasicBlock entry{};
    entry.label = "cascade_entry";
    entry.instructions = {
        MInstr::make(MOpcode::MOVri, {makeVRegOperand(RegClass::GPR, 1), makeImmOperand(42)}),
        MInstr::make(MOpcode::MOVri, {makeVRegOperand(RegClass::GPR, 2), makeImmOperand(7)}),
        MInstr::make(MOpcode::CMPri, {makeVRegOperand(RegClass::GPR, 3), makeImmOperand(1)}),
        MInstr::make(MOpcode::JCC, {makeImmOperand(0), makeLabelOperand("case_one")}),
        MInstr::make(MOpcode::CMPri, {makeVRegOperand(RegClass::GPR, 3), makeImmOperand(2)}),
        MInstr::make(MOpcode::JCC, {makeImmOperand(0), makeLabelOperand("case_two")}),
        MInstr::make(MOpcode::JMP, {makeLabelOperand("case_def")}),
    };

    MBasicBlock caseOne{};
    caseOne.label = "case_one";
    caseOne.instructions = {
        MInstr::make(MOpcode::MOVrr,
                     {makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX)),
                      makeVRegOperand(RegClass::GPR, 1)}),
        MInstr::make(MOpcode::RET),
    };

    MBasicBlock caseTwo{};
    caseTwo.label = "case_two";
    caseTwo.instructions = {
        MInstr::make(MOpcode::MOVrr,
                     {makePhysRegOperand(RegClass::GPR, static_cast<uint16_t>(PhysReg::RAX)),
                      makeVRegOperand(RegClass::GPR, 2)}),
        MInstr::make(MOpcode::RET),
    };

    MBasicBlock caseDef{};
    caseDef.label = "case_def";
    caseDef.instructions = {MInstr::make(MOpcode::RET)};

    fn.blocks = {entry, caseOne, caseTwo, caseDef};

    ra::LivenessAnalysis liveness;
    liveness.run(fn);

    const auto &succs = liveness.successors(0);
    ASSERT_EQ(succs.size(), 3u);
    EXPECT_TRUE(contains(succs, 1));
    EXPECT_TRUE(contains(succs, 2));
    EXPECT_TRUE(contains(succs, 3));

    // The regression that motivated the rewrite: v1 is used only in the FIRST
    // case block. With the old nearest-JCC scan, edge entry->case_one was
    // dropped and v1 vanished from liveOut(entry).
    EXPECT_TRUE(liveness.liveOut(0).count(1) != 0);
    EXPECT_TRUE(liveness.liveOut(0).count(2) != 0);
}

// ---------------------------------------------------------------------------
// x86-64: JCC as final instruction falls through; RET/UD2 end the block.
// ---------------------------------------------------------------------------
TEST(CfgExtract, X64FallthroughAndNoSuccessorTerminators) {
    using namespace viper::codegen::x64;

    MFunction fn{};
    fn.name = "fallthrough";

    MBasicBlock entry{};
    entry.label = "ft_entry";
    entry.instructions = {
        MInstr::make(MOpcode::TESTrr,
                     {makeVRegOperand(RegClass::GPR, 1), makeVRegOperand(RegClass::GPR, 1)}),
        MInstr::make(MOpcode::JCC, {makeImmOperand(0), makeLabelOperand("ft_target")}),
    };

    MBasicBlock next{};
    next.label = "ft_next";
    next.instructions = {MInstr::make(MOpcode::UD2)};

    MBasicBlock target{};
    target.label = "ft_target";
    target.instructions = {MInstr::make(MOpcode::RET)};

    fn.blocks = {entry, next, target};

    ra::LivenessAnalysis liveness;
    liveness.run(fn);

    const auto &entrySuccs = liveness.successors(0);
    ASSERT_EQ(entrySuccs.size(), 2u);
    EXPECT_TRUE(contains(entrySuccs, 1)); // fallthrough
    EXPECT_TRUE(contains(entrySuccs, 2)); // JCC target

    EXPECT_TRUE(liveness.successors(1).empty()); // UD2: no successors
    EXPECT_TRUE(liveness.successors(2).empty()); // RET: no successors
}

// ---------------------------------------------------------------------------
// AArch64: cascade of conditional branches keeps every successor.
// ---------------------------------------------------------------------------
TEST(CfgExtract, A64ConditionalCascadeKeepsAllSuccessors) {
    using namespace viper::codegen::aarch64;

    MFunction fn{};
    fn.name = "a64_cascade";

    MBasicBlock entry{};
    entry.name = "entry";
    entry.instrs = {
        MInstr{MOpcode::CmpRI, {MOperand::vregOp(RegClass::GPR, 3), MOperand::immOp(1)}},
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp("one")}},
        MInstr{MOpcode::CmpRI, {MOperand::vregOp(RegClass::GPR, 3), MOperand::immOp(2)}},
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp("two")}},
        MInstr{MOpcode::Br, {MOperand::labelOp("def")}},
    };

    MBasicBlock one{};
    one.name = "one";
    one.instrs = {MInstr{MOpcode::Ret, {}}};

    MBasicBlock two{};
    two.name = "two";
    two.instrs = {MInstr{MOpcode::Ret, {}}};

    MBasicBlock def{};
    def.name = "def";
    def.instrs = {MInstr{MOpcode::Ret, {}}};

    fn.blocks = {entry, one, two, def};

    ra::LivenessAnalysis liveness;
    liveness.run(fn);

    const auto &succs = liveness.successors(0);
    ASSERT_EQ(succs.size(), 3u);
    EXPECT_TRUE(contains(succs, 1));
    EXPECT_TRUE(contains(succs, 2));
    EXPECT_TRUE(contains(succs, 3));
}

// ---------------------------------------------------------------------------
// AArch64: Cbz/Cbnz fallthrough plus dead code after Br is ignored.
// ---------------------------------------------------------------------------
TEST(CfgExtract, A64CbzFallthroughAndBrEndsScan) {
    using namespace viper::codegen::aarch64;

    MFunction fn{};
    fn.name = "a64_cbz";

    MBasicBlock entry{};
    entry.name = "entry";
    entry.instrs = {
        MInstr{MOpcode::Cbz, {MOperand::vregOp(RegClass::GPR, 1), MOperand::labelOp("zero")}},
        MInstr{MOpcode::Br, {MOperand::labelOp("other")}},
        // Dead conditional after the Br must not contribute an edge.
        MInstr{MOpcode::BCond, {MOperand::condOp("ne"), MOperand::labelOp("zero")}},
    };

    MBasicBlock zero{};
    zero.name = "zero";
    zero.instrs = {MInstr{MOpcode::Ret, {}}};

    MBasicBlock other{};
    other.name = "other";
    other.instrs = {MInstr{MOpcode::Ret, {}}};

    fn.blocks = {entry, zero, other};

    ra::LivenessAnalysis liveness;
    liveness.run(fn);

    const auto &succs = liveness.successors(0);
    ASSERT_EQ(succs.size(), 2u);
    EXPECT_TRUE(contains(succs, 1));
    EXPECT_TRUE(contains(succs, 2));
}

// ---------------------------------------------------------------------------
// Shared extractor: unresolvable labels are skipped; sorting/dedup applied.
// ---------------------------------------------------------------------------
TEST(CfgExtract, SharedExtractorSkipsUnknownLabelsAndDedups) {
    using viper::codegen::ra::BranchDesc;

    struct FakeInstr {
        BranchDesc::Kind kind{BranchDesc::Kind::None};
        std::string label{};
    };
    struct FakeBlock {
        std::vector<FakeInstr> instrs{};
    };

    std::vector<FakeBlock> blocks(2);
    blocks[0].instrs = {
        FakeInstr{BranchDesc::Kind::Cond, "external_symbol"}, // unknown: skipped
        FakeInstr{BranchDesc::Kind::Cond, "b"},
        FakeInstr{BranchDesc::Kind::Uncond, "b"}, // duplicate of the Cond edge
    };
    blocks[1].instrs = {FakeInstr{BranchDesc::Kind::Return, ""}};

    std::unordered_map<std::string, std::size_t> index{{"a", 0}, {"b", 1}};

    auto succs = viper::codegen::ra::extractSuccessors(
        blocks,
        index,
        [](const FakeBlock &blk) -> const std::vector<FakeInstr> & { return blk.instrs; },
        [](const FakeInstr &ins) {
            return BranchDesc{ins.kind, ins.label.empty() ? nullptr : &ins.label};
        });

    ASSERT_EQ(succs[0].size(), 1u);
    EXPECT_EQ(succs[0][0], 1u);
    EXPECT_TRUE(succs[1].empty());
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
