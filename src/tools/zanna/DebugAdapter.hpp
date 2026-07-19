//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/zanna/DebugAdapter.hpp
// Purpose: Run a program under the VM as an interactive source-level debug
//          adapter for Zanna Studio.
// Key invariants:
//   - Control protocol is newline-delimited JSON on stderr, each line prefixed
//     with the "@@VDBG@@ " sentinel; the debuggee's own stdout/stderr pass
//     through untouched so the IDE can show program output directly.
//   - Commands (setBreakpoints/launch/continue/step*/pause/terminate) arrive as
//     newline-delimited JSON on stdin.
// Ownership/Lifetime:
//   - Operates on a caller-owned module and source manager for one run.
// Links: src/tools/zanna/cmd_run.cpp, src/vm/debug/VMDebug.cpp,
//        docs/adr/0009-debug-evaluate-protocol.md,
//        docs/adr/0012-debug-conditional-breakpoints-logpoints.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "zanna/vm/debug/DebugClassLayout.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace il::core {
struct Module;
}
namespace il::support {
class SourceManager;
}

namespace il::tools::debug {

/// @brief Run @p module under the VM as an interactive debug adapter.
/// @param module Lowered IL module to execute (must already be verified).
/// @param programArgs Arguments passed to the debuggee.
/// @param maxSteps Optional VM step limit (0 = unlimited).
/// @param sm Source manager mapping file ids to paths (for stop locations).
/// @param debugLayouts Class-layout sidecar from the module's own compile so
///        stops can expand user class instances field-by-field (ADR 0138);
///        pass empty when unavailable (direct IL, BASIC) to keep leaves.
/// @return The debuggee's exit code.
int runDebugAdapter(il::core::Module &module,
                    const std::vector<std::string> &programArgs,
                    uint64_t maxSteps,
                    il::support::SourceManager &sm,
                    il::vm::DebugClassLayoutTable debugLayouts = {});

} // namespace il::tools::debug
