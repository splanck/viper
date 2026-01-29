//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/AddrOfTests.cpp
// Purpose: Validate VM handler for AddrOf opcode (getting address of a temp).
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"
#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <cstdint>

using namespace il::core;

namespace
{
// Build a module that uses AddrOf to get the address of an alloca'd value
void buildAddrOfModule(Module &module, int64_t value)
{
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    // Allocate space for an int64
    Instr alloca;
    alloca.result = builder.reserveTempId();
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(8));
    alloca.loc = {1, 1, 1};
    bb.instructions.push_back(alloca);

    // Store a value
    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::I64);
    store.operands.push_back(Value::temp(*alloca.result));
    store.operands.push_back(Value::constInt(value));
    store.loc = {1, 1, 1};
    bb.instructions.push_back(store);

    // Get address of the alloca result (the pointer itself)
    Instr addrOf;
    addrOf.result = builder.reserveTempId();
    addrOf.op = Opcode::AddrOf;
    addrOf.type = Type(Type::Kind::Ptr);
    addrOf.operands.push_back(Value::temp(*alloca.result));
    addrOf.loc = {1, 1, 1};
    bb.instructions.push_back(addrOf);

    // Load through the address to verify it points to the right place
    Instr load;
    load.result = builder.reserveTempId();
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.operands.push_back(Value::temp(*addrOf.result));
    load.loc = {1, 1, 1};
    bb.instructions.push_back(load);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*load.result));
    bb.instructions.push_back(ret);
}

int64_t runAddrOf(int64_t value)
{
    Module module;
    buildAddrOfModule(module, value);
    viper::tests::VmFixture fixture;
    return fixture.run(module);
}

} // namespace

int main()
{
    //=========================================================================
    // AddrOf tests
    //=========================================================================

    // Basic: store value, get address, load through address
    assert(runAddrOf(42) == 42);
    assert(runAddrOf(0) == 0);
    assert(runAddrOf(-1) == -1);
    assert(runAddrOf(123456789) == 123456789);

    return 0;
}
