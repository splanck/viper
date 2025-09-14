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
    if (!w)
        return;
    auto it = std::find(ring_.begin(), ring_.end(), w);
    if (it == ring_.end())
        return;

    std::size_t pos = static_cast<std::size_t>(it - ring_.begin());
    bool wasCurrent = (!ring_.empty() && pos == index_);
    ring_.erase(it);

    if (ring_.empty())
    {
        index_ = 0;
        if (wasCurrent)
        {
            w->onFocusChanged(false);
        }
        return;
    }

    if (pos < index_ || index_ >= ring_.size())
        index_ = index_ ? (index_ - 1) : 0;

    if (wasCurrent)
    {
        w->onFocusChanged(false);
        if (Widget *now = ring_[index_])
        {
            now->onFocusChanged(true);
        }
    }
}

Widget *FocusManager::next()
{
    if (ring_.empty())
        return nullptr;
    Widget *old = ring_[index_];
    index_ = (index_ + 1) % ring_.size();
    Widget *now = ring_[index_];
    if (now != old)
    {
        if (old)
            old->onFocusChanged(false);
        if (now)
            now->onFocusChanged(true);
    }
    return now;
}

Widget *FocusManager::prev()
{
    if (ring_.empty())
        return nullptr;
    Widget *old = ring_[index_];
    index_ = (index_ + ring_.size() - 1) % ring_.size();
    Widget *now = ring_[index_];
    if (now != old)
    {
        if (old)
            old->onFocusChanged(false);
        if (now)
            now->onFocusChanged(true);
    }
    return now;
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
