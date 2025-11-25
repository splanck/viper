//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/core/test_MemoryEffects.cpp
// Purpose: Validate opcode memory effect classification helpers.
// Key invariants: Pure arithmetic remains memory-free; loads/stores/calls conservatively marked.
// Ownership/Lifetime: Test constructs transient function objects on the stack.
// Links: src/il/core/OpcodeInfo.hpp
//
//===----------------------------------------------------------------------===//

#include "../../GTestStub.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

using il::core::BasicBlock;
using il::core::Function;
using il::core::Instr;
using il::core::MemoryEffects;
using il::core::Opcode;
using il::core::Type;
using il::core::Value;

namespace
{

[[nodiscard]] Function makeProbeFunction()
{
    Function fn;
    fn.name = "memory_effects";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";

    Instr load;
    load.result = 0U;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.operands.push_back(Value::temp(1));
    entry.instructions.push_back(std::move(load));

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::Void);
    store.operands.push_back(Value::temp(2));
    store.operands.push_back(Value::temp(3));
    entry.instructions.push_back(std::move(store));

    Instr add;
    add.result = 1U;
    add.op = Opcode::Add;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::temp(4));
    add.operands.push_back(Value::temp(5));
    entry.instructions.push_back(std::move(add));

    Instr sub;
    sub.result = 2U;
    sub.op = Opcode::Sub;
    sub.type = Type(Type::Kind::I64);
    sub.operands.push_back(Value::temp(6));
    sub.operands.push_back(Value::temp(7));
    entry.instructions.push_back(std::move(sub));

    Instr cmp;
    cmp.result = 3U;
    cmp.op = Opcode::ICmpEq;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands.push_back(Value::temp(8));
    cmp.operands.push_back(Value::temp(9));
    entry.instructions.push_back(std::move(cmp));

    Instr call;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::Void);
    call.callee = "callee";
    call.operands.push_back(Value::global("callee"));
    entry.instructions.push_back(std::move(call));

    fn.blocks.push_back(std::move(entry));
    return fn;
}

} // namespace

TEST(MemoryEffects, ClassifiesRepresentativeOpcodes)
{
    const Function fn = makeProbeFunction();
    const auto &instructions = fn.blocks.front().instructions;

    ASSERT_EQ(instructions.size(), 6U);

    EXPECT_TRUE(hasMemoryRead(instructions[0].op));
    EXPECT_FALSE(hasMemoryWrite(instructions[0].op));

    EXPECT_TRUE(hasMemoryWrite(instructions[1].op));

    EXPECT_EQ(memoryEffects(instructions[2].op), MemoryEffects::None);
    EXPECT_EQ(memoryEffects(instructions[3].op), MemoryEffects::None);
    EXPECT_EQ(memoryEffects(instructions[4].op), MemoryEffects::None);

    EXPECT_TRUE(hasMemoryRead(instructions[5].op));
    EXPECT_TRUE(hasMemoryWrite(instructions[5].op));
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
