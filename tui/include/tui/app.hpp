// tui/include/tui/app.hpp
// @brief Minimal application loop managing widgets and rendering.
// @invariant tick() processes events once and renders current widget tree.
// @ownership App owns the root widget and screen buffer.
#pragma once

#include "tui/render/renderer.hpp"
#include "tui/ui/widget.hpp"

#include <memory>
#include <vector>

namespace tui::term
{
class TermIO;
}

namespace viper::tui
{

/// @brief Headless application driving a widget tree.
class App
{
  public:
    /// @brief Construct application with a root widget and terminal IO.
    App(std::unique_ptr<ui::Widget> root, ::tui::term::TermIO &tio);

    /// @brief Queue an event for processing on next tick.
    void pushEvent(const ui::Event &ev);

    /// @brief Resize the backing screen buffer.
    void resize(int rows, int cols);

    /// @brief Process queued events and render once.
    void tick();

    /// @brief Access underlying screen buffer.
    [[nodiscard]] render::ScreenBuffer &screen()
    {
        return screen_;
    }

  private:
    std::unique_ptr<ui::Widget> root_{};
    render::ScreenBuffer screen_{};
    render::Renderer renderer_;
    std::vector<ui::Event> events_{};
    int rows_{0};
    int cols_{0};
};

} // namespace viper::tui
