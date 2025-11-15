// File: tests/unit/test_il_liveness.cpp
// Purpose: Regression tests for liveness analysis on complex control flow.
// Key invariants: Live-in/out sets reflect required SSA values across branches.
// Ownership/Lifetime: Test constructs modules locally and discards on exit.
// Links: docs/codemap.md

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/transform/analysis/Liveness.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

using namespace il;

namespace
{
const char *kProgram = R"(il 0.1.2

func @complex(%flag:i1) -> i64 {
entry(%flag:i1):
  %base = iadd.ovf 0, 1
  %incr = iadd.ovf %base, 1
  cbr %flag, left(%incr), right(%base)
left(%lv:i64):
  %left = iadd.ovf %lv, %incr
  br join(%left, %lv)
right(%rv:i64):
  %right = iadd.ovf %rv, %base
  br join(%right, %rv)
join(%x:i64, %y:i64):
  %sum = iadd.ovf %x, %y
  ret %sum
}
)";

unsigned findValueId(const core::Function &fn, std::string_view name)
{
    for (std::size_t idx = fn.valueNames.size(); idx-- > 0;)
    {
        if (fn.valueNames[idx] == name)
            return static_cast<unsigned>(idx);
    }
    assert(false && "value not found");
    return 0;
}

const core::BasicBlock *findBlock(const core::Function &fn, std::string_view label)
{
    for (const auto &block : fn.blocks)
    {
        if (block.label == label)
            return &block;
    }
    assert(false && "block not found");
    return nullptr;
}
} // namespace

int main()
{
    core::Module module;
    std::istringstream input(kProgram);
    auto parsed = il::api::v2::parse_text_expected(input, module);
    if (!parsed)
    {
        il::support::printDiag(parsed.error(), std::cerr);
        return 1;
    }

    assert(module.functions.size() == 1);
    core::Function &fn = module.functions[0];

    transform::LivenessInfo liveness = transform::computeLiveness(module, fn);

    const unsigned flagId = findValueId(fn, "flag");
    const unsigned baseId = findValueId(fn, "base");
    const unsigned incrId = findValueId(fn, "incr");

    const core::BasicBlock *entry = findBlock(fn, "entry");
    const core::BasicBlock *left = findBlock(fn, "left");
    const core::BasicBlock *right = findBlock(fn, "right");
    const core::BasicBlock *join = findBlock(fn, "join");

    auto entryIn = liveness.liveIn(entry);
    assert(entryIn.empty());

    auto entryOut = liveness.liveOut(entry);
    assert(entryOut.contains(baseId));
    assert(entryOut.contains(incrId));
    assert(!entryOut.contains(flagId));

    auto leftIn = liveness.liveIn(left);
    assert(leftIn.contains(incrId));
    assert(!leftIn.contains(baseId));
    auto leftOut = liveness.liveOut(left);
    assert(leftOut.empty());

    auto rightIn = liveness.liveIn(right);
    assert(rightIn.contains(baseId));
    assert(!rightIn.contains(incrId));
    auto rightOut = liveness.liveOut(right);
    assert(rightOut.empty());

    auto joinIn = liveness.liveIn(join);
    assert(joinIn.empty());
    auto joinOut = liveness.liveOut(join);
    assert(joinOut.empty());

    return 0;
}
