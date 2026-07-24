//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/windows_installer/WindowsInstallerCleanupPolicy.hpp
// Purpose: Define the pure path-validation policy used by the detached
//          Windows installer cleanup helper.
//
// Key invariants:
//   - Only fully qualified drive or UNC paths with at least one child component
//     are accepted.
//   - Win32 device names, alternate streams, traversal components, and
//     normalization-ambiguous names are rejected.
//
// Ownership/Lifetime:
//   - Inputs are borrowed string views and no storage is retained.
//
// Links: WindowsInstallerCleanup.cpp,
//        src/tests/unit/test_windows_installer_cleanup_policy.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

namespace zanna::installer::cleanup {

/// @brief Return whether a path is safe for an exact, non-recursive cleanup operation.
bool isSafeAbsolutePath(std::wstring_view path) noexcept;

/// @brief Compare two validated Windows path spellings case-insensitively.
bool pathsEqual(std::wstring_view left, std::wstring_view right) noexcept;

} // namespace zanna::installer::cleanup
