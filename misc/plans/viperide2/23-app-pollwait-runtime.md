# Plan 23 — `Viper.GUI.App.PollWait(timeoutMs)`: blocking event wait

## 1. Objective & scope

Add a blocking wait-for-events primitive to the platform layer and expose it to
Zia as `Viper.GUI.App.PollWait(timeoutMs)`. Today the only pump is the
non-blocking `App.Poll` — a frame loop that has nothing to do can only spin.
This primitive is what lets ViperIDE idle at genuinely 0% CPU (consumer:
**plan 03**; also used by **plan 09** for unfocused throttling).

**In scope:** `vgfx_wait_events(window, timeout_ms)` implemented in all three
platform backends + mock; `rt_gui_app_poll_wait` bridge; `runtime.def` entry;
tests.

**Out of scope:** changing ViperIDE's main loop (plan 03/09), frame pacing
inside `rt_gui_app_render` (plan 03), any timer/wakeup framework.

## 2. Current state (verified anchors)

- `rt_gui_app_poll` (`src/runtime/graphics/gui/rt_gui_app.c:1405+`) drains the
  queue via non-blocking `vgfx_poll_event` (`:1435`) and returns immediately.
- The only sleep in the stack is FPS pacing inside `vgfx_update`
  (`src/lib/graphics/src/vgfx.c:1177-1198`), which the GUI render path skips on
  clean frames (`rt_gui_app.c:1738-1741` early-returns before `vgfx_update`).
- macOS already polls with `nextEventMatchingMask ... untilDate:[NSDate distantPast]`
  ("Don't wait", `vgfx_platform_macos.m:1221`) — the same call with a real date
  is the blocking variant. `vgfx_platform_sleep_ms` exists (`:1480-1487`).
- `App.IsFocused` already exists (`runtime.def:2261`,
  `rt_app_is_focused`) — no work needed there. `vgfx_is_focused` is declared at
  `src/lib/graphics/include/vgfx.h:487`.
- Platform files: `src/lib/graphics/src/vgfx_platform_macos.m`,
  `vgfx_platform_win32.c`, `vgfx_platform_linux.c`, `vgfx_platform_mock.c`;
  shared internal header `vgfx_internal.h`; public header
  `src/lib/graphics/include/vgfx.h`.

## 3. Design

### 3.1 Platform API

```c
/// vgfx.h
/// @brief Block until at least one OS event is available for this window, or
///        the timeout elapses. Does not dequeue or dispatch anything — call
///        vgfx_poll_event afterwards as usual.
/// @param timeout_ms  Max wait; 0 = return immediately; clamped to [0, 1000].
/// @return 1 if events are (probably) available, 0 on timeout.
int32_t vgfx_wait_events(vgfx_window_t window, int32_t timeout_ms);
```

Semantics: *hint*, not contract — spurious wakeups are fine; callers always run
their normal poll afterwards. Clamp to ≤1s so a bug can never hang the UI, and
so OS-level things the queue can't see (monitor changes, etc.) get re-checked.

Per-platform implementation:

- **macOS** (`vgfx_platform_macos.m`): if the app's event queue already has
  pending events, return 1 immediately. Else
  `[NSApp nextEventMatchingMask:NSEventMaskAny
     untilDate:[NSDate dateWithTimeIntervalSinceNow:timeout_ms/1000.0]
     inMode:NSDefaultRunLoopMode dequeue:NO]` — returns nil on timeout.
  `dequeue:NO` leaves the event for the normal pump. Must run on the main
  thread (all vgfx window calls already are).
- **Windows** (`vgfx_platform_win32.c`): if `PeekMessage(..., PM_NOREMOVE)`
  finds a message, return 1; else
  `MsgWaitForMultipleObjectsEx(0, NULL, (DWORD)timeout_ms, QS_ALLINPUT,
  MWMO_INPUTAVAILABLE)`; return `result != WAIT_TIMEOUT`.
- **Linux/X11** (`vgfx_platform_linux.c`): if `XPending(display) > 0`, return 1;
  else `poll()` one `struct pollfd { ConnectionNumber(display), POLLIN }` with
  the timeout; return whether it fired. (Use `poll`, not `select` — no FD_SETSIZE
  hazard.)
- **Mock** (`vgfx_platform_mock.c`): if the injected queue is non-empty return 1,
  else `vgfx_platform_sleep_ms(timeout_ms)` and return 0. Headless probes and
  tests use this backend.

Also mirror the declaration in `src/lib/graphics/tests/vgfx_mock.h` if the mock
test harness needs it.

### 3.2 Runtime bridge + registration

`rt_gui_app.c`:

```c
/// @brief Viper.GUI.App.PollWait — block until events or timeout, then poll.
int64_t rt_gui_app_poll_wait(void *app_ptr, int64_t timeout_ms);
```

Implementation: validate handle (`rt_gui_app_handle_checked`,
`RT_ASSERT_MAIN_THREAD()` like the other App entry points), clamp timeout,
call `vgfx_wait_events(app->window, timeout)`, then call the existing
`rt_gui_app_poll(app_ptr)` so the Zia call is a drop-in replacement for `Poll`.
Return whether events arrived (lets callers distinguish wake-from-input from
timeout ticks).

`runtime.def` (next to `GuiAppPoll` at `:2227`):

```c
RT_FUNC(GuiAppPollWait, rt_gui_app_poll_wait, "Viper.GUI.App.PollWait", "i1(obj,i64)")
```

plus `RT_METHOD("PollWait", "i1(i64)", GuiAppPollWait)` in the
`Viper.GUI.App` class block (near `:8960`). Stub build: no-op returning 0 with a
short sleep (mirror how other `rt_gui_*` stubs behave).

### 3.3 Caveats to document at the call site (for plans 03/09)

- Callers with animations (cursor blink ~0.5s cadence, `vg_codeeditor_api.inc:52`)
  must cap the timeout at their next animation deadline.
- Callers pumping child processes/PTYs must keep the timeout small enough that
  output latency is acceptable (ViperIDE will use 10–50ms while sessions are
  active; up to ~500ms when fully idle). That policy lives in plan 03, not here.

## 4. Implementation steps

1. Declare `vgfx_wait_events` in `vgfx.h` + `vgfx_internal.h` plumbing
   (`vgfx_platform_wait_events` if the existing pattern routes platform calls
   through an internal shim — match how `vgfx_platform_sleep_ms` is layered).
2. Implement macOS, Win32, Linux, mock variants. Keep each under ~30 lines.
3. C test in `src/lib/graphics/tests/` (mock backend): empty queue + 50ms
   timeout returns 0 after ≥45ms; pre-injected event returns 1 immediately.
4. `rt_gui_app_poll_wait` bridge + `runtime.def` + stub; run
   `./scripts/check_runtime_completeness.sh`.
5. Zia probe `viperide/src/probes/poll_wait_probe.zia`: create an App, call
   `PollWait(50)` in a loop 5×, assert it returns and total elapsed time is
   roughly ≥200ms (use `Viper.Time`/clock API already used by other probes);
   register with `LABELS "zia;viperide;shell"`.
6. Cross-platform gate: `./scripts/lint_platform_policy.sh` and
   `./scripts/run_cross_platform_smoke.sh`.
7. Full build + test run.

## 5. Files to modify

- `src/lib/graphics/include/vgfx.h`, `src/lib/graphics/src/vgfx_internal.h`.
- `src/lib/graphics/src/vgfx_platform_macos.m`, `vgfx_platform_win32.c`,
  `vgfx_platform_linux.c`, `vgfx_platform_mock.c` (+ `tests/vgfx_mock.h` if needed).
- `src/runtime/graphics/gui/rt_gui_app.c` (+ gui stub counterpart).
- `src/il/runtime/runtime.def`.
- `src/lib/graphics/tests/` — C test.
- `viperide/src/probes/poll_wait_probe.zia` — **new**; `src/tests/CMakeLists.txt`.

## 6. Testing

- Mock-backend C test (deterministic, runs everywhere).
- Zia probe for the runtime surface.
- Manual on macOS: temporary 3-line loop `while: PollWait(500); Render` —
  Activity Monitor shows ~0% CPU with no input, instant response to mouse move.
  (Windows/Linux verification happens when the user next runs those platforms;
  the implementations are small and mirror well-known OS idioms.)

## 7. Acceptance criteria

- `vgfx_wait_events` blocks (measured) on empty queues and wakes early on input
  on macOS; mock test proves both paths deterministically.
- `Viper.GUI.App.PollWait` visible in `viper --dump-runtime-api`;
  completeness check passes.
- No behavior change for any existing caller (nothing calls the new API yet).

## 8. Repo rules (read before starting)

- Build with `./scripts/build_viper_unix.sh` — never raw cmake. Fast inner loop:
  `VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_unix.sh`.
- Rebuild the IDE with `./scripts/build_ide.sh` after C changes + full build.
- Every new runtime function needs BOTH `RT_FUNC` and `RT_METHOD` entries; run
  `./scripts/check_runtime_completeness.sh` after `runtime.def` edits.
- Full Viper file header on all new/modified C files.
- 100% cross-platform — this plan touches all three platform backends; raw
  platform ifdefs stay inside `vgfx_platform_*` files only.
- Zero external dependencies. Zia code binds namespace aliases.
- Finish with a full no-skip build + test pass. Never commit. No CI changes.
