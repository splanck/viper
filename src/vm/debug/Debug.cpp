//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the debugger control surface that powers breakpoint and watch
// handling in the IL virtual machine.  The translation unit provides utilities
// for normalising paths, tracking per-block and per-source breakpoints, and
// reporting watch state changes to the user.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Debugger control utilities for the IL virtual machine.
/// @details The helpers in this file manage breakpoint normalisation, interact
///          with the shared string interner, and surface source-level watch
///          notifications for developers.  They borrow VM-owned state such as
///          the source manager to avoid duplicating heavyweight resources.

#include "viper/vm/debug/Debug.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"
#include "support/source_location.hpp"
#include "support/source_manager.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace il::vm
{
namespace
{
[[maybe_unused]] constexpr bool kDebugBreakpoints = false;
} // namespace

/// @brief Normalise a file-system path so breakpoint comparisons are stable.
///
/// @details Replaces Windows-style separators with forward slashes, feeds the
///          result through `std::filesystem::path::lexically_normal()`, and
///          returns the generic string representation.  Empty inputs collapse to
///          "." so the debugger never returns an empty path, while absolute
///          roots remain intact.
///
/// @param p Path string to normalise; modified in place.
/// @return Canonical path with redundant segments removed.
std::string DebugCtrl::normalizePath(std::string p)
{
    std::replace(p.begin(), p.end(), '\\', '/');

    if (p.empty())
        return ".";

    std::filesystem::path normalized = std::filesystem::path(p).lexically_normal();
    std::string generic = normalized.generic_string();

    if (generic.empty())
        return p.front() == '/' ? std::string{"/"} : std::string{"."};

    return generic;
}

/// @brief Produce both the canonical path and basename for breakpoint matching.
///
/// @details Breakpoints can trigger by either full path or basename.  This helper
///          normalises the supplied path and splits the final segment so both
///          representations stay in sync.  Returning by value allows callers to
///          stash the pair without further allocation.
///
/// @param path Original path supplied by the user; consumed by the function.
/// @return Pair of canonical path and basename strings.
std::pair<std::string, std::string> DebugCtrl::normalizePathWithBase(std::string path)
{
    std::string normFile = normalizePath(std::move(path));
    size_t pos = normFile.find_last_of('/');
    std::string base = (pos == std::string::npos) ? normFile : normFile.substr(pos + 1);
    return {std::move(normFile), std::move(base)};
}

/// @brief Intern a block label for breakpoint lookup.
///
/// @details The controller stores breakpoints using interned symbols to avoid
///          repeated allocations during dispatch.  Interning here guarantees the
///          same symbol identity as other call sites using the shared interner.
///
/// @param label Block label text.
/// @return Interned symbol suitable for set membership queries.
il::support::Symbol DebugCtrl::internLabel(std::string_view label)
{
    return interner_.intern(label);
}

/// @brief Register a block-level breakpoint.
///
/// @details Inserts the interned symbol into the `breaks_` set.  Duplicate calls
///          are harmless because the underlying container is idempotent.
///
/// @param sym Interned symbol identifying the target block.
void DebugCtrl::addBreak(il::support::Symbol sym)
{
    if (sym)
        breaks_.insert(sym);
}

/// @brief Determine whether the currently executing block has a breakpoint.
///
/// @details The block label is interned using the same symbol table as
///          registration so lookups become O(1) hash checks.  This keeps the
///          runtime overhead negligible even when many breakpoints exist.
///
/// @param blk Block currently under execution.
/// @return @c true when a breakpoint has been registered for @p blk.
bool DebugCtrl::shouldBreak(const il::core::BasicBlock &blk) const
{
    il::support::Symbol sym = interner_.intern(blk.label);
    return breaks_.count(sym) != 0;
}

/// @brief Register a source-location breakpoint.
///
/// @details Normalises the provided file path and stores both the canonical path
///          and its basename together with the one-based line number.  Matching
///          code compares against both strings so users can specify either form.
///
/// @param file Source file path supplied by the user.
/// @param line One-based line number that should trigger a breakpoint.
void DebugCtrl::addBreakSrcLine(std::string file, uint32_t line)
{
    auto [normFile, base] = normalizePathWithBase(std::move(file));
    srcLineBPs_.push_back({std::move(normFile), std::move(base), line});
}

/// @brief Query whether any source-level breakpoints exist.
///
/// @return @c true when the controller holds at least one source breakpoint.
bool DebugCtrl::hasSrcLineBPs() const
{
    return !srcLineBPs_.empty();
}

/// @brief Install the source manager used to resolve file identifiers.
///
/// @details The debugger does not own the source manager; it simply stores a
///          pointer so it can translate @ref il::support::SourceLoc identifiers
///          back into canonical paths when evaluating breakpoints.
///
/// @param sm Source manager responsible for path resolution.
void DebugCtrl::setSourceManager(const il::support::SourceManager *sm)
{
    sm_ = sm;
}

/// @brief Access the source manager previously provided to the debugger.
///
/// @return Pointer installed via @ref setSourceManager or @c nullptr when unset.
const il::support::SourceManager *DebugCtrl::getSourceManager() const
{
    return sm_;
}

/// @brief Determine whether the given instruction hits a source breakpoint.
///
/// @details The helper resolves the instruction's source file identifier through
///          the installed source manager, normalises the resulting path, and
///          compares both the canonical path and basename against registered
///          breakpoints.  The last-hit cache prevents the debugger from stopping
///          repeatedly on the same line unless execution leaves and re-enters it.
///
/// @param I Instruction currently being executed.
/// @return @c true when a matching breakpoint is found.
bool DebugCtrl::shouldBreakOn(const il::core::Instr &I) const
{
    if (!sm_ || srcLineBPs_.empty() || !I.loc.hasFile() || !I.loc.hasLine())
        return false;

    const uint32_t fileId = I.loc.file_id;
    const uint32_t line = I.loc.line;
    if (lastHitSrc_ && lastHitSrc_->first == fileId && lastHitSrc_->second == line)
        return false;

    std::string_view pathView = sm_->getPath(fileId);
    if (pathView.empty())
    {
        if constexpr (kDebugBreakpoints)
        {
            std::cerr << "[DEBUG][DebugCtrl] unresolved file id " << fileId
                      << " while checking breakpoint for line " << line << "\n";
        }
        return false;
    }

    // Resolve the instruction's source file, normalize the path, and derive its
    // basename. A breakpoint matches if both the line number and either the
    // normalized path or basename are equal.
    auto [normFile, base] = normalizePathWithBase(std::string(pathView));
    for (const auto &bp : srcLineBPs_)
    {
        if (line != bp.line)
            continue;
        if (normFile == bp.normFile || base == bp.base)
        {
            lastHitSrc_ = std::make_pair(fileId, line);
            return true;
        }
    }
    return false;
}

/// @brief Register a variable to watch for changes.
///
/// @details Watching a value interns the identifier and ensures an entry exists
///          in the watch table.  Actual value comparisons happen inside
///          @ref onStore so registering is effectively O(1).
///
/// @param name Identifier of the variable to track.
void DebugCtrl::addWatch(std::string_view name)
{
    il::support::Symbol sym = interner_.intern(name);
    if (sym)
        watches_[sym];
}

/// @brief Handle a store to a watched variable and report changes.
///
/// @details After interning the identifier, the helper compares the new payload
///          against the last observed value.  Unsupported types yield a short
///          diagnostic while numeric and floating-point types trigger an update
///          message when the value changes.  Watches remember the most recent
///          value so subsequent stores can detect differences.
///
/// @param name Identifier being stored to.
/// @param ty Type of the stored value.
/// @param i64 Integer payload when @p ty is an integer type.
/// @param f64 Floating payload when @p ty is F64.
/// @param fn Function name containing the store.
/// @param blk Basic block label.
/// @param ip Instruction index within the block.
void DebugCtrl::onStore(std::string_view name,
                        il::core::Type::Kind ty,
                        int64_t i64,
                        double f64,
                        std::string_view fn,
                        std::string_view blk,
                        size_t ip)
{
    il::support::Symbol sym = interner_.intern(name);
    auto it = watches_.find(sym);
    if (it == watches_.end())
        return;
    WatchEntry &w = it->second;
    if (ty != il::core::Type::Kind::I1 && ty != il::core::Type::Kind::I16 &&
        ty != il::core::Type::Kind::I32 && ty != il::core::Type::Kind::I64 &&
        ty != il::core::Type::Kind::F64)
    {
        std::cerr << "[WATCH] " << name << "=[unsupported]  (fn=@" << fn << " blk=" << blk
                  << " ip=#" << ip << ")\n";
        return;
    }
    const bool typeChanged = w.hasValue && w.type != ty;
    bool changed = !w.hasValue || typeChanged;

    auto isIntegerKind = [](il::core::Type::Kind kind)
    {
        return kind == il::core::Type::Kind::I1 || kind == il::core::Type::Kind::I16 ||
               kind == il::core::Type::Kind::I32 || kind == il::core::Type::Kind::I64;
    };

    if (!changed)
    {
        if (ty == il::core::Type::Kind::F64)
        {
            if (w.type == il::core::Type::Kind::F64 && w.f64 != f64)
                changed = true;
        }
        else if (isIntegerKind(ty))
        {
            if (isIntegerKind(w.type) && w.i64 != i64)
                changed = true;
        }
    }

    if (changed)
    {
        std::cerr << "[WATCH] " << name << "=" << il::core::kindToString(ty) << ":";
        if (ty == il::core::Type::Kind::F64)
            std::cerr << f64;
        else
            std::cerr << i64;
        std::cerr << "  (fn=@" << fn << " blk=" << blk << " ip=#" << ip << ")\n";
    }

    if (ty == il::core::Type::Kind::F64)
    {
        if (typeChanged)
            w.i64 = 0;
        w.f64 = f64;
    }
    else
    {
        if (typeChanged)
            w.f64 = 0.0;
        w.i64 = i64;
    }

    w.type = ty;
    w.hasValue = true;
}

/// @brief Forget the last source-line breakpoint location that was triggered.
///
/// @details Clearing the cache allows the debugger to stop again on the same
///          line, for example after the user single-steps past it.
void DebugCtrl::resetLastHit()
{
    lastHitSrc_.reset();
}

/// @brief Register a memory watch entry consisting of an address range and tag.
void DebugCtrl::addMemWatch(const void *addr, std::size_t size, std::string tag)
{
    if (!addr || size == 0)
        return;
    memWatches_.push_back(MemWatchRange{addr, size, std::move(tag)});
}

/// @brief Remove a memory watch entry matching the triple (addr,size,tag).
bool DebugCtrl::removeMemWatch(const void *addr, std::size_t size, std::string_view tag)
{
    for (auto it = memWatches_.begin(); it != memWatches_.end(); ++it)
    {
        if (it->addr == addr && it->size == size && it->tag == tag)
        {
            memWatches_.erase(it);
            return true;
        }
    }
    return false;
}

bool DebugCtrl::hasMemWatches() const noexcept
{
    return !memWatches_.empty();
}

/// @brief Check memory write against installed ranges and enqueue hits.
void DebugCtrl::onMemWrite(const void *addr, std::size_t size)
{
    if (memWatches_.empty() || !addr || size == 0)
        return;
    const auto start = reinterpret_cast<std::uintptr_t>(addr);
    const auto end = start + size; // exclusive
    for (const auto &w : memWatches_)
    {
        const auto wStart = reinterpret_cast<std::uintptr_t>(w.addr);
        const auto wEnd = wStart + w.size;
        const bool intersects = !(end <= wStart || wEnd <= start);
        if (intersects)
            memEvents_.push_back(MemWatchHit{addr, size, w.tag});
    }
}

/// @brief Drain pending memory watch hit events for external consumption.
std::vector<MemWatchHit> DebugCtrl::drainMemWatchEvents()
{
    std::vector<MemWatchHit> out;
    out.swap(memEvents_);
    return out;
}

} // namespace il::vm
