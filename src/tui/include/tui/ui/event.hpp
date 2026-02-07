//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Event struct, the generic input event wrapper used
// throughout Viper's TUI widget system. Events encapsulate terminal input
// (key presses, modifiers) and are routed through the widget tree by the
// App and FocusManager.
//
// The Event struct currently wraps a single KeyEvent, but is designed as
// an extensible envelope that may later include mouse events, paste events,
// or custom application events.
//
// Key invariants:
//   - Events are value types and are cheap to copy.
//   - The key field is always valid (default-constructed if no key data).
//
// Ownership: Event owns its KeyEvent by value; no heap allocation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tui/term/key_event.hpp"

namespace viper::tui::ui
{
/// @brief Generic input event wrapper for the TUI widget system.
/// @details Encapsulates terminal input data routed through the widget tree.
///          Currently wraps a KeyEvent; designed for future extension to include
///          mouse, paste, and custom events.
struct Event
{
    term::KeyEvent key{};
};
} // namespace viper::tui::ui
