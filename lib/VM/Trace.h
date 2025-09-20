// File: lib/VM/Trace.h
// Purpose: Declare tracing configuration and sink for VM instruction steps.
// Key invariants: Trace output is deterministic and line-oriented.
// Ownership/Lifetime: Sink holds configuration by value; no dynamic state.
// Links: docs/dev/vm.md
#pragma once

namespace il::core
{
struct Instr;
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
    void onStep(const il::core::Instr &in, const Frame &fr);

  private:
    TraceConfig cfg; ///< Active configuration
};

} // namespace il::vm
