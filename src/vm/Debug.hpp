// File: src/vm/Debug.hpp
// Purpose: Declare breakpoint control and path normalization utilities for the VM.
// Key invariants: Block breakpoints use interned labels. Source line breakpoints
// match when both the line number and either the normalized path or basename are
// equal.
// Ownership/Lifetime: DebugCtrl owns its interner, breakpoint set, and source
// line list.
// Links: docs/dev/vm.md
#pragma once

#include "il/core/Type.hpp"
#include "support/string_interner.hpp"
#include "support/symbol.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace il::core
{
struct BasicBlock;
struct Instr;
} // namespace il::core

namespace il::support
{
class SourceManager;
} // namespace il::support

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

    /// @brief Check whether entering @p blk triggers a breakpoint.
    bool shouldBreak(const il::core::BasicBlock &blk) const;

    /// @brief Add breakpoint at source @p file and line @p line.
    void addBreakSrcLine(std::string file, uint32_t line);

    /// @brief Check if any source line breakpoints are registered.
    bool hasSrcLineBPs() const;

    /// @brief Check whether instruction @p I matches a source line breakpoint.
    /// A match occurs when the instruction's line number equals a breakpoint's
    /// line and the path comparison matches the user-specified granularity.
    bool shouldBreakOn(const il::core::Instr &I) const;

    /// @brief Set source manager used to resolve file paths.
    void setSourceManager(const il::support::SourceManager *sm);

    /// @brief Retrieve associated source manager.
    const il::support::SourceManager *getSourceManager() const;

    /// @brief Normalize @p p by canonicalizing separators and dot segments.
    /// Uses std::filesystem::path::lexically_normal() after converting
    /// backslashes to forward slashes to ensure stable breakpoint comparisons.
    static std::string normalizePath(std::string p);

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

    /// @brief Reset coalesced source line state.
    void resetLastHit();

  private:
    static std::pair<std::string, std::string> normalizePathWithBase(std::string path);

    mutable il::support::StringInterner interner_;   ///< Label interner
    std::unordered_set<il::support::Symbol> breaks_; ///< Registered breakpoints

    struct SrcLineBP
    {
        std::string normFile;    ///< Normalized source file path
        std::string base;        ///< Basename of source file
        uint32_t line;           ///< 1-based line number
        bool requireFullPath;    ///< True when breakpoint was defined with a path
    };

    const il::support::SourceManager *sm_ = nullptr; ///< Source manager for paths
    std::vector<SrcLineBP> srcLineBPs_;              ///< Source line breakpoints;
                                                     ///< match strictly or by basename

    mutable std::optional<std::pair<uint32_t, uint32_t>> lastHitSrc_; ///< (file id + line)

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
