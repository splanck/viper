//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/app.cpp
// Purpose: Implement the headless application event loop that powers Viper's
//          terminal UI entry points.
// Key invariants: Renderer draws exactly one frame per call to tick() after
//                 draining all pending events.
// Ownership/Lifetime: App owns the root widget hierarchy and backing screen
//                     buffer while borrowing a TermIO implementation.
// Links: docs/tui/architecture.md#app-loop
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the terminal UI application loop and event dispatch.
/// @details The `viper::tui::App` type coordinates the root widget tree, focus
///          manager, and terminal renderer.  Each `tick()` consumes queued
///          events, routes them through the keymap, and paints the widget tree
///          into the backing buffer before flushing to the terminal.

#include "tui/app.hpp"

namespace viper::tui
{
/// @brief Construct an application host around a widget tree and renderer.
/// @details Stores the provided root widget, initialises the renderer with the
///          given terminal I/O backend, and sizes the backing buffer to match
///          the reported terminal dimensions so the first paint uses the full
///          screen real estate.
/// @param root Root widget that defines the UI hierarchy.
/// @param tio Terminal I/O shim responsible for drawing frames to the user.
/// @param rows Number of rows in the terminal viewport.
/// @param cols Number of columns in the terminal viewport.
/// @param truecolor Whether the renderer may emit 24-bit colour sequences.
App::App(std::unique_ptr<ui::Widget> root,
         ::viper::tui::term::TermIO &tio,
         int rows,
         int cols,
         bool truecolor)
    : root_(std::move(root)), renderer_(tio, truecolor), rows_(rows), cols_(cols)
{
    screen_.resize(rows, cols);
}

/// @brief Queue an input event to be processed on the next @ref tick cycle.
/// @details Events are appended to an internal vector so producers can submit
///          them from arbitrary contexts.  The vector is drained during
///          @ref tick, enabling deterministic ordering and deferring widget
///          interaction until the frame boundary.
/// @param ev Event captured from the terminal or synthetic input pipeline.
void App::pushEvent(const ui::Event &ev)
{
    events_.push_back(ev);
}

/// @brief Expose the focus manager that tracks the currently active widget.
/// @details Callers use the manager to manipulate focus programmatically, for
///          example when changing views or jumping to specific controls.  The
///          reference remains valid for the lifetime of the application.
/// @return Mutable reference to the focus manager instance owned by the app.
ui::FocusManager &App::focus()
{
    return focus_;
}

/// @brief Install a keymap that translates key presses into widget actions.
/// @details Keymaps intercept keyboard events before widgets receive them,
///          enabling application-wide shortcuts.  Passing @c nullptr removes
///          the active keymap, allowing events to flow directly to the focused
///          widget.
/// @param km Pointer to the keymap that should process keyboard events.
void App::setKeymap(input::Keymap *km)
{
    keymap_ = km;
}

/// @brief Advance the application by one frame, processing events and drawing.
/// @details The loop drains the queued events, handling focus traversal for tab
///          events, delegating keyboard handling to the keymap when present, and
///          finally dispatching unhandled events to the focused widget.  After
///          input is processed the root widget is laid out, painted into the
///          backing buffer, and the renderer flushes the frame to the terminal
///          before snapshotting the previous state for diff-based redraws.
void App::tick()
{
    for (const auto &ev : events_)
    {
        if (ev.key.code == term::KeyEvent::Code::Tab)
        {
            if (ev.key.mods & term::KeyEvent::Shift)
            {
                (void)focus_.prev();
            }
            else
            {
                (void)focus_.next();
            }
            continue;
        }
        bool handled = false;
        if (keymap_)
        {
            handled = keymap_->handle(focus_.current(), ev.key);
        }
        if (!handled)
        {
            if (auto *w = focus_.current())
            {
                w->onEvent(ev);
            }
        }
    }
    events_.clear();
    if (root_)
    {
        ui::Rect full{0, 0, cols_, rows_};
        root_->layout(full);
        screen_.clear(render::Style{});
        root_->paint(screen_);
    }
    renderer_.draw(screen_);
    screen_.snapshotPrev();
}

/// @brief Update the terminal dimensions and resize internal buffers.
/// @details Called when the terminal reports a geometry change.  Resizing the
///          backing buffer ensures subsequent paints cover the new viewport and
///          prevents stale data from previous dimensions persisting on screen.
/// @param rows Updated row count reported by the terminal.
/// @param cols Updated column count reported by the terminal.
void App::resize(int rows, int cols)
{
    rows_ = rows;
    cols_ = cols;
    screen_.resize(rows_, cols_);
}

} // namespace viper::tui
