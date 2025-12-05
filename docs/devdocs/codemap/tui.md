# CODEMAP: TUI

Terminal UI library (`src/tui/`) for text-based interfaces.

The current implementation is primarily header-first with a thin runtime.

## Public Headers (`src/tui/include/tui/`)

| Directory | Purpose |
|-----------|---------|
| `term/` | Terminal abstractions (session, I/O, input, CSI, clipboard, UTF-8) |
| `render/` | Renderer and screen buffer interfaces |
| `text/` | Text buffer, piece table, edit history, line index, search |
| `ui/` | Minimal UI framework (widget, container, focus, modal, events) |
| `views/` | View widgets (text view) |
| `widgets/` | Common widgets (button, label, list/tree view, splitter, status/search bars, palette) |
| `style/` | Theme definitions |
| `syntax/` | Syntax highlighting rules |
| `config/` | Configuration types |
| `input/` | Keymap definitions |
| `util/` | Unicode helpers |
| `support/` | Utility types (e.g., `function_ref`) |
| root | `app.hpp`, `version.hpp` public API |

## Source (`src/tui/src/`)

| File | Purpose |
|------|---------|
| `app.cpp` | Minimal app runtime glue (focus/layout/render orchestration) |
| `version.cpp` | Library version API |

## Demo (`src/tui/apps/`)

| File | Purpose |
|------|---------|
| `tui_demo.cpp` | Sample TUI application |

## Tests (`src/tui/tests/`)

Comprehensive unit tests covering app lifecycle, input decoding, renderer, widgets,
and Unicode handling (UTF-8 graphemes/width). See the files under `tests/` for
the full list.
