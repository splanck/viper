// File: tests/common/VmFixture.hpp
// Purpose: Provide shared helpers for executing IL modules on the VM in tests
//          while capturing exit codes and trap diagnostics.
// Key invariants: Trap execution helpers fork the current process to isolate the
//                 VM run and capture stderr reliably.
// Ownership/Lifetime: Stateless helper usable across multiple tests.

#pragma once

#include "il/core/Module.hpp"

#include <cstdint>
#include <string>

namespace viper::tests
{
struct VmTrapResult
{
    bool exited = false;
    int exitCode = -1;
    std::string stderrText;
};

class VmFixture
{
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
