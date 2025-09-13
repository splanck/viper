// tui/src/ui/focus.cpp
// @brief FocusManager implementation cycling through registered widgets.
// @invariant Ring contains widgets that want focus; current index is valid or -1.
// @ownership FocusManager holds raw pointers without ownership.

#include "tui/ui/focus.hpp"
#include "tui/ui/widget.hpp"

#include <algorithm>

namespace viper::tui::ui
{
void FocusManager::registerWidget(Widget *w)
{
    if (!w || !w->wantsFocus())
    {
        return;
    }
    ring_.push_back(w);
    if (current_ == -1)
    {
        current_ = 0;
    }
}

void FocusManager::unregisterWidget(Widget *w)
{
    auto it = std::find(ring_.begin(), ring_.end(), w);
    if (it == ring_.end())
    {
        return;
    }
    int idx = static_cast<int>(std::distance(ring_.begin(), it));
    ring_.erase(it);
    if (ring_.empty())
    {
        current_ = -1;
    }
    else if (idx <= current_)
    {
        current_ = current_ % static_cast<int>(ring_.size());
    }
}

Widget *FocusManager::next()
{
    if (ring_.empty())
    {
        return nullptr;
    }
    current_ = (current_ + 1) % static_cast<int>(ring_.size());
    return ring_[current_];
}

Widget *FocusManager::prev()
{
    if (ring_.empty())
    {
        return nullptr;
    }
    current_ = (current_ + static_cast<int>(ring_.size()) - 1) % static_cast<int>(ring_.size());
    return ring_[current_];
}

Widget *FocusManager::focused() const
{
    if (current_ < 0 || current_ >= static_cast<int>(ring_.size()))
    {
        return nullptr;
    }
    return ring_[current_];
}

} // namespace viper::tui::ui
