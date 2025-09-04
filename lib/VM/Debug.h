// File: lib/VM/Debug.h
// Purpose: Declare breakpoint control for the VM.
// Key invariants: Breakpoints are keyed by interned block labels.
// Ownership/Lifetime: DebugCtrl owns its interner and breakpoint set.
// Links: docs/dev/vm.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "support/string_interner.hpp"
#include "support/symbol.hpp"
#include <string_view>
#include <unordered_set>

namespace il::vm
{

/// @brief Breakpoint identified by a block label symbol.
struct Breakpoint
{
    il::support::Symbol label; ///< Target block label
};

/// @brief Controller for debug breakpoints.
class DebugCtrl
{
  public:
    /// @brief Intern @p label and return its symbol.
    il::support::Symbol internLabel(std::string_view label);

    /// @brief Add breakpoint for block label @p sym.
    void addBreak(il::support::Symbol sym);

    /// @brief Ignore all breakpoints when set to true.
    void setIgnoreBreaks(bool ignore);

    /// @brief Check whether entering @p blk triggers a breakpoint.
    bool shouldBreak(const il::core::BasicBlock &blk) const;

  private:
    mutable il::support::StringInterner interner_;   ///< Label interner
    std::unordered_set<il::support::Symbol> breaks_; ///< Registered breakpoints
    bool ignoreBreaks_ = false;                      ///< When true, disables breaking
};

} // namespace il::vm
