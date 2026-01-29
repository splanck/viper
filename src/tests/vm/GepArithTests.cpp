//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/GepArithTests.cpp
// Purpose: Validate VM handler for GEP (GetElementPointer) opcode
//          for pointer arithmetic.
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cstdint>

using namespace il::core;

namespace
{
// Build a module that allocates an array on stack and uses GEP to access elements
void buildGepModule(Module &module, int64_t index)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    // Allocate 64 bytes (8 int64_t values)
    Instr alloca;
    alloca.result = builder.reserveTempId();
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(64));
    alloca.loc = {1, 1, 1};
    bb.instructions.push_back(alloca);

    // Store a known value at offset 0: arr[0] = 100
    Instr store0;
    store0.op = Opcode::Store;
    store0.type = Type(Type::Kind::I64);
    store0.operands.push_back(Value::temp(*alloca.result));
    store0.operands.push_back(Value::constInt(100));
    store0.loc = {1, 1, 1};
    bb.instructions.push_back(store0);

    // Calculate pointer to element at index using GEP
    // GEP adds byte offset = index * 8 (assuming 8 bytes per element)
    Instr gep;
    gep.result = builder.reserveTempId();
    gep.op = Opcode::GEP;
    gep.type = Type(Type::Kind::Ptr);
    gep.operands.push_back(Value::temp(*alloca.result));
    gep.operands.push_back(Value::constInt(index * 8)); // Byte offset
    gep.loc = {1, 1, 1};
    bb.instructions.push_back(gep);

    // Store a different value at the indexed position
    Instr storeN;
    storeN.op = Opcode::Store;
    storeN.type = Type(Type::Kind::I64);
    storeN.operands.push_back(Value::temp(*gep.result));
    storeN.operands.push_back(Value::constInt(200 + index));
    storeN.loc = {1, 1, 1};
    bb.instructions.push_back(storeN);

    // Load value from the indexed position
    Instr load;
    load.result = builder.reserveTempId();
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.operands.push_back(Value::temp(*gep.result));
    load.loc = {1, 1, 1};
    bb.instructions.push_back(load);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*load.result));
    bb.instructions.push_back(ret);
}

// Build module that tests negative GEP offset
void buildNegativeGepModule(Module &module)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    // Allocate 64 bytes
    Instr alloca;
    alloca.result = builder.reserveTempId();
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(64));
    alloca.loc = {1, 1, 1};
    bb.instructions.push_back(alloca);

    // Store value at offset 16 (index 2)
    Instr gep1;
    gep1.result = builder.reserveTempId();
    gep1.op = Opcode::GEP;
    gep1.type = Type(Type::Kind::Ptr);
    gep1.operands.push_back(Value::temp(*alloca.result));
    gep1.operands.push_back(Value::constInt(16));
    gep1.loc = {1, 1, 1};
    bb.instructions.push_back(gep1);

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::I64);
    store.operands.push_back(Value::temp(*gep1.result));
    store.operands.push_back(Value::constInt(42));
    store.loc = {1, 1, 1};
    bb.instructions.push_back(store);

    // Now use negative GEP to go back: ptr at offset 16, then -8 = offset 8
    Instr gep2;
    gep2.result = builder.reserveTempId();
    gep2.op = Opcode::GEP;
    gep2.type = Type(Type::Kind::Ptr);
    gep2.operands.push_back(Value::temp(*gep1.result));
    gep2.operands.push_back(Value::constInt(-8));
    gep2.loc = {1, 1, 1};
    bb.instructions.push_back(gep2);

    // Store at offset 8
    Instr store2;
    store2.op = Opcode::Store;
    store2.type = Type(Type::Kind::I64);
    store2.operands.push_back(Value::temp(*gep2.result));
    store2.operands.push_back(Value::constInt(99));
    store2.loc = {1, 1, 1};
    bb.instructions.push_back(store2);

    // Load from offset 8 to verify
    Instr load;
    load.result = builder.reserveTempId();
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.operands.push_back(Value::temp(*gep2.result));
    load.loc = {1, 1, 1};
    bb.instructions.push_back(load);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*load.result));
    bb.instructions.push_back(ret);
}

int64_t runGep(int64_t index)
{
    Module module;
    buildGepModule(module, index);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

int64_t runNegativeGep()
{
    Module module;
    buildNegativeGepModule(module);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

} // namespace

int main()
{
    //=========================================================================
    // Basic GEP tests
    //=========================================================================

    // GEP with index 0 accesses first element
    assert(runGep(0) == 200); // 200 + 0

    // GEP with positive indices
    assert(runGep(1) == 201); // 200 + 1
    assert(runGep(2) == 202); // 200 + 2
    assert(runGep(3) == 203); // 200 + 3

    //=========================================================================
    // Negative offset GEP test
    //=========================================================================

    // Verify negative offset works correctly
    assert(runNegativeGep() == 99);

    return 0;
}
