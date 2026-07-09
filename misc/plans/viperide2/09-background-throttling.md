# Plan 09 — Throttle background work when the window is unfocused

## 1. Objective & scope

When ViperIDE is not the focused window, stop running the full per-frame
intelligence stack. Today the loop pumps the language frame (completion,
diagnostics, hover, signature, symbols, tokens, inlay hints, indexing),
workspace watchers, SCM view, and file-watch polling at full frame rate even
while the user works in another app for hours. Gate the non-essential pumps to
a slow cadence (~10Hz) while unfocused, and restore instantly on focus.

Pure Zia-side plan. `App.IsFocused` already exists in the runtime.
**Soft dependency:** plan 23/03 make the *loop itself* sleep; this plan reduces
the *work per iteration* — they compose but don't require each other.

**In scope:** `main.zia` gating, a small focus-state helper, cadence policy,
what must NEVER be throttled, probe.

**Out of scope:** frame pacing (plan 03), scheduler-internal debounce tuning
(`editor/scheduler.zia` constants stay as-is).

## 2. Current state (verified anchors)

- The loop (`viperide/src/main.zia:335-655`) runs every controller every frame:
  `languageFrame.BeginFrame/PumpWorkspace/SyncBreakpoints/RouteCompletionKeys/
  PumpForegroundTools/PumpBudgetedTools/...` (`:362-376`), `scmView.HandleEvents()`
  (`:345`), `terminalCtrl.Pump()` (`:346`), `fileWatchCtrl.Pump(...)` (`:624`),
  `workspace_watcher.PumpWorkspaceWatcher(...)` (`:626`), findbar updates
  (`:422-432`), auto-save debounce (`:603-619`).
- Focus API: `Viper.GUI.App.IsFocused` (`runtime.def:2261,8960`,
  `rt_app_is_focused`; platform `vgfx_is_focused`, `vgfx.h:487`). Not used
  anywhere in `viperide/src` today (verified by grep).
- File-watch polling already has its own interval (`OPEN_DOC_EXTERNAL_POLL_MS
  = 1000`, `main.zia:108`) — it is cheap but still frame-called.
- Time source for cadence: the scheduler already works on tick counts
  (`editor/scheduler.zia` `dueMs` model); `Viper.Threads.Debouncer` exists
  (used at `main.zia:330`).

## 3. Design

### 3.1 Focus/cadence helper

New tiny module `viperide/src/app/focus_throttle.zia`:

```zia
class FocusThrottle {
    expose Boolean focused;          // current state
    expose Boolean becameFocused;    // edge, true for one frame
    expose Boolean becameUnfocused;  // edge
    hide Integer slowTickCounter;

    expose func Update(app: GUI.App) { ... }        // poll IsFocused, compute edges
    expose func AllowBackgroundWork() -> Boolean {  // true every frame when focused;
        ...                                          // true every Nth frame when not
    }
}
```

`AllowBackgroundWork` when unfocused: true once per ~100ms. Implement by
counting frames is wrong once plan 03 lands (frame rate varies) — use a
millisecond deadline via the same clock the scheduler uses (reuse whatever
`scheduler.zia` uses for `nowMs`; grep its time source and share it).

### 3.2 What gets gated (unfocused → slow cadence)

- `languageFrame.PumpWorkspace` (workspace indexing pump, `main.zia:366`)
- `languageFrame.PumpBudgetedTools` (`:373`)
- `workspace_watcher.PumpWorkspaceWatcher` (`:626`)
- `fileWatchCtrl.Pump` (`:624`) — effective poll becomes max(1000ms, cadence),
  unchanged in practice
- `scmView.HandleEvents` **UI refresh portion** — but its async job pump must
  keep running (a `git push` started before unfocusing must complete): gate
  only if `scmView` separates job-pump from event-poll; if not separable
  cheaply, do NOT gate SCM (its idle cost is trivial when no job is active).

### 3.3 What must never be gated

- `shell.Poll()` / `shell.Render()` — input/close handling and paint.
- `terminalCtrl.Pump()` — PTY drain (hidden-session draining is exactly the
  bug class this repo already fixed once; see `terminal_controller.zia:251-264`).
- `buildSys` / `dbgSession` pumps inside `PumpForegroundTools` — running builds
  and debug sessions must stream at full rate. `PumpForegroundTools`
  (`main.zia:371`) mixes editor-foreground tools with build/debug pumping —
  read `language_tool_frame.zia:PumpForegroundTools` and, if needed, split it
  into `PumpProcessStreams` (never gated) and `PumpEditorTools` (gated), keeping
  the public behavior identical when focused.
- Auto-save debounce (`main.zia:603-619`) — saving must not be deferred.
- On `becameFocused`: force one immediate full pump of everything gated (so
  external file changes are picked up the moment the user returns, not 100ms
  later), and reset any scheduler staleness.

### 3.4 Edge behavior

On `becameUnfocused`: nothing special — in-flight semantic jobs complete
normally (the scheduler's generation tokens already handle stale results,
`editor/scheduler.zia:34-43`).

## 4. Implementation steps

1. Create `focus_throttle.zia`; instantiate in `main.zia` next to the other
   controllers; `Update` right after `shell.Poll()`.
2. Read `language_tool_frame.zia`; split `PumpForegroundTools` if it mixes
   process streaming with editor tools (§3.3).
3. Apply the gates (§3.2) — each gate is
   `if focusThrottle.AllowBackgroundWork() { ... }` around the existing call.
4. Focus-return burst (§3.3 last bullet).
5. Probe `viperide/src/probes/focus_throttle_probe.zia`: unit-style — drive
   `FocusThrottle` with a fake focused/unfocused sequence and assert cadence
   and edge flags (pure logic, no window focus needed headlessly). Register
   with `LABELS "zia;viperide;perf"`.
6. Manual: `./scripts/build_ide.sh`; with a debug build running + IDE
   unfocused, confirm build output keeps streaming and terminal stays live;
   focus another window, edit a watched file externally, refocus → change
   detected immediately.
7. Full no-skip build + test run.

## 5. Files to modify

- `viperide/src/app/focus_throttle.zia` — **new**.
- `viperide/src/main.zia` — gates + instantiation.
- `viperide/src/app/language_tool_frame.zia` — possible pump split.
- `viperide/src/probes/focus_throttle_probe.zia` — **new**;
  `src/tests/CMakeLists.txt`.
- `viperide/docs/architecture.md` — one line in Main Loop Responsibilities.

## 6. Testing

Probe for the throttle logic; existing probes confirm no focused-path change
(all probes run focused/headless — gating must default to "focused" when
`IsFocused` reports false in headless environments? **Check**: if the mock/
headless backend reports unfocused, probes would exercise gated paths — make
`FocusThrottle` treat "never yet seen focused" as focused to keep headless
probes at full cadence, and note this in the module header).

## 7. Acceptance criteria

- Unfocused idle IDE performs no indexing/watcher/budgeted-tool work except at
  ~10Hz (verify via perf_monitor counters or temporary logging).
- Builds, debug sessions, terminal output, and auto-save behave identically
  regardless of focus.
- Refocus picks up external changes within one frame.
- All existing probes pass (headless-focus rule respected).

## 8. Repo rules (read before starting)

- Zia-only change: rebuild with `./scripts/build_ide.sh`.
- Zia code binds namespace aliases (`bind GUI = Viper.GUI;` etc.).
- New module carries the standard module header per
  `viperide/docs/architecture.md`.
- Finish with a full no-skip `./scripts/build_viper_unix.sh` + test pass
  (probes are registered through the main build). Never commit. No CI changes.
