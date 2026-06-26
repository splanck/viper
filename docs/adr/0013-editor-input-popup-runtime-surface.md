# ADR 0013: Editor Input and Popup Runtime Surface

Date: 2026-06-26

Status: Accepted

## Context

The ViperIDE polish pass fixes editor and popup behaviors that are implemented
below Zia command code in the shared GUI runtime:

- Context menus can be created by runtime callers without inheriting the active
  application theme or default font, which makes right-click popup menus
  unreadable when their fallback colors collide with the shell theme.
- The widget event system synthesizes `CLICK` and `DOUBLE_CLICK`, but classic
  editor selection also needs a third click within the click threshold to select
  a line.
- CodeEditor word and line selection should remain a widget behavior so
  interpreted and native GUI callers observe the same mouse semantics.

The change touches the public GUI C surface by adding an event enum value,
persisting click-count state in the widget runtime snapshot, and exposing a
context-menu theme application helper. Per ADR 0006, runtime C ABI surface
changes require ADR coverage. The scope is GUI runtime behavior only; no IL
opcode, grammar, verifier, VM, native codegen, or language semantics change.

## Decision

Add `VG_EVENT_TRIPLE_CLICK` to the GUI event contract. The central widget event
dispatcher now tracks click count per widget/button/position and emits
single-click, double-click, or triple-click events from mouse-up transitions. A
triple click clears the stored click sequence after dispatch so later clicks
start a new gesture.

Store the latest click count in `vg_widget_runtime_state_t` so modal/popup event
dispatch can save and restore the same click-state snapshot as the existing
last-click widget, button, time, and position fields.

Add `vg_contextmenu_apply_theme(vg_contextmenu_t *, const vg_theme_t *)` as the
shared helper for copying popup background, hover, text, disabled, border, and
separator colors into a context-menu tree. Runtime-created menus and active
application context menus apply the current app theme and default font before
display/paint, including nested submenus.

Keep CodeEditor selection semantics inside the widget:

- double-click selects the word under the pointer;
- triple-click selects the logical line;
- pointer drags only start after a small movement threshold, preserving click
  synthesis for normal press/release gestures;
- keyboard navigation supports Shift selection, word movement, and word delete
  without duplicating that behavior in ViperIDE command handlers.

## Consequences

Right-click menus inherit the active app theme and default font, making popup
items readable across light and dark themes without each caller manually
restyling a menu before showing it.

The editor gains conventional double-click word and triple-click line
selection through the shared CodeEditor widget, so future GUI applications get
the same behavior without ViperIDE-specific event code. Tests cover triple-click
event synthesis, editor word/line selection, and recursive context-menu theme
and font propagation.

Future changes that remove or reinterpret `VG_EVENT_TRIPLE_CLICK`, change the
widget runtime-state layout, or expose additional context-menu C ABI helpers
need ADR coverage or an explicit migration plan.

## Spec Impact

No IL, verifier, VM, or codegen semantics changed. The impact is limited to the
GUI runtime C surface and shared widget behavior.
