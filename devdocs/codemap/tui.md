# CODEMAP: TUI

- **tui/apps/tui_demo.cpp**

  Serves as the sample executable demonstrating how to wire the terminal UI stack together. It inspects the `VIPERTUI_NO_TTY` environment variable to decide whether to run headless, constructs a `TerminalSession`, real terminal IO adapter, theme, and backing text buffer, then builds a widget tree containing a `TextView` and `ListView` joined by an `HSplitter`. The app registers those widgets with the `FocusManager`, primes the event loop, and in interactive mode enters a platform-specific read loop that decodes keystrokes into `ui::Event`s fed to `App`. Execution exits when the user presses Ctrl+Q, making the demo handy for manual testing and screenshots. Dependencies include `tui/app.hpp`, `tui/style/theme.hpp`, `tui/term/input.hpp`, `tui/term/session.hpp`, `tui/term/term_io.hpp`, `tui/text/text_buffer.hpp`, `tui/views/text_view.hpp`, `tui/widgets/list_view.hpp`, `tui/widgets/splitter.hpp`, plus `<cstdlib>`, `<memory>`, `<string>`, `<vector>`, and platform console headers.

- **tui/src/app.cpp**

  Implements the headless `viper::tui::App` loop that coordinates focus, layout, and rendering for the widget hierarchy. The constructor captures the root widget, wraps the provided `TermIO` in the renderer, and pre-sizes the internal `ScreenBuffer` to match the requested terminal dimensions. `tick` drains queued events, handles tab-based focus cycling, consults an optional `input::Keymap`, and forwards remaining events to the focused widget before laying out and painting the tree. After rendering it flushes the buffer through `Renderer::draw`, snapshots the previous frame for diffing, and exposes `resize` so callers can adjust the terminal geometry. Dependencies come from `tui/app.hpp`, which pulls in the UI widget base classes, focus manager, renderer, terminal key event definitions, and screen buffer utilities.

- **tui/src/config/config.cpp**

  Parses the INI-style configuration files that customize themes, key bindings, and editor behaviour. Static helpers trim whitespace, parse hexadecimal colours, interpret key chords, and normalize boolean strings so the loader tolerates user formatting quirks. `loadFromFile` walks the file line by line, tracking the current section to fill theme palettes, append global keymap bindings, and configure editor options like tab width or soft wrapping. Dependencies include `tui/config/config.hpp` along with `<algorithm>`, `<cctype>`, `<fstream>`, `<sstream>`, and `<string_view>`.

- **tui/src/input/keymap.cpp**

  Defines the `Keymap` that maps key chords onto executable commands. It provides comparison and hash operators for `KeyChord`, allowing global and widget-specific bindings to live in unordered maps keyed by modifiers, key codes, and Unicode codepoints. The implementation records command metadata, registers bindings, executes the stored callbacks, and exposes lookup helpers so widgets or palettes can inspect the command list. `handle` checks widget overrides before consulting the global table, returning whether the event was consumed. Dependencies reside in `tui/input/keymap.hpp`, which itself pulls in the UI widget base class and `tui::term::KeyEvent` definitions.

- **tui/src/render/renderer.cpp**

  Implements the ANSI escape renderer that emits minimal terminal updates based on `ScreenBuffer` diffs. `setStyle` lazily writes either 24-bit or 256-colour sequences depending on the true-colour flag while caching the last style to avoid redundant output. `moveCursor` and `draw` cooperate to reposition the terminal cursor, walk changed spans, encode UTF-8 glyphs by hand, and stream them through the borrowed `TermIO` before flushing. Dependencies include `tui/render/renderer.hpp` together with `<string>`, `<string_view>`, and `<vector>`.

- **tui/src/render/screen.cpp**

  Backs the renderer with a `ScreenBuffer` that tracks both the current and previous frame at cell granularity. Equality operators for `RGBA`, `Style`, and `Cell` make diffing straightforward when searching for modified regions. `resize`, `clear`, `snapshotPrev`, and `computeDiff` maintain the double buffer and produce compact spans describing changes so the renderer can minimize writes. Dependencies consist of `tui/render/screen.hpp` and `<algorithm>` for bulk operations.

- **tui/src/style/theme.cpp**

  Provides the default colour theme consumed by widgets and renderers. The constructor seeds normal, accent, disabled, and selection palettes with RGBA values tuned for dark terminals. `style` returns the palette entry matching a requested role so drawing code can translate semantic roles into colours without duplicating tables. Dependencies include `tui/style/theme.hpp` and the render style definitions it exposes.

- **tui/src/syntax/rules.cpp**

  Implements a lightweight syntax highlighter driven by a JSON array of regex rules. An internal `JsonParser` walks the configuration, interpreting each rule's pattern and style block (including colour and bold attributes) into compiled `std::regex` objects and `render::Style` values. `SyntaxRuleSet::spans` caches the last highlighted text for each line to avoid recomputation until edits invalidate the entry. Dependencies include `tui/syntax/rules.hpp`, `<cctype>`, `<fstream>`, `<sstream>`, and the rendering and regex support provided through that header.

- **tui/src/term/CsiParser.cpp**

  Translates terminal control-sequence introducer (CSI) packets into structured `KeyEvent` and `MouseEvent` records for the input decoder. It recognises SGR mouse sequences, modifier encodings, and the bracketed-paste sentinels so higher layers receive canonical events instead of raw escape bytes. Parsed events are appended directly into the caller-provided buffers, preserving ordering while keeping the parser stateless between invocations. Dependencies include `tui/term/CsiParser.hpp`, which wires in the `KeyEvent`/`MouseEvent` definitions and supporting `<string_view>`/`<vector>` utilities.

- **tui/src/term/Utf8Decoder.cpp**

  Implements the incremental UTF-8 decoder that the terminal input pipeline uses to assemble Unicode codepoints from byte streams. The state machine tracks the number of continuation bytes required for the current codepoint, emits decoded scalars when the sequence completes, and flags malformed sequences so callers can recover gracefully. A replay flag lets the higher-level parser reprocess unexpected leading bytes once the decoder resets, ensuring resilience to noisy streams. Dependencies include `tui/term/Utf8Decoder.hpp`, which defines the `Utf8Result` structure and the small state fields the implementation updates.

- **tui/src/term/clipboard.cpp**

  Offers clipboard implementations that rely on OSC 52 escape sequences to copy text from the terminal. Helper routines perform base64 encoding, honour the `VIPERTUI_DISABLE_OSC52` environment flag, and assemble the precise control strings terminals expect. `Osc52Clipboard` writes the encoded payload through a `TermIO` adapter and flushes it, while `MockClipboard` records the sequence and decodes it back to text for tests. Dependencies include `tui/term/clipboard.hpp`, `tui/term/term_io.hpp`, and standard `<cstdlib>`, `<string>`, and `<string_view>` utilities.

- **tui/src/term/input.cpp**

  Translates raw terminal bytes into structured key and mouse events through the `InputDecoder` state machine. The decoder buffers partial UTF-8 sequences, recognises CSI escape patterns, and understands bracketed paste and SGR mouse encodings so higher-level code receives meaningful events. Helper functions parse numeric parameters, derive modifier masks, and enqueue `KeyEvent` or `MouseEvent` objects for later consumption. Dependencies come from `tui/term/input.hpp` alongside `<string_view>` and the container types supplied via the header.

- **tui/src/term/term_io.cpp**

  Provides concrete `TermIO` implementations backed by `stdout` or an in-memory string buffer so renderers can target either a real terminal or tests. `RealTermIO` writes and flushes escape sequences using `std::fwrite`/`std::fflush`, avoiding locale surprises while streaming bytes verbatim. `StringTermIO` accumulates output and exposes getters/clearers so golden tests can assert on rendered escape sequences without touching the console. Dependencies include `tui/term/term_io.hpp` and the C standard `<cstdio>` facilities that drive the real implementation.

- **tui/src/term/session.cpp**

  Wraps terminal initialisation in an RAII `TerminalSession` that toggles raw mode, alternate screens, and optional mouse reporting. Construction checks environment toggles, consults `isatty`/`tcgetattr` on POSIX or `GetConsoleMode` on Windows, and uses `RealTermIO` to write the escape sequences that prepare the host terminal. The destructor reverses those changes—disabling mouse tracking, leaving the alternate screen, restoring the cursor, and reapplying saved terminal attributes—only when initialisation succeeded. Dependencies include `tui/term/session.hpp`, `tui/term/term_io.hpp`, and the platform headers pulled in transitively.

- **tui/src/text/text_buffer.cpp**

  Implements a piece-table `TextBuffer` supporting efficient insertions, deletions, undo/redo, and line lookups for the editor. `load` seeds the original buffer and line index, while `insert` and `erase` split pieces, update size counters, maintain the line-start table, and record operations into grouped transactions. `beginTxn`, `endTxn`, and query helpers such as `getText` rebuild substrings on demand without copying stored text, keeping edits and history compact. Dependencies include `tui/text/text_buffer.hpp` together with `<algorithm>` and `<cassert>`; the header supplies the container types used here.

- **tui/src/text/EditHistory.cpp**

  Tracks grouped undo and redo operations for the editor so callers can record edits without micromanaging stacks. Transactions aggregate insert/erase ops via `beginTxn`/`endTxn`, clearing the redo stack whenever new work arrives to maintain linear history semantics. Undo walks a transaction in reverse order and replays each operation through a callback, while redo replays in insertion order, keeping both stacks and the current transaction in sync. Dependencies include `tui/text/EditHistory.hpp`, which defines the operation types and callback signature, plus `<utility>` for move semantics.

- **tui/src/text/LineIndex.cpp**

  Maintains the mapping from line numbers to byte offsets so the text view can jump to specific rows quickly. `reset` scans an entire buffer to seed the index, and incremental `onInsert`/`onErase` hooks update later offsets while splicing in or removing newline positions. Erase updates shrink remaining offsets and drop any interior line starts, preserving a sorted vector with an initial zero sentinel for convenience. Dependencies include `tui/text/LineIndex.hpp` and `<algorithm>` for binary searches and bulk offset adjustments.

- **tui/src/text/PieceTable.cpp**

  Implements the editor’s piece-table storage, managing slices of the original and append buffers while emitting change notifications for observers. Insertions splice a new piece into the linked sequence, splitting an existing piece when necessary and recording the inserted text so callbacks can update indexes. Erasures materialise the removed string via `getText`, carve out fully or partially covered pieces, and queue notifications describing the deleted span. Dependencies include `tui/text/PieceTable.hpp` alongside `<algorithm>` and `<utility>` for piece management and move-aware change records.

- **tui/src/text/search.cpp**

  Provides literal and regular-expression search helpers for the text buffer with safeguards against scanning arbitrarily large inputs. `findAll` truncates buffers beyond 1 MiB, then either performs simple substring matching or iterates a compiled `std::regex`, returning every span discovered. `findNext` resumes from an arbitrary offset and catches `std::regex_error` so a malformed pattern simply yields no match instead of crashing the UI. Dependencies include `tui/text/search.hpp`, the `TextBuffer` interface it references, and the C++ `<regex>` library.

- **tui/src/ui/container.cpp**

  Implements stack-style container widgets that own child widgets and redistribute layout space among them. `VStack` and `HStack` compute equal-height or equal-width slices, adjust the remainder for the final child, and forward both layout and paint calls so nested widgets render correctly. The base `Container` simply records children and iterates them during painting, keeping composition rules simple while enforcing that layouts derive from the parent rectangle. Dependencies include `tui/ui/container.hpp`, which defines the hierarchy, and `tui/render/screen.hpp` for painting support.

- **tui/src/ui/focus.cpp**

  Provides the focus ring that decides which widget currently receives keyboard events. Registration adds widgets that opt into focus, while unregistering adjusts the active index and toggles `onFocusChanged` notifications to keep UI state consistent. `next`/`prev` wrap around the ring, handing off focus only when the target widget differs so focus transitions remain well-ordered. Dependencies include `tui/ui/focus.hpp`, `tui/ui/widget.hpp`, and `<algorithm>` for searches through the ring container.

- **tui/src/ui/modal.cpp**

  Hosts the modal management layer that draws a dimmed backdrop and routes input to popups layered over the main UI. `ModalHost` keeps a root widget plus a stack of modal dialogs, cloning popup dismiss callbacks so modals can close themselves. `Popup` centers itself within the host rectangle, paints an ASCII border, and dismisses on Esc or Enter, keeping the modal interaction model predictable. Dependencies include `tui/ui/modal.hpp`, `tui/render/screen.hpp`, and `<algorithm>` for rectangle math and container utilities.

- **tui/src/ui/widget.cpp**

  Provides the default behaviour for the abstract `ui::Widget` base class. It caches the rectangle passed to `layout`, implements `paint` and `onEvent` as no-ops to be overridden by concrete widgets, and reports that widgets do not accept focus unless they explicitly opt in. Additional helpers expose the stored rectangle and ignore focus-change notifications, keeping the base contract minimal. Dependencies are limited to `tui/ui/widget.hpp`, which defines the widget interface and supporting structures.

- **tui/src/util/unicode.cpp**

  Supplies Unicode utilities shared by the renderer and widgets, notably East Asian width detection and UTF-8 decoding. `char_width` checks combining-mark ranges and a curated list of double-width blocks so layout code can keep monospace grids aligned. `decode_utf8` walks byte sequences defensively, rejecting overlong encodings or surrogate codepoints and appending the replacement character when malformed data appears. Dependencies include `tui/util/unicode.hpp`, which declares the helpers and pulls in the minimal standard headers needed for the tables.

- **tui/src/version.cpp**

  Binds the terminal UI library to a semantic version string returned through the C-compatible `viper_tui_version` API. Keeping the implementation in its own translation unit isolates the constant so consumers can link against the helper without dragging in unrelated UI code. The function simply returns a static literal, letting packaging scripts or future build steps swap the string without touching headers. Dependencies include `tui/version.hpp`, which declares the exported function and documents linkage expectations.

- **tui/src/views/text_view.cpp**

  Implements the primary text editor view that renders a `TextBuffer`, tracks selection state, and responds to navigation commands. It translates between byte offsets and screen columns using UTF-8 decoding plus `util::char_width`, clamps cursor movement, and maintains scroll offsets so the caret stays visible. The painting routine draws an optional gutter, applies syntax highlight spans, and writes selection or highlight styles into the `ScreenBuffer` using the injected theme. Dependencies span `tui/views/text_view.hpp`, `tui/render/screen.hpp`, `tui/syntax/rules.hpp`, and standard `<algorithm>` and `<string>` helpers.

- **tui/src/widgets/button.cpp**

  Renders a clickable button with a drawn ASCII border and integrates with the theme palette for styling. The `paint` routine lays out the rectangle, draws borders, clears the interior, and centers the label text while applying accent versus normal colours. Handling Enter or space key events invokes the stored callback and returns true so event loops know the button consumed the action, and `wantsFocus` ensures it can receive those keystrokes. Dependencies include `tui/widgets/button.hpp`, `tui/render/screen.hpp`, and the theme definitions pulled in through the header.

- **tui/src/widgets/command_palette.cpp**

  Defines the command palette widget that lets users search and run registered keymap commands. It keeps a lowercase query string, rebuilds the filtered command ID list on every change, and captures focus so keystrokes immediately update the palette. Event handling consumes backspace, printable characters, and enter to trigger the top match, while `paint` draws the prompt and visible results into the `ScreenBuffer`. Dependencies include `tui/widgets/command_palette.hpp`, `tui/render/screen.hpp`, and the keymap facilities provided through the header.

- **tui/src/widgets/label.cpp**

  Implements the simplest widget: a read-only label that paints a single line of text using the theme’s normal style. Rendering iterates characters until it hits the layout width, writing each glyph and style into the `ScreenBuffer` row anchored at the widget’s origin. The widget holds a copy of the text so callers can freely destroy source strings after construction without invalidating the view. Dependencies include `tui/widgets/label.hpp` and `tui/render/screen.hpp` for drawing primitives.

- **tui/src/widgets/list_view.cpp**

  Provides a selectable list widget with keyboard navigation, optional custom rendering, and multi-selection support. The constructor seeds an item vector, tracks a parallel boolean selection array, and installs a default renderer that draws chevron prefixes using theme accents. Arrow keys move the cursor, while shift-selection expands the highlighted range and ctrl keys allow movement without toggling selections, keeping anchor/cursor bookkeeping in sync. Dependencies include `tui/widgets/list_view.hpp`, `tui/render/screen.hpp`, and `<algorithm>` for selection helpers.

- **tui/src/widgets/search_bar.cpp**

  Implements the incremental search bar that drives the text view’s highlight overlay. The widget maintains the query string, optional regex mode, and the list of current matches, updating highlights and cursor positions through the injected `TextBuffer`/`TextView` whenever the query changes. Keyboard input handles backspace, printable ASCII, F3/Enter cycling, and funnels matches into the view so navigation stays in sync with the status display. Dependencies include `tui/widgets/search_bar.hpp`, `tui/render/screen.hpp`, and the text search utilities declared in `tui/text/search.hpp`.

- **tui/src/widgets/splitter.cpp**

  Splits available space between two child widgets either horizontally or vertically while remembering a floating-point ratio. Layout recomputes child rectangles from the current ratio, clamping the split to a reasonable range, and repaint simply forwards to each child so they render into their slice. Keyboard handlers listen for Ctrl+arrow keys to nudge the ratio and immediately relayout children, enabling interactive resizing. Dependencies include `tui/widgets/splitter.hpp`, `tui/render/screen.hpp`, and `<algorithm>` for clamping math.

- **tui/src/widgets/status_bar.cpp**

  Draws a single-line status bar with left-aligned and right-aligned segments rendered using the theme’s normal style. The `paint` routine clears the bottom row of the widget, writes the left text until it reaches the right margin, then backfills the right text from the end while guarding against overlap. Setters for each segment allow the controller to update status messages without rebuilding the widget. Dependencies include `tui/widgets/status_bar.hpp`, `tui/render/screen.hpp`, and `<algorithm>` for clamping offsets.

- **tui/src/widgets/tree_view.cpp**

  Implements a collapsible tree navigator that owns hierarchical nodes and surfaces keyboard navigation for expansion. A depth-first rebuild flattens the visible nodes vector whenever expansion state changes, and `paint` renders indentation, +/- markers, and selection chevrons using theme styles. Arrow keys traverse siblings or toggle expansion, while Enter flips the expanded flag, keeping cursor and parent pointers consistent for quick navigation. Dependencies include `tui/widgets/tree_view.hpp`, `tui/render/screen.hpp`, and `<algorithm>` for node searches.

- **tui/include/tui/app.hpp**

  Declares the headless application loop API coordinating focus, layout, and rendering across a widget tree with a backing renderer and terminal IO.

- **tui/include/tui/version.hpp**, **tui/src/version.cpp**

  Declares and implements the `viper_tui_version` API returning the library semantic version string for embedding and packaging integration.

- **tui/include/tui/render/screen.hpp**

  Declares the `ScreenBuffer` and diff machinery used by the renderer to compute minimal ANSI updates between frames.

- **tui/include/tui/render/renderer.hpp**

  Declares the ANSI renderer interface and style/cursor primitives used to draw buffers to a terminal via the `TermIO` abstraction.

- **tui/include/tui/term/session.hpp**

  Declares the RAII `TerminalSession` wrapper that toggles raw mode, alternate screen, and mouse reporting for interactive apps.

- **tui/include/tui/term/term_io.hpp**

  Declares the platform‑abstracted terminal IO interface used by the renderer to write bytes and control codes.

- **tui/include/tui/term/CsiParser.hpp**

  Declares a small CSI escape‑sequence parser used by input and rendering helpers to interpret terminal responses.

- **tui/include/tui/term/input.hpp**

  Declares terminal input helpers that convert raw device streams into key events and higher‑level actions.

- **tui/include/tui/term/key_event.hpp**

  Declares the `KeyEvent` structure and enums describing keys and modifiers.

- **tui/include/tui/term/Utf8Decoder.hpp**, **tui/src/term/Utf8Decoder.cpp**

  Declares and implements the incremental UTF‑8 decoder used by the terminal input pipeline.

- **tui/include/tui/term/clipboard.hpp**, **tui/src/term/clipboard.cpp**

  Declares and implements a basic clipboard abstraction with platform stubs used by the demo app and tests.

- **tui/include/tui/text/text_buffer.hpp**, **tui/src/text/text_buffer.cpp**

  Declares and implements a piece‑table text buffer with undo/redo and line indexing supporting the text view widget.

- **tui/include/tui/text/EditHistory.hpp**, **tui/src/text/EditHistory.cpp**

  Declares and implements grouped undo/redo tracking via transactions with replay callbacks for editor operations.

- **tui/include/tui/text/PieceTable.hpp**, **tui/src/text/PieceTable.cpp**

  Declares and implements the core piece‑table storage that underpins the `TextBuffer`.
  Declares and implements the core piece‑table storage that underpins the `TextBuffer`.

- **tui/include/tui/input/keymap.hpp**

  Declares the keymap command registry and chord data structures used to map keyboard input to commands.

- **tui/include/tui/style/theme.hpp**

  Declares the theme palette types used across widgets and rendering.

- **tui/include/tui/syntax/rules.hpp**

  Declares the syntax rule representation and interfaces consumed by the highlighter.

- **tui/include/tui/support/function_ref.hpp**

  Declares a lightweight non‑owning function reference used throughout the UI stack to pass callbacks without allocations.

- **tui/include/tui/text/LineIndex.hpp**, **tui/include/tui/text/search.hpp**

  Declares line indexing and text search utilities used by the editor and text view.

- **tui/include/tui/ui/container.hpp**, **tui/include/tui/ui/event.hpp**, **tui/include/tui/ui/focus.hpp**, **tui/include/tui/ui/modal.hpp**, **tui/include/tui/ui/widget.hpp**

  Declares the UI framework building blocks: containers, event types, focus management, modal scaffolding, and the base widget interface.

- **tui/include/tui/util/unicode.hpp**

  Declares Unicode utilities used by input/rendering and text layout.

- **tui/include/tui/views/text_view.hpp**

  Declares the text view widget API used by the demo and tests.

- **tui/include/tui/config/config.hpp**

  Declares configuration schema and loader options (theme, keymap, editor settings).

- **tui/include/tui/widgets/button.hpp**, **tui/include/tui/widgets/command_palette.hpp**, **tui/include/tui/widgets/label.hpp**, **tui/include/tui/widgets/list_view.hpp**, **tui/include/tui/widgets/search_bar.hpp**, **tui/include/tui/widgets/splitter.hpp**, **tui/include/tui/widgets/status_bar.hpp**, **tui/include/tui/widgets/tree_view.hpp**

  Declares the widget APIs used by the demo; each pairs with the corresponding implementation already documented.

- **tui/src/views/text_view_cursor.cpp**, **tui/src/views/text_view_input.cpp**, **tui/src/views/text_view_render.cpp**

  Splits the text view implementation across cursor movement, input handling, and rendering for clarity.
