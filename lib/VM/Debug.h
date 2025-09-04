// File: lib/VM/Debug.h
// Purpose: Manage VM breakpoints keyed by block labels.
// Key invariants: Breakpoints compare exact block label strings.
// Ownership/Lifetime: DebugCtrl owns interned label strings and breakpoint list.
// Links: docs/dev/vm.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "support/string_interner.hpp"
#include "support/symbol.hpp"
#include <string>
#include <vector>

namespace il::vm
{

/// @brief Label-based breakpoint.
struct Breakpoint
{
    il::support::Symbol label; ///< Target block label.
};

/// @brief Collects breakpoints and evaluates block matches.
class DebugCtrl
{
  public:
    /// @brief Intern @p lbl and return its Symbol.
    il::support::Symbol internLabel(const std::string &lbl);

    /// @brief Add breakpoint for block label @p lbl.
    void addBreak(il::support::Symbol lbl);

    /// @brief Check if execution should break before entering @p blk.
    bool shouldBreak(const il::core::BasicBlock &blk) const;

  private:
    il::support::StringInterner interner; ///< Interns breakpoint labels.
    std::vector<Breakpoint> bps;          ///< Active breakpoints.
};

} // namespace il::vm
