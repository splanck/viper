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
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::vm
{

/// @brief Breakpoint specification.
struct Breakpoint
{
    /// @brief Kinds of breakpoints.
    enum class Kind
    {
        Label,   ///< Break on block entry
        SrcLine, ///< Break on source file and line
    } kind{Kind::Label};

    il::support::Symbol label{}; ///< Target block label when kind == Label
    std::string file;            ///< Source file when kind == SrcLine
    int line = 0;                ///< Source line when kind == SrcLine
};

/// @brief Controller for debug breakpoints.
class DebugCtrl
{
  public:
    /// @brief Intern @p label and return its symbol.
    il::support::Symbol internLabel(std::string_view label);

    /// @brief Add breakpoint for block label @p sym.
    void addBreak(il::support::Symbol sym);

    /// @brief Add source line breakpoint for @p file:@p line.
    void addSrcBreak(std::string file, int line);

    /// @brief Set source manager for resolving file paths.
    void setSourceManager(const il::support::SourceManager *sm);

    /// @brief Check whether instruction @p in within @p blk hits a breakpoint.
    /// @param blk Current basic block.
    /// @param in Current instruction.
    /// @param ip Instruction index within block.
    /// @return Breakpoint kind if triggered.
    std::optional<Breakpoint::Kind> shouldBreak(const il::core::BasicBlock &blk,
                                                const il::core::Instr &in,
                                                size_t ip);

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
    const il::support::SourceManager *sm_ = nullptr; ///< Source manager
    std::vector<Breakpoint> breaks_;                 ///< Registered breakpoints

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
