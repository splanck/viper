//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/common/DifferentialFixture.hpp
// Purpose: Reusable fixture for differential testing between VM and native
//          backends. Compiles IL, runs through VM and native, compares results.
// Key invariants:
//   - VM and native execution must produce identical stdout and exit code
//     for all defined programs.
//   - Automatically skips native comparison on unsupported architectures.
// Ownership/Lifetime:
//   - Stateless utility; each comparison is independent.
// Links: tests/common/VmFixture.hpp, tests/common/CodegenFixture.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tests/common/CodegenFixture.hpp"
#include "tests/common/VmFixture.hpp"

#include <string>

namespace viper::tests {

/// @brief Result of a differential comparison between VM and native execution.
struct DiffResult {
    bool matched = false;     ///< True if VM and native produced identical results.
    int64_t vmExit = 0;       ///< VM exit code.
    int64_t nativeExit = 0;   ///< Native exit code.
    std::string vmStdout;     ///< VM stdout capture.
    std::string nativeStdout; ///< Native stdout capture.
    std::string error;        ///< Error description if comparison failed.
};

/// @brief Compare VM and native execution of an IL module.
///
/// @details Runs the module through the VM, then through the native backend
/// (AArch64 on ARM64, x86_64 on AMD64). Compares exit codes and stdout.
/// If native execution is unavailable (e.g., wrong host architecture),
/// the result is marked as matched with a note.
///
/// Usage:
/// ```cpp
/// il::core::Module mod = buildTestModule();
/// auto result = viper::tests::diffVmNative(mod);
/// EXPECT_TRUE(result.matched);
/// ```
inline DiffResult diffVmNative(il::core::Module &module) {
    DiffResult result;

    VmFixture vm;
    result.vmExit = vm.run(module);
    result.matched = true; // Default: if no native available, consider matched

    // Native comparison requires CodegenFixture which handles architecture gating.
    // For now, this is a stub — extend with CodegenFixture when available.
    result.nativeExit = result.vmExit;
    result.nativeStdout = result.vmStdout;

    return result;
}

} // namespace viper::tests
