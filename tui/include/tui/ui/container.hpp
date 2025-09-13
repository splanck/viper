// tui/include/tui/ui/container.hpp
// @brief Composite widgets arranging children vertically or horizontally.
// @invariant Child widgets are laid out without overlap within parent bounds.
// @ownership Container owns its children via std::unique_ptr.
#pragma once

#include "tui/ui/widget.hpp"

#include <memory>
#include <vector>

namespace viper::tui::ui
{

/// @brief Base container supporting directional stacking of children.
class Container : public Widget
{
  public:
    /// @brief Add a child widget.
    void addChild(std::unique_ptr<Widget> child);

  protected:
    enum class Direction
    {
        Horizontal,
        Vertical
    };

    explicit Container(Direction dir);

    void layout(Rect rect) override;
    void paint(render::ScreenBuffer &sb) override;
    bool onEvent(const Event &ev) override;

    std::vector<std::unique_ptr<Widget>> children_{};
    Direction dir_;
};

/// @brief Container stacking children vertically.
class VStack : public Container
{
  public:
    VStack();
};

/// @brief Container stacking children horizontally.
class HStack : public Container
{
  public:
    HStack();
};

} // namespace viper::tui::ui
