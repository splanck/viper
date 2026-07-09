# Plan 05 — Edge autoscroll during selection drag

## 1. Objective & scope

When the user drags a selection past the editor's edge and *holds the mouse
still*, the view should keep scrolling and the selection keep extending. Today
scrolling only advances when a MOUSE_MOVE event arrives, so a stationary
pointer beyond the edge freezes the scroll — selecting more than one screenful
requires wiggling the mouse. Implement timer-driven edge autoscroll with
distance-proportional speed, for both vertical and horizontal edges.

**In scope:** CodeEditor widget (`tick` + drag state). Also apply the same
pattern to the ListBox if trivially reusable, but the editor is the deliverable.

**Out of scope:** drag-and-drop of text (not a current feature), scrollbar drag
behavior (already correct — thumb follows pointer).

## 2. Current state (verified anchors)

- Selection drag: `selection_drag_pending` → threshold →
  `selection_dragging` state machine
  (`src/lib/gui/src/widgets/vg_codeeditor_input.inc:1471-1500`,
  fields `vg_ide_widgets_editor.h:258-263`); position update + anchor-based
  selection recompute + `ensure_cursor_visible(editor)` happen **only inside
  `VG_EVENT_MOUSE_MOVE`** (`:1481-1499`).
- Input capture is held during the drag (`vg_widget_set_input_capture(widget)`
  at `:1615`), so MOUSE_MOVE events keep flowing even outside widget bounds —
  the machinery to receive out-of-bounds coordinates already works; only the
  stationary case is broken.
- Per-frame hook exists: `vg_codeeditor_tick(editor, dt)` is called every frame
  for every visible editor (`rt_gui_app.c:332` via `rt_gui_tick_widget_tree`,
  render loop `:1680`), currently used only for cursor blink
  (`vg_codeeditor_api.inc:47-57`). Note its early-return when unfocused
  (`:48-49`) — autoscroll must run *before* that gate or the gate must be
  restructured (during a drag the editor has focus anyway, but don't rely on
  it: reorder so drag-autoscroll is checked first).
- Last known pointer position: the app tracks `app->mouse_x/mouse_y` per frame
  (`rt_gui_app.c:1430-1431`), but the *widget* only sees event coordinates.
  Simplest correct source: record the last drag-move position on the editor
  (`last_drag_x/last_drag_y`, widget-local coords) — during capture, every
  actual move updates it; between moves the pointer is by definition still at
  that position.

## 3. Design

New fields (`vg_ide_widgets_editor.h`, next to the drag state):

```c
float last_drag_x, last_drag_y;   // widget-local pointer pos during drag
float autoscroll_accum;           // fractional line accumulator (vertical)
float autoscroll_accum_x;         // fractional px accumulator (horizontal)
```

`VG_EVENT_MOUSE_MOVE` while `selection_dragging`: store
`last_drag_x/y = event->mouse.x/y` (in addition to existing logic).

New function in `vg_codeeditor_input.inc` (called from `vg_codeeditor_tick`):

```c
/// Advance edge-autoscroll while a selection drag is active. dt in seconds.
static void codeeditor_drag_autoscroll_tick(vg_codeeditor_t *editor,
                                            vg_widget_t *widget, float dt);
```

Rules:

- Active only while `selection_dragging`.
- Vertical: if `last_drag_y < 0` scroll up; if `last_drag_y > viewport_height`
  scroll down. Speed proportional to overshoot distance, clamped:
  `lines_per_sec = clamp(overshoot_px / line_height, 1, 3) * 10` (≈10–30
  lines/sec — match the feel of mainstream editors; tune constant
  `CODEEDITOR_AUTOSCROLL_MAX_LPS 30`).
- Horizontal (skip when `word_wrap`): same shape with `char_width` units past
  the left/right content edges (respect gutter on the left).
- Accumulate `dt * speed` into the accumulators; apply whole-pixel scroll
  deltas, then **re-run the selection update** exactly as the MOUSE_MOVE
  handler does — factor the block at `:1481-1499` into
  `codeeditor_update_drag_selection(editor, widget, x, y)` and call it from
  both the event handler and the tick (with `last_drag_x/y`), so position
  mapping, anchor selection, `has_selection`, and `needs_paint` stay in one place.
- `ensure_cursor_visible` is NOT called from the tick path (it would fight the
  proportional speed); clamping via `codeeditor_clamp_scroll` bounds everything.
- On drag end (MOUSE_UP handler `:1430-1436`), zero the accumulators.

If plan 04 (horizontal scrollbar) is not yet merged, the horizontal half still
works — `scroll_x` already exists and is clamped; there is simply no bar to
show it. No ordering constraint between the two plans.

## 4. Implementation steps

1. Factor `codeeditor_update_drag_selection` out of the MOUSE_MOVE case.
2. Add fields + init (lifecycle) + reset on MOUSE_UP.
3. Implement `codeeditor_drag_autoscroll_tick`; call it at the top of
   `vg_codeeditor_tick` before the focus gate (`vg_codeeditor_api.inc:47-49`).
4. C tests (`src/lib/gui/tests/`): create editor with 200 lines in a 10-line
   viewport; synthesize mouse-down at line 5, move to `y = height + 40`, then
   call `vg_codeeditor_tick(editor, 0.1f)` repeatedly with **no further mouse
   events**; assert `scroll_y` increases monotonically, selection end advances,
   and stops at document end. Mirror test upward and (with a long line)
   rightward/leftward. Assert nothing scrolls when not dragging.
5. Manual feel pass in the IDE (`./scripts/build_ide.sh`): drag past bottom,
   hold; speed ramps with distance; release stops instantly.
6. Full no-skip build + test run (viperide probes included).

## 5. Files to modify

- `src/lib/gui/include/vg_ide_widgets_editor.h` — fields + docs.
- `src/lib/gui/src/widgets/vg_codeeditor_input.inc` — factor + tick logic +
  move/up handlers.
- `src/lib/gui/src/widgets/vg_codeeditor_api.inc` — tick call order.
- `src/lib/gui/src/widgets/vg_codeeditor_lifecycle.inc` — init.
- `src/lib/gui/tests/` — C test file.

## 6. Testing

C tests drive the whole behavior deterministically via `tick(dt)` — no probe
needed (no new runtime surface). Existing editor probes guard against
regressions in normal click/drag selection.

## 7. Acceptance criteria

- Hold-drag past any edge scrolls continuously at distance-proportional speed
  and extends the selection; releasing stops immediately.
- No scrolling when merely hovering outside bounds without a drag.
- Blink behavior unchanged; `editor_hot_path_probe` unchanged.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` after C changes + full build.
- Full Viper header maintained on modified C files.
- 100% cross-platform; no platform code involved.
- Zero external dependencies.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
