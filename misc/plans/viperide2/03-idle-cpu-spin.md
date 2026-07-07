# Plan 03 — Stop the idle 100%-CPU spin

## 1. Objective & scope

ViperIDE currently burns a full CPU core doing nothing: when no widget is dirty,
the main loop's Poll→pump→Render cycle runs unthrottled thousands of times per
second because the only sleep in the stack is skipped on clean frames. Fix the
pacing so an idle IDE consumes ~0% CPU while staying instantly responsive.

**Soft dependency:** Plan 23 (`App.PollWait`) gives the ideal zero-CPU wait.
This plan is written to land a self-contained fix first (Phase A) and adopt
PollWait when available (Phase B). Phase A alone already reduces idle CPU from
~100% of a core to ~1-2%.

**In scope:** clean-frame pacing in `rt_gui_app_render`; ViperIDE main-loop
adoption; cursor-blink cadence interaction; perf-monitor visibility.

**Out of scope:** unfocused-window throttling of semantic work (plan 09),
damage regions (plan 07).

## 2. Current state (verified anchors)

- FPS pacing exists ONLY inside `vgfx_update`
  (`src/lib/graphics/src/vgfx.c:1160-1205`): present → sleep-to-deadline when
  `window->fps > 0` (`:1177-1198`).
- The GUI render path skips it on clean frames: `rt_gui_app_render` early-returns
  after `vgfx_pump_events` when `!did_layout && !size_changed &&
  !root_needs_paint && !overlays_need_paint`
  (`src/runtime/graphics/gui/rt_gui_app.c:1736-1741`) — **before** `vgfx_update`,
  so no sleep ever happens on the (overwhelmingly common) clean frame.
- `rt_gui_app_poll` (`rt_gui_app.c:1405+`) is non-blocking (`vgfx_poll_event`
  loop at `:1435`).
- ViperIDE's loop (`viperide/src/main.zia:335-655`) has no sleep of any kind:
  `shell.Poll()` → ~30 controller pumps → `shell.Render()`.
- Blink cadence: focused editor marks itself dirty every `CURSOR_BLINK_RATE`
  (~0.5s, `vg_codeeditor_api.inc:47-57`), so at most ~2 dirty frames/sec exist
  when idle-but-focused.
- `vgfx_platform_sleep_ms` exists and is precise (`vgfx_platform_macos.m:1473-1487`
  uses nanosleep; equivalents exist per platform — it is the same helper
  `vgfx_update` uses, so it is already cross-platform).
- `window->fps` default comes from `vgfx_read_default_fps()`
  (`vgfx.c:1050`); `vgfx_set_fps` clamps 1..1000 (`:796-805`).
- Frame timing telemetry: `perf_monitor.zia` wraps the loop
  (`main.zia:336,651-654`), useful for before/after measurement.

## 3. Design

### Phase A — pace the clean frame (self-contained, C-side)

In `rt_gui_app_render`, replace the bare early return:

```c
if (!did_layout && !size_changed && !root_needs_paint && !overlays_need_paint) {
    vgfx_pump_events(app->window);
    rt_gui_frame_pace(app);      // NEW
    return;
}
```

`rt_gui_frame_pace(app)`: reuse the exact deadline logic of `vgfx_update`
(`vgfx.c:1177-1198`) without the present — factor that block into a shared
helper `vgfx_frame_pace(window)` exported from vgfx (so the deadline field
`next_frame_deadline_ms` stays consistent between painted and clean frames),
and call it from both `vgfx_update` and the new clean path. If `fps <= 0`
(unlimited), clean frames still sleep a minimum of 1ms to break the spin —
unlimited FPS is a game-loop setting, not a GUI setting; document that GUI apps
get the 1ms floor only on *clean* frames so benchmarks that paint every frame
are unaffected.

Result: idle IDE runs at the window's FPS cap (default from
`vgfx_read_default_fps()`) doing only event-pump + controller pumps — measured
CPU drops to low single digits.

### Phase B — adopt PollWait in ViperIDE (needs plan 23)

Replace `shell.Poll()` in `main.zia` with an adaptive wait:

```zia
var waitMs = 0;
if ide_is_quiescent { waitMs = idleWait; }   // see below
shell.PollWait(waitMs);                       // AppShell wraps App.PollWait
```

Quiescence predicate (all false → waitMs stays 0):
- build job active (`buildSys` running), debug session active, terminal session
  active (PTY needs draining), SCM job active, any scheduler job queued
  (`editorScheduler` exposes queued flags), indexer warming, search running.

Wait tiers: 8ms while any of the above is active (imperceptible; keeps PTY
latency low), else 50ms when focused (blink stays smooth at 500ms cadence),
else 250ms when unfocused (pairs with plan 09).

`AppShell.PollWait(ms)` added next to `Poll` (`viperide/src/ui/app_shell.zia:102-104`).

### Interaction notes

- Cursor blink: with 50ms waits the tick advances on wake; blink cadence is
  driven by accumulated `dt` in render (`rt_gui_app.c:1661-1668`), so a 500ms
  blink stays regular. With 250ms unfocused waits the editor isn't focused, so
  no blink dirt at all — clean.
- Toasts/notifications/tooltips animate via `now_ms` in render; they only exist
  transiently and any input wakes the loop. Acceptable: a toast's fade may step
  at wait granularity when totally idle. If visually bothersome, add
  "overlays animating" to the quiescence predicate (the notification manager
  exposes active count — check `rt_gui_app.c:1716-1722` usage).

## 4. Implementation steps

1. Factor `vgfx_frame_pace(window)` out of `vgfx_update` (`vgfx.c:1177-1198`);
   keep `vgfx_update` behavior byte-identical (call the helper).
2. Export it (`vgfx.h` declaration + header docs), call from the clean-frame
   path in `rt_gui_app_render` with the 1ms floor rule.
3. Measure: run any GUI example or ViperIDE, confirm idle CPU near 0 in
   Activity Monitor and that dragging/typing still feels immediate.
4. (Phase B, if plan 23 is merged) `AppShell.PollWait`; quiescence predicate as
   a small function in `main.zia` reading existing controller state; tiered waits.
5. Probe: extend `viperide/src/probes/editor_hot_path_probe.zia` (or a new
   `frame_pace_probe.zia`) — render 100 clean frames, assert wall-clock time
   ≥ N ms (proves pacing engaged; use a generous lower bound to avoid flakes,
   e.g. 100 clean frames at 60fps cap ≥ 1000ms — assert ≥ 500ms).
   Register with `LABELS "zia;viperide;perf"`.
6. Full no-skip build + test run. Watch for probes that assume free-running
   frames (any probe timing out because frames now sleep) — raise those probes'
   frame budgets, not the pacing.

## 5. Files to modify

- `src/lib/graphics/src/vgfx.c` + `include/vgfx.h` — pace helper.
- `src/runtime/graphics/gui/rt_gui_app.c` — clean-frame call.
- `viperide/src/ui/app_shell.zia`, `viperide/src/main.zia` — Phase B.
- `viperide/src/probes/frame_pace_probe.zia` — **new**; `src/tests/CMakeLists.txt`.

## 6. Testing

- Probe (step 5) locks in pacing.
- Manual before/after CPU measurement (step 3) — record numbers in the summary.
- Full suite: GUI-driving tests (canvas/game examples under `ctest -L` graphics
  labels) must not regress — pacing only touches the *clean* GUI frame path,
  and `vgfx_update`-driven game loops are untouched by construction.

## 7. Acceptance criteria

- Idle ViperIDE (focused, blinking cursor): < 5% of one core (Phase A),
  ~0% with Phase B.
- Typing/scrolling latency unchanged (input events wake immediately).
- No test regressions; game/canvas loop behavior identical.

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` after C changes + full build.
- Full Viper header on modified C files.
- 100% cross-platform; the pace helper uses the existing per-platform sleep.
- Zero external dependencies. Zia code binds namespace aliases.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
