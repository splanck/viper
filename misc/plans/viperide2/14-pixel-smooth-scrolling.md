# Plan 14 — Pixel-smooth scrolling

## 1. Objective & scope

Make wheel/trackpad scrolling feel native. The CodeEditor quantizes every wheel
event to whole lines (`delta_y × line_height × WHEEL_LINES`), which reads as
coarse stepping on trackpads that deliver many small fractional deltas.
`scroll_y` is already a float — remove the artificial quantization, accumulate
fractional deltas, and render at pixel offsets (which the paint code already
supports). Apply the same treatment to ListBox and OutputPane.

**In scope:** CodeEditor wheel path, ListBox wheel path, OutputPane wheel path,
a configurable wheel-speed multiplier setting in the IDE, verification that
pixel-offset painting is artifact-free in each widget.

**Out of scope:** inertial/momentum *simulation* (trackpads deliver their own
momentum event streams — passing them through is the point), animated
scroll-to (GoToLine jumps stay instant), minimap drag behavior.

## 2. Current state (verified anchors)

- CodeEditor wheel: `editor->scroll_y -= event->wheel.delta_y *
  editor->line_height * CODEEDITOR_MOUSE_WHEEL_LINES` then clamp
  (`src/lib/gui/src/widgets/vg_codeeditor_input.inc:1951-1956`). The multiply
  by `WHEEL_LINES` per event is the coarseness: platform trackpad deltas are
  already fine-grained and the constant amplifies them into line-sized jumps.
- Platform deltas arrive as floats both-axis (`rt_gui_app.c:1453-1456`;
  `vgfx` scroll event carries `delta_x/delta_y`).
- Paint renders at float offsets: text/cursor/selection all compute
  `... - editor->scroll_x` / scroll_y-based `y` as floats
  (`vg_codeeditor_paint.inc:226,404-529`) — first visible line derives from
  `scroll_y / line_height` with sub-line remainder already handled (verify the
  visible-range computation truncates and offsets rather than snapping — read
  the paint entry; if it snaps `scroll_y` to line multiples anywhere, that snap
  is part of what this plan removes).
- ListBox: `item_height`-based layout with `max_scroll` float math
  (`vg_listbox.c:229-243`); wheel handler — locate (grep `MOUSE_WHEEL` in
  `vg_listbox.c`) and check quantization.
- OutputPane: same check (grep `MOUSE_WHEEL` in `vg_outputpane.c`); note its
  terminal mode has auto-scroll state (`SetAutoScroll`, used by terminal +
  output panels, `tool_panel_shell.zia:178,254`) — user wheel-up must disengage
  auto-scroll and wheel-to-bottom re-engage (verify existing behavior, keep it).
- IDE settings plumbing for the multiplier: `core/settings.zia` +
  `ui/ide_overlays.zia` editing section + `settings_applier.zia`
  (same wiring as plan 06 §3.2 describes).

## 3. Design

### 3.1 Wheel semantics (all three widgets)

```c
// Per event:
scroll_y -= event->wheel.delta_y * unit * wheel_multiplier;
```

- `unit`: keep `line_height` (CodeEditor), `item_height` (ListBox), line height
  (OutputPane) so a "notch" on a clicky mouse wheel still moves a sane amount —
  but drop the extra `CODEEDITOR_MOUSE_WHEEL_LINES` factor for high-resolution
  deltas. Discrete mice deliver ±1.0 per notch; trackpads deliver streams of
  small fractions. To keep mouse-notch travel at ~3 lines while letting
  trackpads be 1:1, apply the widget-level rule:
  `step = (fabsf(delta_y) >= 1.0f) ? WHEEL_LINES : 1.0f` — coarse devices keep
  their 3-line notch, fine devices get pixel-true tracking. Comment this
  clearly; it is the crux of the plan.
- No snapping of `scroll_y` to line multiples anywhere (audit `clamp_scroll`
  and scroll_to_line-style helpers — `vg_codeeditor_scroll_to_line` may snap
  intentionally for programmatic jumps; that is fine, only *wheel* paths must
  not snap).
- `wheel_multiplier`: per-widget float, default 1.0.

### 3.2 Runtime + IDE setting

One global knob is enough:

```c
RT_FUNC(GuiAppSetWheelMultiplier, rt_app_set_wheel_multiplier, "Viper.GUI.App.SetWheelMultiplier", "void(obj,f64)")
```

Stored on the app; widgets read it at event time via the active-app accessor
(`rt_gui_activate_app`/theme-refresh pattern shows how widgets reach app scope —
simplest: store it in the shared theme/metrics scale location that
`rt_gui_refresh_theme` already propagates, see the UI-zoom precedent in
`rt_gui_app.c` SetUiScale). IDE: setting key `wheelSpeed` (float 0.25–4.0,
default 1.0), Editing section slider/input in `ide_overlays.zia`, applied at
startup + on settings apply.

### 3.3 Artifact checks (why this needs eyes, not just tests)

Sub-line offsets mean partially visible first/last lines everywhere:

- CodeEditor: gutter line numbers must clip at top/bottom, current-line
  highlight must track the fractional offset, minimap viewport indicator uses
  line indices (unchanged — it can stay line-quantized).
- ListBox: row hit-testing must use the float offset
  (`y + scroll` → index math — audit the click handler's row calculation).
- OutputPane terminal mode: cell-grid rendering may legitimately require
  row-quantized scroll — if partial rows corrupt the terminal redraw, keep the
  terminal-mode pane row-quantized and apply smooth scroll only in normal
  output mode. Decide by testing; document the outcome.

## 4. Implementation steps

1. CodeEditor wheel change (fine/coarse rule + multiplier), snap audit.
2. ListBox + OutputPane wheel changes + hit-test audits.
3. Wheel-multiplier plumbing (app field + RT_FUNC/RT_METHOD +
   `./scripts/check_runtime_completeness.sh`).
4. IDE setting + overlay row + applier.
5. C tests: synthesize wheel streams — 30 events of delta 0.1 → scroll_y moved
   exactly 3×unit×multiplier (no quantization loss); one event of delta 1.0 →
   WHEEL_LINES×unit; clamping at both ends; ListBox row-click correctness at a
   half-row offset.
6. Manual (mandatory): trackpad flick in editor/problems/terminal — smoothness,
   no seams in gutter/current-line/selection at fractional offsets; discrete
   mouse wheel still steps ~3 lines; terminal-mode decision from §3.3 verified.
7. Full no-skip build + test run (`editor_hot_path_probe` + render probes green).

## 5. Files to modify

- `src/lib/gui/src/widgets/vg_codeeditor_input.inc` — wheel rule.
- `src/lib/gui/src/widgets/vg_listbox.c`, `vg_outputpane.c` — wheel + hit-test.
- `src/runtime/graphics/gui/rt_gui_app.c` (+ header/stub) — multiplier.
- `src/il/runtime/runtime.def` — one RT_FUNC + RT_METHOD.
- `viperide/src/core/settings.zia`, `ui/ide_overlays.zia`,
  `app/settings_applier.zia` — setting.
- `src/lib/gui/tests/` — wheel-stream tests.

## 6. Testing

C wheel-stream tests (step 5) lock the math; the manual pass (step 6) is the
real gate for visual artifacts. Existing render probes
(`syntax_render_probe`, `terminal_render_probe`) guard paint regressions.

## 7. Acceptance criteria

- Trackpad scrolling tracks the finger 1:1 with no line-snapping; mouse-wheel
  notches still travel ~3 lines; `wheelSpeed` setting scales both.
- No rendering artifacts at fractional offsets in editor, list panels, output.
- Terminal-mode behavior explicitly decided and documented in the code.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` after C changes + full build.
- New runtime function: RT_FUNC + RT_METHOD + completeness check.
- Full Viper header on modified C files.
- 100% cross-platform; no platform code involved.
- Zero external dependencies. Zia code binds namespace aliases.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
