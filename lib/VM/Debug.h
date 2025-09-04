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
#include <vector>

namespace il::vm
{

/// @brief Debug breakpoint identified by label or source location.
struct Breakpoint
{
    /// @brief Breakpoint kinds.
    enum class Kind
    {
        Label,  ///< Break on basic block label
        SrcLine ///< Break on source file and line
    };

    Kind kind; ///< Active breakpoint kind

    union
    {
        il::support::Symbol label; ///< Target block label

        struct
        {
            std::string file; ///< Source file path
            int line;         ///< 1-based line number
        } src;                ///< Source location breakpoint
    };

    /// @brief Construct label breakpoint for @p sym.
    explicit Breakpoint(il::support::Symbol sym);

    /// @brief Construct source breakpoint for @p file:@p line.
    Breakpoint(std::string file, int line);

    /// @brief Copy constructor.
    Breakpoint(const Breakpoint &other);

    /// @brief Move constructor.
    Breakpoint(Breakpoint &&other) noexcept;

    /// @brief Copy assignment.
    Breakpoint &operator=(const Breakpoint &other);

    /// @brief Move assignment.
    Breakpoint &operator=(Breakpoint &&other) noexcept;

    /// @brief Destroy breakpoint.
    ~Breakpoint();
};

/// @brief Controller for debug breakpoints.
class DebugCtrl
{
  public:
    /// @brief Create controller optionally tied to source manager @p sm.
    explicit DebugCtrl(const il::support::SourceManager *sm = nullptr) : sm_(sm) {}

    /// @brief Intern @p label and return its symbol.
    il::support::Symbol internLabel(std::string_view label);

    /// @brief Add breakpoint for block label @p sym.
    void addBreak(il::support::Symbol sym);

    /// @brief Add source line breakpoint @p file:@p line.
    void addBreak(std::string file, int line);

    /// @brief Check whether entering @p blk triggers a label breakpoint.
    bool shouldBreak(const il::core::BasicBlock &blk) const;

    /// @brief Check whether executing instruction at @p loc triggers a source breakpoint.
    bool shouldBreak(const il::support::SourceLoc &loc);

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
    mutable il::support::StringInterner interner_; ///< Label interner
    std::vector<Breakpoint> breaks_;               ///< Registered breakpoints
    const il::support::SourceManager *sm_;         ///< Optional source manager

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
