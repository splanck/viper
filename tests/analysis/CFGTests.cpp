// File: tests/analysis/CFGTests.cpp
// Purpose: Tests for Analysis CFG utilities.
// Key invariants: Builds a diamond-shaped function to check successors and predecessors.
// Ownership/Lifetime: IR objects owned by a local module.
// Links: docs/dev/analysis.md

#include "Analysis/CFG.h"
#include "il/build/IRBuilder.hpp"
#include <algorithm>
#include <cassert>

int main()
{
    il::core::Module m;
    il::build::IRBuilder builder(m);
    auto &fn = builder.startFunction("cfg", il::core::Type(il::core::Type::Kind::Void), {});
    builder.addBlock(fn, "entry");
    builder.addBlock(fn, "t");
    builder.addBlock(fn, "f");
    builder.addBlock(fn, "join");

    auto &entry = fn.blocks[0];
    auto &t = fn.blocks[1];
    auto &f = fn.blocks[2];
    auto &join = fn.blocks[3];

    builder.setInsertPoint(entry);
    builder.cbr(il::core::Value::temp(0), t, {}, f, {});

    builder.setInsertPoint(t);
    builder.br(join, {});

    builder.setInsertPoint(f);
    builder.br(join, {});

    builder.setInsertPoint(join);
    builder.emitRet(std::nullopt, {});

    auto succ_entry = viper::analysis::successors(fn, entry);
    bool entry_has_t = std::find(succ_entry.begin(), succ_entry.end(), &t) != succ_entry.end();
    bool entry_has_f = std::find(succ_entry.begin(), succ_entry.end(), &f) != succ_entry.end();
    assert(entry_has_t && entry_has_f);

    auto succ_t = viper::analysis::successors(fn, t);
    assert(succ_t.size() == 1 && succ_t[0] == &join);

    auto succ_f = viper::analysis::successors(fn, f);
    assert(succ_f.size() == 1 && succ_f[0] == &join);

    auto succ_join = viper::analysis::successors(fn, join);
    assert(succ_join.empty());

    auto preds_join = viper::analysis::predecessors(fn, join);
    bool has_t = std::find(preds_join.begin(), preds_join.end(), &t) != preds_join.end();
    bool has_f = std::find(preds_join.begin(), preds_join.end(), &f) != preds_join.end();
    assert(has_t && has_f);

    return 0;
}
