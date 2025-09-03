// File: tests/unit/test_il_block_params.cpp
// Purpose: Unit test verifying block parameters and branch arguments.
// Key invariants: Block parameter count matches branch argument count.
// Ownership/Lifetime: N/A
// Links: docs/dev/ir-builder.md

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include <cassert>

int main()
{
    using namespace il::core;
    Module m;
    il::build::IRBuilder b(m);
    Function &f = b.startFunction("f", Type(Type::Kind::Void), {});
    BasicBlock &entry = b.addBlock(f, "entry");
    BasicBlock &bb = b.addBlock(f, "bb", {{"x", Type(Type::Kind::I64), 0}});
    b.setInsertPoint(entry);
    b.emitBr(bb, {Value::constInt(1)});

    assert(bb.params.size() == 1);
    assert(bb.params[0].type.kind == Type::Kind::I64);
    Value paramVal = b.blockParam(bb, 0);
    assert(paramVal.kind == Value::Kind::Temp);
    assert(paramVal.id == bb.params[0].id);
    const Instr &br = entry.instructions.back();
    assert(br.op == Opcode::Br);
    assert(br.operands.size() == 1);
    assert(br.labels.size() == 1);
    return 0;
}
