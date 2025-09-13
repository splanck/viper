// tui/include/tui/app.hpp
// @brief Minimal application loop processing events and rendering a widget tree.
// @invariant Screen buffer dimensions define root layout bounds.
// @ownership App owns root widget and screen buffer, borrows TermIO via Renderer.
#pragma once

#include "tui/render/renderer.hpp"
#include "tui/ui/container.hpp"
#include "tui/ui/focus.hpp"
#include "tui/ui/widget.hpp"

#include <memory>
#include <vector>

namespace viper::tui
{
/// @brief Headless app driving a widget tree and renderer.
class App
{
  public:
    /// @brief Construct app with root widget and terminal I/O.
    App(std::unique_ptr<ui::Widget> root,
        ::tui::term::TermIO &tio,
        int rows,
        int cols,
        bool truecolor = false);

    /// @brief Enqueue an input event.
    void pushEvent(const ui::Event &ev);

    /// @brief Process pending events and render one frame.
    void tick();

    /// @brief Access focus manager for widget registration.
    [[nodiscard]] ui::FocusManager &focus()
    {
        return focus_;
    }

  private:
    std::unique_ptr<ui::Widget> root_{};
    render::ScreenBuffer screen_{};
    render::Renderer renderer_;
    std::vector<ui::Event> events_{};
    ui::FocusManager focus_{};
    int rows_{0};
    int cols_{0};
};

} // namespace viper::tui
