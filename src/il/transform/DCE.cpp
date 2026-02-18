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
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace
{
static bool traceEnabled()
{
    static const bool enabled = std::getenv("VIPER_DCE_TRACE") != nullptr;
    return enabled;
}
} // namespace

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
        if (traceEnabled() && F.name == "main")
        {
            std::cerr << "[dce] === BEFORE DCE for " << F.name << " ===\n";
            for (auto &B : F.blocks)
            {
                std::cerr << B.label << ":\n";
                for (auto &I : B.instructions)
                {
                    std::cerr << "  ";
                    if (I.result)
                        std::cerr << "%" << *I.result << " = ";
                    std::cerr << toString(I.op);
                    for (auto &op : I.operands)
                    {
                        std::cerr << " ";
                        if (op.kind == Value::Kind::Temp)
                            std::cerr << "%t" << op.id;
                        else if (op.kind == Value::Kind::ConstInt)
                            std::cerr << "i64(" << op.i64 << ")";
                        else if (op.kind == Value::Kind::ConstStr)
                            std::cerr << "str(\"" << op.str << "\")";
                        else if (op.kind == Value::Kind::GlobalAddr)
                            std::cerr << "global(@" << op.str << ")";
                        else if (op.kind == Value::Kind::ConstFloat)
                            std::cerr << "f64(" << op.f64 << ")";
                        else if (op.kind == Value::Kind::NullPtr)
                            std::cerr << "null";
                        else
                            std::cerr << "?kind=" << static_cast<int>(op.kind);
                    }
                    if (!I.callee.empty())
                        std::cerr << " " << I.callee;
                    for (size_t li = 0; li < I.labels.size(); ++li)
                        std::cerr << " -> " << I.labels[li];
                    std::cerr << "\n";
                }
            }
            std::cerr << "[dce] === END BEFORE ===\n";
        }
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
        // Gather allocas and track if they are "observed" (loaded from or used by GEP).
        // An alloca is dead only if it has no uses at all, OR if it's only used by
        // stores (no loads or GEPs that might lead to loads).
        std::unordered_map<unsigned, bool> allocaObserved;
        for (auto &B : F.blocks)
            for (auto &I : B.instructions)
            {
                if (I.op == Opcode::Alloca && I.result)
                {
                    allocaObserved[*I.result] = false;
                    if (traceEnabled())
                        std::cerr << "[dce] tracking alloca %" << *I.result << " in " << F.name
                                  << "\n";
                }
                // Mark as observed if loaded from directly
                if (I.op == Opcode::Load && !I.operands.empty() &&
                    I.operands[0].kind == Value::Kind::Temp)
                {
                    allocaObserved[I.operands[0].id] = true;
                    if (traceEnabled())
                        std::cerr << "[dce] marking %" << I.operands[0].id
                                  << " as observed (load) in " << F.name << "\n";
                }
                // Mark as observed if used by GEP (GEP computes derived pointer)
                if (I.op == Opcode::GEP && !I.operands.empty() &&
                    I.operands[0].kind == Value::Kind::Temp)
                {
                    allocaObserved[I.operands[0].id] = true;
                    if (traceEnabled())
                        std::cerr << "[dce] marking %" << I.operands[0].id
                                  << " as observed (gep) in " << F.name << "\n";
                }
                // Mark as observed if passed to a function call (the callee may read from it)
                if ((I.op == Opcode::Call || I.op == Opcode::CallIndirect) && !I.operands.empty())
                {
                    for (auto &op : I.operands)
                    {
                        if (op.kind == Value::Kind::Temp)
                        {
                            allocaObserved[op.id] = true;
                            if (traceEnabled())
                                std::cerr << "[dce] marking %" << op.id
                                          << " as observed (call arg) in " << F.name << "\n";
                        }
                    }
                }
            }

        // Remove dead loads/stores/allocas
        for (auto &B : F.blocks)
        {
            for (std::size_t i = 0; i < B.instructions.size();)
            {
                Instr &I = B.instructions[i];
                if (I.op == Opcode::Load && I.result && uses[*I.result] == 0)
                {
                    if (traceEnabled())
                        std::cerr << "[dce] removing dead load %" << *I.result << " in " << F.name
                                  << ":" << B.label << "\n";
                    B.instructions.erase(B.instructions.begin() + i);
                    continue;
                }
                if (I.op == Opcode::Store && !I.operands.empty() &&
                    I.operands[0].kind == Value::Kind::Temp &&
                    allocaObserved.find(I.operands[0].id) != allocaObserved.end() &&
                    !allocaObserved[I.operands[0].id])
                {
                    if (traceEnabled())
                        std::cerr << "[dce] removing dead store to %" << I.operands[0].id << " in "
                                  << F.name << ":" << B.label << "\n";
                    B.instructions.erase(B.instructions.begin() + i);
                    continue;
                }
                if (I.op == Opcode::Alloca && I.result &&
                    allocaObserved.find(*I.result) != allocaObserved.end() &&
                    !allocaObserved[*I.result])
                {
                    if (traceEnabled())
                        std::cerr << "[dce] removing dead alloca %" << *I.result << " in " << F.name
                                  << ":" << B.label << "\n";
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
                        if (traceEnabled())
                            std::cerr << "[dce] removing pure call %" << *I.result << " = "
                                      << I.callee << " in " << F.name << ":" << B.label << "\n";
                        B.instructions.erase(B.instructions.begin() + i);
                        continue;
                    }
                }
                ++i;
            }
        }

        // Remove unused block params using compaction.
        //
        // The previous implementation iterated over params in reverse and called
        // erase() for each dead param, then iterated over all predecessors to
        // erase the corresponding brArg. This was O(#params * #preds) per block.
        //
        // The new algorithm:
        // 1. Build a vector of indices to keep (live params).
        // 2. Compact B.params in one pass using the keep list.
        // 3. For each predecessor edge, compact brArgs[succIdx] in one pass.
        // This reduces the complexity to O(#params + #preds) per block.
        for (auto &B : F.blocks)
        {
            const size_t numParams = B.params.size();
            if (numParams == 0)
                continue;

            // Handler blocks have a required ABI: (%err:Error, %tok:ResumeTok).
            // The VM populates both slots on exception dispatch regardless of
            // whether user code references %err.  Never remove params from a
            // block that starts with eh.entry (the handler entry marker).
            if (!B.instructions.empty() && B.instructions.front().op == il::core::Opcode::EhEntry)
                continue;

            // Identify which param indices to keep (those with non-zero use counts).
            std::vector<size_t> keepIndices;
            keepIndices.reserve(numParams);
            for (size_t i = 0; i < numParams; ++i)
            {
                const unsigned id = B.params[i].id;
                if (id < uses.size() && uses[id] == 0)
                {
                    if (traceEnabled())
                        std::cerr << "[dce] removing unused block param %" << id << " from "
                                  << B.label << "\n";
                    continue; // Dead param, skip
                }
                keepIndices.push_back(i);
            }

            // If all params are kept, nothing to do.
            if (keepIndices.size() == numParams)
                continue;

            // Compact B.params: rebuild the vector with only kept params.
            {
                std::vector<Param> compacted;
                compacted.reserve(keepIndices.size());
                for (size_t idx : keepIndices)
                    compacted.push_back(std::move(B.params[idx]));
                B.params = std::move(compacted);
            }

            // Compact predecessor brArgs for each edge targeting this block.
            auto it = predEdges.find(B.label);
            if (it != predEdges.end())
            {
                for (auto &[term, succIdx] : it->second)
                {
                    if (term->brArgs.size() <= succIdx)
                        continue;
                    auto &args = term->brArgs[succIdx];
                    if (args.size() != numParams)
                        continue; // Mismatch, skip to avoid corruption

                    std::vector<Value> compacted;
                    compacted.reserve(keepIndices.size());
                    for (size_t idx : keepIndices)
                        compacted.push_back(std::move(args[idx]));
                    args = std::move(compacted);
                }
            }
        }
    }
}

} // namespace il::transform
