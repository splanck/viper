//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/style/theme.cpp
// Purpose: Define the default colour palette used by the Viper TUI renderer and
//          expose helpers for mapping semantic roles to styles.
// Key invariants: Colour assignments remain constant after construction so that
//                 callers can cache pointers to the returned styles without
//                 observing mutation.
// Ownership/Lifetime: Theme stores styles by value and owns no external
//                     resources.
// Links: docs/architecture.md#vipertui-architecture
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the default TUI theme and role-to-style mapping.
/// @details Consolidates the palette initialisation and lookup logic to keep the
///          renderer agnostic to concrete RGB values.  Alternate themes can
///          reuse the same interface while providing different colours.

#include "tui/style/theme.hpp"

namespace viper::tui::style
{
/// @brief Construct the default Viper TUI theme.
/// @details Initialises each semantic role—normal, accent, disabled, and
///          selection—with hand-picked RGBA tuples tuned for readability in
///          dark terminals.  Values are stored directly inside the @ref Theme so
///          subsequent lookups require only a reference return.
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

/// @brief Retrieve the style associated with a semantic UI role.
/// @details Uses a @c switch to return a reference to the precomputed style
///          instance.  The default case falls back to the normal role so callers
///          remain resilient to future enum additions.
/// @param r Semantic role identifying the desired palette entry.
/// @return Reference to the immutable style describing colours for @p r.
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
