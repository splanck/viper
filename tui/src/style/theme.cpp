//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/style/theme.cpp
// Purpose: Define the default colour palette used by the terminal UI widgets.
// Key invariants: Theme categories always resolve to a valid render::Style
//                 instance even when additional roles are introduced.
// Ownership/Lifetime: Theme stores colour values by value and retains no
//                     external references.
// Links: docs/tui/style-guide.md#themes
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the default styling rules for terminal UI roles.
/// @details The theme groups colour pairs for normal, accent, disabled, and
///          selection states.  Centralising the definitions here allows future
///          platforms to specialise behaviour without inflating the header.

#include "tui/style/theme.hpp"

namespace viper::tui::style
{
/// @brief Construct the default theme with sensible contrast values.
/// @details Initialises each role's foreground/background pair to maximise
///          readability in a dark terminal while maintaining consistent alpha
///          channels so the renderer can safely blend between states.
Theme::Theme()
{
    normal_.fg = {255, 255, 255, 255};
    normal_.bg = {0, 0, 0, 255};

    accent_.fg = {0, 0, 0, 255};
    accent_.bg = {200, 200, 200, 255};

    disabled_.fg = {128, 128, 128, 255};
    disabled_.bg = normal_.bg;

    selection_.fg = {0, 0, 0, 255};
    selection_.bg = {0, 128, 255, 255};
}

/// @brief Retrieve the colour style associated with a given semantic role.
/// @details Uses a switch to translate the caller-provided @ref Role enum into
///          one of the stored style instances.  The default branch intentionally
///          falls back to @ref Role::Normal so adding new enum values continues
///          to produce a sane colour scheme until the theme is extended.
/// @param r Semantic role describing the UI element's desired styling.
/// @return Reference to the render::Style configured for the role.
const render::Style &Theme::style(Role r) const
{
    switch (r)
    {
        case Role::Accent:
            return accent_;
        case Role::Disabled:
            return disabled_;
        case Role::Selection:
            return selection_;
        case Role::Normal:
        default:
            return normal_;
    }
}

} // namespace viper::tui::style
