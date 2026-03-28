//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/common/VmFixture.hpp
// Purpose: Provide shared helpers for executing IL modules on the VM in tests
// Key invariants: Trap execution helpers fork the current process to isolate the
// Ownership/Lifetime: Stateless helper usable across multiple tests.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Module.hpp"

#include <cstdint>
#include <string>

namespace viper::tests {
struct VmTrapResult {
    bool exited = false;   ///< True if child exited via exit()/_exit()/_Exit().
    bool signaled = false; ///< True if child was killed by a signal.
    int exitCode = -1;     ///< WEXITSTATUS if exited, 128+signal if signaled.
    std::string stderrText;

    /// @brief Check whether the child terminated due to a trap.
    /// @details Returns true when the child either exited with non-zero code
    ///          (the rt_abort path) or was killed by a signal (atexit crash).
    [[nodiscard]] bool trapped() const noexcept {
        return (exited && exitCode != 0) || signaled;
    }
};

class VmFixture {
  public:
    VmFixture() = default;
    VmFixture(const VmFixture &) = delete;
    VmFixture &operator=(const VmFixture &) = delete;
    VmFixture(VmFixture &&) = delete;
    VmFixture &operator=(VmFixture &&) = delete;
    ~VmFixture() = default;

    [[nodiscard]] int64_t run(il::core::Module &module) const;
    [[nodiscard]] VmTrapResult runExpectingTrap(il::core::Module &module) const;
    [[nodiscard]] std::string captureTrap(il::core::Module &module) const;
};

} // namespace viper::tests
