// File: lib/Analysis/CFG.h
// Purpose: Declares control-flow graph utilities for IL blocks.
// Key invariants: No global state; queries are on-demand.
// Ownership/Lifetime: Operates on externally owned IL structures.
// Links: docs/dev/analysis.md
#pragma once

#include <vector>

namespace il::core
{
struct Function;
struct BasicBlock;
} // namespace il::core

namespace viper::analysis
{

/// @brief Return successor blocks for @p B by examining its terminator.
/// @param F Function containing @p B.
/// @param B Block to inspect.
/// @return Successor blocks in the order encoded by the terminator.
std::vector<il::core::BasicBlock *> successors(const il::core::Function &F,
                                               const il::core::BasicBlock &B);

/// @brief Return predecessors of block @p B by scanning @p F's terminators.
/// @param F Function to scan.
/// @param B Block whose predecessors are queried.
/// @return Blocks that branch to @p B.
std::vector<il::core::BasicBlock *> predecessors(const il::core::Function &F,
                                                 const il::core::BasicBlock &B);

} // namespace viper::analysis
