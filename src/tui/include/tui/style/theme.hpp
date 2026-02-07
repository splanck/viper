//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Theme class and Role enum for Viper's TUI styling
// system. The Theme maps semantic roles (Normal, Accent, Disabled, Selection)
// to concrete render Styles, allowing widgets to query appropriate colors
// without hard-coding color values.
//
// Widgets reference theme roles rather than raw colors, enabling consistent
// theming and easy palette changes across the entire UI.
//
// Key invariants:
//   - The default constructor produces a sensible dark-theme color palette.
//   - All four roles map to valid Style objects.
//   - Theme is copyable and lightweight (four Style structs).
//
// Ownership: Theme owns its Style values by value. No heap allocation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tui/render/screen.hpp"

namespace viper::tui::style
{

/// @brief Semantic roles used by widgets to look up theme-appropriate styles.
/// @details Widgets query the theme using roles rather than hard-coded colors,
///          enabling consistent appearance and easy palette customization.
enum class Role
{
    Normal,
    Accent,
    Disabled,
    Selection
};

/// @brief Maps semantic roles to concrete render styles for consistent widget theming.
/// @details Provides a centralized color palette for the TUI. Widgets query the theme
///          using Role values to obtain foreground/background colors and text attributes.
///          The default constructor initializes a dark-theme palette suitable for most
///          terminal emulators.
class Theme
{
  public:
    /// @brief Construct theme with default color palette.
    Theme();

    /// @brief Look up the render style associated with a semantic role.
    /// @details Returns the configured style for the given role (Normal, Accent,
    ///          Disabled, or Selection). Used by widgets during paint() to apply
    ///          theme-consistent colors and attributes.
    /// @param r The semantic role to look up.
    /// @return Const reference to the Style associated with the role.
    [[nodiscard]] const render::Style &style(Role r) const;

  private:
    render::Style normal_{};
    render::Style accent_{};
    render::Style disabled_{};
    render::Style selection_{};
};

} // namespace viper::tui::style
