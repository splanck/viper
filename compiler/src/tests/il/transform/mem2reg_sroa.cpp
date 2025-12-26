//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for the mem2reg pass with a conservative SROA-style split of small
// aggregates. Verifies that fixed-offset loads/stores are scalarised and that
// dynamic/gep-heavy cases are left untouched.
//
//===----------------------------------------------------------------------===//

#include "il/transform/Mem2Reg.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <cassert>

using namespace il::core;

static bool hasOp(const Function &F, Opcode op)
{
    for (const auto &B : F.blocks)
        for (const auto &I : B.instructions)
            if (I.op == op)
                return true;
    return false;
}

static Function makeTwoFieldAggregate()
{
    Function F;
    F.name = "two_fields";
    F.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    unsigned nextId = 0;

    Instr alloc;
    alloc.result = nextId++;
    alloc.op = Opcode::Alloca;
    alloc.type = Type(Type::Kind::Ptr);
    alloc.operands.push_back(Value::constInt(16));
    entry.instructions.push_back(std::move(alloc));

    Instr store0;
    store0.op = Opcode::Store;
    store0.type = Type(Type::Kind::I64);
    store0.operands.push_back(Value::temp(0));
    store0.operands.push_back(Value::constInt(1));
    entry.instructions.push_back(std::move(store0));

    Instr gep;
    gep.result = nextId++;
    gep.op = Opcode::GEP;
    gep.type = Type(Type::Kind::Ptr);
    gep.operands.push_back(Value::temp(0));
    gep.operands.push_back(Value::constInt(8));
    entry.instructions.push_back(std::move(gep));

    Instr store1;
    store1.op = Opcode::Store;
    store1.type = Type(Type::Kind::I64);
    store1.operands.push_back(Value::temp(1));
    store1.operands.push_back(Value::constInt(2));
    entry.instructions.push_back(std::move(store1));

    Instr load0;
    unsigned load0Id = nextId++;
    load0.result = load0Id;
    load0.op = Opcode::Load;
    load0.type = Type(Type::Kind::I64);
    load0.operands.push_back(Value::temp(0));
    entry.instructions.push_back(std::move(load0));

    Instr load1;
    unsigned load1Id = nextId++;
    load1.result = load1Id;
    load1.op = Opcode::Load;
    load1.type = Type(Type::Kind::I64);
    load1.operands.push_back(Value::temp(1));
    entry.instructions.push_back(std::move(load1));

    Instr add;
    add.result = nextId++;
    add.op = Opcode::Add;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::temp(load0Id));
    add.operands.push_back(Value::temp(load1Id));
    entry.instructions.push_back(std::move(add));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(nextId - 1));
    entry.instructions.push_back(std::move(ret));

    entry.terminated = true;
    F.blocks.push_back(std::move(entry));
    F.valueNames.resize(nextId);
    return F;
}

static Function makeDynamicGEP()
{
    Function F;
    F.name = "dynamic_gep";
    F.retType = Type(Type::Kind::I64);

    Param idx{"idx", Type(Type::Kind::I64), 0};
    F.params.push_back(idx);
    F.valueNames.resize(1);

    BasicBlock entry;
    entry.label = "entry";
    unsigned nextId = 1;

    Instr alloc;
    alloc.result = nextId++;
    alloc.op = Opcode::Alloca;
    alloc.type = Type(Type::Kind::Ptr);
    alloc.operands.push_back(Value::constInt(16));
    entry.instructions.push_back(std::move(alloc));

    Instr store0;
    store0.op = Opcode::Store;
    store0.type = Type(Type::Kind::I64);
    store0.operands.push_back(Value::temp(1));
    store0.operands.push_back(Value::constInt(5));
    entry.instructions.push_back(std::move(store0));

    Instr gep;
    gep.result = nextId++;
    gep.op = Opcode::GEP;
    gep.type = Type(Type::Kind::Ptr);
    gep.operands.push_back(Value::temp(1));
    gep.operands.push_back(Value::temp(0)); // dynamic offset prevents SROA
    entry.instructions.push_back(std::move(gep));

    Instr store1;
    store1.op = Opcode::Store;
    store1.type = Type(Type::Kind::I64);
    store1.operands.push_back(Value::temp(2));
    store1.operands.push_back(Value::constInt(6));
    entry.instructions.push_back(std::move(store1));

    Instr load0;
    load0.result = nextId++;
    load0.op = Opcode::Load;
    load0.type = Type(Type::Kind::I64);
    load0.operands.push_back(Value::temp(1));
    entry.instructions.push_back(std::move(load0));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(nextId - 1));
    entry.instructions.push_back(std::move(ret));

    entry.terminated = true;
    F.blocks.push_back(std::move(entry));
    return F;
}

static void test_scalarize_two_fields()
{
    Module M;
    M.functions.push_back(makeTwoFieldAggregate());

    viper::passes::mem2reg(M);

    const Function &F = M.functions.front();
    assert(!hasOp(F, Opcode::Alloca));
    assert(!hasOp(F, Opcode::Load));
    assert(!hasOp(F, Opcode::Store));
    assert(!hasOp(F, Opcode::GEP));
}

static void test_skip_dynamic_gep()
{
    Module M;
    M.functions.push_back(makeDynamicGEP());

    viper::passes::mem2reg(M);

    const Function &F = M.functions.front();
    assert(hasOp(F, Opcode::Load)); // dynamic GEP prevents scalarisation
    assert(hasOp(F, Opcode::Store));
}

int main()
{
    test_scalarize_two_fields();
    test_skip_dynamic_gep();
    return 0;
}
