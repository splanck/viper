// File: lib/VM/Debug.h
// Purpose: Declare breakpoint control for the VM.
// Key invariants: Breakpoints are keyed by interned block labels.
// Ownership/Lifetime: DebugCtrl owns its interner and breakpoint set.
// Links: docs/dev/vm.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Type.hpp"
#include "support/string_interner.hpp"
#include "support/symbol.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::vm
{

/// @brief Debug breakpoint by label or source location.
struct Breakpoint
{
    /// @brief Kinds of breakpoints.
    enum class Kind
    {
        Label,   ///< Break before block with matching label
        SrcLine, ///< Break before instruction at file:line
    } kind;

    union
    {
        il::support::Symbol label; ///< Target block label

        struct
        {
            std::string file; ///< Source file path
            int line;         ///< 1-based line number
        } src;
    };

    /// @brief Construct label breakpoint.
    explicit Breakpoint(il::support::Symbol l) : kind(Kind::Label), label(l) {}

    /// @brief Construct source line breakpoint.
    Breakpoint(std::string f, int ln) : kind(Kind::SrcLine)
    {
        new (&src) decltype(src){std::move(f), ln};
    }

    /// @brief Copy constructor.
    Breakpoint(const Breakpoint &o) : kind(o.kind)
    {
        if (kind == Kind::Label)
            label = o.label;
        else
            new (&src) decltype(src){o.src.file, o.src.line};
    }

    /// @brief Assignment operator.
    Breakpoint &operator=(const Breakpoint &o)
    {
        if (this == &o)
            return *this;
        this->~Breakpoint();
        kind = o.kind;
        if (kind == Kind::Label)
            label = o.label;
        else
            new (&src) decltype(src){o.src.file, o.src.line};
        return *this;
    }

    /// @brief Destructor.
    ~Breakpoint()
    {
        if (kind == Kind::SrcLine)
            src.file.~basic_string();
    }
};

/// @brief Controller for debug breakpoints.
class DebugCtrl
{
  public:
    /// @brief Intern @p label and return its symbol.
    il::support::Symbol internLabel(std::string_view label);

    /// @brief Add breakpoint for block label @p sym.
    void addBreakLabel(il::support::Symbol sym);

    /// @brief Add breakpoint for source file and line.
    void addBreakSrc(std::string file, int line);

    /// @brief Check label breakpoint when entering @p blk.
    bool shouldBreakLabel(const il::core::BasicBlock &blk) const;

    /// @brief Check source line breakpoint for @p loc.
    /// @param loc Instruction source location.
    /// @param sm  Source manager for resolving file paths.
    /// @return Pointer to matching breakpoint or null.
    const Breakpoint *shouldBreakSrc(const il::support::SourceLoc &loc,
                                     const il::support::SourceManager *sm) const;

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
