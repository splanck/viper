// File: tests/unit/test_vm_many_temps.cpp
// Purpose: Ensure VM handles functions with more than 64 SSA temporaries.
// Key invariants: Function with 70 temporaries executes and returns expected value.
// Ownership: Test constructs IL module and executes VM.
// Links: docs/il-guide.md#reference

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

    for (unsigned i = 0; i < 70; ++i)
    {
        bb.instructions.emplace_back();
        Instr &in = bb.instructions.back();
        in.result = i;
        in.op = Opcode::IAddOvf;
        in.type = Type(Type::Kind::I64);
        if (i == 0)
            in.operands = {Value::constInt(0), Value::constInt(0)};
        else
            in.operands = {Value::temp(i - 1), Value::constInt(1)};
    }

    bb.instructions.emplace_back();
    Instr &ret = bb.instructions.back();
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(69)};
    bb.terminated = true;

    fn.blocks.push_back(std::move(bb));
    fn.valueNames.resize(70);
    m.functions.push_back(std::move(fn));

    il::vm::VM vm(m);
    int64_t res = vm.run();
    assert(res == 69);
    return 0;
}
