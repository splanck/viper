<!--
SPDX-License-Identifier: MIT
File: docs/tui-architecture.md
Purpose: Overview of ViperTUI architecture and testing strategy.
-->

# ViperTUI Architecture

ViperTUI is an experimental terminal UI library built in layers. Each layer stays
focused and exposes a small surface so higher tiers can be tested without a real
terminal.

## Layers

### Term
Low-level terminal handling lives under `tui/term/`. `TermIO` abstracts writes to
the terminal while `TerminalSession` configures raw mode and manages alt‑screen
state. Clipboard support uses OSC 52 sequences but can be disabled for tests.

### Render
`render/` converts a widget tree into escape sequences. It maintains an in-memory
surface and computes minimal diffs before emitting to `TermIO`.

### UI
`ui/` holds the widget tree and focus management. It delivers input events,
invokes widget callbacks, and triggers re-renders when state changes.

### Widgets
Reusable components such as lists, containers, and modals live in `widgets/`.
Widgets compose other widgets and render through the UI and render layers.

### Text
Text helpers under `text/` provide buffer management and search utilities used by
widgets that edit or display text.

### Tests
Tests exercise the layers without a real TTY by using `StringTermIO` to capture
rendered output. Setting `VIPERTUI_NO_TTY=1` ensures `TerminalSession` stays
inactive so tests run headless.

## Environment Flags

- `VIPERTUI_NO_TTY` &ndash; when set to `1`, `TerminalSession` skips TTY setup and
  the application renders a single frame then exits. Useful for CI and tests.
- `VIPERTUI_DISABLE_OSC52` &ndash; disables OSC 52 clipboard sequences so tests do not
  emit control codes on unsupported terminals.

## Headless Testing Pattern

```cpp
using tui::term::StringTermIO;

// Force headless mode and capture output.
setenv("VIPERTUI_NO_TTY", "1", 1);
StringTermIO tio;           // acts like a fake terminal
// ... render widgets using `tio` ...
assert(tio.buffer().find("expected text") != std::string::npos);
```

`StringTermIO` records all writes and allows assertions on the exact escape
sequences produced by the render layer.

