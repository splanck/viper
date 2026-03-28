//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/common/VmFixture.cpp
// Purpose: Implement shared VM execution helpers for tests.
// Key invariants: Trap helpers use process isolation (fork on POSIX,
//                 CreateProcess on Windows) to survive rt_abort/exit calls.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "common/VmFixture.hpp"

#include "common/ProcessIsolation.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdio>

namespace viper::tests {
int64_t VmFixture::run(il::core::Module &module) const {
    il::vm::VM vm(module);
    return vm.run();
}

VmTrapResult VmFixture::runExpectingTrap(il::core::Module &module) const {
    ChildResult child = runModuleIsolated(module);

    VmTrapResult result{};
    result.stderrText = std::move(child.stderrText);
    result.exited = child.exited;
    result.signaled = child.signaled;
    result.exitCode = child.exitCode;
    return result;
}

std::string VmFixture::captureTrap(il::core::Module &module) const {
    const VmTrapResult trap = runExpectingTrap(module);
    if (!trap.trapped()) {
        std::fprintf(stderr,
                     "captureTrap: expected trap but child %s (exitCode=%d)\n"
                     "  stderr: %s\n",
                     trap.exited ? "exited cleanly" : "terminated unexpectedly",
                     trap.exitCode,
                     trap.stderrText.c_str());
        assert(false && "captureTrap: child did not trap");
    }
    return trap.stderrText;
}

} // namespace viper::tests
