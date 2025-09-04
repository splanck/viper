// File: lib/VM/Debug.h
// Purpose: Declare breakpoint control for the VM.
// Key invariants: Breakpoints are keyed by interned block labels.
// Ownership/Lifetime: DebugCtrl owns its interner and breakpoint set.
// Links: docs/dev/vm.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Type.hpp"
#include "support/source_manager.hpp"
#include "support/string_interner.hpp"
#include "support/symbol.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::vm
{

/// @brief Breakpoint identified by a block label symbol.
struct Breakpoint
{
    il::support::Symbol label; ///< Target block label
};

/// @brief Breakpoint identified by source file and line.
struct SrcLineBP
{
    std::string file; ///< Source file path (exact match)
    int line = 0;     ///< 1-based line number
};

/// @brief Controller for debug breakpoints.
class DebugCtrl
{
  public:
    /// @brief Intern @p label and return its symbol.
    il::support::Symbol internLabel(std::string_view label);

    /// @brief Add breakpoint for block label @p sym.
    void addBreak(il::support::Symbol sym);

    /// @brief Check whether entering @p blk triggers a breakpoint.
    bool shouldBreak(const il::core::BasicBlock &blk) const;

    /// @brief Add breakpoint for exact source file @p file and line @p line.
    void addBreakSrcLine(std::string file, int line);

    /// @brief Whether any source line breakpoints are registered.
    bool hasSrcLineBPs() const;

    /// @brief Check whether instruction @p I matches a source line breakpoint.
    bool shouldBreakOn(const il::core::Instr &I) const;

    /// @brief Provide source manager for resolving file identifiers.
    void setSourceManager(const il::support::SourceManager *sm)
    {
        sm_ = sm;
    }

    /// @brief Access the source manager.
    const il::support::SourceManager *sourceManager() const
    {
        return sm_;
    }

    /// @brief Register a watch on variable @p name.
    void addWatch(std::string_view name);

    /// @brief Record store to watched variable.
    void onStore(std::string_view name,
                 il::core::Type::Kind ty,
                 int64_t i64,
                 double f64,
                 std::string_view fn,
                 std::string_view blk,
                 size_t ip);

  private:
    mutable il::support::StringInterner interner_;   ///< Label interner
    std::unordered_set<il::support::Symbol> breaks_; ///< Registered breakpoints
    std::vector<SrcLineBP> srcLineBPs_;             ///< Source line breakpoints
    const il::support::SourceManager *sm_ = nullptr; ///< For resolving file paths

    struct WatchEntry
    {
        il::core::Type::Kind type = il::core::Type::Kind::Void;
        int64_t i64 = 0;
        double f64 = 0.0;
        bool hasValue = false;
    };

    std::unordered_map<il::support::Symbol, WatchEntry> watches_; ///< Watched variables
};

} // namespace il::vm
