// File: tests/vm/UnknownOpcodeTests.cpp
// Purpose: Ensure the VM handles previously unimplemented opcodes once handlers land.
// Key invariants: Executing `const_null` completes without raising a trap.
// Ownership/Lifetime: Builds a small module and executes it directly.
// Links: docs/il-guide.md#reference

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>

using namespace il::core;

int main()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr bad;
    bad.result = builder.reserveTempId();
    bad.op = Opcode::ConstNull;
    bad.type = Type(Type::Kind::Ptr);
    bad.loc = {1, 1, 1};
    bb.instructions.push_back(bad);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::constInt(0));
    bb.instructions.push_back(ret);

    il::vm::VM vm(module);
    const int64_t exitCode = vm.run();
    assert(exitCode == 0 && "const_null execution should not raise a trap");

    return 0;
}
