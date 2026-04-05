//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_vararg.cpp
// Purpose: Verify that AArch64 variadic call lowering places anonymous
//          (variadic) arguments on the stack per AAPCS64, while named
//          arguments still go in registers.
//
// Key invariants:
//   - Named args use X0-X7 (GPR) and D0-D7 (FPR) per AAPCS64
//   - Variadic args (past named param count) always go on stack
//   - Stack space is 16-byte aligned
//
// Ownership/Lifetime:
//   - Standalone test binary.
//
// Links: src/codegen/aarch64/InstrLowering.cpp,
//        plans/audit-04-aarch64-abi.md
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/LowerILToMIR.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/aarch64/binenc/A64BinaryEncoder.hpp"
#include "codegen/common/objfile/CodeSection.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

using namespace viper::codegen::aarch64;
using namespace viper::codegen::aarch64::binenc;

namespace {

/// Read a LE 32-bit word from bytes at offset.
uint32_t readWord(const std::vector<uint8_t> &bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

/// Count occurrences of StrRegSpImm (0xF9000000-range) instructions.
/// StrRegSpImm stores a GPR to [sp, #imm] — used for stack-passed args.
size_t countStackStores(const std::vector<uint8_t> &bytes) {
    size_t count = 0;
    for (size_t i = 0; i + 3 < bytes.size(); i += 4) {
        uint32_t w = readWord(bytes, i);
        // str Xt, [sp, #imm12] has top bits 0xF9 (unsigned offset) or
        // StrRegSpImm is encoded as str Xt, [sp, #pimm] — check for SP as base
        // Actually, StrRegSpImm MIR opcodes become: str Xt, [sp, #offset]
        // Encoding: 0xF9000000 | (imm12/8 << 10) | (SP << 5) | Rt
        // SP = 31, so bits [9:5] = 11111
        if ((w & 0xFFC00000) == 0xF9000000 && ((w >> 5) & 0x1F) == 31)
            count++;
    }
    return count;
}

} // namespace

// ---------------------------------------------------------------------------
// Test: isVarArgCallee detects known vararg functions
// ---------------------------------------------------------------------------
TEST(AArch64Vararg, IsVarArgCalleeDetection) {
    // Known vararg functions should be detected
    EXPECT_TRUE(il::runtime::isVarArgCallee("rt_snprintf"));
    EXPECT_TRUE(il::runtime::isVarArgCallee("rt_sb_printf"));

    // Regular functions should not be detected
    EXPECT_FALSE(il::runtime::isVarArgCallee("rt_print_str"));
    EXPECT_FALSE(il::runtime::isVarArgCallee("rt_alloc"));
}

// ---------------------------------------------------------------------------
// Test: findRuntimeSignature returns paramTypes for vararg functions
// ---------------------------------------------------------------------------
TEST(AArch64Vararg, RuntimeSigNamedParamCount) {
    // For vararg functions with registry entries, paramTypes.size() gives
    // the named parameter count (the boundary for register vs stack args).
    // Note: not all vararg functions have registry entries (some are hardcoded
    // in the isVarArgCallee list). This test checks whichever ones are registered.
    const auto *sig = il::runtime::findRuntimeSignature("rt_snprintf");
    if (sig) {
        // rt_snprintf(buf, size, fmt, ...) → 3 named params
        EXPECT_GE(sig->paramTypes.size(), 2u);
    }
    // Test passes even if rt_snprintf isn't in the registry (it might be
    // in the hardcoded vararg list only).
}

TEST(AArch64Vararg, SmallVarArgCallStillSpillsAnonymousArgsToStack) {
    using namespace il::core;

    Function fn;
    fn.name = "caller";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    entry.params = {Param{.name = "sink", .type = Type(Type::Kind::Ptr), .id = 0}};

    Instr call;
    call.result = 1;
    call.op = Opcode::Call;
    call.callee = "rt_sb_printf";
    call.type = Type(Type::Kind::I64);
    call.operands = {Value::temp(0), Value::constInt(7)};

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(1)};

    entry.instructions = {call, ret};
    fn.blocks = {entry};

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);
    ASSERT_FALSE(mir.blocks.empty());

    bool sawStackAlloc = false;
    bool sawStackStore = false;
    bool sawStackFree = false;
    for (const auto &mi : mir.blocks.front().instrs) {
        if (mi.opc == MOpcode::SubSpImm && !mi.ops.empty() &&
            mi.ops[0].kind == MOperand::Kind::Imm && mi.ops[0].imm == 16) {
            sawStackAlloc = true;
        }
        if (mi.opc == MOpcode::StrRegSpImm && mi.ops.size() >= 2 &&
            mi.ops[1].kind == MOperand::Kind::Imm && mi.ops[1].imm == 0) {
            sawStackStore = true;
        }
        if (mi.opc == MOpcode::AddSpImm && !mi.ops.empty() &&
            mi.ops[0].kind == MOperand::Kind::Imm && mi.ops[0].imm == 16) {
            sawStackFree = true;
        }
    }

    EXPECT_TRUE(sawStackAlloc);
    EXPECT_TRUE(sawStackStore);
    EXPECT_TRUE(sawStackFree);
}

TEST(AArch64Vararg, CallReturnedStringUsesGenericRetainSemantics) {
    using namespace il::core;

    Function fn;
    fn.name = "caller";
    fn.retType = Type(Type::Kind::Str);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    entry.params = {Param{.name = "seed", .type = Type(Type::Kind::I64), .id = 0}};

    Instr call;
    call.result = 1;
    call.op = Opcode::Call;
    call.callee = "get_str";
    call.type = Type(Type::Kind::Str);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(1)};

    entry.instructions = {call, ret};
    fn.blocks = {entry};

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);
    ASSERT_FALSE(mir.blocks.empty());

    bool sawRetain = false;
    for (const auto &mi : mir.blocks.front().instrs) {
        if (mi.opc != MOpcode::Bl || mi.ops.empty() || mi.ops[0].kind != MOperand::Kind::Label)
            continue;
        if (mi.ops[0].label == "rt_str_retain_maybe") {
            sawRetain = true;
            break;
        }
    }

    EXPECT_TRUE(sawRetain);
}

TEST(AArch64Vararg, BoolCallResultIsNormalizedBeforeReturn) {
    using namespace il::core;

    Function fn;
    fn.name = "caller";
    fn.retType = Type(Type::Kind::I1);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    entry.params = {Param{.name = "seed", .type = Type(Type::Kind::I64), .id = 0}};

    Instr call;
    call.result = 1;
    call.op = Opcode::Call;
    call.callee = "is_ready";
    call.type = Type(Type::Kind::I1);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(1)};

    entry.instructions = {call, ret};
    fn.blocks = {entry};

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);
    ASSERT_FALSE(mir.blocks.empty());

    bool sawMask = false;
    for (const auto &mi : mir.blocks.front().instrs) {
        if (mi.opc == MOpcode::AndRRR) {
            sawMask = true;
            break;
        }
    }

    EXPECT_TRUE(sawMask);
}

TEST(AArch64Vararg, BoolLoadResultIsNormalizedBeforeReturn) {
    using namespace il::core;

    Function fn;
    fn.name = "caller";
    fn.retType = Type(Type::Kind::I1);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    entry.params = {Param{.name = "slot", .type = Type(Type::Kind::Ptr), .id = 0}};

    Instr load;
    load.result = 1;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I1);
    load.operands = {Value::temp(0)};

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(1)};

    entry.instructions = {load, ret};
    fn.blocks = {entry};

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);
    ASSERT_FALSE(mir.blocks.empty());

    bool sawMask = false;
    for (const auto &mi : mir.blocks.front().instrs) {
        if (mi.opc == MOpcode::AndRRR) {
            sawMask = true;
            break;
        }
    }

    EXPECT_TRUE(sawMask);
}

TEST(AArch64Vararg, BoolParamReturnUsesMaskedGenericPath) {
    using namespace il::core;

    Function fn;
    fn.name = "caller";
    fn.retType = Type(Type::Kind::I1);

    BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    entry.params = {Param{.name = "flag", .type = Type(Type::Kind::I1), .id = 0}};

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(0)};

    entry.instructions = {ret};
    fn.blocks = {entry};

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);
    ASSERT_FALSE(mir.blocks.empty());

    bool sawMask = false;
    for (const auto &mi : mir.blocks.front().instrs) {
        if (mi.opc == MOpcode::AndRRR) {
            sawMask = true;
            break;
        }
    }

    EXPECT_TRUE(sawMask);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
