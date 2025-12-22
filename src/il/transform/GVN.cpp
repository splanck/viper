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

/// @file
/// @brief Implements Global Value Numbering with redundant load elimination.
/// @details Performs value numbering along dominator-tree paths to replace
///          redundant pure computations, and memoizes load results when alias
///          analysis proves they are still valid. The traversal is preorder so
///          only dominating information is visible in each block.

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
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::transform
{

namespace
{
using il::transform::ValueEq;
using il::transform::ValueHash;
using il::transform::ValueKey;
using il::transform::ValueKeyHash;

/// @brief Key describing a load by pointer, type, and (optional) byte size.
/// @details Used to memoize load results for redundant load elimination. The
///          size field is optional because some types may not map to a known
///          size; in that case the key still differentiates by pointer+type.
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

/// @brief Hash functor for @ref LoadKey.
/// @details Combines the pointer hash, type kind, and size (when present) to
///          produce a stable hash for unordered maps.
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

/// @brief Per-path state threaded through the dominator-tree traversal.
/// @details Contains value-numbering expressions and memoized loads visible on
///          the current dominating path. State is copied when recursing into
///          children to preserve path sensitivity.
struct State
{
    std::unordered_map<ValueKey, Value, ValueKeyHash> exprs;
    std::unordered_map<LoadKey, Value, LoadKeyHash> loads;
};

/// @brief Visit a basic block and apply GVN/RLE transformations.
/// @details Walks instructions in order, eliminating redundant loads and pure
///          expressions. Load elimination uses exact key matches first, then
///          falls back to MustAlias checks. Stores and impure calls invalidate
///          load memoization conservatively. After processing the block, the
///          function recurses into dominator children, passing a copy of the
///          current state so only dominating facts are visible.
/// @param F Function being optimized.
/// @param B Current basic block in dominator traversal.
/// @param DT Dominator tree for the function.
/// @param AA Alias analysis used for load/store reasoning.
/// @param state Current value/load memoization state (copied per child).
/// @param changed Output flag set true if any instruction is removed.
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

/// @brief Return the unique identifier for the GVN pass.
/// @details Used by the pass registry and pipeline definitions.
/// @return The canonical pass id string "gvn".
std::string_view GVN::id() const
{
    return "gvn";
}

/// @brief Execute GVN over a function.
/// @details Initializes analysis dependencies (CFG, dominators, alias analysis),
///          then walks the dominator tree from the entry block. If no changes
///          are made, all analyses are preserved; otherwise a conservative
///          invalidation is returned.
/// @param function Function to optimize.
/// @param analysis Analysis manager used to query required analyses.
/// @return Preserved analysis set after the transformation.
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

/// @brief Register the GVN pass in the pass registry.
/// @details Associates the "gvn" identifier with a factory that constructs
///          a new @ref GVN instance.
/// @param registry Pass registry to update.
void registerGVNPass(PassRegistry &registry)
{
    registry.registerFunctionPass("gvn", []() { return std::make_unique<GVN>(); });
}

} // namespace il::transform
