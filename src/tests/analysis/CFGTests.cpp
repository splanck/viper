// File: tests/analysis/CFGTests.cpp
// Purpose: Verify CFG successor and predecessor utilities.
// Key invariants: Successor and predecessor sets reflect branch targets.
// Ownership/Lifetime: Builds a local module via IRBuilder.
// Links: docs/dev/analysis.md

#include "il/analysis/CFG.hpp"
#include "il/build/IRBuilder.hpp"
#include <algorithm>
#include <cassert>

int main()
{
    using namespace il::core;
    using viper::analysis::predecessors;
    using viper::analysis::successors;

    Module m;
    il::build::IRBuilder b(m);
    Function &fn = b.startFunction("f", Type(Type::Kind::Void), {});
    b.createBlock(fn, "entry");
    b.createBlock(fn, "t");
    b.createBlock(fn, "f");
    b.createBlock(fn, "join");
    b.createBlock(fn, "handler");
    Block &entry = fn.blocks[0];
    Block &t = fn.blocks[1];
    Block &f = fn.blocks[2];
    Block &join = fn.blocks[3];
    Block &handler = fn.blocks[4];

    b.setInsertPoint(entry);
    b.cbr(Value::constInt(1), t, {}, f, {});

    b.setInsertPoint(t);
    b.br(join, {});

    b.setInsertPoint(f);
    b.br(handler, {});

    il::core::Instr resume;
    resume.op = Opcode::ResumeLabel;
    resume.type = Type(Type::Kind::Void);
    resume.operands.push_back(Value::temp(0));
    resume.labels.push_back("join");
    handler.instructions.push_back(resume);
    handler.terminated = true;

    b.setInsertPoint(join);
    b.emitRet(std::nullopt, {});

    Function &other = b.startFunction("g", Type(Type::Kind::Void), {});
    b.createBlock(other, "entry");
    b.createBlock(other, "t");
    Block &otherEntry = other.blocks[0];
    Block &otherT = other.blocks[1];

    b.setInsertPoint(otherEntry);
    b.br(otherT, {});

    b.setInsertPoint(otherT);
    b.emitRet(std::nullopt, {});

    viper::analysis::CFGContext ctx(m);

    auto sEntry = successors(ctx, entry);
    assert(sEntry.size() == 2);
    assert((sEntry[0] == &t && sEntry[1] == &f) || (sEntry[0] == &f && sEntry[1] == &t));

    auto sT = successors(ctx, t);
    assert(sT.size() == 1 && sT[0] == &join);
    auto sF = successors(ctx, f);
    assert(sF.size() == 1 && sF[0] == &handler);
    auto sHandler = successors(ctx, handler);
    assert(sHandler.size() == 1 && sHandler[0] == &join);
    auto sJoin = successors(ctx, join);
    assert(sJoin.empty());

    auto sOtherEntry = successors(ctx, otherEntry);
    assert(sOtherEntry.size() == 1 && sOtherEntry[0] == &otherT);
    auto sOtherT = successors(ctx, otherT);
    assert(sOtherT.empty());

    auto pJoin = predecessors(ctx, join);
    assert(pJoin.size() == 2);
    assert((pJoin[0] == &t && pJoin[1] == &handler) || (pJoin[0] == &handler && pJoin[1] == &t));

    auto pHandler = predecessors(ctx, handler);
    assert(pHandler.size() == 1 && pHandler[0] == &f);

    return 0;
}
