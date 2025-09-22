// File: src/vm/Trace.hpp
// Purpose: Declare tracing configuration and sink for VM instruction steps.
// Key invariants: Trace output is deterministic and line-oriented.
// Ownership/Lifetime: Sink holds configuration by value; no dynamic state.
// Links: docs/dev/vm.md
#pragma once

#include <cstddef>

namespace il::core
{
struct Instr;
struct BasicBlock;
} // namespace il::core

namespace il::support
{
class SourceManager;
} // namespace il::support

namespace il::vm
{
struct Frame;

/// @brief Configuration for interpreter tracing.
struct TraceConfig
{
    /// @brief Tracing modes.
    enum Mode
    {
        Off, ///< Tracing disabled
        IL,  ///< Trace IL instructions
        SRC  ///< Trace source locations
    } mode{Off};

    /// @brief Optional source manager for resolving file paths.
    const il::support::SourceManager *sm = nullptr;

    /// @brief Check whether tracing is enabled.
    /// @return True if mode is not Off.
    bool enabled() const;
};

/// @brief Sink that formats and emits trace lines.
class TraceSink
{
  public:
    /// @brief Create sink with configuration @p cfg.
    explicit TraceSink(TraceConfig cfg = {});

    /// @brief Record execution of instruction @p in within frame @p fr.
    /// @param blk Optional basic block containing @p in. When null, the sink
    ///            will locate the instruction by scanning the function.
    /// @param ip  Instruction index within @p blk. Ignored when @p blk is
    ///            omitted and recomputed via the fallback scan.
    void onStep(const il::core::Instr &in,
                const Frame &fr,
                const il::core::BasicBlock *blk = nullptr,
                size_t ip = 0);

  private:
    TraceConfig cfg; ///< Active configuration
};

} // namespace il::vm
