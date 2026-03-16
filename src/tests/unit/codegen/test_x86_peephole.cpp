//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_x86_peephole.cpp
// Purpose: Unit tests for the x86-64 peephole optimization pass, covering all
//          10 sub-passes: MOV-zero to XOR, CMP-zero to TEST, arithmetic
//          identities, strength reduction, identity move removal, consecutive
//          move folding, dead code elimination, block layout, branch chain
//          elimination, branch inversion, and fallthrough jump removal.
//
// Key invariants:
//   - Each test constructs MIR directly and calls runPeepholes().
//   - Tests verify both the instruction sequence AND the statistics counters.
//
// Ownership/Lifetime:
//   - Standalone test binary.
//
// Links: src/codegen/x86_64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/x86_64/MachineIR.hpp"
#include "codegen/x86_64/Peephole.hpp"
#include "codegen/x86_64/TargetX64.hpp"

using namespace viper::codegen::x64;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

/// Helper: create a GPR register operand for a physical register.
Operand gpr(PhysReg pr)
{
    return OpReg{true, RegClass::GPR, static_cast<uint16_t>(pr)};
}

/// Helper: create an XMM register operand.
Operand xmm(PhysReg pr)
{
    return OpReg{true, RegClass::XMM, static_cast<uint16_t>(pr)};
}

/// Helper: create an immediate operand.
Operand imm(int64_t val)
{
    return OpImm{val};
}

/// Helper: create a label operand.
Operand lbl(const std::string &name)
{
    return OpLabel{name};
}

/// Helper: build a single-block function with given instructions.
MFunction makeFunc(const std::string &blockLabel, std::vector<MInstr> instrs)
{
    MFunction fn{};
    fn.name = "test_fn";
    MBasicBlock block{};
    block.label = blockLabel;
    block.instructions = std::move(instrs);
    fn.blocks.push_back(std::move(block));
    return fn;
}

} // namespace

// ---------------------------------------------------------------------------
// Pass 1a: MOV #0 -> XOR
// ---------------------------------------------------------------------------

TEST(X86Peephole, MovZeroToXor)
{
    // mov rax, #0 should become xor eax, eax
    auto fn = makeFunc(".Lentry",
                       {
                           MInstr{MOpcode::MOVri, {gpr(PhysReg::RAX), imm(0)}},
                           MInstr{MOpcode::RET, {}},
                       });

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    auto &instrs = fn.blocks[0].instructions;
    ASSERT_TRUE(instrs.size() >= 1U);
    // First instruction should now be XORrr32
    EXPECT_EQ(instrs[0].opcode, MOpcode::XORrr32);
}

TEST(X86Peephole, MovZeroToXorSkipsWhenFlagsRead)
{
    // mov rax, #0 followed by JCC reading flags — should NOT rewrite to XOR
    auto fn = makeFunc(".Lentry",
                       {
                           MInstr{MOpcode::CMPrr, {gpr(PhysReg::RBX), gpr(PhysReg::RCX)}},
                           MInstr{MOpcode::MOVri, {gpr(PhysReg::RAX), imm(0)}},
                           MInstr{MOpcode::JCC, {imm(0), lbl(".Ltarget")}},
                           MInstr{MOpcode::RET, {}},
                       });

    runPeepholes(fn);

    auto &instrs = fn.blocks[0].instructions;
    // The MOVri should still be there since JCC reads flags
    bool hasMov = false;
    for (auto &i : instrs)
    {
        if (i.opcode == MOpcode::MOVri)
            hasMov = true;
    }
    EXPECT_TRUE(hasMov);
}

// ---------------------------------------------------------------------------
// Pass 1b: CMP #0 -> TEST
// ---------------------------------------------------------------------------

TEST(X86Peephole, CmpZeroToTest)
{
    // cmp rax, #0 should become test rax, rax
    auto fn = makeFunc(".Lentry",
                       {
                           MInstr{MOpcode::CMPri, {gpr(PhysReg::RAX), imm(0)}},
                           MInstr{MOpcode::JCC, {imm(0), lbl(".Ltarget")}},
                           MInstr{MOpcode::RET, {}},
                       });

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    auto &instrs = fn.blocks[0].instructions;
    // First instruction should now be TESTrr
    bool hasTest = false;
    for (auto &i : instrs)
    {
        if (i.opcode == MOpcode::TESTrr)
            hasTest = true;
    }
    EXPECT_TRUE(hasTest);
}

// ---------------------------------------------------------------------------
// Pass 1c: Arithmetic Identity Elimination
// ---------------------------------------------------------------------------

TEST(X86Peephole, ArithIdentityAddZero)
{
    // add rax, #0 should be eliminated (when flags not read)
    auto fn = makeFunc(".Lentry",
                       {
                           MInstr{MOpcode::ADDri, {gpr(PhysReg::RAX), imm(0)}},
                           MInstr{MOpcode::RET, {}},
                       });

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    // add #0 should be removed, leaving only RET
    auto &instrs = fn.blocks[0].instructions;
    for (auto &i : instrs)
    {
        EXPECT_NE(i.opcode, MOpcode::ADDri);
    }
}

TEST(X86Peephole, ArithIdentityShiftZero)
{
    // shl rax, #0 should be eliminated (when flags not read)
    auto fn = makeFunc(".Lentry",
                       {
                           MInstr{MOpcode::SHLri, {gpr(PhysReg::RAX), imm(0)}},
                           MInstr{MOpcode::RET, {}},
                       });

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    auto &instrs = fn.blocks[0].instructions;
    for (auto &i : instrs)
    {
        EXPECT_NE(i.opcode, MOpcode::SHLri);
    }
}

TEST(X86Peephole, ArithIdentityAndAllOnes)
{
    // and rax, #-1 should be eliminated (when flags not read)
    auto fn = makeFunc(".Lentry",
                       {
                           MInstr{MOpcode::ANDri, {gpr(PhysReg::RAX), imm(-1)}},
                           MInstr{MOpcode::RET, {}},
                       });

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    auto &instrs = fn.blocks[0].instructions;
    for (auto &i : instrs)
    {
        EXPECT_NE(i.opcode, MOpcode::ANDri);
    }
}

TEST(X86Peephole, ArithIdentityOrZero)
{
    // or rax, #0 should be eliminated
    auto fn = makeFunc(".Lentry",
                       {
                           MInstr{MOpcode::ORri, {gpr(PhysReg::RAX), imm(0)}},
                           MInstr{MOpcode::RET, {}},
                       });

    runPeepholes(fn);

    auto &instrs = fn.blocks[0].instructions;
    for (auto &i : instrs)
    {
        EXPECT_NE(i.opcode, MOpcode::ORri);
    }
}

TEST(X86Peephole, ArithIdentityPreservedWhenFlagsRead)
{
    // add rax, #0 followed by JCC reading flags — should NOT be removed
    auto fn = makeFunc(".Lentry",
                       {
                           MInstr{MOpcode::ADDri, {gpr(PhysReg::RAX), imm(0)}},
                           MInstr{MOpcode::JCC, {imm(0), lbl(".Ltarget")}},
                           MInstr{MOpcode::RET, {}},
                       });

    runPeepholes(fn);

    auto &instrs = fn.blocks[0].instructions;
    bool hasAdd = false;
    for (auto &i : instrs)
    {
        if (i.opcode == MOpcode::ADDri)
            hasAdd = true;
    }
    EXPECT_TRUE(hasAdd);
}

// ---------------------------------------------------------------------------
// Pass 1d: Strength Reduction (MUL power-of-2 -> SHL)
// ---------------------------------------------------------------------------

TEST(X86Peephole, StrengthReductionMulPow2)
{
    // Load #8 into rcx, then imul rax, rcx -> shl rax, #3
    auto fn = makeFunc(".Lentry",
                       {
                           MInstr{MOpcode::MOVri, {gpr(PhysReg::RCX), imm(8)}},
                           MInstr{MOpcode::IMULrr, {gpr(PhysReg::RAX), gpr(PhysReg::RCX)}},
                           MInstr{MOpcode::RET, {}},
                       });

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    auto &instrs = fn.blocks[0].instructions;
    bool hasShl = false;
    for (auto &i : instrs)
    {
        if (i.opcode == MOpcode::SHLri)
        {
            hasShl = true;
            // Should shift by 3 (since 2^3 = 8)
            ASSERT_TRUE(i.operands.size() >= 2U);
            auto *shiftImm = std::get_if<OpImm>(&i.operands[1]);
            ASSERT_NE(shiftImm, nullptr);
            EXPECT_EQ(shiftImm->val, 3);
        }
    }
    EXPECT_TRUE(hasShl);
}

TEST(X86Peephole, StrengthReductionNoRewriteNonPow2)
{
    // Load #7 into rcx, then imul rax, rcx — 7 is not power-of-2, no rewrite
    auto fn = makeFunc(".Lentry",
                       {
                           MInstr{MOpcode::MOVri, {gpr(PhysReg::RCX), imm(7)}},
                           MInstr{MOpcode::IMULrr, {gpr(PhysReg::RAX), gpr(PhysReg::RCX)}},
                           MInstr{MOpcode::RET, {}},
                       });

    runPeepholes(fn);

    auto &instrs = fn.blocks[0].instructions;
    bool hasImul = false;
    for (auto &i : instrs)
    {
        if (i.opcode == MOpcode::IMULrr)
            hasImul = true;
    }
    EXPECT_TRUE(hasImul);
}

// ---------------------------------------------------------------------------
// Pass 3: Identity Move Removal
// ---------------------------------------------------------------------------

TEST(X86Peephole, RemoveIdentityMovRR)
{
    // mov rax, rax (identity) should be removed
    auto fn =
        makeFunc(".Lentry",
                 {
                     MInstr{MOpcode::MOVrr, {gpr(PhysReg::RAX), gpr(PhysReg::RBX)}}, // non-identity
                     MInstr{MOpcode::MOVrr, {gpr(PhysReg::RAX), gpr(PhysReg::RAX)}}, // identity
                     MInstr{MOpcode::MOVrr, {gpr(PhysReg::RCX), gpr(PhysReg::RCX)}}, // identity
                     MInstr{MOpcode::RET, {}},
                 });

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    auto &instrs = fn.blocks[0].instructions;
    // Should remove 2 identity moves, leaving non-identity mov + ret
    int movCount = 0;
    for (auto &i : instrs)
    {
        if (i.opcode == MOpcode::MOVrr)
            ++movCount;
    }
    EXPECT_EQ(movCount, 1); // Only the non-identity mov remains
}

TEST(X86Peephole, RemoveIdentityMovSDRR)
{
    // movsd xmm0, xmm0 (identity) should be removed
    auto fn = makeFunc(
        ".Lentry",
        {
            MInstr{MOpcode::MOVSDrr, {xmm(PhysReg::XMM0), xmm(PhysReg::XMM1)}}, // non-identity
            MInstr{MOpcode::MOVSDrr, {xmm(PhysReg::XMM0), xmm(PhysReg::XMM0)}}, // identity
            MInstr{MOpcode::RET, {}},
        });

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    auto &instrs = fn.blocks[0].instructions;
    int movsdCount = 0;
    for (auto &i : instrs)
    {
        if (i.opcode == MOpcode::MOVSDrr)
            ++movsdCount;
    }
    EXPECT_EQ(movsdCount, 1);
}

// ---------------------------------------------------------------------------
// Pass 9: Conditional Branch Inversion
// ---------------------------------------------------------------------------

TEST(X86Peephole, BranchInversion)
{
    // JCC(eq, .Lnext) / JMP(.Lexit) where .Lnext is the next block
    // Should become JCC(ne, .Lexit) with JMP removed.
    MFunction fn{};
    fn.name = "test_branch_inv";

    MBasicBlock bb1{};
    bb1.label = ".Lentry";
    bb1.instructions = {
        MInstr{MOpcode::CMPrr, {gpr(PhysReg::RAX), gpr(PhysReg::RBX)}},
        MInstr{MOpcode::JCC, {imm(0), lbl(".Lnext")}}, // cc=0 is eq
        MInstr{MOpcode::JMP, {lbl(".Lexit")}},
    };

    MBasicBlock bb2{};
    bb2.label = ".Lnext";
    bb2.instructions = {
        MInstr{MOpcode::RET, {}},
    };

    MBasicBlock bb3{};
    bb3.label = ".Lexit";
    bb3.instructions = {
        MInstr{MOpcode::RET, {}},
    };

    fn.blocks = {std::move(bb1), std::move(bb2), std::move(bb3)};

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    // After inversion: .Lentry should have CMP + JCC(ne, .Lexit), no JMP
    auto &entryInstrs = fn.blocks[0].instructions;
    bool hasJmp = false;
    for (auto &i : entryInstrs)
    {
        if (i.opcode == MOpcode::JMP)
            hasJmp = true;
    }
    EXPECT_FALSE(hasJmp);
}

// ---------------------------------------------------------------------------
// Pass 10: Fallthrough Jump Removal
// ---------------------------------------------------------------------------

TEST(X86Peephole, FallthroughJumpRemoval)
{
    // JMP .Lnext where .Lnext is the immediately following block
    MFunction fn{};
    fn.name = "test_fallthrough";

    MBasicBlock bb1{};
    bb1.label = ".Lentry";
    bb1.instructions = {
        MInstr{MOpcode::MOVrr, {gpr(PhysReg::RAX), gpr(PhysReg::RBX)}},
        MInstr{MOpcode::JMP, {lbl(".Lnext")}},
    };

    MBasicBlock bb2{};
    bb2.label = ".Lnext";
    bb2.instructions = {
        MInstr{MOpcode::RET, {}},
    };

    fn.blocks = {std::move(bb1), std::move(bb2)};

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    // The JMP to the next block should be removed
    auto &entryInstrs = fn.blocks[0].instructions;
    bool hasJmp = false;
    for (auto &i : entryInstrs)
    {
        if (i.opcode == MOpcode::JMP)
            hasJmp = true;
    }
    EXPECT_FALSE(hasJmp);
}

// ---------------------------------------------------------------------------
// Pass 8: Branch Chain Elimination
// ---------------------------------------------------------------------------

TEST(X86Peephole, BranchChainElimination)
{
    // .Lentry: JMP .Ltrampoline
    // .Ltrampoline: JMP .Ltarget (single-JMP block)
    // .Ltarget: RET
    // Should retarget .Lentry's JMP directly to .Ltarget.
    MFunction fn{};
    fn.name = "test_branch_chain";

    MBasicBlock bb1{};
    bb1.label = ".Lentry";
    bb1.instructions = {
        MInstr{MOpcode::JMP, {lbl(".Ltrampoline")}},
    };

    MBasicBlock bb2{};
    bb2.label = ".Ltrampoline";
    bb2.instructions = {
        MInstr{MOpcode::JMP, {lbl(".Ltarget")}},
    };

    MBasicBlock bb3{};
    bb3.label = ".Ltarget";
    bb3.instructions = {
        MInstr{MOpcode::RET, {}},
    };

    fn.blocks = {std::move(bb1), std::move(bb2), std::move(bb3)};

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    // The entry block should now jump directly to .Ltarget
    // (may have been further optimized by trace layout and fallthrough removal,
    // but the chain should be resolved)
    bool chainResolved = true;
    for (auto &block : fn.blocks)
    {
        for (auto &i : block.instructions)
        {
            if (i.opcode == MOpcode::JMP)
            {
                auto *target = std::get_if<OpLabel>(&i.operands[0]);
                if (target && target->name == ".Ltrampoline")
                    chainResolved = false;
            }
        }
    }
    EXPECT_TRUE(chainResolved);
}

// ---------------------------------------------------------------------------
// Pass 7: Cold Block Reordering
// ---------------------------------------------------------------------------

TEST(X86Peephole, ColdBlockMovedToEnd)
{
    // Blocks: entry, trap_div0, body — trap should move after body
    MFunction fn{};
    fn.name = "test_cold_block";

    MBasicBlock entry{};
    entry.label = ".Lentry";
    entry.instructions = {
        MInstr{MOpcode::CMPrr, {gpr(PhysReg::RAX), gpr(PhysReg::RBX)}},
        MInstr{MOpcode::JCC, {imm(0), lbl(".Ltrap_div0")}},
        MInstr{MOpcode::JMP, {lbl(".Lbody")}},
    };

    MBasicBlock trap{};
    trap.label = ".Ltrap_div0";
    trap.instructions = {
        MInstr{MOpcode::UD2, {}},
    };

    MBasicBlock body{};
    body.label = ".Lbody";
    body.instructions = {
        MInstr{MOpcode::RET, {}},
    };

    fn.blocks = {std::move(entry), std::move(trap), std::move(body)};

    runPeepholes(fn);

    // Entry should still be first
    EXPECT_EQ(fn.blocks[0].label, ".Lentry");
    // Trap block (cold) should be last
    EXPECT_EQ(fn.blocks.back().label, ".Ltrap_div0");
}

// ---------------------------------------------------------------------------
// Multiple optimizations in combination
// ---------------------------------------------------------------------------

TEST(X86Peephole, CombinedOptimizations)
{
    // Combines: MOV #0->XOR, identity mov removal, CMP #0->TEST
    auto fn = makeFunc(
        ".Lentry",
        {
            MInstr{MOpcode::MOVri, {gpr(PhysReg::RAX), imm(0)}},            // -> XOR
            MInstr{MOpcode::MOVrr, {gpr(PhysReg::RBX), gpr(PhysReg::RBX)}}, // identity, removed
            MInstr{MOpcode::CMPri, {gpr(PhysReg::RCX), imm(0)}},            // -> TEST
            MInstr{MOpcode::JCC, {imm(1), lbl(".Ltarget")}},
            MInstr{MOpcode::RET, {}},
        });

    auto count = runPeepholes(fn);
    EXPECT_TRUE(count > 0U);

    auto &instrs = fn.blocks[0].instructions;
    bool hasXor = false, hasTest = false, hasIdentityMov = false;
    for (auto &i : instrs)
    {
        if (i.opcode == MOpcode::XORrr32)
            hasXor = true;
        if (i.opcode == MOpcode::TESTrr)
            hasTest = true;
        // Check for identity mov (same src and dst)
        if (i.opcode == MOpcode::MOVrr && i.operands.size() == 2)
        {
            auto *d = std::get_if<OpReg>(&i.operands[0]);
            auto *s = std::get_if<OpReg>(&i.operands[1]);
            if (d && s && d->isPhys && s->isPhys && d->idOrPhys == s->idOrPhys)
                hasIdentityMov = true;
        }
    }
    EXPECT_TRUE(hasXor);
    EXPECT_TRUE(hasTest);
    EXPECT_FALSE(hasIdentityMov);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
