//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_shift_mask.cpp
// Purpose: Verify VM shift handlers mask the shift amount to avoid undefined behaviour. 
// Key invariants: Left-shift by >= 64 on i64 operands behaves as modulo-64 shift.
// Ownership/Lifetime: Constructs IL module programmatically and executes via VM interpreter.
// Links: docs/il-guide.md#reference §Integer Arithmetic
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"

#include <cassert>

int main()
{
    using namespace il::core;

    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    bb.instructions.emplace_back();
    Instr &shl64 = bb.instructions.back();
    shl64.result = 0;
    shl64.op = Opcode::Shl;
    shl64.type = Type(Type::Kind::I64);
    shl64.operands = {Value::constInt(1), Value::constInt(64)};

    bb.instructions.emplace_back();
    Instr &shl65 = bb.instructions.back();
    shl65.result = 1;
    shl65.op = Opcode::Shl;
    shl65.type = Type(Type::Kind::I64);
    shl65.operands = {Value::constInt(1), Value::constInt(65)};

    bb.instructions.emplace_back();
    Instr &sum = bb.instructions.back();
    sum.result = 2;
    sum.op = Opcode::Add;
    sum.type = Type(Type::Kind::I64);
    sum.operands = {Value::temp(0), Value::temp(1)};

    bb.instructions.emplace_back();
    Instr &ret = bb.instructions.back();
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(2)};
    bb.terminated = true;

    fn.blocks.push_back(std::move(bb));
    fn.valueNames.resize(3);
    m.functions.push_back(std::move(fn));

    il::vm::VM vm(m);
    int64_t result = vm.run();
    assert(result == 3);

    return 0;
}
