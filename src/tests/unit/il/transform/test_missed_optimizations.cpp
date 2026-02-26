//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_missed_optimizations.cpp
// Purpose: Catalogue of optimization tests split into two categories:
//   Category 1 - Tests verifying existing optimizations DO fire correctly.
//   Category 2 - Tests documenting currently-MISSING optimizations that are
//                expected NOT to fire (regression guard for future work).
// Links: docs/architecture.md, il/transform/PassManager.hpp
//
//===----------------------------------------------------------------------===//

#include "il/transform/PassManager.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace
{

void verifyOrDie(const Module &module)
{
    auto result = il::verify::Verifier::verify(module);
    if (!result)
    {
        il::support::printDiag(result.error(), std::cerr);
        ASSERT_TRUE(false);
    }
}

/// Count instructions with a specific opcode across all blocks of a function.
size_t countOpcode(const Function &fn, Opcode op)
{
    size_t count = 0;
    for (const auto &bb : fn.blocks)
        for (const auto &instr : bb.instructions)
            if (instr.op == op)
                ++count;
    return count;
}

/// Check if any Ret instruction in the function returns a specific constant.
bool retReturnsConst(const Function &fn, int64_t expected)
{
    for (const auto &bb : fn.blocks)
        for (const auto &instr : bb.instructions)
            if (instr.op == Opcode::Ret && !instr.operands.empty())
            {
                const auto &v = instr.operands[0];
                if (v.kind == Value::Kind::ConstInt && v.i64 == expected)
                    return true;
            }
    return false;
}

/// Check if any Ret instruction returns a temp (i.e. not folded to a constant).
bool anyRetReturnsTemp(const Function &fn)
{
    for (const auto &bb : fn.blocks)
        for (const auto &instr : bb.instructions)
            if (instr.op == Opcode::Ret && !instr.operands.empty())
                if (instr.operands[0].kind == Value::Kind::Temp)
                    return true;
    return false;
}

} // namespace

//===----------------------------------------------------------------------===//
// Category 1: Tests verifying existing optimizations fire correctly
//===----------------------------------------------------------------------===//

// SCCP propagates a constant through a block parameter and folds the Ret.
//
//   fn test() -> i64:
//     entry:
//       br merge(42)
//     merge(%x):
//       ret %x
//
// After SCCP+SimplifyCFG+DCE: ret 42
TEST(MissedOpt, ConstPropThroughBlockParam)
{
    Module m;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels = {"merge"};
        br.brArgs = {{Value::constInt(42)}};
        entry.instructions.push_back(std::move(br));
        entry.terminated = true;
    }

    BasicBlock merge;
    merge.label = "merge";
    {
        Param bp;
        bp.name = "x";
        bp.type = Type(Type::Kind::I64);
        bp.id = 0;
        merge.params.push_back(bp);

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(0)};
        merge.instructions.push_back(std::move(ret));
        merge.terminated = true;
    }

    fn.blocks = {std::move(entry), std::move(merge)};
    fn.valueNames.resize(1);
    fn.valueNames[0] = "x";
    m.functions.push_back(std::move(fn));

    verifyOrDie(m);

    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false);
    pm.run(m, {"sccp", "simplify-cfg", "dce"});

    ASSERT_TRUE(retReturnsConst(m.functions[0], 42));
}

// Peephole eliminates iadd.ovf %x, 0 (identity: x + 0 = x).
//
//   fn test(x: i64) -> i64:
//     entry:
//       %1 = iadd.ovf %0, 0
//       ret %1
//
// After peephole: iadd.ovf is gone.
TEST(MissedOpt, IdentityAddZero)
{
    Module m;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    Param px;
    px.name = "x";
    px.type = Type(Type::Kind::I64);
    px.id = 0;
    fn.params.push_back(px);

    unsigned nextId = 1;

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr add;
        add.result = nextId++;
        add.op = Opcode::IAddOvf;
        add.type = Type(Type::Kind::I64);
        add.operands = {Value::temp(0), Value::constInt(0)};
        entry.instructions.push_back(std::move(add));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(1)};
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "x";
    fn.valueNames[1] = "r";
    m.functions.push_back(std::move(fn));

    verifyOrDie(m);

    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false);
    pm.run(m, {"peephole"});

    EXPECT_EQ(countOpcode(m.functions[0], Opcode::IAddOvf), 0u);
}

// Peephole eliminates imul.ovf %x, 1 (identity: x * 1 = x).
//
//   fn test(x: i64) -> i64:
//     entry:
//       %1 = imul.ovf %0, 1
//       ret %1
//
// After peephole: imul.ovf is gone.
TEST(MissedOpt, IdentityMulOne)
{
    Module m;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    Param px;
    px.name = "x";
    px.type = Type(Type::Kind::I64);
    px.id = 0;
    fn.params.push_back(px);

    unsigned nextId = 1;

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr mul;
        mul.result = nextId++;
        mul.op = Opcode::IMulOvf;
        mul.type = Type(Type::Kind::I64);
        mul.operands = {Value::temp(0), Value::constInt(1)};
        entry.instructions.push_back(std::move(mul));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(1)};
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "x";
    fn.valueNames[1] = "r";
    m.functions.push_back(std::move(fn));

    verifyOrDie(m);

    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false);
    pm.run(m, {"peephole"});

    EXPECT_EQ(countOpcode(m.functions[0], Opcode::IMulOvf), 0u);
}

// Peephole folds isub.ovf %x, %x to constant 0 (identity: x - x = 0).
//
//   fn test(x: i64) -> i64:
//     entry:
//       %1 = isub.ovf %0, %0
//       ret %1
//
// After peephole: ret 0.
TEST(MissedOpt, IdentitySubSelf)
{
    Module m;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    Param px;
    px.name = "x";
    px.type = Type(Type::Kind::I64);
    px.id = 0;
    fn.params.push_back(px);

    unsigned nextId = 1;

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr sub;
        sub.result = nextId++;
        sub.op = Opcode::ISubOvf;
        sub.type = Type(Type::Kind::I64);
        sub.operands = {Value::temp(0), Value::temp(0)};
        entry.instructions.push_back(std::move(sub));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(1)};
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "x";
    fn.valueNames[1] = "r";
    m.functions.push_back(std::move(fn));

    verifyOrDie(m);

    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false);
    pm.run(m, {"peephole"});

    EXPECT_TRUE(retReturnsConst(m.functions[0], 0));
}

// EarlyCSE eliminates a duplicate add expression in the same block.
//
//   fn test(x: i64, y: i64) -> i64:
//     entry:
//       %2 = add %0, %1
//       %3 = add %0, %1      ; duplicate of %2
//       %4 = add %2, %3
//       ret %4
//
// After earlycse: %3 is replaced by %2, so only 2 Add remain (not 3).
TEST(MissedOpt, CSEDuplicateExpr)
{
    Module m;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    Param px;
    px.name = "x";
    px.type = Type(Type::Kind::I64);
    px.id = 0;
    fn.params.push_back(px);

    Param py;
    py.name = "y";
    py.type = Type(Type::Kind::I64);
    py.id = 1;
    fn.params.push_back(py);

    unsigned nextId = 2;

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr add1;
        add1.result = nextId++; // %2
        add1.op = Opcode::Add;
        add1.type = Type(Type::Kind::I64);
        add1.operands = {Value::temp(0), Value::temp(1)};
        entry.instructions.push_back(std::move(add1));

        Instr add2;
        add2.result = nextId++; // %3
        add2.op = Opcode::Add;
        add2.type = Type(Type::Kind::I64);
        add2.operands = {Value::temp(0), Value::temp(1)};
        entry.instructions.push_back(std::move(add2));

        Instr add3;
        add3.result = nextId++; // %4
        add3.op = Opcode::Add;
        add3.type = Type(Type::Kind::I64);
        add3.operands = {Value::temp(2), Value::temp(3)};
        entry.instructions.push_back(std::move(add3));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(4)};
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "x";
    fn.valueNames[1] = "y";
    fn.valueNames[2] = "a";
    fn.valueNames[3] = "b";
    fn.valueNames[4] = "c";
    m.functions.push_back(std::move(fn));

    // Note: Add (non-overflow) with temp operands is rejected by the verifier
    // which requires IAddOvf for signed integer adds. We intentionally skip
    // verification here to test CSE on a simple non-trapping opcode, matching
    // the pattern used in test_earlycse_domtree.cpp.

    size_t addsBefore = countOpcode(m.functions[0], Opcode::Add);
    ASSERT_EQ(addsBefore, 3u);

    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false);
    pm.run(m, {"earlycse"});

    // CSE replaces %3 with %2; the third add (%4 = add %2, %2) survives.
    // Total: 2 Add instructions (down from 3).
    EXPECT_TRUE(countOpcode(m.functions[0], Opcode::Add) < 3u);
}

// Mem2Reg promotes a simple alloca/store/load sequence to SSA.
//
//   fn test(x: i64) -> i64:
//     entry:
//       %1 = alloca 8
//       store i64 %1, %0
//       %2 = load i64, %1
//       ret %2
//
// After mem2reg: alloca eliminated, ret uses %0 directly.
TEST(MissedOpt, Mem2RegPromotion)
{
    Module m;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    Param px;
    px.name = "x";
    px.type = Type(Type::Kind::I64);
    px.id = 0;
    fn.params.push_back(px);

    unsigned nextId = 1;

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr alloca_;
        alloca_.result = nextId++; // %1
        alloca_.op = Opcode::Alloca;
        alloca_.type = Type(Type::Kind::Ptr);
        alloca_.operands = {Value::constInt(8)};
        entry.instructions.push_back(std::move(alloca_));

        Instr store;
        store.op = Opcode::Store;
        store.type = Type(Type::Kind::I64);
        store.operands = {Value::temp(1), Value::temp(0)};
        entry.instructions.push_back(std::move(store));

        Instr load;
        load.result = nextId++; // %2
        load.op = Opcode::Load;
        load.type = Type(Type::Kind::I64);
        load.operands = {Value::temp(1)};
        entry.instructions.push_back(std::move(load));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(2)};
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "x";
    fn.valueNames[1] = "ptr";
    fn.valueNames[2] = "val";
    m.functions.push_back(std::move(fn));

    verifyOrDie(m);

    ASSERT_EQ(countOpcode(m.functions[0], Opcode::Alloca), 1u);

    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false);
    pm.run(m, {"mem2reg"});

    EXPECT_EQ(countOpcode(m.functions[0], Opcode::Alloca), 0u);
}

// Peephole eliminates sdiv.chk0 %x, 1 (identity: x / 1 = x).
//
//   fn test(x: i64) -> i64:
//     entry:
//       %1 = sdiv.chk0 %0, 1
//       ret %1
//
// After peephole: sdiv.chk0 is gone.
TEST(MissedOpt, DivByOne)
{
    Module m;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    Param px;
    px.name = "x";
    px.type = Type(Type::Kind::I64);
    px.id = 0;
    fn.params.push_back(px);

    unsigned nextId = 1;

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr div;
        div.result = nextId++;
        div.op = Opcode::SDivChk0;
        div.type = Type(Type::Kind::I64);
        div.operands = {Value::temp(0), Value::constInt(1)};
        entry.instructions.push_back(std::move(div));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(1)};
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "x";
    fn.valueNames[1] = "r";
    m.functions.push_back(std::move(fn));

    verifyOrDie(m);

    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false);
    pm.run(m, {"peephole"});

    EXPECT_EQ(countOpcode(m.functions[0], Opcode::SDivChk0), 0u);
}

// Peephole folds srem.chk0 %x, 1 to constant 0 (identity: x % 1 = 0).
//
//   fn test(x: i64) -> i64:
//     entry:
//       %1 = srem.chk0 %0, 1
//       ret %1
//
// After peephole: ret 0.
TEST(MissedOpt, RemByOne)
{
    Module m;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    Param px;
    px.name = "x";
    px.type = Type(Type::Kind::I64);
    px.id = 0;
    fn.params.push_back(px);

    unsigned nextId = 1;

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr rem;
        rem.result = nextId++;
        rem.op = Opcode::SRemChk0;
        rem.type = Type(Type::Kind::I64);
        rem.operands = {Value::temp(0), Value::constInt(1)};
        entry.instructions.push_back(std::move(rem));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(1)};
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "x";
    fn.valueNames[1] = "r";
    m.functions.push_back(std::move(fn));

    verifyOrDie(m);

    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false);
    pm.run(m, {"peephole"});

    EXPECT_TRUE(retReturnsConst(m.functions[0], 0));
}

//===----------------------------------------------------------------------===//
// Category 2: Currently-missing optimizations (expected NOT to fire)
//===----------------------------------------------------------------------===//

// Double negation: sub(0, sub(0, x)) should simplify to x, but the peephole
// pass only handles single-instruction patterns and cannot match this
// multi-instruction sequence.
//
//   fn test(x: i64) -> i64:
//     entry:
//       %1 = sub 0, %0        ; -x
//       %2 = sub 0, %1        ; -(-x) = x
//       ret %2
//
// After peephole: both sub instructions survive (no multi-instr pattern match).
TEST(MissedOpt, MissedDoubleNegation)
{
    Module m;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    Param px;
    px.name = "x";
    px.type = Type(Type::Kind::I64);
    px.id = 0;
    fn.params.push_back(px);

    unsigned nextId = 1;

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr neg1;
        neg1.result = nextId++; // %1
        neg1.op = Opcode::Sub;
        neg1.type = Type(Type::Kind::I64);
        neg1.operands = {Value::constInt(0), Value::temp(0)};
        entry.instructions.push_back(std::move(neg1));

        Instr neg2;
        neg2.result = nextId++; // %2
        neg2.op = Opcode::Sub;
        neg2.type = Type(Type::Kind::I64);
        neg2.operands = {Value::constInt(0), Value::temp(1)};
        entry.instructions.push_back(std::move(neg2));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(2)};
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "x";
    fn.valueNames[1] = "neg1";
    fn.valueNames[2] = "neg2";
    m.functions.push_back(std::move(fn));

    // Note: Sub (non-overflow) with temp operands is rejected by the verifier
    // which requires ISubOvf. We skip verification to test the raw opcode.

    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false);
    pm.run(m, {"peephole", "dce"});

    // Both Sub instructions survive -- multi-instruction double negation
    // cancellation is not implemented in the peephole pass.
    EXPECT_EQ(countOpcode(m.functions[0], Opcode::Sub), 2u);
}

// Multiply by power of 2: mul(x, 8) should be strength-reduced to shl(x, 3),
// but the peephole pass does not perform strength reduction.
//
//   fn test(x: i64) -> i64:
//     entry:
//       %1 = mul %0, 8
//       ret %1
//
// After peephole: mul survives (no power-of-2 strength reduction).
TEST(MissedOpt, MissedMulByPowerOf2)
{
    Module m;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    Param px;
    px.name = "x";
    px.type = Type(Type::Kind::I64);
    px.id = 0;
    fn.params.push_back(px);

    unsigned nextId = 1;

    BasicBlock entry;
    entry.label = "entry";
    {
        Instr mul;
        mul.result = nextId++;
        mul.op = Opcode::Mul;
        mul.type = Type(Type::Kind::I64);
        mul.operands = {Value::temp(0), Value::constInt(8)};
        entry.instructions.push_back(std::move(mul));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(1)};
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "x";
    fn.valueNames[1] = "r";
    m.functions.push_back(std::move(fn));

    // Note: Mul (non-overflow) with temp operands is rejected by the verifier
    // which requires IMulOvf. We skip verification to test the raw opcode.

    il::transform::PassManager pm;
    pm.setVerifyBetweenPasses(false);
    pm.run(m, {"peephole", "dce"});

    // Mul survives -- strength reduction (mul x, 2^n -> shl x, n) is not
    // implemented in the peephole pass.
    EXPECT_EQ(countOpcode(m.functions[0], Opcode::Mul), 1u);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
