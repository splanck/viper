// tui/src/style/theme.cpp
// @brief Default theme color mappings.
// @invariant Color values remain constant after construction.
// @ownership Theme owns no external resources.

#include "tui/style/theme.hpp"

namespace viper::tui::style
{

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
