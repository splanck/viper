# Plan 05 — Motion and Present Pacing

Date: 2026-07-17 · Track: R (C rendering) · Loop: C · Size: M-L

## 1. Objective

The "feels premium" frame: animations advance on real delta time, scrolling is
smooth and inertial, presentation aligns with the display, and the minimap is
good enough to default on.

## 2. Scope

1. **Real-dt animation.** `vg_widget_anim_tick` hardcodes a 16 ms step
   (`src/lib/gui/src/core/vg_widget.c:553`) even though the frame driver
   already computes clamped real dt (`rt_gui_app.c:2398`). Thread dt through
   the tick path; update any test assuming the fixed step. Keep the
   headless-safe behavior (never query frame time from a non-window canvas —
   documented prior segfault gotcha).
2. **Smooth scrolling.** Q16.16 fixed-point velocity/friction integrator in
   `vg_scrollview.c` and the CodeEditor scroll path: wheel input sets a
   target/velocity; per-frame integration eases toward it. Honors
   `theme->motion.enabled` (reduced motion = instant). Keyboard/programmatic
   scrolls jump (or short-ease) to preserve editor snappiness.
3. **Present pacing** in `src/lib/graphics/src/vgfx.c` + platform files, OS
   APIs only: macOS `CVDisplayLink` (CoreVideo); Windows `DwmFlush` /
   `DwmGetCompositionTimingInfo` (dwmapi); Linux X11 Present-extension query
   with graceful fallback to the existing drift-corrected sleep. Pace only
   while dirty/animating — never spin when idle (the adaptive PollWait in the
   IDE main loop must keep its idle-CPU profile).
4. **Minimap default-on.** Incremental per-line color-block cache in
   `vg_minimap.c` integrated with the damage-region system; then flip the IDE
   setting default (`core/settings.zia`) and welcome-page mention.

## 2a. As-built record (2026-07-18)

- Real-dt animation was already flowing: `rt_gui_tick_widget_tree`
  (`rt_gui_app.c`) advances `vg_widget_anim_advance(node, dt*1000)` with the
  frame's clamped dt; the hardcoded-16ms `vg_widget_anim_tick` is only a
  legacy compat wrapper with no remaining callers.
- Smooth scrolling: shared easing in `vg_widget.c`
  (`vg_smooth_scroll_step`, ~63%/62ms exponential approach, half-pixel
  snap; `vg_set_smooth_scroll_enabled` global honoring
  `theme->motion.enabled` via `vg_smooth_scroll_effective`). Wheel input on
  `vg_scrollview` and `vg_codeeditor` accumulates into clamped targets;
  per-frame `vg_scrollview_tick` / `vg_codeeditor_smooth_tick` run from the
  app loop's per-type dispatch. Direct scrolls (drags, click-jump,
  SetScroll, caret reveals) cancel easing.
- Runtime surface (ADR 0137): `Zanna.GUI.App.SetSmoothScroll` /
  `GetSmoothScroll` (default on; stubs included). GUI ABI manifest pins
  reviewed and updated.
- Present pacing: Windows present now calls `DwmFlush` after the blit
  (dynamically resolved dwmapi; absent/legacy sessions keep sleep pacing).
  macOS is compositor-synced by design (`setNeedsDisplay` path); Linux
  keeps the drift-corrected sleep as the documented first-class fallback.
- Minimap: the line-summary raster cache already renders incrementally
  (bounded, O(1) lookups), so the default flipped ON
  (`settings.zia` + `phase0_phase1_probe` pin updated); the
  `editor_hot_path` perf probe stays green.
- Tests: new `test_vg_smooth_scroll` (convergence, snap, symmetry,
  large-delta clamp, identical trajectories, toggle); wheel tests in
  `test_vg_codeeditor_behavior` / `test_vg_widgets_new` settle easing
  before asserting.

## 3. Runtime surface

At most `Zanna.GUI.App.SetSmoothScroll(enabled: Boolean)` (+ getter) — folded
into the Plan 04 consolidated rendering ADR with the full runtime checklist.

## 4. Tests / verification (exit gate)

- Integrator determinism C test: identical input sequence → identical
  fixed-point positions (mock backend).
- Anim-dt unit tests (varying dt steps ease consistently).
- `zia_zannastudio_editor_hot_path` perf probe must not regress.
- Minimap correctness leaning on the `paint_damage_probe` pattern (no stale
  pixels after edits).
- Incremental build + targeted ctest; manual smoothness pass on host
  platform; idle CPU checked (Activity Monitor / top) before/after.

## 5. Risks

- Linux vsync is best-effort (X11 Present availability varies) — fallback is
  first-class, not an afterthought.
- Pacing interacting with the sleep-based loop — the dirty/animating gate is
  the design center; measure idle wakeups.
- Smooth scroll must not break scroll-position-dependent probes — audit
  probes that assert scroll offsets and use the reduced-motion path in tests.
