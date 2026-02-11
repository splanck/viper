//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/analysis/CFG.hpp
// Purpose: Control flow graph analysis utilities for IL functions -- successor/
//          predecessor queries, DFS post-order, reverse post-order, topological
//          order, and acyclicity testing. Provides the CFGContext caching layer
//          that precomputes and stores CFG metadata for efficient reuse.
// Key invariants:
//   - CFGContext must be rebuilt if the module's function/block layout changes.
//   - All query functions take const references and are read-only.
//   - Traversal orders assume entry block is the first block in the function.
// Ownership/Lifetime: CFGContext owns its internal maps and caches. Created
//          per-module; callers must ensure the referenced module outlives the
//          context. Query functions borrow state from the context.
// Links: il/analysis/Dominators.hpp, il/core/Function.hpp,
//        il/core/BasicBlock.hpp
//
//===----------------------------------------------------------------------===//

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
/// basic blocks to their owning functions. Successor and predecessor lists are
/// computed eagerly so subsequent CFG utilities reuse cached edge data without
/// rescanning block terminators. The caller is responsible for rebuilding the
/// context if the module's function/block layout changes.
struct CFGContext
{
    explicit CFGContext(il::core::Module &module);

    il::core::Module *module{nullptr};
    std::unordered_map<const il::core::Block *, il::core::Function *> blockToFunction;
    /// @brief Cache mapping function pointers to their blocks indexed by label.
    std::unordered_map<il::core::Function *, std::unordered_map<std::string, il::core::Block *>>
        functionLabelToBlock;
    /// @brief Cached successor lists per block constructed eagerly.
    std::unordered_map<const il::core::Block *, std::vector<il::core::Block *>> blockSuccessors;
    /// @brief Cached predecessor lists derived from the successor cache.
    std::unordered_map<const il::core::Block *, std::vector<il::core::Block *>> blockPredecessors;
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
