//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/common/PlatformSkip.h
// Purpose: Shared skip-return helper for C and C++ test executables that do
//          not use the TestHarness per-test skip mechanism.
// Key invariants:
//   - Returning 77 marks the test as skipped in CTest.
//   - The helper prints a stable "SKIP:" prefix for diagnostics.
// Ownership/Lifetime:
//   - Header-only macro helper; no runtime state.
// Links: src/tests/cmake/TestHelpers.cmake
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdio.h>

#define VIPER_TEST_SKIP_CODE 77

#define VIPER_PLATFORM_SKIP(reason)                                                                \
    do {                                                                                           \
        fprintf(stdout, "SKIP: %s\n", (reason));                                                   \
        return VIPER_TEST_SKIP_CODE;                                                               \
    } while (0)
