// tui/src/ui/modal.cpp
// @brief Modal host and popup widget implementations with backdrop.
// @invariant ModalHost paints root then modals with full-screen backdrop.
// @ownership ModalHost owns root and modals; Popup borrows dismiss callback.

#include "tui/ui/modal.hpp"
#include "tui/render/screen.hpp"

#include <algorithm>

namespace viper::tui::ui
{
ModalHost::ModalHost(std::unique_ptr<Widget> root) : root_(std::move(root)) {}

Widget *ModalHost::root()
{
    return root_.get();
}

/// @brief Modal host requires focus to intercept input ahead of child widgets.
bool ModalHost::wantsFocus() const
{
    return true;
}

void ModalHost::pushModal(std::unique_ptr<Widget> modal)
{
    if (auto *p = dynamic_cast<Popup *>(modal.get()))
    {
        p->setOnClose([this] { popModal(); });
    }
    modals_.push_back(std::move(modal));
}

void ModalHost::popModal()
{
    if (!modals_.empty())
    {
        modals_.pop_back();
    }
}

void ModalHost::layout(const Rect &r)
{
    Widget::layout(r);
    if (root_)
    {
        root_->layout(r);
    }
    for (auto &m : modals_)
    {
        m->layout(r);
    }
}

void ModalHost::paint(render::ScreenBuffer &sb)
{
    if (root_)
    {
        root_->paint(sb);
    }
    if (!modals_.empty())
    {
        for (int y = rect_.y; y < rect_.y + rect_.h; ++y)
        {
            for (int x = rect_.x; x < rect_.x + rect_.w; ++x)
            {
                sb.at(y, x).ch = U' ';
            }
        }
        for (auto &m : modals_)
        {
            m->paint(sb);
        }
    }
}

bool ModalHost::onEvent(const Event &ev)
{
    if (!modals_.empty())
    {
        (void)modals_.back()->onEvent(ev);
        return true;
    }
    if (root_)
    {
        return root_->onEvent(ev);
    }
    return false;
}

Popup::Popup(int w, int h) : width_(w), height_(h) {}

/// @brief Popups accept focus to ensure dismissal keys are delivered.
bool Popup::wantsFocus() const
{
    return true;
}

void Popup::setOnClose(std::function<void()> cb)
{
    onClose_ = std::move(cb);
}

void Popup::layout(const Rect &r)
{
    Widget::layout(r);
    int w = std::min(width_, r.w);
    int h = std::min(height_, r.h);
    int x = r.x + (r.w - w) / 2;
    int y = r.y + (r.h - h) / 2;
    box_ = {x, y, w, h};
}

void Popup::paint(render::ScreenBuffer &sb)
{
    int x0 = box_.x;
    int y0 = box_.y;
    int w = box_.w;
    int h = box_.h;

    for (int x = 0; x < w; ++x)
    {
        auto &top = sb.at(y0, x0 + x);
        top.ch = (x == 0 || x == w - 1) ? U'+' : U'-';
        auto &bot = sb.at(y0 + h - 1, x0 + x);
        bot.ch = (x == 0 || x == w - 1) ? U'+' : U'-';
    }
    for (int y = 1; y < h - 1; ++y)
    {
        auto &left = sb.at(y0 + y, x0);
        left.ch = U'|';
        auto &right = sb.at(y0 + y, x0 + w - 1);
        right.ch = U'|';
        for (int x = 1; x < w - 1; ++x)
        {
            sb.at(y0 + y, x0 + x).ch = U' ';
        }
    }
}

bool Popup::onEvent(const Event &ev)
{
    const auto &k = ev.key;
    if (k.code == term::KeyEvent::Code::Esc || k.code == term::KeyEvent::Code::Enter)
    {
        if (onClose_)
        {
            onClose_();
        }
        return true;
    }
    return false;
}

} // namespace viper::tui::ui
