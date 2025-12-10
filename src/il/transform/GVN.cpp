//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a dominator-tree based Global Value Numbering pass with a simple
// Redundant Load Elimination. We conservatively match pure, side-effect-free
// instructions by opcode/type/operands (with commutative normalization) and
// reuse dominating results. For loads, we memoise (ptr,type) reads and reuse
// when no intervening clobber occurs (based on BasicAA and coarse memory
// effects). We traverse blocks in dominator-tree preorder and thread a local
// state to children so information flows along dominating paths only.
//
//===----------------------------------------------------------------------===//

#include "il/transform/GVN.hpp"

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/ValueKey.hpp"

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/transform/analysis/Liveness.hpp" // for CFGInfo

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include "il/utils/Utils.hpp"

#include <algorithm>
#include <cassert>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>

using namespace il::core;

namespace il::transform
{

namespace
{
using il::transform::ValueEq;
using il::transform::ValueHash;
using il::transform::ValueKey;
using il::transform::ValueKeyHash;

struct LoadKey
{
    Value ptr;
    Type::Kind type;
    std::optional<unsigned> size;

    bool operator==(const LoadKey &o) const noexcept
    {
        ValueEq eq;
        return type == o.type && eq(ptr, o.ptr) && size == o.size;
    }
};

struct LoadKeyHash
{
    size_t operator()(const LoadKey &k) const noexcept
    {
        ValueHash hv;
        size_t h = hv(k.ptr) ^ (static_cast<size_t>(k.type) * 0x9e3779b97f4a7c15ULL);
        if (k.size)
            h ^= static_cast<size_t>(*k.size + 0x517cc1b727220a95ULL);
        return h;
    }
};

struct State
{
    std::unordered_map<ValueKey, Value, ValueKeyHash> exprs;
    std::unordered_map<LoadKey, Value, LoadKeyHash> loads;
};

void visitBlock(Function &F,
                BasicBlock *B,
                const viper::analysis::DomTree &DT,
                viper::analysis::BasicAA &AA,
                State state,
                bool &changed)
{
    for (std::size_t idx = 0; idx < B->instructions.size();)
    {
        Instr &I = B->instructions[idx];

        // Redundant Load Elimination
        if (I.op == Opcode::Load && I.result && !I.operands.empty())
        {
            const Value &ptr = I.operands[0];
            auto loadSize = viper::analysis::BasicAA::typeSizeBytes(I.type);
            LoadKey key{ptr, I.type.kind, loadSize};

            // Try exact match first
            auto it = state.loads.find(key);
            if (it != state.loads.end())
            {
                viper::il::replaceAllUses(F, *I.result, it->second);
                B->instructions.erase(B->instructions.begin() + static_cast<long>(idx));
                changed = true;
                continue; // don't advance idx
            }

            // Otherwise, scan for alias-equivalent entries (MustAlias)
            bool replaced = false;
            for (const auto &kv : state.loads)
            {
                if (kv.first.type != key.type)
                    continue;
                if (AA.alias(kv.first.ptr, key.ptr, kv.first.size, key.size) ==
                    viper::analysis::AliasResult::MustAlias)
                {
                    viper::il::replaceAllUses(F, *I.result, kv.second);
                    B->instructions.erase(B->instructions.begin() + static_cast<long>(idx));
                    changed = true;
                    replaced = true;
                    break;
                }
            }
            if (replaced)
                continue;

            // Record available load
            state.loads.emplace(key, Value::temp(*I.result));
            ++idx;
            continue;
        }

        // Memory clobber: stores or other writes invalidate relevant loads
        if (I.op == Opcode::Store && I.operands.size() >= 2)
        {
            const Value &stPtr = I.operands[0];
            auto storeSize = viper::analysis::BasicAA::typeSizeBytes(I.type);
            for (auto it = state.loads.begin(); it != state.loads.end();)
            {
                if (AA.alias(it->first.ptr, stPtr, it->first.size, storeSize) !=
                    viper::analysis::AliasResult::NoAlias)
                    it = state.loads.erase(it);
                else
                    ++it;
            }
            ++idx;
            continue;
        }

        if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
        {
            auto mr = AA.modRef(I);
            if (mr != viper::analysis::ModRefResult::NoModRef &&
                mr != viper::analysis::ModRefResult::Ref)
            {
                state.loads.clear();
            }
            ++idx;
            continue;
        }

        // Other known writes invalidate all memoised loads. Be careful to not
        // treat Unknown (e.g. branch/ret) as a write.
        {
            using il::core::MemoryEffects;
            auto me = memoryEffects(I.op);
            if (me == MemoryEffects::Write || me == MemoryEffects::ReadWrite)
            {
                state.loads.clear();
                ++idx;
                continue;
            }
        }

        // Pure expression GVN
        if (auto key = makeValueKey(I))
        {
            auto found = state.exprs.find(*key);
            if (found != state.exprs.end())
            {
                viper::il::replaceAllUses(F, *I.result, found->second);
                B->instructions.erase(B->instructions.begin() + static_cast<long>(idx));
                changed = true;
                continue;
            }
            state.exprs.emplace(std::move(*key), Value::temp(*I.result));
            ++idx;
            continue;
        }

        // Default: advance
        ++idx;
    }

    // Recurse to children in dominator-tree preorder
    auto it = DT.children.find(B);
    if (it != DT.children.end())
    {
        for (auto *Child : it->second)
        {
            visitBlock(F, Child, DT, AA, state, changed);
        }
    }
}

} // namespace

std::string_view GVN::id() const
{
    return "gvn";
}

PreservedAnalyses GVN::run(Function &function, AnalysisManager &analysis)
{
    // Query required analyses
    (void)analysis.getFunctionResult<il::transform::CFGInfo>("cfg", function); // ensure available
    auto &dom = analysis.getFunctionResult<viper::analysis::DomTree>("dominators", function);
    auto &aa = analysis.getFunctionResult<viper::analysis::BasicAA>("basic-aa", function);

    bool changed = false;

    if (function.blocks.empty())
        return PreservedAnalyses::all();

    State state;

    // Start at entry block
    visitBlock(function, &function.blocks.front(), dom, aa, state, changed);

    if (!changed)
        return PreservedAnalyses::all();

    PreservedAnalyses p;
    p.preserveAllModules();
    return p;
}

void registerGVNPass(PassRegistry &registry)
{
    registry.registerFunctionPass("gvn", []() { return std::make_unique<GVN>(); });
}

} // namespace il::transform
