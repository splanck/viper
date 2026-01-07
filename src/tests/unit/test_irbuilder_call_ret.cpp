//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_irbuilder_call_ret.cpp
// Purpose: Verify IRBuilder::emitCall records results for non-void functions.
// Key invariants: Call instruction captures result id and return type.
// Ownership/Lifetime: Test constructs module and inspects instruction.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include <cassert>

int main()
{
    using namespace il::core;
    Module m;
    il::build::IRBuilder b(m);
    b.addExtern("rt_len", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
    b.addGlobalStr("g", "hi");
    auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);
    Value s = b.emitConstStr("g", {});
    Value dst = Value::temp(0);
    b.emitCall("rt_len", {s}, dst, {});
    b.emitRet(dst, {});
    assert(bb.instructions.size() >= 2);
    const Instr &call = bb.instructions[1];
    assert(call.result && *call.result == dst.id);
    assert(call.type.kind == Type::Kind::I64);
    return 0;
}
