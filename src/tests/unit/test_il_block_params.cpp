//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_block_params.cpp
// Purpose: Verify block parameters and branch arguments in IRBuilder. 
// Key invariants: Parameter counts and branch arities match.
// Ownership/Lifetime: Uses builder with local module.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include <cassert>

int main()
{
    using namespace il::core;
    Module m;
    il::build::IRBuilder b(m);

    auto &fn = b.startFunction("f", Type(Type::Kind::Void), {});
    auto &entry = b.createBlock(fn, "entry");
    auto &loop = b.createBlock(fn, "loop", {{"x", Type(Type::Kind::I64), 0}});

    b.setInsertPoint(entry);
    b.br(loop, {Value::constInt(0)});

    b.setInsertPoint(loop);
    Value x = b.blockParam(loop, 0);
    b.cbr(x, loop, {x}, loop, {x});

    assert(loop.params.size() == 1);
    assert(loop.params[0].type.kind == Type::Kind::I64);

    const Instr &br0 = entry.instructions.back();
    assert(br0.brArgs.size() == 1);
    assert(br0.brArgs[0].size() == 1);

    const Instr &cbr0 = loop.instructions.back();
    assert(cbr0.brArgs.size() == 2);
    assert(cbr0.brArgs[0].size() == 1);
    assert(cbr0.brArgs[1].size() == 1);

    return 0;
}
