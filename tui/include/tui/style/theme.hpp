// tui/include/tui/style/theme.hpp
// @brief Simple color theme with role-based styles.
// @invariant Provides fixed styles per role.
// @ownership Theme does not own external resources.
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
