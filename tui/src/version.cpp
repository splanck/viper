//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/version.cpp
// Purpose: Provide the stable version query for the terminal UI toolkit so
//          downstream tooling can assert compatibility.
// Key invariants: Returned string remains valid for the process lifetime and
//                 matches the semantic version recorded in CMake metadata.
// Ownership/Lifetime: Returns a pointer to a string with static storage
//                     duration; callers must not attempt to free it.
// Links: docs/tui/architecture.md#versioning
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the exported version accessor for the TUI component.
/// @details Downstream consumers load the terminal UI library dynamically and
///          call @ref viper_tui_version to confirm ABI compatibility.  Keeping
///          the implementation in a dedicated translation unit simplifies the
///          process of bumping versions during releases.

#include "tui/version.hpp"

namespace viper::tui
{
/// @brief Report the semantic version string of the terminal UI library.
/// @details Returns a pointer to a statically allocated, null-terminated string
///          literal.  The value is intentionally constexpr so it can be inlined
///          into shared libraries while still providing an address stable across
///          translation units.
/// @return Pointer to a string containing the "major.minor.patch" version.
const char *viper_tui_version() noexcept
{
    return "0.1.0";
}
} // namespace viper::tui
