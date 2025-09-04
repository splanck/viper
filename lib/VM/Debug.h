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
        Label,   ///< Break on block label
        SrcLine, ///< Break on source file and line
    } kind{Kind::Label};

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
    explicit Breakpoint(il::support::Symbol sym) : kind(Kind::Label), label(sym) {}

    /// @brief Construct source-line breakpoint.
    Breakpoint(std::string f, int l) : kind(Kind::SrcLine)
    {
        new (&src.file) std::string(std::move(f));
        src.line = l;
    }

    /// @brief Copy constructor.
    Breakpoint(const Breakpoint &other) : kind(other.kind)
    {
        if (kind == Kind::Label)
            label = other.label;
        else
        {
            new (&src.file) std::string(other.src.file);
            src.line = other.src.line;
        }
    }

    /// @brief Destructor.
    ~Breakpoint()
    {
        if (kind == Kind::SrcLine)
            src.file.~basic_string();
    }

    /// @brief Assignment operator.
    Breakpoint &operator=(const Breakpoint &other)
    {
        if (this == &other)
            return *this;
        if (kind == Kind::SrcLine)
            src.file.~basic_string();
        kind = other.kind;
        if (kind == Kind::Label)
            label = other.label;
        else
        {
            new (&src.file) std::string(other.src.file);
            src.line = other.src.line;
        }
        return *this;
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

    /// @brief Add breakpoint for source @p file:@p line.
    void addBreak(std::string file, int line);

    /// @brief Check whether entering @p blk triggers a label breakpoint.
    bool shouldBreak(const il::core::BasicBlock &blk) const;

    /// @brief Check whether executing instruction @p in hits a source line breakpoint.
    bool shouldBreak(const il::core::Instr &in);

    /// @brief Set source manager used for resolving file ids.
    void setSourceManager(const il::support::SourceManager *sm);

    /// @brief Access associated source manager.
    const il::support::SourceManager *sourceManager() const;

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
    std::vector<Breakpoint> breaks_;                 ///< Registered breakpoints
    const il::support::SourceManager *sm_ = nullptr; ///< Source manager

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
