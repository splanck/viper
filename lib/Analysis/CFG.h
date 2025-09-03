// File: lib/Analysis/CFG.h
// Purpose: Minimal control-flow graph utilities for IL blocks and functions.
// Key invariants: Computes edges on demand without caching.
// Ownership/Lifetime: Operates on IL structures owned by caller.
// Links: docs/dev/analysis.md
#pragma once

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

/// @brief Set the current module for CFG queries.
/// @param M IL module providing function/block context.
void setModule(const il::core::Module &M);

/// @brief Return successors of block @p B by inspecting its terminator.
/// @param B Block whose outgoing edges are requested.
/// @return List of successor blocks (may be empty).
std::vector<il::core::Block *> successors(const il::core::Block &B);

/// @brief Return predecessors of block @p B within function @p F.
/// @param F Function containing blocks to scan.
/// @param B Target block whose incoming edges are requested.
/// @return List of predecessor blocks (may be empty).
std::vector<il::core::Block *> predecessors(const il::core::Function &F, const il::core::Block &B);

/// @brief Compute DFS post-order of blocks in @p F starting from the entry block.
/// @param F Function whose blocks are traversed.
/// @return Blocks in post-order; the entry block is last.
std::vector<il::core::Block *> postOrder(il::core::Function &F);

/// @brief Compute reverse post-order (RPO) of blocks in @p F.
/// @param F Function whose blocks are traversed.
/// @return Blocks in RPO; the entry block is first.
std::vector<il::core::Block *> reversePostOrder(il::core::Function &F);

} // namespace viper::analysis
