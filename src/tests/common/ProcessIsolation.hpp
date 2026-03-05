//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/common/ProcessIsolation.hpp
// Purpose: Cross-platform process isolation for trap tests. On POSIX, uses
//          fork+pipe+waitpid. On Windows, uses CreateProcess self-relaunch
//          with Job Object sandbox for crash safety.
// Key invariants:
//   - runIsolated() always returns to the parent; the child never returns.
//   - On Windows, child processes are confined to a Job Object that kills
//     them when the parent exits (prevents zombie processes).
//   - IL modules are transferred to Windows child processes via temp file
//     serialization (round-trips through Serializer/Parser).
// Ownership/Lifetime:
//   - ChildResult owns its stderrText string.
//   - Temp files are cleaned up by the parent after child exit.
// Links: tests/common/VmFixture.hpp, docs/cross-platform/platform-checklist.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <string>

namespace il
{
namespace core
{
class Module;
}
} // namespace il

namespace viper
{
namespace tests
{

/// @brief Result from an isolated child process execution.
struct ChildResult
{
    bool exited = false;   ///< True if child exited via exit()/_exit()/_Exit().
    bool signaled = false; ///< True if child was killed by a signal.
    int exitCode = -1;     ///< WEXITSTATUS if exited, 128+signal if signaled.
    std::string stderrText;

    /// @brief Check whether the child terminated due to a trap.
    [[nodiscard]] bool trapped() const noexcept
    {
        return (exited && exitCode != 0) || signaled;
    }
};

/// @brief Run a function in an isolated child process, capturing stderr.
///
/// On POSIX: fork + pipe + waitpid. The lambda runs directly in the child.
/// On Windows: CreateProcess self-relaunch with --viper-child-run flag.
///   The childFn is called in the child via dispatchChild().
///
/// @param childFn Code to run in the isolated child (may call exit/abort).
/// @param timeoutMs Maximum time to wait for child (0 = infinite).
/// @return ChildResult with exit code and captured stderr.
ChildResult runIsolated(std::function<void()> childFn, unsigned timeoutMs = 10000);

/// @brief Run an IL module in the VM in an isolated child process.
///
/// The module is serialized to a temp file (Windows) or run directly in
/// a forked child (POSIX). This is the cross-platform replacement for
/// VmFixture's fork-based trap capture.
///
/// @param module IL module to execute in the child.
/// @param timeoutMs Maximum time to wait for child (0 = infinite).
/// @return ChildResult with exit code and captured stderr.
ChildResult runModuleIsolated(il::core::Module &module, unsigned timeoutMs = 10000);

/// @brief Check if this process was launched as an isolated child.
///
/// Call at the top of main() in any test that uses runIsolated() or
/// runModuleIsolated(). If this process is a child, dispatches to the
/// appropriate handler and calls _exit() (never returns). If this is
/// the parent, returns false immediately.
///
/// @return false if this is the parent process (always; children never return).
bool dispatchChild(int argc, char *argv[]);

/// @brief Register the current child function for Windows self-relaunch.
///
/// Called internally by runIsolated() before CreateProcess. The child
/// process calls dispatchChild() which invokes this function.
/// On POSIX, this is unused (fork inherits the function pointer).
void setChildFunction(std::function<void()> fn);

} // namespace tests
} // namespace viper
