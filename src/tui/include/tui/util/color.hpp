//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/util/color.hpp
// Purpose: Provides color parsing utilities for the TUI library.
// Key invariants: Parses hex RGB triplets into RGBA structs.
// Ownership/Lifetime: Stateless utility functions.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tui/render/screen.hpp"

#include <string>

namespace viper::tui::util
{

/// @brief Parse a hex RGB color string into an RGBA struct.
/// @details Accepts colors in the forms "#RRGGBB" or "RRGGBB" and stores the
///          converted components in the output struct. The alpha channel is
///          set to 255 (fully opaque) on success.
/// @param s Input color string (with or without leading #).
/// @param out Destination RGBA struct populated on success.
/// @return True when parsing succeeds, false otherwise.
[[nodiscard]] bool parseHexColor(const std::string &s, render::RGBA &out);

} // namespace viper::tui::util
