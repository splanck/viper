//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_x86_isel_lea_mul.cpp
// Purpose: Verify that ISel::lowerArithmetic transforms unchecked IMUL by
//          small constants (3, 5, 9) into LEA instructions on x86-64.
//
// Background:
//   IMULrr dst, src  (where src == constant 3, 5, or 9)
//   can be replaced by:
//     LEA dst, [dst + dst*2]   (factor 3 = 1 + 2)
//     LEA dst, [dst + dst*4]   (factor 5 = 1 + 4)
//     LEA dst, [dst + dst*8]   (factor 9 = 1 + 8)
//
//   LEA avoids the multiply latency (3+ cycles) and does not touch flags,
//   whereas IMUL sets OF/CF. The transformation erases the MOVri that loaded
//   the constant when the register has exactly one use.
//
// Tests:
//   1. Factor 3  → LEA with scale=2
//   2. Factor 5  → LEA with scale=4
//   3. Factor 9  → LEA with scale=8
//   4. Factor 2  → no LEA (power-of-two; handled by peephole MUL→SHL, not ISel)
//   5. Factor 7  → no LEA (not a 1+2^k constant)
//   6. Multi-use constant → no LEA, MOVri retained
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/x86_64/ISel.hpp"
#include "codegen/x86_64/MachineIR.hpp"
#include "codegen/x86_64/OperandUtils.hpp"
#include "codegen/x86_64/TargetX64.hpp"

using namespace viper::codegen::x64;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

/// Build a minimal MFunction containing:
///   MOVri  vreg1, <factor>
///   IMULrr vreg2, vreg1
MFunction buildMulFunc(int64_t factor)
{
    MFunction fn{};
    fn.name = "test_mul";

    MBasicBlock block{};
    block.label = ".Lentry";

    // vreg1 = constant factor (id=1)
    const Operand constReg = makeVRegOperand(RegClass::GPR, 1);
    // vreg2 = multiply destination (id=2)
    const Operand dstReg = makeVRegOperand(RegClass::GPR, 2);

    block.instructions.push_back(MInstr::make(MOpcode::MOVri, {constReg, makeImmOperand(factor)}));
    block.instructions.push_back(MInstr::make(MOpcode::IMULrr, {dstReg, constReg}));

    fn.blocks.push_back(std::move(block));
    return fn;
}

/// Build a MFunction where the constant register has two uses.
MFunction buildMultiUseMulFunc(int64_t factor)
{
    MFunction fn{};
    fn.name = "test_mul_multiuse";

    MBasicBlock block{};
    block.label = ".Lentry";

    const Operand constReg = makeVRegOperand(RegClass::GPR, 1);
    const Operand dstReg = makeVRegOperand(RegClass::GPR, 2);
    const Operand dst2Reg = makeVRegOperand(RegClass::GPR, 3);

    // MOVri vreg1, factor
    block.instructions.push_back(MInstr::make(MOpcode::MOVri, {constReg, makeImmOperand(factor)}));
    // IMULrr vreg2, vreg1   ← first use of vreg1
    block.instructions.push_back(MInstr::make(MOpcode::IMULrr, {dstReg, constReg}));
    // IMULrr vreg3, vreg1   ← second use of vreg1 (multi-use)
    block.instructions.push_back(MInstr::make(MOpcode::IMULrr, {dst2Reg, constReg}));

    fn.blocks.push_back(std::move(block));
    return fn;
}

bool hasOpcode(const MFunction &fn, MOpcode op)
{
    for (const auto &block : fn.blocks)
        for (const auto &instr : block.instructions)
            if (instr.opcode == op)
                return true;
    return false;
}

/// Count total occurrences of an opcode in a function.
int countOpcode(const MFunction &fn, MOpcode op)
{
    int n = 0;
    for (const auto &block : fn.blocks)
        for (const auto &instr : block.instructions)
            if (instr.opcode == op)
                ++n;
    return n;
}

/// Return the scale used in the LEA memory operand, or 0 if not found.
uint8_t leaScale(const MFunction &fn)
{
    for (const auto &block : fn.blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.opcode != MOpcode::LEA || instr.operands.size() < 2)
                continue;
            if (const auto *mem = std::get_if<OpMem>(&instr.operands[1]))
                return mem->scale;
        }
    }
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Test 1: IMULrr by 3 → LEA with scale=2
// ---------------------------------------------------------------------------
TEST(X86ISelLeaMul, Factor3ToLeaScale2)
{
    auto fn = buildMulFunc(3);
    ISel isel{sysvTarget()};
    isel.lowerArithmetic(fn);

    // IMULrr should be gone; a LEA should be present
    EXPECT_FALSE(hasOpcode(fn, MOpcode::IMULrr));
    EXPECT_TRUE(hasOpcode(fn, MOpcode::LEA));

    // The LEA should use scale=2 ([dst + dst*2] = dst*3)
    EXPECT_TRUE(leaScale(fn) == 2);

    // The MOVri loading the constant should be erased
    EXPECT_FALSE(hasOpcode(fn, MOpcode::MOVri));
}

// ---------------------------------------------------------------------------
// Test 2: IMULrr by 5 → LEA with scale=4
// ---------------------------------------------------------------------------
TEST(X86ISelLeaMul, Factor5ToLeaScale4)
{
    auto fn = buildMulFunc(5);
    ISel isel{sysvTarget()};
    isel.lowerArithmetic(fn);

    EXPECT_FALSE(hasOpcode(fn, MOpcode::IMULrr));
    EXPECT_TRUE(hasOpcode(fn, MOpcode::LEA));
    EXPECT_TRUE(leaScale(fn) == 4);
    EXPECT_FALSE(hasOpcode(fn, MOpcode::MOVri));
}

// ---------------------------------------------------------------------------
// Test 3: IMULrr by 9 → LEA with scale=8
// ---------------------------------------------------------------------------
TEST(X86ISelLeaMul, Factor9ToLeaScale8)
{
    auto fn = buildMulFunc(9);
    ISel isel{sysvTarget()};
    isel.lowerArithmetic(fn);

    EXPECT_FALSE(hasOpcode(fn, MOpcode::IMULrr));
    EXPECT_TRUE(hasOpcode(fn, MOpcode::LEA));
    EXPECT_TRUE(leaScale(fn) == 8);
    EXPECT_FALSE(hasOpcode(fn, MOpcode::MOVri));
}

// ---------------------------------------------------------------------------
// Test 4: IMULrr by 2 → NOT transformed to LEA (power-of-2 → peephole SHL)
// ---------------------------------------------------------------------------
TEST(X86ISelLeaMul, Factor2NoLea)
{
    auto fn = buildMulFunc(2);
    ISel isel{sysvTarget()};
    isel.lowerArithmetic(fn);

    // ISel should not produce LEA for factor=2; peephole handles this as SHL
    EXPECT_FALSE(hasOpcode(fn, MOpcode::LEA));
    EXPECT_TRUE(hasOpcode(fn, MOpcode::IMULrr));
}

// ---------------------------------------------------------------------------
// Test 5: IMULrr by 7 → NOT transformed (7 != 1+2^k for any k)
// ---------------------------------------------------------------------------
TEST(X86ISelLeaMul, Factor7NoLea)
{
    auto fn = buildMulFunc(7);
    ISel isel{sysvTarget()};
    isel.lowerArithmetic(fn);

    EXPECT_FALSE(hasOpcode(fn, MOpcode::LEA));
    EXPECT_TRUE(hasOpcode(fn, MOpcode::IMULrr));
}

// ---------------------------------------------------------------------------
// Test 6: Multi-use constant → IMUL kept, MOVri not erased
// ---------------------------------------------------------------------------
TEST(X86ISelLeaMul, MultiUseConstantNotFolded)
{
    auto fn = buildMultiUseMulFunc(3);
    ISel isel{sysvTarget()};
    isel.lowerArithmetic(fn);

    // vreg1 is used by two IMULrr instructions — the MOVri must stay
    EXPECT_TRUE(hasOpcode(fn, MOpcode::MOVri));
    // Both IMULrr should remain untouched
    EXPECT_TRUE(countOpcode(fn, MOpcode::IMULrr) == 2);
    // No LEA should be produced
    EXPECT_FALSE(hasOpcode(fn, MOpcode::LEA));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
