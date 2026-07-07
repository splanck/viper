# Plan 07 — Damage-region (partial) rendering

## 1. Objective & scope

Stop repainting the entire window when one widget changes. Today any dirty
widget triggers a full framebuffer clear plus a full widget-tree software
repaint — so the focused editor's cursor blink repaints the whole IDE twice a
second, and every keystroke repaints menus, tree, tabs, panels, and status bar.
Implement dirty-rectangle tracking and clip-bounded partial repaints over the
retained software framebuffer. This is the single largest perceived-performance
lever for the IDE on large/HiDPI windows.

**In scope:** vg widget core (dirty-rect collection), `rt_gui_app_render`
partial paint path, vg_draw clipping enforcement, overlay interaction rules,
opt-out safety valve, perf counters. Phased so each phase ships alone.

**Out of scope:** GPU compositing, per-widget backing stores/layers, plan 03's
frame pacing (independent), reducing *layout* cost (layout already short-circuits:
`rt_gui_widget_tree_needs_layout`, `rt_gui_app.c:343-349`).

## 2. Current state (verified anchors)

- Render path (`src/runtime/graphics/gui/rt_gui_app.c:1630-1822`):
  1. layout if needed (`:1672-1681`), tick tree (`:1680`),
  2. dirty check: `root_needs_paint = rt_gui_widget_tree_needs_paint(app->root)`
     + overlays (`:1736-1737`); clean frames early-return (`:1738-1741`),
  3. else **full clear** `vgfx_cls(app->window, theme_bg)` (`:1745`),
  4. full tree walk `render_widget_tree(...)` (`:1753`) + overlay tree
     (`:1759`) + palettes/dialogs/context menu/notifications/tooltips
     (`:1762-1817`),
  5. `rt_gui_app_clear_paint_flags(app)` then `vgfx_update` present (`:1820-1821`).
- Paint traversal uses an explicit stack accumulating absolute coords
  (`render_widget_tree`, `:1902-1960`; coords restored after paint — hit
  testing depends on relative coords, `:1747-1751`).
- Dirty tracking today is boolean: `widget->needs_paint`; the tree check
  short-circuits on the first dirty widget (`rt_gui_app.c:370-397`).
- The framebuffer is retained between frames (software framebuffer blitted by
  `vgfx_platform_present`; `vgfx.h:13-14` "operations modify a software
  framebuffer that gets blitted"; it is only reallocated/cleared on resize,
  `vgfx.h:143`).
- Clipping: vg_draw has clip support (`vg_draw.h` / `vg_draw.c` — verify the
  exact API: grep `clip` in `src/lib/gui/src/core/vg_draw.c`; widgets like
  ScrollView already paint children clipped).
- Cursor blink marks the editor dirty 2×/s (`vg_codeeditor_api.inc:52-55`).
- Perf counters precedent: `vg_codeeditor_perf_stats_t`
  (`vg_ide_widgets_editor.h:99-110`) with a probe asserting on them
  (`viperide/src/probes/editor_hot_path_probe.zia`).

## 3. Design

### 3.1 Damage collection

Extend the widget core (`vg_widget.h` / `vg_widget.c`):

```c
// vg_widget_t additions
// (screen-space damage is computed during collection, not stored per widget)
```

New pass in `rt_gui_app.c`, replacing the boolean check:

```c
typedef struct { float x, y, w, h; } rt_rect_t;
#define RT_GUI_MAX_DAMAGE_RECTS 8
// Walk the tree accumulating absolute coords (same stack pattern as
// render_widget_tree). For each widget with needs_paint, add its absolute
// bounds (clamped to window) to the damage list, merging overlapping/nearby
// rects; if count exceeds MAX or coverage > ~60% of the window, collapse to
// full-window damage.
```

Rules that make this correct with a retained framebuffer:

- A widget's damage rect must cover its **previous** bounds too. Store
  `last_painted_x/y/w/h` (absolute) on each widget at paint time; damage =
  union(current bounds, last painted bounds). This handles moved/hidden
  widgets. `SetVisible(false)` must set needs_paint on the *parent* (or record
  the old rect as damage) — audit `vg_widget` visibility setter.
- Layout runs (`did_layout`) or window resize → full damage (unchanged from
  today's behavior; resize already clears the framebuffer, `vgfx.h:143`).
- Overlays (palettes, dialogs, context menu, notifications, tooltips) are
  painted above the tree without their own damage tracking. Phase 1 rule: any
  visible overlay OR any overlay visibility change this frame → full damage.
  (Overlay-heavy frames are rare; this keeps correctness trivial. Phase 3 can
  refine with overlay bounds-as-damage.)

### 3.2 Partial repaint

For each damage rect:

1. `vgfx_cls_rect(window, rect, theme_bg)` — new vgfx helper (fill rect in the
   framebuffer; trivial memset-per-row loop next to `vgfx_cls`).
2. Set a global paint clip to the rect (vg_draw clip API), then walk the tree
   painting **only widgets whose absolute bounds intersect the rect**
   (containers whose bounds don't intersect are pruned with their whole
   subtree — cheap because bounds are already computed during the damage walk;
   reuse one combined walk that both collects damage and records
   absolute bounds per widget for the paint pass).
3. Pop the clip. After all rects: clear paint flags, present.

Present: `vgfx_update` blits the whole framebuffer (platform present is
whole-surface). That is fine — the win is skipping the software *rasterization*,
which is where the milliseconds go, not the blit. (A partial-present
optimization on macOS/Win32/X11 is possible later; explicitly out of scope.)

**Hard requirement:** every widget paint function must respect the clip — they
draw through vg_draw, which enforces the clip centrally. Verify vg_draw clips
ALL primitives (text runs included, including the font raster path in
`src/lib/gui/src/font/`); any primitive that writes pixels directly without
clip checks must be fixed in Phase 0.

### 3.3 Safety valve + counters

- Env/theme-independent kill switch: `rt_gui_set_partial_paint(bool)` +
  `VIPER_GUI_FULL_REPAINT=1` env check at app create — one-line revert for any
  rendering artifact report.
- Counters on the app: `frames_full`, `frames_partial`, `damage_rects_painted`,
  `widgets_painted` — exposed via a debug getter and logged by ViperIDE's
  `perf_monitor.zia` (it already reads editor perf stats each frame,
  `main.zia:654`). Runtime exposure can be a single
  `Viper.GUI.App.GetPaintStats` returning a formatted string (avoids a new
  class; RT_FUNC + RT_METHOD + completeness check) — optional, Phase 3.

### 3.4 Phases (each independently shippable)

- **Phase 0 — clip audit:** verify/fix vg_draw clip enforcement across all
  primitives; add `vgfx_cls_rect`. No behavior change.
- **Phase 1 — single-rect damage:** collect damage, collapse to ONE bounding
  rect (union of all dirty widgets); partial repaint of that rect; overlay →
  full damage rule; kill switch; counters. This alone fixes the blink case
  (damage = caret line area) and typing (damage = editor bounds at worst).
- **Phase 2 — editor-precision damage:** the CodeEditor reports a tighter
  damage rect than its full bounds when only the caret/current line changed:
  add optional vtable hook `get_damage_rect(widget, out_rect)` — blink and
  single-line edits damage one line strip. This is the biggest UX win (typing
  repaints ~one text line).
- **Phase 3 — multi-rect + overlay rects + stats runtime exposure.**

## 4. Implementation steps

1. Phase 0: grep/audit clip usage in `vg_draw.c` + font raster; fix gaps;
   add `vgfx_cls_rect` (+ mock); C test: draw with clip, assert pixels outside
   clip untouched (framebuffer is inspectable in tests via
   `vgfx_framebuffer_t`, `vgfx.h:74-119`).
2. Phase 1: bounds-recording walk; damage union; partial path in
   `rt_gui_app_render` behind the kill switch (default ON after local
   validation); `last_painted_*` fields + visibility-setter audit; counters.
3. Validate visually in ViperIDE: type, scroll, switch tabs/panels, open
   palette/dialogs/menus/context menus, resize, toggle sidebar, drag splitter,
   tooltips, toasts, find bar, completion popup — screenshot-level inspection
   for stale-pixel artifacts in each.
4. C tests for damage math (union/merge/collapse thresholds) as pure functions.
5. Phase 2: `get_damage_rect` hook + editor implementation (caret line rect on
   blink; edited-line span on single-line edits — the editor knows via its
   revision/paint bookkeeping; be conservative: anything multi-line → full
   editor bounds).
6. Probe: `viperide/src/probes/paint_damage_probe.zia` — using paint counters:
   render, edit one char, render, assert `widgets_painted` for the second
   frame ≪ total widget count (requires the Phase 3 stats getter — or keep the
   probe assertion at "no trap + revision advanced" if stats exposure is
   deferred; state which in the summary). `LABELS "zia;viperide;perf"`.
7. Full no-skip build + test run; run the GUI example apps under
   `src/lib/gui/examples/` (if runnable) as extra visual coverage.

## 5. Files to modify

- `src/lib/gui/src/core/vg_draw.c` (+ header) — clip audit/fixes.
- `src/lib/graphics/src/vgfx.c`/`vgfx_draw.c` + `include/vgfx.h` — `vgfx_cls_rect`.
- `src/lib/gui/include/vg_widget.h`, `src/lib/gui/src/core/vg_widget.c` —
  `last_painted_*`, visibility damage, optional `get_damage_rect` vtable slot.
- `src/runtime/graphics/gui/rt_gui_app.c` — damage collection + partial paint
  + kill switch + counters.
- `src/lib/gui/src/widgets/vg_codeeditor_paint.inc` / `_api.inc` — Phase 2 hook.
- Tests under `src/lib/gui/tests/` and/or `src/lib/graphics/tests/`.
- `viperide/src/editor/perf_monitor.zia` — surface counters (optional).
- `viperide/src/probes/paint_damage_probe.zia` — **new**; `src/tests/CMakeLists.txt`.

## 6. Testing

- Phase-0 clip C test is the foundation — without it, partial paint corrupts.
- Damage-math unit tests (pure functions).
- The manual artifact sweep (step 3) is mandatory before flipping the default;
  repeat with `VIPER_GUI_FULL_REPAINT=1` to confirm the valve works.
- All existing gui/viperide/canvas tests must pass with partial paint ON.

## 7. Acceptance criteria

- Idle-focused IDE: blink frames rasterize only the caret-line strip (Phase 2)
  or editor bounds (Phase 1) — verified via counters.
- Typing latency visibly improved on a maximized window; no stale-pixel
  artifacts in any surface from the step-3 sweep.
- Kill switch restores today's behavior exactly.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` after C changes + full build.
- If the optional stats getter is added: RT_FUNC + RT_METHOD +
  `./scripts/check_runtime_completeness.sh`.
- Full Viper header on modified C files.
- 100% cross-platform; `vgfx_cls_rect` is platform-neutral framebuffer code.
- Zero external dependencies.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
