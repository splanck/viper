# CODEMAP: TUI

Terminal UI library (`src/tui/`) for text-based interfaces.

## Application Framework (`src/`)

| File | Purpose |
|------|---------|
| `app.cpp` | Main application loop: focus, layout, rendering |
| `version.cpp` | Library version API |

## Terminal (`src/term/`)

| File | Purpose |
|------|---------|
| `session.cpp` | RAII terminal session (raw mode, alt screen) |
| `term_io.cpp` | Terminal I/O abstraction (real and mock) |
| `input.cpp` | Input decoder: bytes to key/mouse events |
| `CsiParser.cpp` | CSI escape sequence parser |
| `Utf8Decoder.cpp` | Incremental UTF-8 decoder |
| `clipboard.cpp` | OSC 52 clipboard implementation |

## Rendering (`src/render/`)

| File | Purpose |
|------|---------|
| `renderer.cpp` | ANSI escape renderer with diff-based updates |
| `screen.cpp` | Double-buffered screen with cell diffing |

## Text Editing (`src/text/`)

| File | Purpose |
|------|---------|
| `text_buffer.cpp` | Piece-table buffer with undo/redo |
| `PieceTable.cpp` | Core piece-table storage |
| `EditHistory.cpp` | Grouped undo/redo transactions |
| `LineIndex.cpp` | Line number to byte offset mapping |
| `search.cpp` | Literal and regex text search |

## UI Framework (`src/ui/`)

| File | Purpose |
|------|---------|
| `widget.cpp` | Base widget class |
| `container.cpp` | Stack containers (VStack, HStack) |
| `focus.cpp` | Focus ring management |
| `modal.cpp` | Modal dialog hosting |

## Views (`src/views/`)

| File | Purpose |
|------|---------|
| `text_view_cursor.cpp` | Text view cursor movement |
| `text_view_input.cpp` | Text view input handling |
| `text_view_render.cpp` | Text view rendering |

## Widgets (`src/widgets/`)

| File | Purpose |
|------|---------|
| `button.cpp` | Clickable button widget |
| `label.cpp` | Read-only text label |
| `list_view.cpp` | Selectable list with navigation |
| `tree_view.cpp` | Collapsible tree navigator |
| `splitter.cpp` | Resizable split container |
| `status_bar.cpp` | Single-line status bar |
| `search_bar.cpp` | Incremental search widget |
| `command_palette.cpp` | Command search and execution |

## Style and Syntax (`src/style/`, `src/syntax/`)

| File | Purpose |
|------|---------|
| `theme.cpp` | Default color theme |
| `rules.cpp` | Regex-based syntax highlighting |

## Configuration (`src/config/`)

| File | Purpose |
|------|---------|
| `config.cpp` | INI-style config parser |

## Input (`src/input/`)

| File | Purpose |
|------|---------|
| `keymap.cpp` | Key chord to command mapping |

## Utilities (`src/util/`)

| File | Purpose |
|------|---------|
| `unicode.cpp` | Unicode width detection, UTF-8 decoding |

## Demo Application (`apps/`)

| File | Purpose |
|------|---------|
| `tui_demo.cpp` | Sample TUI application |

## Headers (`include/tui/`)

| Directory | Purpose |
|-----------|---------|
| `term/` | Terminal abstractions (session, input, clipboard) |
| `render/` | Renderer and screen buffer |
| `text/` | Text buffer, piece table, search |
| `ui/` | Widget framework (container, focus, modal) |
| `views/` | View widgets (text_view) |
| `widgets/` | Concrete widgets |
| `style/` | Theme definitions |
| `syntax/` | Syntax highlighting |
| `config/` | Configuration types |
| `input/` | Keymap definitions |
| `util/` | Unicode utilities |
| `support/` | function_ref and helpers |
