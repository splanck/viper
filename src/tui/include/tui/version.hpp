//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/version.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
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
