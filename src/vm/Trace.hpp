// File: src/vm/Trace.hpp
// Purpose: Declare tracing configuration and sink for VM instruction steps.
// Key invariants: Trace output is deterministic and line-oriented.
// Ownership/Lifetime: Sink holds configuration by value; no dynamic state.
// Links: docs/dev/vm.md
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace il::core
{
struct Instr;
struct BasicBlock;
struct Function;
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

    /// @brief Prepare per-function lookup tables for tracing @p fr.
    void onFramePrepared(const Frame &fr);

    /// @brief Record execution of instruction @p in within frame @p fr.
    void onStep(const il::core::Instr &in, const Frame &fr);

  private:
    /// @brief Cached mapping from instructions to their block and index.
    struct InstrLocation
    {
        const il::core::BasicBlock *block = nullptr; ///< Owning basic block pointer.
        size_t ip = 0;                               ///< Instruction position within the block.
    };

    /// @brief Cache entry describing a traced source file.
    struct FileCacheEntry
    {
        std::string path;               ///< Canonical file path.
        std::vector<std::string> lines; ///< File contents split into lines.
    };

    /// @brief Retrieve cached file for @p file_id, loading it on first access.
    /// @param file_id Source file identifier.
    /// @param path_hint Optional canonical path to avoid redundant lookups.
    /// @return Pointer to cache entry or nullptr if unavailable.
    const FileCacheEntry *getOrLoadFile(uint32_t file_id, std::string path_hint = {});

    TraceConfig cfg; ///< Active configuration
    std::unordered_map<const il::core::Function *,
                       std::unordered_map<const il::core::Instr *, InstrLocation>>
        instrLocations; ///< Per-function instruction location cache.
    std::unordered_map<uint32_t, FileCacheEntry> fileCache; ///< Cached source text.
};

} // namespace il::vm
