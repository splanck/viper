# Plan 04 — Horizontal scrollbar + trackpad/Shift-wheel scrolling

## 1. Objective & scope

Make long lines navigable. The CodeEditor currently has no horizontal
scrollbar, ignores horizontal wheel/trackpad deltas, and doesn't map
Shift+wheel — with word wrap off (the default), the only way to see the right
half of a long line is to arrow-key the cursor there. Add:

1. horizontal trackpad scrolling (`wheel.delta_x`),
2. Shift+vertical-wheel → horizontal scroll,
3. a bottom horizontal scrollbar (thumb drag + track jump), auto-hidden when
   content fits or word wrap is on.

**In scope:** CodeEditor widget only (input + paint + metrics). OutputPane/ListBox
horizontal handling is out (they wrap or elide today; plan 14 touches their
vertical smoothness only).

## 2. Current state (verified anchors)

- Wheel handler consumes only `delta_y`
  (`src/lib/gui/src/widgets/vg_codeeditor_input.inc:1951-1956`); `delta_x`
  reaches the widget in `vg_event_t` (`event->wheel.delta_x` — the platform
  layer already delivers both axes: `rt_gui_app.c:1453-1456` forwards
  `scroll.delta_x/delta_y`).
- Scrollbar metrics are vertical-only:
  `codeeditor_get_scrollbar_metrics(editor, widget, &track_x, &thumb_y,
  &thumb_height, &max_scroll, &thumb_travel)` used by MOUSE_DOWN/MOVE
  (`vg_codeeditor_input.inc:1447-1470, 1503-1536`), constant
  `CODEEDITOR_SCROLLBAR_WIDTH` (`:1504`).
- `scroll_x` exists and is respected everywhere in paint
  (`vg_codeeditor_paint.inc:226,274,404-529` — text, selections, guides,
  cursor all subtract `scroll_x`) and by `ensure_cursor_visible` (which is what
  currently moves it).
- Content width for clamping: the widget maintains
  `runtime_content_width` cache (`vg_ide_widgets_editor.h:242-247`) and layout
  caches keyed by `layout_generation`; monospace `char_width` is fixed
  (`:163`). Longest-line width must be computed/maintained — check whether a
  max-line-length is already tracked (grep `content_width` in the .inc files)
  before adding one.
- Word wrap: `word_wrap` flag (`:218`); when on, horizontal scrolling must be
  inert and the bar hidden (`scroll_x` forced 0 — verify `ensure_cursor_visible`
  already does this in wrap mode).
- `codeeditor_clamp_scroll(editor, widget)` clamps today — extend for X.

## 3. Design

### 3.1 Metrics

Add `codeeditor_get_hscrollbar_metrics(editor, widget, &track_y, &thumb_x,
&thumb_width, &max_scroll_x, &thumb_travel)`:

- `content_width = max_line_cols * char_width + padding` where `max_line_cols`
  is the longest line's *visual* column count. Maintain
  `int longest_line_cols` + `int longest_line_idx` on the **editor** (or buffer,
  if plan 21 landed first — put it with `lines`): updated incrementally on line
  edit/insert/delete (O(1) for growth; on shrink of the longest line, rescan
  lazily — set a `longest_line_dirty` flag and recompute on next query, O(n)
  worst case only after shrinking the longest line).
- Track occupies the bottom edge (height = `CODEEDITOR_SCROLLBAR_WIDTH`),
  spanning from `gutter_width` to `widget->width - CODEEDITOR_SCROLLBAR_WIDTH`
  (leave the corner square empty).
- Visible only when `!word_wrap && content_width > viewport_width`.

### 3.2 Input

- `VG_EVENT_MOUSE_WHEEL` (`vg_codeeditor_input.inc:1951`):

```c
float dx = event->wheel.delta_x;
float dy = event->wheel.delta_y;
if ((event->modifiers & VG_MOD_SHIFT) && dx == 0.0f) { dx = dy; dy = 0.0f; }
if (dy != 0) editor->scroll_y -= dy * line_height * CODEEDITOR_MOUSE_WHEEL_LINES;
if (dx != 0 && !editor->word_wrap)
    editor->scroll_x -= dx * editor->char_width * CODEEDITOR_MOUSE_WHEEL_LINES;
codeeditor_clamp_scroll(editor, widget);
```

  (Sign convention: match the vertical handler's existing direction; verify on
  macOS natural scrolling — the platform layer already normalizes for vertical,
  so horizontal follows the same convention.)
- `VG_EVENT_MOUSE_DOWN/MOVE/UP`: mirror the vertical thumb-drag/track-jump code
  (`:1503-1536`, `:1447-1470`, `:1422-1429`) with `hscrollbar_dragging` +
  `hscrollbar_drag_offset` fields. Hit-test order: vertical bar first (existing),
  then horizontal bar, then gutter, then content.
- `codeeditor_clamp_scroll`: clamp `scroll_x` to `[0, max(0, content_width -
  viewport_width)]`; force 0 when word wrap on.

### 3.3 Paint

In `vg_codeeditor_paint.inc`, after the vertical scrollbar drawing: track rect +
thumb rect using the same theme colors as the vertical bar (find the color
source in the vertical bar's paint block and reuse). Reduce the text clip/
viewport height by the bar height when visible so the last line isn't obscured:
the visible-line math uses `widget->height` in several places
(`vg_codeeditor_input.inc:1762,1848` PAGE_UP/DOWN; paint's visible range) —
introduce `codeeditor_viewport_height(editor, widget)` returning
`widget->height - (hbar_visible ? CODEEDITOR_SCROLLBAR_WIDTH : 0)` and use it in
paint, clamp, ensure-visible, page-size, and hit-testing (`codeeditor_local_point_to_position`).

## 4. Implementation steps

1. Longest-line tracking + `content_width` (with lazy rescan on shrink).
2. `codeeditor_get_hscrollbar_metrics` + `viewport_height` helper; thread the
   helper through paint/clamp/ensure-visible/page/hit-test call sites.
3. Wheel handler changes (delta_x + Shift mapping + clamp).
4. H-thumb drag/track-jump input + `hscrollbar_*` fields (init in lifecycle,
   no destroy needs).
5. Paint the bar.
6. C tests (`src/lib/gui/tests/`): synthesize wheel events with delta_x, with
   Shift+delta_y; assert `scroll_x` moves and clamps; drag the h-thumb via
   mouse events; longest-line shrink triggers correct re-clamp; word-wrap mode
   keeps `scroll_x == 0` and hides the bar (metrics function returns false).
7. Probe: extend `viperide/src/probes/editor_hot_path_probe.zia` or add
   `hscroll_probe.zia` — load a document with a 500-col line, set cursor to
   col 400, assert cursor pixel query stays within viewport after
   ensure-visible (proves scroll_x math through the runtime wrapper caches,
   `vg_ide_widgets_editor.h:242-247`). Register with `LABELS "zia;viperide;editor"`.
8. Manual: trackpad two-axis scroll on macOS; Shift+wheel with a mouse; drag
   the bar; toggle word wrap and confirm the bar disappears and scroll resets.
9. Full no-skip build + test run.

## 5. Files to modify

- `src/lib/gui/include/vg_ide_widgets_editor.h` — new fields
  (`hscrollbar_dragging`, `hscrollbar_drag_offset`, `longest_line_cols`,
  `longest_line_idx`, `longest_line_dirty`) + doc updates.
- `src/lib/gui/src/widgets/vg_codeeditor_input.inc` — wheel + drag + hit-test.
- `src/lib/gui/src/widgets/vg_codeeditor_paint.inc` — bar painting + viewport height.
- `src/lib/gui/src/widgets/vg_codeeditor_core.inc` / `_editing.inc` — longest-line
  maintenance at line mutation sites; clamp changes.
- `src/lib/gui/src/widgets/vg_codeeditor_lifecycle.inc` — field init.
- `src/lib/gui/tests/` — C tests.
- Probe + `src/tests/CMakeLists.txt` if a new probe is added.

## 6. Testing

C tests per step 6 (primary); probe for the runtime-visible invariants; manual
trackpad verification on macOS (the only local platform).

## 7. Acceptance criteria

- A 500-column line can be fully inspected via trackpad, Shift+wheel, and thumb
  drag; the bar appears only when needed; word wrap disables it entirely.
- Cursor navigation/ensure-visible still works and never leaves the caret under
  the bar; PAGE_UP/DOWN unchanged in effective size (uses reduced viewport).
- No perf regression in `editor_hot_path_probe` counters (longest-line
  maintenance is O(1) on the hot path).

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` after C changes + full build.
- Full Viper header on modified C files.
- 100% cross-platform; no platform code involved (deltas already arrive both-axis).
- Zero external dependencies.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
