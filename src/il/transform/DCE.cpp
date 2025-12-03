//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the trivial dead-code elimination pass used by the IL optimizer.
// The pass performs syntactic use counting, removes instructions whose results
// are never consumed, and prunes unused block parameters together with their
// corresponding branch arguments.  Additionally, it eliminates pure runtime
// helper calls whose results are unused, consulting the runtime signatures
// registry for side-effect metadata.  All mutations happen in place so callers
// can run DCE over a fully materialised module without rebuilding auxiliary
// data structures.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the dead-code elimination (DCE) pass for the IL pipeline.
/// @details The pass complements the lightweight optimiser by erasing trivially
///          dead temporaries, redundant stack traffic, and unused block
///          parameters.  It relies solely on syntactic information so that it
///          can run quickly and deterministically even in debug builds where
///          richer analysis infrastructure may be disabled.  The implementation
///          lives out of line to keep the public header minimal while providing a
///          single, well-documented reference for the pass' heuristics.

#include "il/transform/DCE.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Value.hpp"
#include "il/transform/CallEffects.hpp"
#include <unordered_map>
#include <unordered_set>

using namespace il::core;

namespace il::transform
{
/// @brief Count how many times each temporary identifier is referenced.
///
/// @details Determines the maximum SSA id and uses an indexed vector for
///          counts to avoid hashing overhead in large functions. The vector is
///          seeded to include block parameters (zero uses) and incremented for
///          every operand or branch argument referencing a temp id.
///
/// @param F Function whose temporaries are inspected.
/// @return Vector indexed by temp id with use counts.
static std::vector<size_t> countUses(Function &F)
{
    // Compute maximum SSA id encountered across params, block params, and results
    unsigned maxId = 0;
    for (auto &p : F.params)
        maxId = std::max(maxId, p.id);
    for (auto &B : F.blocks)
    {
        for (auto &p : B.params)
            maxId = std::max(maxId, p.id);
        for (auto &I : B.instructions)
            if (I.result)
                maxId = std::max(maxId, static_cast<unsigned>(*I.result));
    }

    std::vector<size_t> uses(static_cast<size_t>(maxId) + 1, 0);

    // Touch block params to ensure zero entries exist even when unused
    for (auto &B : F.blocks)
        for (auto &p : B.params)
            (void)uses[p.id];

    auto touch = [&](unsigned id)
    {
        if (id < uses.size())
            uses[id]++;
    };

    for (auto &B : F.blocks)
    {
        for (auto &I : B.instructions)
        {
            for (auto &Op : I.operands)
                if (Op.kind == Value::Kind::Temp)
                    touch(Op.id);
            for (auto &ArgList : I.brArgs)
                for (auto &Arg : ArgList)
                    if (Arg.kind == Value::Kind::Temp)
                        touch(Arg.id);
        }
    }
    return uses;
}

/// @brief Eliminate trivially dead instructions and block parameters.
///
/// @details The transformation iterates over every function in @p M and:
///   1. Builds use counts for temporaries via @ref countUses so later stages can
///      determine which results are never observed.
///   2. Records which @c alloca results are observed by @c load instructions to
///      distinguish genuine stack slots from those that only ever receive
///      stores.
///   3. Deletes loads whose results have zero uses, stores that write to never
///      loaded allocas, and allocas that are never read.  The instruction
///      traversal keeps indices stable by only advancing when no erasure
///      occurred.
///   4. Walks block parameters in reverse order, removing unused entries and
///      erasing the corresponding operands from predecessor branch argument
///      lists so SSA form remains consistent.
/// Instructions that may observe side effects are generally preserved, but
/// calls to pure runtime helpers (as determined by the runtime signature
/// registry) can be safely eliminated when their results are unused.  This
/// enables more aggressive dead code removal while maintaining correctness.
/// The in-place updates mean callers can reuse existing module objects
/// without re-running expensive analyses.
///
/// @param M Module simplified in place.
void dce(Module &M)
{
    for (auto &F : M.functions)
    {
        auto uses = countUses(F);

        // Build a predecessor edge index once: for each target label, collect
        // (terminator*, successorIndex) pairs. Speeds up param pruning.
        std::unordered_map<std::string, std::vector<std::pair<Instr *, size_t>>> predEdges;
        // Reserve buckets roughly based on total labels encountered to
        // minimize rehashing while building the map.
        {
            std::size_t totalLabels = 0;
            for (auto &PB : F.blocks)
                for (auto &I : PB.instructions)
                    totalLabels += I.labels.size();
            if (totalLabels)
                predEdges.reserve(totalLabels);
        }
        for (auto &PB : F.blocks)
        {
            for (auto &I : PB.instructions)
            {
                if (I.op != Opcode::Br && I.op != Opcode::CBr && I.op != Opcode::SwitchI32)
                    continue;
                for (size_t l = 0; l < I.labels.size(); ++l)
                {
                    predEdges[I.labels[l]].emplace_back(&I, l);
                }
            }
        }
        // Gather allocas and whether they have loads
        std::unordered_map<unsigned, bool> hasLoad;
        for (auto &B : F.blocks)
            for (auto &I : B.instructions)
            {
                if (I.op == Opcode::Alloca && I.result)
                    hasLoad[*I.result] = false;
                if (I.op == Opcode::Load && !I.operands.empty() &&
                    I.operands[0].kind == Value::Kind::Temp)
                    hasLoad[I.operands[0].id] = true;
            }

        // Remove dead loads/stores/allocas
        for (auto &B : F.blocks)
        {
            for (std::size_t i = 0; i < B.instructions.size();)
            {
                Instr &I = B.instructions[i];
                if (I.op == Opcode::Load && I.result && uses[*I.result] == 0)
                {
                    B.instructions.erase(B.instructions.begin() + i);
                    continue;
                }
                if (I.op == Opcode::Store && !I.operands.empty() &&
                    I.operands[0].kind == Value::Kind::Temp &&
                    hasLoad.find(I.operands[0].id) != hasLoad.end() && !hasLoad[I.operands[0].id])
                {
                    B.instructions.erase(B.instructions.begin() + i);
                    continue;
                }
                if (I.op == Opcode::Alloca && I.result &&
                    hasLoad.find(*I.result) != hasLoad.end() && !hasLoad[*I.result])
                {
                    B.instructions.erase(B.instructions.begin() + i);
                    continue;
                }
                // Eliminate pure calls whose results are unused.
                // A call is safe to remove if:
                // 1. It produces a result that is never used
                // 2. The callee is marked pure (no observable side effects)
                if (I.op == Opcode::Call && I.result && uses[*I.result] == 0)
                {
                    const CallEffects effects = classifyCallEffects(I);
                    if (effects.canEliminateIfUnused())
                    {
                        B.instructions.erase(B.instructions.begin() + i);
                        continue;
                    }
                }
                ++i;
            }
        }

        // Remove unused block params
        for (auto &B : F.blocks)
        {
            for (int idx = static_cast<int>(B.params.size()) - 1; idx >= 0; --idx)
            {
                unsigned id = B.params[idx].id;
                if (id >= uses.size() || uses[id] != 0)
                    continue;
                B.params.erase(B.params.begin() + idx);
                auto it = predEdges.find(B.label);
                if (it != predEdges.end())
                {
                    for (auto &[term, succIdx] : it->second)
                    {
                        if (term->brArgs.size() > succIdx &&
                            term->brArgs[succIdx].size() > static_cast<std::size_t>(idx))
                        {
                            term->brArgs[succIdx].erase(term->brArgs[succIdx].begin() + idx);
                        }
                    }
                }
            }
        }
    }
}

} // namespace il::transform
