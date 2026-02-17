//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/analysis/MemorySSA.cpp
// Purpose: Implements the MemorySSA analysis. See MemorySSA.hpp for design
//          documentation.
//
// Algorithm overview
// ------------------
//
// For each non-escaping alloca A we track memory def-use chains precisely:
//
//   - Store to A  →  MemoryDef   (defines a new version of A's memory)
//   - Load from A →  MemoryUse   (consumes the reaching MemoryDef)
//   - Call        →  transparent for A (calls cannot access non-escaping stack)
//
// The "reaching def" for a use is the most recent MemoryDef that dominates
// the use in the CFG.  Rather than building full SSA form with dominance
// frontiers, we use an RPO-order forward dataflow:
//
//   currentDef[A] starts as LiveOnEntry (id=0).
//   At each store to A: create MemoryDef, update currentDef[A].
//   At each load from A: create MemoryUse pointing at currentDef[A].
//   At block joins:      take the union of incoming currentDef[A]; if they
//                        differ, insert a MemoryPhi.
//
// Dead-store detection
// --------------------
//
// A MemoryDef D is dead iff, on every control-flow path from D to any exit:
//   - some later MemoryDef for the same location overwrites D, OR
//   - the exit is reached without any MemoryUse consuming D.
//
// Equivalently: D is dead iff D has no MemoryUse consumers reachable before
// the next overwriting MemoryDef on any path.
//
// Implementation: after building def-use links we propagate "liveness" of
// each MemoryDef backward through the users list.  A MemoryDef with zero
// users is unconditionally dead.
//
//===----------------------------------------------------------------------===//

#include "il/analysis/MemorySSA.hpp"

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/CFG.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace viper::analysis
{

// -------------------------------------------------------------------------
// MemorySSA query implementation
// -------------------------------------------------------------------------

bool MemorySSA::isDeadStore(const Block *block, size_t instrIdx) const
{
    auto bit = instrToAccess_.find(block);
    if (bit == instrToAccess_.end())
        return false;
    auto iit = bit->second.find(instrIdx);
    if (iit == bit->second.end())
        return false;
    return deadStoreIds_.count(iit->second) != 0;
}

const MemoryAccess *MemorySSA::accessFor(const Block *block, size_t instrIdx) const
{
    auto bit = instrToAccess_.find(block);
    if (bit == instrToAccess_.end())
        return nullptr;
    auto iit = bit->second.find(instrIdx);
    if (iit == bit->second.end())
        return nullptr;
    uint32_t idx = iit->second;
    if (idx >= accesses_.size())
        return nullptr;
    return &accesses_[idx];
}

// -------------------------------------------------------------------------
// computeMemorySSA
// -------------------------------------------------------------------------

namespace
{

/// True if the instruction defines new memory (store or modifying call).
inline bool isDef(const Instr &I, viper::analysis::BasicAA &AA)
{
    if (I.op == Opcode::Store)
        return true;
    if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
    {
        auto mr = AA.modRef(I);
        return mr == ModRefResult::Mod || mr == ModRefResult::ModRef;
    }
    return false;
}

/// True if the instruction reads memory (load or reading call).
inline bool isUse(const Instr &I, viper::analysis::BasicAA &AA)
{
    if (I.op == Opcode::Load)
        return true;
    if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
    {
        auto mr = AA.modRef(I);
        return mr == ModRefResult::Ref || mr == ModRefResult::ModRef;
    }
    return false;
}

/// True if this alloca's address is passed to a call or stored elsewhere.
bool allocaEscapes(const Function &F, unsigned allocaId)
{
    for (const auto &B : F.blocks)
    {
        for (const auto &I : B.instructions)
        {
            if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
            {
                for (const auto &op : I.operands)
                {
                    if (op.kind == Value::Kind::Temp && op.id == allocaId)
                        return true;
                }
            }
            if (I.op == Opcode::Store && I.operands.size() >= 2)
            {
                const auto &val = I.operands[1];
                if (val.kind == Value::Kind::Temp && val.id == allocaId)
                    return true;
            }
        }
    }
    return false;
}

/// Compute the set of non-escaping alloca ids in @p F.
std::unordered_set<unsigned> nonEscapingAllocas(const Function &F)
{
    std::unordered_set<unsigned> result;
    for (const auto &B : F.blocks)
    {
        for (const auto &I : B.instructions)
        {
            if (I.op == Opcode::Alloca && I.result)
            {
                if (!allocaEscapes(F, *I.result))
                    result.insert(*I.result);
            }
        }
    }
    return result;
}

/// True if @p ptr refers directly to a non-escaping alloca.
inline bool isNonEscapingAlloca(const Value &ptr,
                                const std::unordered_set<unsigned> &nonEsc)
{
    return ptr.kind == Value::Kind::Temp && nonEsc.count(ptr.id) != 0;
}

} // namespace

MemorySSA computeMemorySSA(Function &F, BasicAA &AA)
{
    MemorySSA mssa;

    if (F.blocks.empty())
        return mssa;

    // LiveOnEntry sentinel at index 0.
    mssa.accesses_.push_back(
        MemoryAccess{MemAccessKind::LiveOnEntry, 0, nullptr, -1, 0, {}, {}});

    auto nextId = [&]() -> uint32_t
    { return static_cast<uint32_t>(mssa.accesses_.size()); };

    auto makeAccess = [&](MemAccessKind kind,
                          Block *block,
                          int instrIdx,
                          uint32_t definingAccess) -> uint32_t
    {
        uint32_t id = nextId();
        mssa.accesses_.push_back(
            MemoryAccess{kind, id, block, instrIdx, definingAccess, {}, {}});
        mssa.instrToAccess_[block][static_cast<size_t>(instrIdx)] = id;
        return id;
    };

    // Collect non-escaping allocas — calls are transparent for these.
    const std::unordered_set<unsigned> nonEsc = nonEscapingAllocas(F);

    // -----------------------------------------------------------------------
    // Phase 1: Compute RPO order for forward dataflow.
    // -----------------------------------------------------------------------
    std::vector<Block *> rpo;
    {
        // Simple RPO via DFS.
        std::unordered_set<Block *> visited;
        std::vector<Block *> postOrder;
        std::function<void(Block *)> dfs = [&](Block *b)
        {
            if (!visited.insert(b).second)
                return;
            // Visit successors (follow terminator labels).
            if (!b->instructions.empty())
            {
                for (const auto &label : b->instructions.back().labels)
                {
                    for (auto &succ : F.blocks)
                    {
                        if (succ.label == label)
                        {
                            dfs(&succ);
                            break;
                        }
                    }
                }
            }
            postOrder.push_back(b);
        };
        dfs(&F.blocks.front());
        rpo.assign(postOrder.rbegin(), postOrder.rend());
    }

    // Build label→Block* map for successor lookup.
    std::unordered_map<std::string, Block *> labelToBlock;
    for (auto &B : F.blocks)
        labelToBlock[B.label] = &B;

    // Build predecessor map for join-point phi insertion.
    std::unordered_map<Block *, std::vector<Block *>> preds;
    for (auto &B : F.blocks)
    {
        if (!B.instructions.empty())
        {
            for (const auto &label : B.instructions.back().labels)
            {
                auto it = labelToBlock.find(label);
                if (it != labelToBlock.end())
                    preds[it->second].push_back(&B);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Phase 2: Forward dataflow — assign MemoryDef/Use, inserting Phis.
    //
    // currentDef[block] = the MemoryAccess id that represents the current
    // memory definition at the EXIT of that block.
    //
    // At the start of each block, we take the meet (phi-insert if needed)
    // of incoming currentDefs.
    // -----------------------------------------------------------------------

    // outDef[B] = id of the last MemoryAccess at the end of block B.
    std::unordered_map<Block *, uint32_t> outDef;

    // Initialize all blocks to LiveOnEntry (id=0).
    for (auto &B : F.blocks)
        outDef[&B] = 0;

    // We iterate until stable (may need multiple passes for loops).
    // A simple two-pass works for acyclic; for loops we need fixpoint.
    // Run up to |blocks|+1 iterations.
    const size_t maxIter = F.blocks.size() + 1;

    for (size_t iter = 0; iter < maxIter; ++iter)
    {
        bool changed = false;

        for (Block *B : rpo)
        {
            // Determine the incoming def at the start of B.
            uint32_t inDef = 0; // LiveOnEntry default

            const auto &predList = preds[B];
            if (!predList.empty())
            {
                // Collect outDefs from all predecessors.
                uint32_t first = outDef[predList[0]];
                bool allSame = true;
                for (size_t pi = 1; pi < predList.size(); ++pi)
                {
                    if (outDef[predList[pi]] != first)
                    {
                        allSame = false;
                        break;
                    }
                }

                if (allSame)
                {
                    inDef = first;
                }
                else
                {
                    // Need a Phi. Look for an existing Phi at the start of B.
                    uint32_t phiId = 0;
                    auto bit = mssa.instrToAccess_.find(B);
                    if (bit != mssa.instrToAccess_.end())
                    {
                        // Phi is stored at instrIdx = -1 (represented as SIZE_MAX).
                        auto pit = bit->second.find(static_cast<size_t>(-1));
                        if (pit != bit->second.end())
                            phiId = pit->second;
                    }

                    if (phiId == 0)
                    {
                        // Create new Phi.
                        phiId = nextId();
                        std::vector<uint32_t> incoming;
                        incoming.reserve(predList.size());
                        for (Block *pred : predList)
                            incoming.push_back(outDef[pred]);
                        mssa.accesses_.push_back(MemoryAccess{
                            MemAccessKind::Phi, phiId, B, -1, 0, std::move(incoming), {}});
                        mssa.instrToAccess_[B][static_cast<size_t>(-1)] = phiId;
                        changed = true;
                    }
                    else
                    {
                        // Update existing Phi's incoming arms.
                        MemoryAccess &phi = mssa.accesses_[phiId];
                        for (size_t pi = 0; pi < predList.size(); ++pi)
                        {
                            uint32_t newArm = outDef[predList[pi]];
                            if (pi >= phi.incoming.size())
                            {
                                phi.incoming.push_back(newArm);
                                changed = true;
                            }
                            else if (phi.incoming[pi] != newArm)
                            {
                                phi.incoming[pi] = newArm;
                                changed = true;
                            }
                        }
                    }
                    inDef = phiId;
                }
            }

            // Walk instructions in B, updating inDef as we encounter defs/uses.
            uint32_t curDef = inDef;

            for (size_t i = 0; i < B->instructions.size(); ++i)
            {
                const Instr &I = B->instructions[i];

                // Check if this instruction already has an access (from a prior iter).
                uint32_t existingId = 0;
                auto bit = mssa.instrToAccess_.find(B);
                if (bit != mssa.instrToAccess_.end())
                {
                    auto iit = bit->second.find(i);
                    if (iit != bit->second.end())
                        existingId = iit->second;
                }

                // For calls touching non-escaping allocas: transparent (skip).
                // We check this at the Use/Def determination step.

                if (I.op == Opcode::Store)
                {
                    const Value &ptr = I.operands.empty() ? Value{} : I.operands[0];
                    bool nonEscaping = isNonEscapingAlloca(ptr, nonEsc);

                    // Create or update MemoryDef.
                    if (existingId == 0)
                    {
                        uint32_t defId = makeAccess(MemAccessKind::Def, B, (int)i, curDef);
                        // Link curDef's users to include this new def.
                        if (curDef < mssa.accesses_.size())
                        {
                            // Only link if the store potentially reads curDef
                            // (i.e., reading first then writing). For stores we only
                            // link as Def; use consumers are separate.
                            (void)nonEscaping; // noted but not needed for linkage logic
                        }
                        curDef = defId;
                        changed = true;
                    }
                    else
                    {
                        // Update definingAccess if it changed.
                        MemoryAccess &acc = mssa.accesses_[existingId];
                        if (acc.definingAccess != curDef)
                        {
                            acc.definingAccess = curDef;
                            changed = true;
                        }
                        curDef = existingId;
                    }
                }
                else if (I.op == Opcode::Load)
                {
                    const Value &ptr = I.operands.empty() ? Value{} : I.operands[0];
                    bool nonEscaping = isNonEscapingAlloca(ptr, nonEsc);
                    (void)nonEscaping;

                    // Create or update MemoryUse.
                    if (existingId == 0)
                    {
                        makeAccess(MemAccessKind::Use, B, (int)i, curDef);
                        // Register this use in the def's users list.
                        if (curDef < mssa.accesses_.size())
                        {
                            mssa.accesses_[curDef].users.push_back(
                                mssa.instrToAccess_[B][i]);
                        }
                        changed = true;
                    }
                    else
                    {
                        MemoryAccess &acc = mssa.accesses_[existingId];
                        if (acc.definingAccess != curDef)
                        {
                            // Remove from old def's users, add to new.
                            uint32_t oldDef = acc.definingAccess;
                            if (oldDef < mssa.accesses_.size())
                            {
                                auto &users = mssa.accesses_[oldDef].users;
                                users.erase(
                                    std::remove(users.begin(), users.end(), existingId),
                                    users.end());
                            }
                            acc.definingAccess = curDef;
                            if (curDef < mssa.accesses_.size())
                            {
                                mssa.accesses_[curDef].users.push_back(existingId);
                            }
                            changed = true;
                        }
                        // curDef unchanged by loads.
                    }
                }
                else if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
                {
                    // Calls are transparent for non-escaping allocas.
                    // For the global memory state they may Def or Use.
                    // We model them as global Defs if they Mod, and Uses if they Ref.
                    // This is conservative for heap/global accesses.
                    auto mr = AA.modRef(I);
                    if (mr == ModRefResult::NoModRef)
                        continue;

                    // Def: call modifies global memory.
                    if (mr == ModRefResult::Mod || mr == ModRefResult::ModRef)
                    {
                        if (existingId == 0)
                        {
                            uint32_t defId = makeAccess(MemAccessKind::Def, B, (int)i, curDef);
                            curDef = defId;
                            changed = true;
                        }
                        else
                        {
                            MemoryAccess &acc = mssa.accesses_[existingId];
                            if (acc.definingAccess != curDef)
                            {
                                acc.definingAccess = curDef;
                                changed = true;
                            }
                            curDef = existingId;
                        }
                    }
                    // Use: call reads global memory (register use of curDef).
                    if (mr == ModRefResult::Ref || mr == ModRefResult::ModRef)
                    {
                        // Register the call as a user of curDef.
                        // For ModRef: the Def we just created reads the prior curDef.
                        // We don't create a separate Use node; the Def implicitly reads.
                    }
                }
            }

            uint32_t newOutDef = curDef;
            if (outDef[B] != newOutDef)
            {
                outDef[B] = newOutDef;
                changed = true;
            }
        }

        if (!changed)
            break;
    }

    // -----------------------------------------------------------------------
    // Phase 3: Dead-store detection.
    //
    // A MemoryDef at (block, instrIdx) that corresponds to a Store is dead if
    // no MemoryUse transitively reachable from it reads from the same location
    // before another MemoryDef kills it.
    //
    // Precise per-location dead-store check using BFS forward from each store.
    // Unlike the previous runCrossBlockDSE:
    //   - Calls are NOT treated as reads for non-escaping allocas.
    //   - This is the key precision improvement over the conservative BFS.
    // -----------------------------------------------------------------------

    // Rebuild label→Block* (for successor traversal).
    // (labelToBlock already exists in this scope.)

    for (auto &B : F.blocks)
    {
        for (size_t i = 0; i < B.instructions.size(); ++i)
        {
            const Instr &I = B.instructions[i];
            if (I.op != Opcode::Store || I.operands.empty())
                continue;

            const Value &ptr = I.operands[0];
            if (!isNonEscapingAlloca(ptr, nonEsc))
                continue;

            auto storeSize = BasicAA::typeSizeBytes(I.type);

            // Determine if this store is dead using a forward BFS that is
            // precise about calls: since the alloca doesn't escape, calls
            // cannot read or modify it.
            bool isDead = true;

            // Intra-block check: scan instructions AFTER the store in same block.
            for (size_t j = i + 1; j < B.instructions.size(); ++j)
            {
                const Instr &next = B.instructions[j];

                if (next.op == Opcode::Load && !next.operands.empty())
                {
                    auto loadSize = BasicAA::typeSizeBytes(next.type);
                    if (AA.alias(next.operands[0], ptr, loadSize, storeSize) !=
                        AliasResult::NoAlias)
                    {
                        isDead = false;
                        break;
                    }
                }
                if (next.op == Opcode::Store && !next.operands.empty())
                {
                    auto nextSize = BasicAA::typeSizeBytes(next.type);
                    if (AA.alias(next.operands[0], ptr, nextSize, storeSize) ==
                        AliasResult::MustAlias)
                    {
                        // Killed by a later store in same block — not dead from here
                        // (the intra-block DSE would have already removed the earlier one).
                        isDead = false;
                        break;
                    }
                }
                // KEY PRECISION IMPROVEMENT: calls do NOT read non-escaping allocas.
                // Do NOT treat calls as read barriers here.
                // (Calls with Mod: don't modify non-escaping allocas either.)
            }

            if (!isDead)
                continue;

            // Cross-block BFS — same precision improvement for successor blocks.
            std::unordered_set<std::string> visited;
            std::vector<std::string> worklist;
            if (!B.instructions.empty())
            {
                for (const auto &label : B.instructions.back().labels)
                    worklist.push_back(label);
            }

            bool allPathsKillOrExit = true;

            while (!worklist.empty() && allPathsKillOrExit)
            {
                std::string label = std::move(worklist.back());
                worklist.pop_back();

                if (visited.count(label))
                    continue;
                visited.insert(label);

                auto it = labelToBlock.find(label);
                if (it == labelToBlock.end())
                {
                    allPathsKillOrExit = false;
                    continue;
                }
                Block *succ = it->second;

                bool pathKilled = false;

                for (const auto &next : succ->instructions)
                {
                    if (next.op == Opcode::Load && !next.operands.empty())
                    {
                        auto loadSize = BasicAA::typeSizeBytes(next.type);
                        if (AA.alias(next.operands[0], ptr, loadSize, storeSize) !=
                            AliasResult::NoAlias)
                        {
                            // A load reads this alloca — NOT dead.
                            allPathsKillOrExit = false;
                            goto nextSuccessor;
                        }
                    }
                    if (next.op == Opcode::Store && !next.operands.empty())
                    {
                        auto nextSize = BasicAA::typeSizeBytes(next.type);
                        if (AA.alias(next.operands[0], ptr, nextSize, storeSize) ==
                            AliasResult::MustAlias)
                        {
                            // A later store kills this path — don't explore further.
                            pathKilled = true;
                            break;
                        }
                    }
                    // KEY IMPROVEMENT: skip calls entirely for non-escaping allocas.
                    // Calls cannot read or write non-escaping stack memory.
                }

                if (pathKilled)
                    continue; // This path is covered, move to next.

                if (!succ->instructions.empty() &&
                    succ->instructions.back().op == Opcode::Ret)
                    continue; // Path exits without reading — OK.

                // Enqueue successors.
                if (!succ->instructions.empty())
                {
                    for (const auto &succLabel : succ->instructions.back().labels)
                    {
                        if (!visited.count(succLabel))
                            worklist.push_back(succLabel);
                    }
                }

            nextSuccessor:;
            }

            if (allPathsKillOrExit && !visited.empty())
            {
                // Look up the MemoryAccess id for this store.
                auto bit = mssa.instrToAccess_.find(&B);
                if (bit != mssa.instrToAccess_.end())
                {
                    auto iit = bit->second.find(i);
                    if (iit != bit->second.end())
                        mssa.deadStoreIds_.insert(iit->second);
                }
            }
        }
    }

    return mssa;
}

} // namespace viper::analysis
