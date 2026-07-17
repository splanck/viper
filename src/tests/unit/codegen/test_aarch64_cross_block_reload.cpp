//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_cross_block_reload.cpp
// Purpose: Regression tests for AArch64 cross-block temp reload lowering.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/LowerILToMIR.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <algorithm>

using namespace zanna::codegen::aarch64;

TEST(AArch64CrossBlockReload, ReloadsSharedTempOncePerSuccessorBlock) {
    using namespace il::core;

    Function fn;
    fn.name = "reload_once";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    Instr makeShared;
    makeShared.result = 0;
    makeShared.op = Opcode::Add;
    makeShared.type = Type(Type::Kind::I64);
    makeShared.operands = {Value::constInt(40), Value::constInt(2)};

    Instr branch;
    branch.op = Opcode::Br;
    branch.type = Type(Type::Kind::Void);
    branch.labels = {"use"};
    branch.brArgs = {{}};

    entry.instructions = {makeShared, branch};

    BasicBlock use;
    use.label = "use";
    use.terminated = true;

    Instr plus;
    plus.result = 1;
    plus.op = Opcode::Add;
    plus.type = Type(Type::Kind::I64);
    plus.operands = {Value::temp(0), Value::constInt(1)};

    Instr minus;
    minus.result = 2;
    minus.op = Opcode::Sub;
    minus.type = Type(Type::Kind::I64);
    minus.operands = {Value::temp(0), Value::constInt(1)};

    Instr sum;
    sum.result = 3;
    sum.op = Opcode::Add;
    sum.type = Type(Type::Kind::I64);
    sum.operands = {Value::temp(1), Value::temp(2)};

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(3)};

    use.instructions = {plus, minus, sum, ret};
    fn.blocks = {entry, use};

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);

    ASSERT_EQ(mir.blocks.size(), 2u);
    const auto useIt = std::find_if(mir.blocks.begin(),
                                    mir.blocks.end(),
                                    [](const MBasicBlock &bb) { return bb.name == "use"; });
    ASSERT_TRUE(useIt != mir.blocks.end());

    std::size_t reloadCount = 0;
    uint16_t reloadVReg = 0;
    for (const auto &mi : useIt->instrs) {
        if (mi.opc != MOpcode::LdrRegFpImm || mi.ops.empty())
            continue;
        ASSERT_EQ(mi.ops[0].kind, MOperand::Kind::Reg);
        ASSERT_FALSE(mi.ops[0].reg.isPhys);
        reloadVReg = mi.ops[0].reg.idOrPhys;
        ++reloadCount;
    }

    EXPECT_EQ(reloadCount, 1u);

    std::size_t sharedUseCount = 0;
    for (const auto &mi : useIt->instrs) {
        if (mi.opc == MOpcode::LdrRegFpImm)
            continue;
        for (const auto &op : mi.ops) {
            if (op.kind == MOperand::Kind::Reg && !op.reg.isPhys && op.reg.idOrPhys == reloadVReg)
                ++sharedUseCount;
        }
    }

    EXPECT_GE(sharedUseCount, 2u);
}

TEST(AArch64CrossBlockReload, ReloadsTempsUsedOnlyInBranchArguments) {
    using namespace il::core;

    Function fn;
    fn.name = "reload_branch_args";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    Instr makeShared;
    makeShared.result = 0;
    makeShared.op = Opcode::Add;
    makeShared.type = Type(Type::Kind::I64);
    makeShared.operands = {Value::constInt(20), Value::constInt(22)};

    Instr toMid;
    toMid.op = Opcode::Br;
    toMid.type = Type(Type::Kind::Void);
    toMid.labels = {"mid"};
    toMid.brArgs = {{}};

    entry.instructions = {makeShared, toMid};

    BasicBlock mid;
    mid.label = "mid";
    mid.terminated = true;

    Instr toUse;
    toUse.op = Opcode::Br;
    toUse.type = Type(Type::Kind::Void);
    toUse.labels = {"use"};
    toUse.brArgs = {{Value::temp(0)}};

    mid.instructions = {toUse};

    BasicBlock use;
    use.label = "use";
    use.params = {Param{.name = "x", .type = Type(Type::Kind::I64), .id = 1}};
    use.terminated = true;

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(1)};

    use.instructions = {ret};
    fn.blocks = {entry, mid, use};

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);

    const auto midIt = std::find_if(mir.blocks.begin(),
                                    mir.blocks.end(),
                                    [](const MBasicBlock &bb) { return bb.name == "mid"; });
    ASSERT_TRUE(midIt != mir.blocks.end());

    std::size_t reloadCount = 0;
    for (const auto &mi : midIt->instrs) {
        if (mi.opc == MOpcode::LdrRegFpImm)
            ++reloadCount;
    }

    EXPECT_EQ(reloadCount, 1u);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, &argv);
    return zanna_test::run_all_tests();
}
