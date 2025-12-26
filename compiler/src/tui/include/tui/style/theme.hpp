//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/style/theme.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tui/render/screen.hpp"

namespace viper::tui::style
{

/// @brief Roles used by widgets to query theme colors.
enum class Role
{
    Normal,
    Accent,
    Disabled,
    Selection
};

/// @brief Theme mapping roles to render styles.
class Theme
{
  public:
    /// @brief Construct theme with default color palette.
    Theme();

    /// @brief Retrieve style for a given role.
    [[nodiscard]] const render::Style &style(Role r) const;

  private:
    render::Style normal_{};
    render::Style accent_{};
    render::Style disabled_{};
    render::Style selection_{};
};

} // namespace viper::tui::style
