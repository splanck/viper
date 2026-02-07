//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the viper_tui_version() function, which returns the
// compile-time version string for the ViperTUI library. The version string
// follows semantic versioning and is embedded during the build process.
//
// Key invariants:
//   - The returned string has static storage duration and is always non-null.
//   - The string is null-terminated and valid for the lifetime of the process.
//
// Ownership: No dynamic resources; the version string lives in the
// read-only data segment.
//
//===----------------------------------------------------------------------===//

#pragma once

/// @brief Returns the ViperTUI version string.
/// @invariant The returned pointer is non-null and points to a null-terminated string.
/// @ownership The returned string has static storage duration and must not be freed.
/// @notes Part of the public ViperTUI API.
namespace viper::tui
{
const char *viper_tui_version() noexcept;
} // namespace viper::tui
