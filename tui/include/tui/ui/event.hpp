// tui/include/tui/ui/event.hpp
// @brief UI event wrapper bridging terminal key events to widgets.
// @invariant Contains at most one key event payload for now.
// @ownership Owns copied key event data.
#pragma once

#include "tui/term/key_event.hpp"

namespace viper::tui::ui
{
struct Event
{
    term::KeyEvent key{};
};
} // namespace viper::tui::ui
