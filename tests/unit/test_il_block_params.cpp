#include "il/build/IRBuilder.hpp"
#include <cassert>

int main()
{
    il::core::Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("f", il::core::Type(il::core::Type::Kind::Void), {});

    using Param = il::build::IRBuilder::ParamDef;
    std::vector<Param> params;
    params.push_back({"x", il::core::Type(il::core::Type::Kind::I64)});
    b.addBlock(fn, "entry");
    b.addBlock(fn, "loop", params);
    auto &entry = fn.blocks[0];
    auto &loop = fn.blocks[1];

    b.setInsertPoint(entry);
    b.br(loop, {il::core::Value::constInt(0)}, {});

    b.setInsertPoint(loop);
    il::core::Value x = b.blockParam(loop, 0);
    b.cbr(il::core::Value::constInt(1), entry, {}, loop, {x}, {});

    assert(loop.params.size() == 1);
    assert(loop.params[0].type.kind == il::core::Type::Kind::I64);

    const il::core::Instr &brI = entry.instructions.back();
    assert(brI.op == il::core::Opcode::Br);
    assert(brI.operands.size() == 1);

    const il::core::Instr &cbrI = loop.instructions.back();
    assert(cbrI.op == il::core::Opcode::CBr);
    assert(cbrI.tArgCount == 0);
    unsigned fArgs = cbrI.operands.size() - 1 - cbrI.tArgCount;
    assert(fArgs == 1);
    return 0;
}
