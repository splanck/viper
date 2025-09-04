// File: lib/VM/Debug.h
// Purpose: Declare breakpoint control for the VM.
// Key invariants: Breakpoints are keyed by interned block labels.
// Ownership/Lifetime: DebugCtrl owns its interner and breakpoint set.
// Links: docs/dev/vm.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "support/source_manager.hpp"
#include "support/string_interner.hpp"
#include "support/symbol.hpp"
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::vm
{

/// @brief Breakpoint on either a block label or source line.
struct Breakpoint
{
    /// @brief Kinds of breakpoints.
    enum class Kind
    {
        Label,  ///< Identified by block label
        SrcLine ///< Identified by source file and line
    } kind{Kind::Label};

    /// @brief Construct label breakpoint.
    explicit Breakpoint(il::support::Symbol l) : kind(Kind::Label), label(l) {}

    /// @brief Construct source line breakpoint.
    Breakpoint(il::support::Symbol f, int ln) : kind(Kind::SrcLine)
    {
        src.file = f;
        src.line = ln;
    }

    union
    {
        il::support::Symbol label; ///< Target block label

        struct
        {
            il::support::Symbol file; ///< Source file symbol
            int line;                 ///< Source line number
        } src;
    };
};

/// @brief Controller for debug breakpoints.
class DebugCtrl
{
  public:
    /// @brief Intern @p label and return its symbol.
    il::support::Symbol internLabel(std::string_view label);

    /// @brief Add breakpoint for block label @p sym.
    void addBreak(il::support::Symbol sym);

    /// @brief Add breakpoint for source @p file and @p line.
    void addBreak(il::support::Symbol file, int line);

    /// @brief Check whether entering @p blk triggers a breakpoint.
    bool shouldBreak(const il::core::BasicBlock &blk) const;

    /// @brief Check whether executing @p in triggers a source breakpoint.
    bool shouldBreak(const il::core::Instr &in, const il::support::SourceManager *sm) const;

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
    mutable il::support::StringInterner interner_; ///< String interner
    mutable std::vector<Breakpoint> breaks_;       ///< Registered breakpoints

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
