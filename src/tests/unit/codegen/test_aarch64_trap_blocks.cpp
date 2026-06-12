//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_aarch64_trap_blocks.cpp
// Purpose: Unit tests for per-function shared trap blocks and the
//          MFunction::blocks no-reallocation invariant in AArch64 lowering.
//          Previously every guarded instruction appended its own trap block,
//          and the lowering relied on a fixed reserve(+1024) to keep
//          MBasicBlock references stable — a function with enough guarded
//          sites or switch cases silently invalidated live references (UB).
//
// Key invariants:
//   - One trap block per trap kind per function, regardless of site count.
//   - Lowering a function with a very large switch completes without
//     triggering the capacity ICE (the auxiliary-block budget is computed
//     from the IL rather than guessed).
//
// Ownership/Lifetime:
//   - Standalone test binary.
//
// Links: src/codegen/aarch64/LowerILToMIR.cpp,
//        src/codegen/aarch64/LoweringContext.hpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/LowerILToMIR.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace viper::codegen::aarch64;

namespace {

std::size_t countBlocksNamed(const MFunction &mir, const std::string &name) {
    return static_cast<std::size_t>(
        std::count_if(mir.blocks.begin(), mir.blocks.end(), [&](const MBasicBlock &bb) {
            return bb.name == name;
        }));
}

} // namespace

// ---------------------------------------------------------------------------
// Several guarded divisions and bounds checks share one trap block per kind.
// ---------------------------------------------------------------------------
TEST(AArch64TrapBlocks, SharedPerKindAcrossSites) {
    using namespace il::core;

    Function fn;
    fn.name = "many_guards";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    unsigned nextTemp = 0;
    auto makeDiv = [&nextTemp]() {
        Instr div;
        div.result = nextTemp++;
        div.op = Opcode::SDivChk0;
        div.type = Type(Type::Kind::I64);
        div.operands = {Value::constInt(40), Value::constInt(4)};
        return div;
    };
    auto makeIdxChk = [&nextTemp]() {
        Instr chk;
        chk.result = nextTemp++;
        chk.op = Opcode::IdxChk;
        chk.type = Type(Type::Kind::I64);
        chk.operands = {Value::constInt(3), Value::constInt(0), Value::constInt(10)};
        return chk;
    };

    entry.instructions = {makeDiv(), makeDiv(), makeDiv(), makeIdxChk(), makeIdxChk()};

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(0)};
    entry.instructions.push_back(ret);

    fn.blocks = {entry};

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);

    // Three checked divisions and two bounds checks must produce exactly one
    // trap block per kind, not one per site.
    EXPECT_EQ(countBlocksNamed(mir, ".Ltrap_div0"), 1u);
    EXPECT_EQ(countBlocksNamed(mir, ".Ltrap_ovf"), 1u);
    EXPECT_EQ(countBlocksNamed(mir, ".Ltrap_bounds"), 1u);

    // Each guard still branches to the shared label.
    std::size_t div0Branches = 0;
    std::size_t boundsBranches = 0;
    for (const auto &bb : mir.blocks) {
        for (const auto &mi : bb.instrs) {
            if (mi.opc != MOpcode::BCond || mi.ops.size() < 2 ||
                mi.ops[1].kind != MOperand::Kind::Label)
                continue;
            if (mi.ops[1].label == ".Ltrap_div0")
                ++div0Branches;
            if (mi.ops[1].label == ".Ltrap_bounds")
                ++boundsBranches;
        }
    }
    EXPECT_EQ(div0Branches, 3u);
    EXPECT_GE(boundsBranches, 2u);
}

// ---------------------------------------------------------------------------
// A switch large enough to overflow the old fixed reserve(+1024) lowers
// cleanly: the auxiliary-block budget is computed from the IL, and the
// no-reallocation ICE stays quiet.
// ---------------------------------------------------------------------------
TEST(AArch64TrapBlocks, LargeSwitchStaysWithinBlockBudget) {
    using namespace il::core;

    constexpr std::size_t kCases = 600;

    Function fn;
    fn.name = "big_switch";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    Instr sw;
    sw.op = Opcode::SwitchI32;
    sw.type = Type(Type::Kind::Void);
    sw.operands.push_back(Value::constInt(3)); // scrutinee
    sw.labels.push_back("default");
    for (std::size_t i = 0; i < kCases; ++i) {
        sw.operands.push_back(Value::constInt(static_cast<long long>(i)));
        sw.labels.push_back("case" + std::to_string(i));
    }
    sw.brArgs.assign(sw.labels.size(), {});
    entry.instructions = {sw};
    fn.blocks.push_back(entry);

    auto makeRetBlock = [](std::string label, long long value) {
        BasicBlock bb;
        bb.label = std::move(label);
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::constInt(value)};
        bb.instructions = {ret};
        return bb;
    };

    fn.blocks.push_back(makeRetBlock("default", -1));
    for (std::size_t i = 0; i < kCases; ++i)
        fn.blocks.push_back(makeRetBlock("case" + std::to_string(i), static_cast<long long>(i)));

    LowerILToMIR lowering{linuxTarget()};
    const MFunction mir = lowering.lowerFunction(fn);

    // All IL blocks survive plus the switch tree's auxiliary blocks.
    EXPECT_GE(mir.blocks.size(), kCases + 2u);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
