// tui/src/ui/focus.cpp
// @brief FocusManager implementation.
// @invariant Registered widgets remain valid until unregistered.
// @ownership FocusManager borrows widgets only.

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
    if (ring_.size() == 1)
    {
        index_ = 0;
    }
}

void FocusManager::unregisterWidget(Widget *w)
{
    auto it = std::find(ring_.begin(), ring_.end(), w);
    if (it == ring_.end())
    {
        return;
    }
    std::size_t removed = static_cast<std::size_t>(std::distance(ring_.begin(), it));
    ring_.erase(it);
    if (ring_.empty())
    {
        index_ = 0;
    }
    else if (index_ >= ring_.size())
    {
        index_ = 0;
    }
    else if (index_ > removed)
    {
        --index_;
    }
}

Widget *FocusManager::next()
{
    if (ring_.empty())
    {
        return nullptr;
    }
    index_ = (index_ + 1) % ring_.size();
    return ring_[index_];
}

Widget *FocusManager::prev()
{
    if (ring_.empty())
    {
        return nullptr;
    }
    index_ = (index_ + ring_.size() - 1) % ring_.size();
    return ring_[index_];
}

Widget *FocusManager::current() const
{
    if (ring_.empty())
    {
        return nullptr;
    }
    return ring_[index_];
}

} // namespace viper::tui::ui
