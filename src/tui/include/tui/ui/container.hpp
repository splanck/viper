// tui/include/tui/ui/container.hpp
// @brief Container widget arranging children in vertical or horizontal stacks.
// @invariant Children are laid out within the container's rectangle.
// @ownership Container owns child widgets via unique_ptr.
#pragma once

#include "tui/ui/widget.hpp"

#include <memory>
#include <vector>

namespace viper::tui::ui
{
/// @brief Base container holding child widgets.
class Container : public Widget
{
  public:
    /// @brief Add child widget to container.
    void addChild(std::unique_ptr<Widget> child);

    void layout(const Rect &r) override;
    void paint(render::ScreenBuffer &sb) override;

  protected:
    virtual void layoutChildren() = 0;
    std::vector<std::unique_ptr<Widget>> children_{};
};

/// @brief Vertical stack container.
class VStack : public Container
{
  protected:
    void layoutChildren() override;
};

/// @brief Horizontal stack container.
class HStack : public Container
{
  protected:
    void layoutChildren() override;
};

} // namespace viper::tui::ui
