// File: src/il/analysis/CFG.hpp
// Purpose: Minimal control-flow graph utilities for IL blocks and functions.
// Key invariants: Successor lookups reuse precomputed label maps per function.
// Ownership/Lifetime: Operates on IL structures owned by caller.
// Links: docs/dev/analysis.md
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace il::core
{
struct Module;
struct Function;
struct BasicBlock;
using Block = BasicBlock;
} // namespace il::core

namespace viper::analysis
{

/// @brief Lightweight context bundling module information for CFG queries.
///
/// Stores a reference to the active module alongside a lookup table mapping
/// basic blocks to their owning functions. The mapping is computed eagerly on
/// construction so subsequent CFG utilities can resolve block parents without
/// global state. The caller is responsible for rebuilding the context if the
/// module's function/block layout changes.
struct CFGContext
{
    explicit CFGContext(il::core::Module &module);

    il::core::Module *module{nullptr};
    std::unordered_map<const il::core::Block *, il::core::Function *> blockToFunction;
    /// @brief Cache mapping function pointers to their blocks indexed by label.
    std::unordered_map<il::core::Function *, std::unordered_map<std::string, il::core::Block *>>
        functionLabelToBlock;
};

/// @brief Return successors of block @p B by inspecting its terminator.
/// @param B Block whose outgoing edges are requested.
/// @return List of successor blocks (may be empty).
std::vector<il::core::Block *> successors(const CFGContext &ctx, const il::core::Block &B);

/// @brief Return predecessors of block @p B within function @p F.
/// @param F Function containing blocks to scan.
/// @param B Target block whose incoming edges are requested.
/// @return List of predecessor blocks (may be empty).
std::vector<il::core::Block *> predecessors(const CFGContext &ctx, const il::core::Block &B);

/// @brief Compute DFS post-order of blocks in @p F starting from the entry block.
/// @param F Function whose blocks are traversed.
/// @return Blocks in post-order; the entry block is last.
std::vector<il::core::Block *> postOrder(const CFGContext &ctx, il::core::Function &F);

/// @brief Compute reverse post-order (RPO) of blocks in @p F.
/// @param F Function whose blocks are traversed.
/// @return Blocks in RPO; the entry block is first.
std::vector<il::core::Block *> reversePostOrder(const CFGContext &ctx, il::core::Function &F);

/// @brief Check whether the control-flow graph of @p F has no cycles.
/// @param F Function whose CFG is inspected.
/// @return True if the CFG is acyclic; false otherwise.
bool isAcyclic(const CFGContext &ctx, il::core::Function &F);

/// @brief Compute a topological order of blocks in @p F.
/// @param F Function whose blocks are ordered.
/// @return Blocks in topological order; empty if @p F contains cycles.
std::vector<il::core::Block *> topoOrder(const CFGContext &ctx, il::core::Function &F);

} // namespace viper::analysis
