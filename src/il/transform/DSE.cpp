//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements dead-store elimination with two levels of analysis:
// 1. Intra-block DSE: Backward scan within each basic block
// 2. Cross-block DSE: Forward propagation of stores through the CFG to find
//    stores that are killed on all paths before being read
//
// The pass uses BasicAA for alias disambiguation and is conservative about
// calls that may modify or reference memory.
//
//===----------------------------------------------------------------------===//

#include "il/transform/DSE.hpp"

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/analysis/MemorySSA.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/transform/AnalysisIDs.hpp"
#include "il/transform/analysis/Liveness.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::transform
{

namespace
{
struct Addr
{
    // We track by Value plus optional byte width for more precise AA queries.
    Value v;
    std::optional<unsigned> size;
};

struct AddrHash
{
    size_t operator()(const Addr &a) const noexcept
    {
        // Hash by kind and id/str
        size_t h = static_cast<size_t>(a.v.kind) * 1315423911u;
        switch (a.v.kind)
        {
            case Value::Kind::Temp:
                h ^= static_cast<size_t>(a.v.id + 0x9e3779b97f4a7c15ULL);
                break;
            case Value::Kind::NullPtr:
                h ^= 0xdeadbeefULL;
                break;
            case Value::Kind::GlobalAddr:
            case Value::Kind::ConstStr:
                h ^= std::hash<std::string>{}(a.v.str);
                break;
            default:
                h ^= 0x1234567ULL;
                break;
        }
        return h;
    }
};

struct AddrEq
{
    bool operator()(const Addr &a, const Addr &b) const noexcept
    {
        // Exact match only; potential aliasing is handled via AA when needed.
        if (a.v.kind != b.v.kind)
            return false;
        using K = Value::Kind;
        switch (a.v.kind)
        {
            case K::Temp:
                return a.v.id == b.v.id;
            case K::GlobalAddr:
            case K::ConstStr:
                return a.v.str == b.v.str;
            case K::NullPtr:
                return true;
            default:
                return false;
        }
    }
};

inline bool isStoreToTempPtr(const Instr &I)
{
    return I.op == Opcode::Store && !I.operands.empty() && I.operands[0].kind == Value::Kind::Temp;
}

inline bool isLoadFromTempPtr(const Instr &I)
{
    return I.op == Opcode::Load && !I.operands.empty() && I.operands[0].kind == Value::Kind::Temp;
}

inline std::optional<unsigned> accessSize(const Instr &I)
{
    return viper::analysis::BasicAA::typeSizeBytes(I.type);
}

} // namespace

bool runDSE(Function &F, AnalysisManager &AM)
{
    // Acquire BasicAA when available
    viper::analysis::BasicAA &AA =
        AM.getFunctionResult<viper::analysis::BasicAA>(kAnalysisBasicAA, F);
    bool changed = false;

    for (auto &B : F.blocks)
    {
        // Backward scan in the block
        std::unordered_set<Addr, AddrHash, AddrEq> killed;

        for (std::size_t i = B.instructions.size(); i-- > 0;)
        {
            Instr &I = B.instructions[i];

            // Loads block further elimination for the specific address
            if (isLoadFromTempPtr(I))
            {
                Addr a{I.operands[0], accessSize(I)};
                for (auto it = killed.begin(); it != killed.end();)
                {
                    if (AA.alias(a.v, it->v, a.size, it->size) !=
                        viper::analysis::AliasResult::NoAlias)
                        it = killed.erase(it);
                    else
                        ++it;
                }
                continue;
            }

            // Calls: conservative clobber when may Mod/Ref
            if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
            {
                auto mr = AA.modRef(I);
                if (mr != viper::analysis::ModRefResult::NoModRef)
                    killed.clear();
                continue;
            }

            // Store: if the address is already killed by a later store and no read intervened,
            // then this store is dead.
            if (isStoreToTempPtr(I))
            {
                Addr a{I.operands[0], accessSize(I)};
                bool dead = false;

                // Quick-path exact match
                if (killed.find(a) != killed.end())
                {
                    dead = true;
                }
                else
                {
                    // Check aliasing against the killed set using BasicAA
                    for (const auto &k : killed)
                    {
                        if (AA.alias(a.v, k.v, a.size, k.size) !=
                            viper::analysis::AliasResult::NoAlias)
                        {
                            // An aliasing later store exists; current is dead
                            dead = true;
                            break;
                        }
                    }
                }

                if (dead)
                {
                    B.instructions.erase(B.instructions.begin() + static_cast<std::size_t>(i));
                    changed = true;
                    // Note: do not advance i (the loop decrements i) — keep indices consistent
                    continue;
                }
                // Not dead: mark this address as killed for earlier stores
                killed.insert(a);
                continue;
            }
        }
    }

    return changed;
}

//===----------------------------------------------------------------------===//
// Cross-Block DSE
//===----------------------------------------------------------------------===//
// This extension identifies stores that are provably dead because they are
// overwritten on ALL paths before being read. It uses a forward dataflow
// approach:
// 1. For each store to alloca (non-escaping), track it as potentially dead
// 2. Walk forward through successors
// 3. Mark store as dead if all paths either:
//    - Store to the same location again (killing the original)
//    - Exit the function without reading the location
//===----------------------------------------------------------------------===//

namespace
{

/// Represents a store that might be dead
struct PendingStore
{
    BasicBlock *block;
    size_t instrIdx;
    Value ptr;
    std::optional<unsigned> size;
};

/// Check if an alloca escapes (is passed to a call or has its address taken)
bool allocaEscapes(const Function &F, unsigned allocaId)
{
    for (const auto &B : F.blocks)
    {
        for (const auto &I : B.instructions)
        {
            // Check if alloca is used in a call
            if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
            {
                for (const auto &op : I.operands)
                {
                    if (op.kind == Value::Kind::Temp && op.id == allocaId)
                        return true;
                }
            }
            // Check if address is stored somewhere
            if (I.op == Opcode::Store && I.operands.size() >= 2)
            {
                // operands[0] is dst ptr, operands[1] is value
                const auto &val = I.operands[1];
                if (val.kind == Value::Kind::Temp && val.id == allocaId)
                    return true;
            }
        }
    }
    return false;
}

/// Find the alloca ID if this is a direct pointer to a stack allocation
std::optional<unsigned> getAllocaId(const Value &ptr, const Function &F)
{
    if (ptr.kind != Value::Kind::Temp)
        return std::nullopt;

    // Search for the alloca instruction
    for (const auto &B : F.blocks)
    {
        for (const auto &I : B.instructions)
        {
            if (I.op == Opcode::Alloca && I.result && *I.result == ptr.id)
            {
                return ptr.id;
            }
        }
    }
    return std::nullopt;
}

/// Check if a block reads from the given address
bool blockReadsFrom(const BasicBlock &B,
                    const Value &ptr,
                    std::optional<unsigned> size,
                    viper::analysis::BasicAA &AA)
{
    for (const auto &I : B.instructions)
    {
        if (I.op == Opcode::Load && !I.operands.empty())
        {
            auto loadSize = viper::analysis::BasicAA::typeSizeBytes(I.type);
            if (AA.alias(I.operands[0], ptr, loadSize, size) !=
                viper::analysis::AliasResult::NoAlias)
            {
                return true;
            }
        }
        // Calls might read
        if (I.op == Opcode::Call || I.op == Opcode::CallIndirect)
        {
            auto mr = AA.modRef(I);
            if (mr == viper::analysis::ModRefResult::Ref ||
                mr == viper::analysis::ModRefResult::ModRef)
            {
                return true; // Conservative: assume call reads the address
            }
        }
    }
    return false;
}

/// Check if a block has a store that kills the pending store
bool blockKillsStore(const BasicBlock &B,
                     const Value &ptr,
                     std::optional<unsigned> size,
                     viper::analysis::BasicAA &AA)
{
    for (const auto &I : B.instructions)
    {
        if (I.op == Opcode::Store && !I.operands.empty())
        {
            auto storeSize = viper::analysis::BasicAA::typeSizeBytes(I.type);
            if (AA.alias(I.operands[0], ptr, storeSize, size) ==
                viper::analysis::AliasResult::MustAlias)
            {
                return true;
            }
        }
    }
    return false;
}

/// Get successor block labels from a terminator instruction
std::vector<std::string> getSuccessors(const BasicBlock &B)
{
    std::vector<std::string> succs;
    if (B.instructions.empty())
        return succs;
    const auto &term = B.instructions.back();
    for (const auto &label : term.labels)
    {
        succs.push_back(label);
    }
    return succs;
}

/// Check if terminator is a return instruction
bool isReturn(const BasicBlock &B)
{
    if (B.instructions.empty())
        return false;
    return B.instructions.back().op == Opcode::Ret;
}

} // namespace

/// Cross-block DSE: eliminate stores that are dead across block boundaries
bool runCrossBlockDSE(Function &F, AnalysisManager &AM)
{
    if (F.blocks.empty())
        return false;

    viper::analysis::BasicAA &AA =
        AM.getFunctionResult<viper::analysis::BasicAA>(kAnalysisBasicAA, F);

    // Build a map from block label to block pointer for successor lookup
    std::unordered_map<std::string, BasicBlock *> blockMap;
    for (auto &B : F.blocks)
    {
        blockMap[B.label] = &B;
    }

    bool changed = false;
    std::vector<std::pair<BasicBlock *, size_t>> toRemove;

    // For each block, look for stores to non-escaping allocas
    for (auto &B : F.blocks)
    {
        for (size_t i = 0; i < B.instructions.size(); ++i)
        {
            const Instr &I = B.instructions[i];
            if (I.op != Opcode::Store || I.operands.empty())
                continue;

            const Value &ptr = I.operands[0];

            // Only consider stores to allocas (stack allocations)
            auto allocaId = getAllocaId(ptr, F);
            if (!allocaId)
                continue;

            // Skip if the alloca escapes
            if (allocaEscapes(F, *allocaId))
                continue;

            auto storeSize = viper::analysis::BasicAA::typeSizeBytes(I.type);

            // Check if this store is read before being killed
            // Walk forward from this point to see if store is dead
            bool isDeadStore = true;
            bool reachedRead = false;

            // First check within same block after the store
            for (size_t j = i + 1; j < B.instructions.size(); ++j)
            {
                const Instr &next = B.instructions[j];
                if (next.op == Opcode::Load && !next.operands.empty())
                {
                    auto loadSize = viper::analysis::BasicAA::typeSizeBytes(next.type);
                    if (AA.alias(next.operands[0], ptr, loadSize, storeSize) !=
                        viper::analysis::AliasResult::NoAlias)
                    {
                        reachedRead = true;
                        isDeadStore = false;
                        break;
                    }
                }
                if (next.op == Opcode::Store && !next.operands.empty())
                {
                    auto nextSize = viper::analysis::BasicAA::typeSizeBytes(next.type);
                    if (AA.alias(next.operands[0], ptr, nextSize, storeSize) ==
                        viper::analysis::AliasResult::MustAlias)
                    {
                        // Killed by later store in same block - already handled
                        // by intra-block DSE
                        isDeadStore = false;
                        break;
                    }
                }
                // Conservative for calls
                if (next.op == Opcode::Call || next.op == Opcode::CallIndirect)
                {
                    auto mr = AA.modRef(next);
                    if (mr != viper::analysis::ModRefResult::NoModRef)
                    {
                        isDeadStore = false;
                        break;
                    }
                }
            }

            if (reachedRead || !isDeadStore)
                continue;

            // Now check successor blocks using a worklist
            std::unordered_set<std::string> visited;
            std::vector<std::string> worklist = getSuccessors(B);
            bool allPathsKill = true;

            while (!worklist.empty() && allPathsKill)
            {
                std::string label = worklist.back();
                worklist.pop_back();

                if (visited.count(label))
                    continue;
                visited.insert(label);

                auto it = blockMap.find(label);
                if (it == blockMap.end())
                {
                    allPathsKill = false;
                    continue;
                }
                BasicBlock *succ = it->second;

                // Check if this block reads from the address
                if (blockReadsFrom(*succ, ptr, storeSize, AA))
                {
                    allPathsKill = false;
                    continue;
                }

                // Check if this block kills the store
                if (blockKillsStore(*succ, ptr, storeSize, AA))
                {
                    // This path kills the store, don't need to explore further
                    continue;
                }

                // If this block returns without killing, the store might be
                // observable (unless it's truly dead-on-exit)
                if (isReturn(*succ))
                {
                    // Store is dead if we reach a return without reading
                    continue;
                }

                // Add successors to worklist
                auto nextSuccs = getSuccessors(*succ);
                for (const auto &s : nextSuccs)
                {
                    if (!visited.count(s))
                        worklist.push_back(s);
                }
            }

            if (allPathsKill && visited.size() > 0)
            {
                toRemove.emplace_back(&B, i);
            }
        }
    }

    // Remove dead stores in reverse order to preserve indices
    std::sort(toRemove.begin(),
              toRemove.end(),
              [](const auto &a, const auto &b)
              {
                  if (a.first != b.first)
                      return a.first > b.first;
                  return a.second > b.second;
              });

    for (const auto &[block, idx] : toRemove)
    {
        block->instructions.erase(block->instructions.begin() + static_cast<long>(idx));
        changed = true;
    }

    return changed;
}

/// MemorySSA-based dead store elimination.
///
/// Uses the MemorySSA analysis to discover dead stores that runCrossBlockDSE
/// misses because it conservatively treats calls as read barriers for all
/// allocas.  Since MemorySSA's dead-store computation skips calls for
/// non-escaping allocas (they cannot access non-escaping stack memory), this
/// pass eliminates stores in functions that contain runtime calls inside loops
/// or conditional branches — the most common pattern in Zia-lowered code.
bool runMemorySSADSE(Function &F, AnalysisManager &AM)
{
    viper::analysis::MemorySSA &mssa =
        AM.getFunctionResult<viper::analysis::MemorySSA>(kAnalysisMemorySSA, F);

    std::vector<std::pair<Block *, size_t>> toRemove;

    for (auto &B : F.blocks)
    {
        for (size_t i = 0; i < B.instructions.size(); ++i)
        {
            if (B.instructions[i].op == Opcode::Store && mssa.isDeadStore(&B, i))
            {
                toRemove.emplace_back(&B, i);
            }
        }
    }

    if (toRemove.empty())
        return false;

    // Erase in reverse order to keep indices stable.
    std::sort(toRemove.begin(),
              toRemove.end(),
              [](const auto &a, const auto &b)
              {
                  if (a.first != b.first)
                      return a.first > b.first;
                  return a.second > b.second;
              });

    for (const auto &[block, idx] : toRemove)
        block->instructions.erase(block->instructions.begin() + static_cast<long>(idx));

    return true;
}

} // namespace il::transform
