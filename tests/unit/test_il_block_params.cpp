// File: tests/unit/test_il_block_params.cpp
// Purpose: Validate block parameters and branch arguments in IRBuilder.
// Key invariants: Block param counts and branch argument counts match.
// Ownership/Lifetime: Uses builder-managed objects.

#include "il/build/IRBuilder.hpp"
#include <cassert>

int main()
{
    using namespace il::core;
    Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("f", Type(Type::Kind::Void), {});
    auto &entry = b.addBlock(fn, "entry");
    auto &bb = b.addBlock(fn, "b1", {{"x", Type(Type::Kind::I64), 0}});
    b.setInsertPoint(entry);
    b.br(bb, {Value::constInt(42)}, {});
    assert(bb.params.size() == 1);
    assert(bb.params[0].type.kind == Type::Kind::I64);
    assert(entry.instructions.back().branchArgs.size() == 1);
    assert(entry.instructions.back().branchArgs[0].size() == 1);
    return 0;
}
