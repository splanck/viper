// tui/include/tui/app.hpp
// @brief Minimal application loop processing events and rendering a widget tree.
// @invariant Screen buffer dimensions define root layout bounds.
// @ownership App owns root widget and screen buffer, borrows TermIO via Renderer.
#pragma once

#include "tui/input/keymap.hpp"
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
        ::viper::tui::term::TermIO &tio,
        int rows,
        int cols,
        bool truecolor = false);

    /// @brief Enqueue an input event.
    void pushEvent(const ui::Event &ev);

    /// @brief Process pending events and render one frame.
    void tick();

    /// @brief Access the focus manager.
    [[nodiscard]] ui::FocusManager &focus();

    /// @brief Set global keymap for command dispatch.
    void setKeymap(input::Keymap *km);

    /// @brief Resize the app's screen and request re-layout on next tick.
    /// @param rows New number of rows (height).
    /// @param cols New number of columns (width).
    void resize(int rows, int cols);

  private:
    std::unique_ptr<ui::Widget> root_{};
    render::ScreenBuffer screen_{};
    render::Renderer renderer_;
    std::vector<ui::Event> events_{};
    int rows_{0};
    int cols_{0};
    ui::FocusManager focus_{};
    input::Keymap *keymap_{nullptr};
};

} // namespace viper::tui
