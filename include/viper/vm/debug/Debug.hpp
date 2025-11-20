//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/vm/debug/Debug.hpp
// Purpose: Expose lightweight debugger and tracing configuration types for the
//          IL virtual machine without requiring full VM internals.
// Invariants: Public types remain header-only data containers and helpers so
//             tools can configure debugging without pulling interpreter
//             implementation details.
// Ownership: Debug controller owns its internal caches by value; trace sinks do
//            not own frame memory and operate on caller-managed VM state.
// Links: docs/codemap/vm-runtime.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Type.hpp"
#include "support/source_location.hpp"
#include "support/string_interner.hpp"
#include "support/symbol.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace il::core
{
struct BasicBlock;
struct Function;
struct Instr;
} // namespace il::core

namespace il::support
{
class SourceManager;
} // namespace il::support

namespace il::vm
{
struct Frame;

/// @brief Discrete debugger events surfaced by the VM.
enum class DebugEvent
{
    None = 0,
    TailCall,     ///< A tail-call reused the current frame (from -> to)
    MemWatchHit,  ///< A memory write intersected a watched range.
};

/// @brief Payload describing a tail-call optimisation event.
struct TailCallInfo
{
    const il::core::Function *from = nullptr;
    const il::core::Function *to = nullptr;
};

/// @brief Payload describing a memory watch hit event.
struct MemWatchHit
{
    const void *addr = nullptr; ///< Address of the write.
    std::size_t size = 0;       ///< Number of bytes written.
    std::string tag;            ///< User-provided tag for the watch range.
};

/// @brief Configuration for interpreter tracing.
struct TraceConfig
{
    /// @brief Tracing modes supported by the interpreter.
    enum Mode
    {
        Off, ///< Tracing disabled.
        IL,  ///< Trace IL instructions.
        SRC  ///< Trace source locations.
    } mode{Off};

    /// @brief Optional source manager for resolving file paths.
    const il::support::SourceManager *sm = nullptr;

    /// @brief Determine whether tracing should emit output.
    /// @return True when tracing is enabled.
    [[nodiscard]] bool enabled() const;
};

/// @brief Sink that formats and emits trace lines for each interpreter step.
class TraceSink
{
  public:
    /// @brief Construct a sink configured with @p cfg.
    explicit TraceSink(TraceConfig cfg = {});

    /// @brief Prepare per-function lookup tables for tracing @p fr.
    void onFramePrepared(const Frame &fr);

    /// @brief Record execution of instruction @p in within frame @p fr.
    void onStep(const il::core::Instr &in, const Frame &fr);

    /// @brief Emit a tail-call event trace when enabled.
    void onTailCall(const il::core::Function *from, const il::core::Function *to);

  private:
    struct InstrLocation
    {
        const il::core::BasicBlock *block = nullptr; ///< Owning basic block.
        size_t ip = 0;                               ///< Instruction position within the block.
    };

    struct FileCacheEntry
    {
        std::string path;               ///< Canonical file path.
        std::vector<std::string> lines; ///< Cached source text split into lines.
    };

    const FileCacheEntry *getOrLoadFile(uint32_t fileId, std::string pathHint = {});

    TraceConfig cfg; ///< Active configuration.
    std::unordered_map<const il::core::Function *,
                       std::unordered_map<const il::core::Instr *, InstrLocation>>
        instrLocations; ///< Per-function instruction lookup cache.
    std::unordered_map<uint32_t, FileCacheEntry> fileCache; ///< Cached source files.
};

/// @brief Breakpoint identified by a block label symbol.
struct Breakpoint
{
    il::support::Symbol label; ///< Target block label symbol.
};

/// @brief Controller responsible for breakpoint, watch, and source-line debugging state.
class DebugCtrl
{
  public:
    /// @brief Intern @p label and return its symbol.
    il::support::Symbol internLabel(std::string_view label);

    /// @brief Add a breakpoint for label symbol @p sym.
    void addBreak(il::support::Symbol sym);

    /// @brief Check whether entering @p blk triggers a breakpoint.
    [[nodiscard]] bool shouldBreak(const il::core::BasicBlock &blk) const;

    /// @brief Add breakpoint at source @p file and line @p line.
    void addBreakSrcLine(std::string file, uint32_t line);

    /// @brief Check if any source line breakpoints are registered.
    [[nodiscard]] bool hasSrcLineBPs() const;

    /// @brief Check whether instruction @p instr matches a source line breakpoint.
    [[nodiscard]] bool shouldBreakOn(const il::core::Instr &instr) const;

    /// @brief Set source manager used to resolve file paths.
    void setSourceManager(const il::support::SourceManager *sm);

    /// @brief Retrieve associated source manager.
    [[nodiscard]] const il::support::SourceManager *getSourceManager() const;

    /// @brief Normalize @p path by canonicalizing separators and dot segments.
    static std::string normalizePath(std::string path);

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

    // Memory watch API -------------------------------------------------------
    /// @brief Register a memory watch range [addr, addr+size) with a tag.
    void addMemWatch(const void *addr, std::size_t size, std::string tag);

    /// @brief Remove a previously registered memory watch range.
    /// @return True when a matching entry was removed.
    bool removeMemWatch(const void *addr, std::size_t size, std::string_view tag);

    /// @brief Check whether any memory watches are installed.
    [[nodiscard]] bool hasMemWatches() const noexcept;

    /// @brief Record a memory write and enqueue hit events for intersecting ranges.
    void onMemWrite(const void *addr, std::size_t size);

    /// @brief Consume and return pending memory watch hit events.
    std::vector<MemWatchHit> drainMemWatchEvents();

  private:
    static std::pair<std::string, std::string> normalizePathWithBase(std::string path);

    mutable il::support::StringInterner interner_;
    std::unordered_set<il::support::Symbol> breaks_;

    struct SrcLineBP
    {
        std::string normFile;
        std::string base;
        uint32_t line = 0;
    };

    const il::support::SourceManager *sm_ = nullptr;
    std::vector<SrcLineBP> srcLineBPs_;
    mutable std::optional<std::pair<uint32_t, uint32_t>> lastHitSrc_;

    struct WatchEntry
    {
        il::core::Type::Kind type = il::core::Type::Kind::Void;
        int64_t i64 = 0;
        double f64 = 0.0;
        bool hasValue = false;
    };

    std::unordered_map<il::support::Symbol, WatchEntry> watches_;

    struct MemWatchRange
    {
        const void *addr = nullptr;
        std::size_t size = 0;
        std::string tag;
    };
    std::vector<MemWatchRange> memWatches_;
    std::vector<MemWatchHit> memEvents_;
};

/// @brief Action produced by a debugger script.
enum class DebugActionKind
{
    Continue,
    Step,
};

struct DebugAction
{
    DebugActionKind kind = DebugActionKind::Continue;
    uint64_t count = 0;
};

/// @brief Parses debugger automation scripts describing desired actions.
class DebugScript
{
  public:
    DebugScript() = default;
    explicit DebugScript(const std::string &path);

    bool empty() const;
    void addStep(uint64_t count);
    DebugAction nextAction();

  private:
    std::queue<DebugAction> actions;
};

} // namespace il::vm
