// tui/src/widgets/splitter.cpp
// @brief Splitter widgets dividing area between two children.
// @invariant Child layouts recomputed from stored ratio on resize.
// @ownership Splitters own child widgets and delegate painting.

#include "tui/widgets/splitter.hpp"

#include <algorithm>

namespace viper::tui::widgets
{
HSplitter::HSplitter(std::unique_ptr<ui::Widget> left,
                     std::unique_ptr<ui::Widget> right,
                     float ratio)
    : left_(std::move(left)), right_(std::move(right)), ratio_(ratio)
{
}

void HSplitter::layout(const ui::Rect &r)
{
    Widget::layout(r);
    int leftW = static_cast<int>(static_cast<float>(r.w) * ratio_);
    leftW = std::clamp(leftW, 0, r.w);
    int rightW = r.w - leftW;
    ui::Rect lr{r.x, r.y, leftW, r.h};
    ui::Rect rr{r.x + leftW, r.y, rightW, r.h};
    if (left_)
    {
        left_->layout(lr);
    }
    if (right_)
    {
        right_->layout(rr);
    }
}

void HSplitter::paint(render::ScreenBuffer &sb)
{
    if (left_)
    {
        left_->paint(sb);
    }
    if (right_)
    {
        right_->paint(sb);
    }
}

VSplitter::VSplitter(std::unique_ptr<ui::Widget> top,
                     std::unique_ptr<ui::Widget> bottom,
                     float ratio)
    : top_(std::move(top)), bottom_(std::move(bottom)), ratio_(ratio)
{
}

void VSplitter::layout(const ui::Rect &r)
{
    Widget::layout(r);
    int topH = static_cast<int>(static_cast<float>(r.h) * ratio_);
    topH = std::clamp(topH, 0, r.h);
    int bottomH = r.h - topH;
    ui::Rect tr{r.x, r.y, r.w, topH};
    ui::Rect br{r.x, r.y + topH, r.w, bottomH};
    if (top_)
    {
        top_->layout(tr);
    }
    if (bottom_)
    {
        bottom_->layout(br);
    }
}

void VSplitter::paint(render::ScreenBuffer &sb)
{
    if (top_)
    {
        top_->paint(sb);
    }
    if (bottom_)
    {
        bottom_->paint(sb);
    }
}

} // namespace viper::tui::widgets
