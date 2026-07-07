//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/vm/debug/DebugFrontend.hpp
// Purpose: Interface a host (e.g. the ViperIDE debug adapter) implements to drive
//          interactive debugging, plus the plain-data stop descriptor the VM hands
//          it at each pause.
// Key invariants:
//   - DebugStopInfo is pure data: the frontend never touches VM internals.
//   - onStop is called on the VM thread while execution is paused; it returns the
//     next DebugAction (continue/step/...) the VM should apply.
// Ownership/Lifetime:
//   - The frontend is owned by the host and must outlive the VM run.
// Links: src/vm/debug/VMDebug.cpp, src/tools/viper/DebugAdapter.cpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "viper/vm/debug/Debug.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace il::vm {

/// @brief One call-stack frame at a debugger stop (most-recent first in a list).
struct DebugFrameInfo {
    std::string function; ///< Function name (without leading '@').
    std::string path;     ///< Source file of the frame, or empty when unknown.
    uint32_t line = 0;    ///< 1-based line, or 0 when unknown.
};

/// @brief One named local at a debugger stop.
/// @details Composite values (lists, seqs, maps) advertise a non-zero @ref varRef
///          and a @ref childCount; a host expands them one level at a time through
///          the stop's DebugVarExpander. Leaves keep varRef == 0, matching the
///          historical flat shape so pre-expansion consumers are unaffected.
struct DebugLocalInfo {
    std::string name;       ///< Source-level variable name.
    std::string value;      ///< Formatted value (a compact summary for composites).
    std::string type;       ///< Type label (e.g. "i64", "f64", "str", "List").
    int64_t varRef = 0;     ///< >0 when expandable this stop; 0 = leaf. Dies on resume.
    int64_t childCount = 0; ///< Child count when varRef>0; 0 for leaves.
};

/// @brief Lazy, one-level child provider for structured debugger values.
/// @details Produced by the VM for a single stop and owned by that stop's
///          DebugStopInfo. All @ref varRef handles it hands out are valid only
///          while that DebugStopInfo is alive (i.e. while the VM is paused at the
///          stop); they become invalid as soon as execution resumes and the info
///          is discarded. Implementations never recurse — each call expands
///          exactly one level so depth is bounded by host requests.
class DebugVarExpander {
  public:
    virtual ~DebugVarExpander() = default;

    /// @brief Return children [start, start+count) of the value behind @p ref.
    /// @param ref A varRef previously advertised on a DebugLocalInfo this stop.
    /// @param start First child index to return (0-based).
    /// @param count Maximum number of children to return.
    /// @return The requested slice of children; empty when @p ref is unknown.
    virtual std::vector<DebugLocalInfo> expand(int64_t ref, int64_t start, int64_t count) = 0;
};

/// @brief Plain, serializable description of why and where execution paused.
struct DebugStopInfo {
    std::string reason;                 ///< "breakpoint" | "step" | "step-over" | ...
    std::string path;                   ///< Source file of the top frame.
    uint32_t line = 0;                  ///< 1-based line of the top frame; 0 if unknown.
    uint32_t column = 0;                ///< 1-based column; 0 if unknown.
    std::vector<DebugFrameInfo> frames; ///< Backtrace, most-recent first.
    std::vector<DebugLocalInfo> locals; ///< Named locals of the top frame.
    /// @brief Expander for locals whose varRef>0, or null when nothing is
    ///        expandable at this stop. Lifetime is tied to this DebugStopInfo.
    std::shared_ptr<DebugVarExpander> vars;
};

/// @brief Interactive debugger driver implemented by the host.
/// @details The VM calls @ref onStop at every pause with a plain DebugStopInfo and
///          applies the returned DebugAction. Implementations live outside the VM
///          (an IDE adapter) and communicate over their own transport.
class DebugFrontend {
  public:
    virtual ~DebugFrontend() = default;

    /// @brief Report a pause and obtain the next action to apply.
    /// @param info Where/why execution paused (top location, backtrace, locals).
    /// @return The action the VM should take (continue/step/step-over/step-out).
    virtual DebugAction onStop(const DebugStopInfo &info) = 0;
};

} // namespace il::vm
