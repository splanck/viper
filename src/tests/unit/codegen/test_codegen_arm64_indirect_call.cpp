//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_arm64_indirect_call.cpp
// Purpose: Verify AArch64 indirect call lowering uses BLR, preserves floating
//          argument lanes, and spills overflow arguments onto the stack.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/LowerILToMIR.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include <string>

TEST(Arm64IndirectCall, PointerCallUsesBlrRatherThanDirectBl) {
    using namespace il::core;
    using namespace viper::codegen::aarch64;

    Function fn;
    fn.name = "caller";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    Instr gaddr;
    gaddr.result = 0;
    gaddr.op = Opcode::GAddr;
    gaddr.type = Type(Type::Kind::Ptr);
    gaddr.operands = {Value::global("target")};

    Instr call;
    call.result = 1;
    call.op = Opcode::CallIndirect;
    call.type = Type(Type::Kind::I64);
    call.operands = {Value::temp(0)};

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(1)};

    entry.instructions = {gaddr, call, ret};
    fn.blocks = {entry};

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);
    ASSERT_FALSE(mir.blocks.empty());

    bool sawBlr = false;
    bool sawDirectBl = false;
    for (const auto &mi : mir.blocks.front().instrs) {
        if (mi.opc == MOpcode::Blr && !mi.ops.empty() && mi.ops[0].kind == MOperand::Kind::Reg &&
            mi.ops[0].reg.isPhys &&
            static_cast<PhysReg>(mi.ops[0].reg.idOrPhys) == PhysReg::X9) {
            sawBlr = true;
        }
        if (mi.opc == MOpcode::Bl)
            sawDirectBl = true;
    }

    EXPECT_TRUE(sawBlr);
    EXPECT_FALSE(sawDirectBl);
}

TEST(Arm64IndirectCall, FloatingArgumentsStayInFprLanes) {
    using namespace il::core;
    using namespace viper::codegen::aarch64;

    Function fn;
    fn.name = "caller";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    entry.params = {Param{.name = "a", .type = Type(Type::Kind::F64), .id = 0},
                    Param{.name = "b", .type = Type(Type::Kind::F64), .id = 1}};

    Instr gaddr;
    gaddr.result = 2;
    gaddr.op = Opcode::GAddr;
    gaddr.type = Type(Type::Kind::Ptr);
    gaddr.operands = {Value::global("target")};

    Instr call;
    call.result = 3;
    call.op = Opcode::CallIndirect;
    call.type = Type(Type::Kind::I64);
    call.operands = {Value::temp(2), Value::temp(1)};

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(3)};

    entry.instructions = {gaddr, call, ret};
    fn.blocks = {entry};

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);
    ASSERT_FALSE(mir.blocks.empty());

    bool sawFprMarshal = false;
    bool sawBlr = false;
    for (const auto &mi : mir.blocks.front().instrs) {
        if (mi.opc == MOpcode::FMovRR && mi.ops.size() >= 2 &&
            mi.ops[0].kind == MOperand::Kind::Reg && mi.ops[0].reg.isPhys &&
            static_cast<PhysReg>(mi.ops[0].reg.idOrPhys) == PhysReg::V0) {
            sawFprMarshal = true;
        }
        if (mi.opc == MOpcode::Blr && !mi.ops.empty() && mi.ops[0].kind == MOperand::Kind::Reg &&
            mi.ops[0].reg.isPhys &&
            static_cast<PhysReg>(mi.ops[0].reg.idOrPhys) == PhysReg::X9) {
            sawBlr = true;
        }
    }

    EXPECT_TRUE(sawFprMarshal);
    EXPECT_TRUE(sawBlr);
}

TEST(Arm64IndirectCall, OverflowArgumentsUseStackArea) {
    using namespace il::core;
    using namespace viper::codegen::aarch64;

    Function fn;
    fn.name = "caller";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    Instr gaddr;
    gaddr.result = 0;
    gaddr.op = Opcode::GAddr;
    gaddr.type = Type(Type::Kind::Ptr);
    gaddr.operands = {Value::global("target")};

    Instr call;
    call.result = 1;
    call.op = Opcode::CallIndirect;
    call.type = Type(Type::Kind::I64);
    call.operands = {Value::temp(0),
                     Value::constInt(1),
                     Value::constInt(2),
                     Value::constInt(3),
                     Value::constInt(4),
                     Value::constInt(5),
                     Value::constInt(6),
                     Value::constInt(7),
                     Value::constInt(8),
                     Value::constInt(9),
                     Value::constInt(10)};

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(1)};

    entry.instructions = {gaddr, call, ret};
    fn.blocks = {entry};

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);
    ASSERT_FALSE(mir.blocks.empty());

    bool sawStackAlloc = false;
    bool sawStore0 = false;
    bool sawStore8 = false;
    bool sawBlr = false;
    bool sawStackFree = false;
    for (const auto &mi : mir.blocks.front().instrs) {
        if (mi.opc == MOpcode::SubSpImm && !mi.ops.empty() && mi.ops[0].kind == MOperand::Kind::Imm &&
            mi.ops[0].imm == 16) {
            sawStackAlloc = true;
        }
        if (mi.opc == MOpcode::StrRegSpImm && mi.ops.size() >= 2 &&
            mi.ops[1].kind == MOperand::Kind::Imm) {
            if (mi.ops[1].imm == 0)
                sawStore0 = true;
            if (mi.ops[1].imm == 8)
                sawStore8 = true;
        }
        if (mi.opc == MOpcode::Blr)
            sawBlr = true;
        if (mi.opc == MOpcode::AddSpImm && !mi.ops.empty() && mi.ops[0].kind == MOperand::Kind::Imm &&
            mi.ops[0].imm == 16) {
            sawStackFree = true;
        }
    }

    EXPECT_TRUE(sawStackAlloc);
    EXPECT_TRUE(sawStore0);
    EXPECT_TRUE(sawStore8);
    EXPECT_TRUE(sawBlr);
    EXPECT_TRUE(sawStackFree);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
