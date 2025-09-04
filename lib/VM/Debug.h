// File: lib/VM/Debug.h
// Purpose: Declare breakpoint control for the VM.
// Key invariants: Breakpoints are keyed by interned block labels or
//                 file+line pairs.
// Ownership/Lifetime: DebugCtrl owns its interner and breakpoint set.
// Links: docs/dev/vm.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "support/string_interner.hpp"
#include "support/symbol.hpp"
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::support
{
class SourceManager;
} // namespace il::support

namespace il::vm
{

/// @brief Breakpoint either by block label or source location.
struct Breakpoint
{
    /// @brief Kinds of breakpoint.
    enum Kind
    {
        Label,  ///< Break on block entry
        SrcLine ///< Break on source file and line
    } kind{Label};

    union
    {
        il::support::Symbol label; ///< Target block label

        struct
        {
            il::support::Symbol file; ///< Source file symbol
            int line;                 ///< 1-based source line
        } src;                        ///< Source location breakpoint
    };

    /// @brief Construct label breakpoint.
    explicit Breakpoint(il::support::Symbol l) : kind(Label), label(l) {}

    /// @brief Construct source line breakpoint.
    Breakpoint(il::support::Symbol f, int ln) : kind(SrcLine)
    {
        src.file = f;
        src.line = ln;
    }
};

/// @brief Controller for debug breakpoints.
class DebugCtrl
{
  public:
    /// @brief Intern @p label and return its symbol.
    il::support::Symbol internLabel(std::string_view label);

    /// @brief Add breakpoint for block label @p sym.
    void addBreak(il::support::Symbol sym);

    /// @brief Add breakpoint for source file @p file and line @p line.
    void addBreak(il::support::Symbol file, int line);

    /// @brief Check whether entering @p blk triggers a label breakpoint.
    bool shouldBreak(const il::core::BasicBlock &blk) const;

    /// @brief Check whether executing @p in triggers a source-line breakpoint.
    bool shouldBreak(const il::core::Instr &in) const;

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

    /// @brief Attach source manager for file path resolution.
    void setSourceManager(const il::support::SourceManager *sm)
    {
        sm_ = sm;
    }

  private:
    mutable il::support::StringInterner interner_;   ///< String interner
    std::vector<Breakpoint> breaks_;                 ///< Registered breakpoints
    const il::support::SourceManager *sm_ = nullptr; ///< Source manager

    struct SrcPos
    {
        il::support::Symbol file;
        int line;
    };

    mutable std::optional<SrcPos> lastSrc_; ///< Last triggered source location

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
