// File: lib/VM/Debug.h
// Purpose: Manage VM breakpoints by block label.
// Key invariants: Breakpoints compare interned symbols; controller has no ownership of interner.
// Ownership/Lifetime: DebugCtrl holds non-owning pointer to StringInterner; breakpoints stored by
// value. Links: docs/dev/vm.md
#pragma once

#include "support/symbol.hpp"
#include <unordered_set>

namespace il::core
{
struct BasicBlock;
} // namespace il::core

namespace il::support
{
class StringInterner;
} // namespace il::support

namespace il::vm
{

/// @brief Breakpoint identified by a block label symbol.
struct Breakpoint
{
    il::support::Symbol label; ///< Label of block where execution halts
};

/// @brief Controller managing breakpoints for the VM.
class DebugCtrl
{
  public:
    /// @brief Construct controller using symbol interner @p si.
    DebugCtrl(il::support::StringInterner *si = nullptr);

    /// @brief Add breakpoint for block label @p label.
    void addBreak(il::support::Symbol label);

    /// @brief Check whether execution should break on block @p blk.
    /// @param blk Block about to be entered.
    /// @return True if a breakpoint matches @p blk.
    bool shouldBreak(const il::core::BasicBlock &blk) const;

  private:
    il::support::StringInterner *interner;        ///< Non-owning interner
    std::unordered_set<il::support::Symbol> brks; ///< Set of breakpoint labels
};

} // namespace il::vm
