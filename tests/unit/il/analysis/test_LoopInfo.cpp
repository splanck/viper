// File: tests/unit/il/analysis/test_LoopInfo.cpp
// Purpose: Validate natural loop discovery using dominator backedge detection.
// Key invariants: Single backedge forms one loop with expected header, latch, and exit blocks.
// Ownership/Lifetime: Builds an ephemeral module via IRBuilder for analysis.
// Links: docs/dev/analysis.md

#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/analysis/LoopInfo.hpp"
#include "il/build/IRBuilder.hpp"

#include <algorithm>
#include <cassert>

int main()
{
    using namespace il::core;

    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("loop", Type(Type::Kind::Void), {});
    builder.addBlock(fn, "entry");
    builder.addBlock(fn, "header");
    builder.addBlock(fn, "body");
    builder.addBlock(fn, "exit");

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &header = fn.blocks[1];
    BasicBlock &body = fn.blocks[2];
    BasicBlock &exit = fn.blocks[3];

    builder.setInsertPoint(entry);
    builder.br(header, {});

    builder.setInsertPoint(header);
    builder.cbr(Value::constInt(1), body, {}, exit, {});

    builder.setInsertPoint(body);
    builder.br(header, {});

    builder.setInsertPoint(exit);
    builder.emitRet(std::nullopt, {});

    viper::analysis::CFGContext ctx(module);
    auto dom = viper::analysis::computeDominatorTree(ctx, fn);
    auto loops = viper::analysis::LoopInfo::compute(module, fn, dom);

    const auto &topLevel = loops.topLevelLoops();
    assert(topLevel.size() == 1);
    const auto *loop = topLevel.front().get();
    assert(loop->Header == &header);
    assert(std::find(loop->Blocks.begin(), loop->Blocks.end(), &header) != loop->Blocks.end());
    assert(std::find(loop->Blocks.begin(), loop->Blocks.end(), &body) != loop->Blocks.end());
    assert(std::find(loop->Latches.begin(), loop->Latches.end(), &body) != loop->Latches.end());
    assert(std::find(loop->Exits.begin(), loop->Exits.end(), &exit) != loop->Exits.end());
    assert(loops.getLoopFor(&header) == loop);
    assert(loops.getLoopFor(&body) == loop);
    assert(loops.getLoopFor(&entry) == nullptr);
    assert(loops.getLoopFor(&exit) == nullptr);

    return 0;
}
