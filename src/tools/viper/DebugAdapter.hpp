//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/viper/DebugAdapter.hpp
// Purpose: Run a program under the VM as an interactive source-level debug
//          adapter for ViperIDE.
// Key invariants:
//   - Control protocol is newline-delimited JSON on stderr, each line prefixed
//     with the "@@VDBG@@ " sentinel; the debuggee's own stdout/stderr pass
//     through untouched so the IDE can show program output directly.
//   - Commands (setBreakpoints/launch/continue/step*/pause/terminate) arrive as
//     newline-delimited JSON on stdin.
// Ownership/Lifetime:
//   - Operates on a caller-owned module and source manager for one run.
// Links: src/tools/viper/cmd_run.cpp, src/vm/debug/VMDebug.cpp,
//        misc/plans/viperide/debugger-design.md
//
//===----------------------------------------------------------------------===//
#pragma once

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
/// @return The debuggee's exit code.
int runDebugAdapter(il::core::Module &module,
                    const std::vector<std::string> &programArgs,
                    uint64_t maxSteps,
                    il::support::SourceManager &sm);

} // namespace il::tools::debug
