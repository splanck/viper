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
#include "il/transform/LoadSafety.hpp"
#include "il/transform/analysis/Liveness.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::transform {

namespace {
using InstructionSite = std::pair<Block *, size_t>;

/// @brief Erase instruction sites without relying on raw pointer ordering.
/// @details Sites are sorted by the owning block's index in @p F and then by
///          descending instruction index inside each block. This keeps same-block
///          erasures stable while avoiding relational comparisons between
///          unrelated @ref BasicBlock pointers.
/// @param F Function that owns every block referenced in @p sites.
/// @param sites Block/instruction pairs to remove in place.
/// @return Number of instructions erased.
std::size_t eraseInstructionSites(Function &F, std::vector<InstructionSite> &sites) {
    std::unordered_map<const Block *, std::size_t> blockOrder;
    blockOrder.reserve(F.blocks.size());
    for (std::size_t i = 0; i < F.blocks.size(); ++i)
        blockOrder.emplace(&F.blocks[i], i);

    std::sort(sites.begin(), sites.end(), [&](const auto &a, const auto &b) {
        const std::size_t aOrder = blockOrder.at(a.first);
        const std::size_t bOrder = blockOrder.at(b.first);
        if (aOrder != bOrder)
            return aOrder < bOrder;
        return a.second > b.second;
    });

    std::size_t erased = 0;
    for (const auto &[block, idx] : sites) {
        block->instructions.erase(block->instructions.begin() + static_cast<long>(idx));
        ++erased;
    }
    return erased;
}

struct Addr {
    // We track by Value plus optional byte width for more precise AA queries.
    Value v;
    std::optional<unsigned> size;
};

struct AddrHash {
    size_t operator()(const Addr &a) const noexcept {
        // Hash by kind and id/str
        size_t h = static_cast<size_t>(a.v.kind) * 1315423911u;
        switch (a.v.kind) {
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
        if (a.size)
            h ^= static_cast<size_t>(*a.size) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

struct AddrEq {
    bool operator()(const Addr &a, const Addr &b) const noexcept {
        // Exact match only; potential aliasing is handled via AA when needed.
        if (a.v.kind != b.v.kind)
            return false;
        if (a.size != b.size)
            return false;
        using K = Value::Kind;
        switch (a.v.kind) {
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

inline bool isStoreToTempPtr(const Instr &I) {
    return I.op == Opcode::Store && !I.operands.empty() && I.operands[0].kind == Value::Kind::Temp;
}

inline bool isLoadFromTempPtr(const Instr &I) {
    return I.op == Opcode::Load && !I.operands.empty() && I.operands[0].kind == Value::Kind::Temp;
}

inline std::optional<unsigned> accessSize(const Instr &I) {
    return viper::analysis::BasicAA::typeSizeBytes(I.type);
}

bool fullyOverwrites(const Value &laterPtr,
                     std::optional<unsigned> laterSize,
                     const Value &earlierPtr,
                     std::optional<unsigned> earlierSize,
                     viper::analysis::BasicAA &AA) {
    if (!laterSize || !earlierSize)
        return false;
    if (*laterSize < *earlierSize)
        return false;
    return AA.alias(laterPtr, earlierPtr, laterSize, earlierSize) ==
           viper::analysis::AliasResult::MustAlias;
}

} // namespace

/// @brief Run dse.
bool runDSE(Function &F, AnalysisManager &AM) {
    // Acquire BasicAA when available
    viper::analysis::BasicAA &AA =
        AM.getFunctionResult<viper::analysis::BasicAA>(kAnalysisBasicAA, F);
    bool changed = false;

    for (auto &B : F.blocks) {
        // Backward scan in the block
        std::unordered_set<Addr, AddrHash, AddrEq> killed;

        for (std::size_t i = B.instructions.size(); i-- > 0;) {
            Instr &I = B.instructions[i];

            // Loads block further elimination for the specific address
            if (isLoadFromTempPtr(I)) {
                Addr a{I.operands[0], accessSize(I)};
                for (auto it = killed.begin(); it != killed.end();) {
                    if (AA.alias(a.v, it->v, a.size, it->size) !=
                        viper::analysis::AliasResult::NoAlias)
                        it = killed.erase(it);
                    else
                        ++it;
                }
                continue;
            }

            // Calls: conservative clobber when may Mod/Ref
            if (I.op == Opcode::Call || I.op == Opcode::CallIndirect) {
                auto mr = AA.modRef(I);
                if (mr != viper::analysis::ModRefResult::NoModRef)
                    killed.clear();
                continue;
            }

            // Store: if the address is already killed by a later store and no read intervened,
            // then this store is dead.
            if (isStoreToTempPtr(I)) {
                Addr a{I.operands[0], accessSize(I)};
                bool dead = false;

                // A later store kills this one only if it is proven to start at
                // the same address and fully cover the earlier write. MayAlias
                // is not enough: it may be a distinct pointer.
                for (const auto &k : killed) {
                    if (fullyOverwrites(k.v, k.size, a.v, a.size, AA)) {
                        dead = true;
                        break;
                    }
                }

                if (dead && isStoreKnownNonTrapping(F, I)) {
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

namespace {

/// Represents a store that might be dead
struct PendingStore {
    BasicBlock *block;
    size_t instrIdx;
    Value ptr;
    std::optional<unsigned> size;
};

struct DefInfo {
    Opcode op;
    std::vector<Value> operands;
};

using RootMap = std::unordered_map<unsigned, std::unordered_set<unsigned>>;

bool hasExceptionHandling(const Function &F) {
    for (const auto &B : F.blocks) {
        for (const auto &I : B.instructions) {
            switch (I.op) {
                case Opcode::EhPush:
                case Opcode::EhPop:
                case Opcode::EhEntry:
                case Opcode::ResumeSame:
                case Opcode::ResumeNext:
                case Opcode::ResumeLabel:
                    return true;
                default:
                    break;
            }
        }
    }
    return false;
}

std::unordered_map<unsigned, DefInfo> collectDefs(const Function &F) {
    std::unordered_map<unsigned, DefInfo> defs;
    for (const auto &B : F.blocks) {
        for (const auto &I : B.instructions) {
            if (I.result)
                defs.emplace(*I.result, DefInfo{I.op, I.operands});
        }
    }
    return defs;
}

RootMap computeAllocaRoots(const Function &F, const std::unordered_map<unsigned, DefInfo> &defs) {
    RootMap roots;
    roots.reserve(defs.size());
    for (const auto &[id, def] : defs)
        if (def.op == Opcode::Alloca)
            roots[id].insert(id);

    std::unordered_map<std::string, const BasicBlock *> blocksByLabel;
    blocksByLabel.reserve(F.blocks.size());
    for (const auto &B : F.blocks)
        blocksByLabel.emplace(B.label, &B);

    auto mergeRoots = [&](unsigned dst, const Value &src) {
        if (src.kind != Value::Kind::Temp)
            return false;
        auto srcIt = roots.find(src.id);
        if (srcIt == roots.end())
            return false;
        auto &dstRoots = roots[dst];
        bool changed = false;
        for (unsigned root : srcIt->second)
            changed |= dstRoots.insert(root).second;
        return changed;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &[id, def] : defs)
            if (def.op == Opcode::GEP && !def.operands.empty())
                changed |= mergeRoots(id, def.operands[0]);

        for (const auto &B : F.blocks) {
            for (const auto &I : B.instructions) {
                for (size_t edge = 0; edge < I.labels.size() && edge < I.brArgs.size(); ++edge) {
                    auto targetIt = blocksByLabel.find(I.labels[edge]);
                    if (targetIt == blocksByLabel.end())
                        continue;
                    const auto *target = targetIt->second;
                    const auto &args = I.brArgs[edge];
                    const size_t count = std::min(args.size(), target->params.size());
                    for (size_t idx = 0; idx < count; ++idx)
                        changed |= mergeRoots(target->params[idx].id, args[idx]);
                }
            }
        }
    }

    return roots;
}

/// Find the root alloca ID if this pointer is an alloca or a GEP derived from one.
std::optional<unsigned> getAllocaId(const Value &ptr,
                                    const std::unordered_map<unsigned, DefInfo> &defs,
                                    unsigned depth = 0) {
    if (ptr.kind != Value::Kind::Temp || depth > 8)
        return std::nullopt;

    auto it = defs.find(ptr.id);
    if (it == defs.end())
        return std::nullopt;

    if (it->second.op == Opcode::Alloca)
        return ptr.id;

    if (it->second.op == Opcode::GEP && !it->second.operands.empty())
        return getAllocaId(it->second.operands[0], defs, depth + 1);

    return std::nullopt;
}

/// Check if an alloca escapes, including through GEP-derived pointer values.
bool allocaEscapes(const Function &F, unsigned allocaId, const RootMap &roots) {
    auto containsAlloca = [&](const Value &value) {
        if (value.kind != Value::Kind::Temp)
            return false;
        auto rootIt = roots.find(value.id);
        return rootIt != roots.end() && rootIt->second.count(allocaId) != 0;
    };

    for (const auto &B : F.blocks) {
        for (const auto &I : B.instructions) {
            // Check if alloca is used in a call
            if (I.op == Opcode::Call || I.op == Opcode::CallIndirect) {
                for (const auto &op : I.operands)
                    if (containsAlloca(op))
                        return true;
            }
            // Check if address is stored somewhere
            if (I.op == Opcode::Store && I.operands.size() >= 2) {
                // operands[0] is dst ptr, operands[1] is value
                const auto &val = I.operands[1];
                if (containsAlloca(val))
                    return true;
            }
            if (I.op == Opcode::Ret) {
                for (const auto &op : I.operands)
                    if (containsAlloca(op))
                        return true;
            }
        }
    }
    return false;
}

/// Get successor block labels from a terminator instruction
std::vector<std::string> getSuccessors(const BasicBlock &B) {
    std::vector<std::string> succs;
    if (B.instructions.empty())
        return succs;
    const auto &term = B.instructions.back();
    for (const auto &label : term.labels) {
        succs.push_back(label);
    }
    return succs;
}

/// @brief Determine whether @p B ends with an explicit function exit.
/// @details Cross-block DSE treats return and trap terminators as safe path
///          endpoints because no later instruction can observe the stored value
///          on that path.  Malformed blocks with no terminator or non-exiting
///          terminators are rejected by returning false, which keeps the analysis
///          conservative when the CFG is incomplete.
/// @param B Basic block whose final instruction is inspected.
/// @return True when @p B terminates with `ret`, `trap`, or `trap_from_err`.
bool isFunctionExit(const BasicBlock &B) {
    if (B.instructions.empty())
        return false;
    switch (B.instructions.back().op) {
        case Opcode::Ret:
        case Opcode::Trap:
        case Opcode::TrapFromErr:
            return true;
        default:
            return false;
    }
}

} // namespace

/// Cross-block DSE: eliminate stores that are dead across block boundaries
bool runCrossBlockDSE(Function &F, AnalysisManager &AM) {
    if (F.blocks.empty())
        return false;
    if (hasExceptionHandling(F))
        return false;

    viper::analysis::BasicAA &AA =
        AM.getFunctionResult<viper::analysis::BasicAA>(kAnalysisBasicAA, F);

    // Build a map from block label to block pointer for successor lookup
    std::unordered_map<std::string, BasicBlock *> blockMap;
    for (auto &B : F.blocks) {
        blockMap[B.label] = &B;
    }

    bool changed = false;
    std::vector<std::pair<BasicBlock *, size_t>> toRemove;
    const auto defs = collectDefs(F);
    const auto roots = computeAllocaRoots(F, defs);

    // For each block, look for stores to non-escaping allocas
    for (auto &B : F.blocks) {
        for (size_t i = 0; i < B.instructions.size(); ++i) {
            const Instr &I = B.instructions[i];
            if (I.op != Opcode::Store || I.operands.empty())
                continue;

            const Value &ptr = I.operands[0];
            if (!isStoreKnownNonTrapping(F, I))
                continue;

            // Only consider stores to allocas (stack allocations)
            auto allocaId = getAllocaId(ptr, defs);
            if (!allocaId)
                continue;

            // Skip if the alloca escapes
            if (allocaEscapes(F, *allocaId, roots))
                continue;

            auto storeSize = viper::analysis::BasicAA::typeSizeBytes(I.type);

            // Check if this store is read before being killed
            // Walk forward from this point to see if store is dead
            bool isDeadStore = true;
            bool reachedRead = false;

            // First check within same block after the store
            for (size_t j = i + 1; j < B.instructions.size(); ++j) {
                const Instr &next = B.instructions[j];
                if (next.op == Opcode::Load && !next.operands.empty()) {
                    auto loadSize = viper::analysis::BasicAA::typeSizeBytes(next.type);
                    if (AA.alias(next.operands[0], ptr, loadSize, storeSize) !=
                        viper::analysis::AliasResult::NoAlias) {
                        reachedRead = true;
                        isDeadStore = false;
                        break;
                    }
                }
                if (next.op == Opcode::Store && !next.operands.empty()) {
                    auto nextSize = viper::analysis::BasicAA::typeSizeBytes(next.type);
                    if (fullyOverwrites(next.operands[0], nextSize, ptr, storeSize, AA)) {
                        // Killed by later store in same block - already handled
                        // by intra-block DSE
                        isDeadStore = false;
                        break;
                    }
                }
                // Conservative for calls
                if (next.op == Opcode::Call || next.op == Opcode::CallIndirect) {
                    auto mr = AA.modRef(next);
                    if (mr != viper::analysis::ModRefResult::NoModRef) {
                        isDeadStore = false;
                        break;
                    }
                }
            }

            if (reachedRead || !isDeadStore)
                continue;

            const auto successors = getSuccessors(B);
            if (successors.empty())
                continue;

            std::unordered_map<std::string, bool> memo;
            std::unordered_set<std::string> visiting;
            std::function<bool(const std::string &)> allPathsKillOrExit =
                [&](const std::string &label) -> bool {
                if (auto memoIt = memo.find(label); memoIt != memo.end())
                    return memoIt->second;

                if (!visiting.insert(label).second)
                    return false; // A cycle may preserve the original store into a later read.

                auto finish = [&](bool value) {
                    visiting.erase(label);
                    memo[label] = value;
                    return value;
                };

                auto it = blockMap.find(label);
                if (it == blockMap.end())
                    return finish(false);

                BasicBlock *succ = it->second;
                for (const auto &next : succ->instructions) {
                    if (next.op == Opcode::Load && !next.operands.empty()) {
                        auto loadSize = viper::analysis::BasicAA::typeSizeBytes(next.type);
                        if (AA.alias(next.operands[0], ptr, loadSize, storeSize) !=
                            viper::analysis::AliasResult::NoAlias)
                            return finish(false);
                    }
                    if (next.op == Opcode::Store && !next.operands.empty()) {
                        auto nextSize = viper::analysis::BasicAA::typeSizeBytes(next.type);
                        if (fullyOverwrites(next.operands[0], nextSize, ptr, storeSize, AA))
                            return finish(true);
                    }
                    if (next.op == Opcode::Call || next.op == Opcode::CallIndirect) {
                        auto mr = AA.modRef(next);
                        if (mr != viper::analysis::ModRefResult::NoModRef)
                            return finish(false);
                    }
                }

                if (isFunctionExit(*succ))
                    return finish(true);

                const auto nextSuccs = getSuccessors(*succ);
                if (nextSuccs.empty())
                    return finish(false);

                for (const auto &succLabel : nextSuccs)
                    if (!allPathsKillOrExit(succLabel))
                        return finish(false);
                return finish(true);
            };

            bool allPathsKill = true;
            for (const auto &label : successors) {
                if (!allPathsKillOrExit(label)) {
                    allPathsKill = false;
                    break;
                }
            }

            if (allPathsKill) {
                toRemove.emplace_back(&B, i);
            }
        }
    }

    changed = eraseInstructionSites(F, toRemove) != 0;

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
bool runMemorySSADSE(Function &F, AnalysisManager &AM) {
    if (hasExceptionHandling(F))
        return false;

    viper::analysis::MemorySSA &mssa =
        AM.getFunctionResult<viper::analysis::MemorySSA>(kAnalysisMemorySSA, F);

    std::vector<InstructionSite> toRemove;

    for (auto &B : F.blocks) {
        for (size_t i = 0; i < B.instructions.size(); ++i) {
            if (B.instructions[i].op == Opcode::Store && mssa.isDeadStore(&B, i) &&
                isStoreKnownNonTrapping(F, B.instructions[i])) {
                toRemove.emplace_back(&B, i);
            }
        }
    }

    if (toRemove.empty())
        return false;

    return eraseInstructionSites(F, toRemove) != 0;
}

} // namespace il::transform
