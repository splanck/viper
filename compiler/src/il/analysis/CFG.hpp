//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares control flow graph (CFG) analysis utilities for IL functions.
// These functions compute successor/predecessor relationships, traversal orders,
// and structural properties needed by optimization passes, verification, and
// code generation.
//
// The CFG represents the control flow structure of a function as a directed graph
// where:
// - Nodes are basic blocks
// - Edges represent possible control flow transfers (branches, calls, returns)
// - Entry block is the first block in the function
// - Exit is any block ending with a return instruction
//
// Key Abstractions:
//
// CFGContext: Caching layer that precomputes and stores CFG metadata to avoid
// redundant traversals. Constructed once per function, stores label→block maps,
// successor/predecessor lists, and block→function relationships. Must be rebuilt
// if the CFG structure changes.
//
// Successor Queries: Extract outgoing edges from a block by examining its
// terminator instruction (br, cbr, switch, ret). Cached in CFGContext for
// repeated queries.
//
// Predecessor Queries: Inverse of successors - which blocks can reach this block.
// Computed by inverting the successor relation. Cached in CFGContext.
//
// Traversal Orders:
// - Post-order: DFS traversal where blocks appear after their descendants
// - Reverse post-order (RPO): Reverse of post-order (entry block first)
// - Topological order: Only defined for acyclic graphs (DAGs)
//
// Use Cases:
// - Dominator analysis: Requires RPO traversal
// - Data flow analysis: Uses predecessor/successor relationships
// - Loop detection: Checks for back edges in post-order
// - Code generation: Topological order for straight-line scheduling
// - Verification: Ensures all blocks are reachable from entry
//
// Design Decisions:
// - Eager caching: CFGContext precomputes all relationships on construction
// - Immutable queries: All functions take const references (read-only)
// - Function-scoped: CFG utilities operate on individual functions, not modules
// - Light-weight: No heavy graph data structures, just vectors and maps
//
// Performance Characteristics:
// - CFGContext construction: O(B + E) where B = blocks, E = edges
// - Successor query: O(1) with caching, O(k) without (k = successor count)
// - Traversal orders: O(B + E) with caching
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
