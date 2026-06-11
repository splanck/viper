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
struct DebugLocalInfo {
    std::string name;  ///< Source-level variable name.
    std::string value; ///< Formatted value.
    std::string type;  ///< Type label (e.g. "i64", "f64", "str").
};

/// @brief Plain, serializable description of why and where execution paused.
struct DebugStopInfo {
    std::string reason;                 ///< "breakpoint" | "step" | "step-over" | ...
    std::string path;                   ///< Source file of the top frame.
    uint32_t line = 0;                  ///< 1-based line of the top frame; 0 if unknown.
    uint32_t column = 0;                ///< 1-based column; 0 if unknown.
    std::vector<DebugFrameInfo> frames; ///< Backtrace, most-recent first.
    std::vector<DebugLocalInfo> locals; ///< Named locals of the top frame.
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
