#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include <cassert>

using namespace il::build;
using namespace il::core;

int main()
{
    Module m;
    IRBuilder b(m);

    Function &fn = b.startFunction("foo", Type(Type::Kind::Void), {});
    BasicBlock &entry = b.addBlock(fn, "entry");
    b.setInsertPoint(entry);

    // Create block with one parameter of type i64
    BasicBlock &blk = b.addBlock(fn, "blk", {Param{"x", Type(Type::Kind::I64)}});

    // Branch to block with one argument
    b.br(blk, {Value::constInt(7)});

    // Validate parameter and branch argument counts
    assert(blk.params.size() == 1);
    assert(blk.params[0].type.kind == Type::Kind::I64);
    assert(entry.instructions.size() == 1);
    const Instr &bi = entry.instructions.back();
    assert(bi.op == Opcode::Br);
    assert(bi.targs.size() == 1);

    // blockParam helper
    Value v = b.blockParam(blk, 0);
    assert(v.kind == Value::Kind::Temp);
    assert(v.id == blk.params[0].id);

    return 0;
}
