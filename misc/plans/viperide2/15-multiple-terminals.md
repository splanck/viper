# Plan 15 — Multiple integrated terminals

## 1. Objective & scope

Support several concurrent terminal sessions with a selector, instead of the
single hardwired session. Users get: a "New Terminal" action (spawning at the
current workspace root — which also cleanly resolves today's "restart terminal
to use <dir>" workaround), a session selector (dropdown or small tab strip),
per-session Stop/Restart, close-session, and background draining of every
non-visible session.

**In scope:** `TerminalController` multi-session refactor, one `OutputPane` per
session (created lazily, shown one-at-a-time), selector UI in the terminal
panel, commands + palette entries, shutdown cleanup, probes.

**Out of scope:** terminal renames, split terminals, full TUI/alt-screen
emulation (existing OutputPane terminal-mode limits stand, `docs/status.md:277-289`),
persistence of sessions across IDE restarts.

## 2. Current state (verified anchors)

- `TerminalController` owns exactly one `TerminalSession` + one pane
  (`viperide/src/terminal/terminal_controller.zia:33-62`): fields `sess`,
  `pane`, `killBtn`, `restartBtn`, one set of `lastCols/lastRows`,
  one hidden-output replay buffer (`hiddenOutput`, cap
  `MAX_HIDDEN_TERMINAL_BUFFER = 200000`, `:12,218-248`).
- Session wrapper: `terminal/terminal_session.zia` wraps
  `Viper.System.Pty.PtySession` — instances are independent (Start/Restart/
  Update/Send/Resize/Stop per instance; `sess.Update()` drains, `:281`).
- Lifecycle behaviors to preserve per session: lazy start when visible
  (`EnsureStarted`, `:193-198`), manual-stop latch (`manualStop`, `:41,297-301`),
  exit notice append (`:285-287`), focus-on-first-show (`didFocus`, `:275`),
  resize-on-metrics-change (`ResizeIfNeeded`, `:203-211`), hidden draining
  (`PumpHiddenSession`, `:255-264`), cwd note (`SetCwd`, `:80-86`).
- UI: `terminalPanel` VBox contains `terminalPane` + button row with
  Stop/Restart (`ui/tool_panel_shell.zia:246-260`); controller wired in
  `main.zia:220-222`, pumped at `:346`, cwd set on project open (`:222`) and
  presumably on project switch (grep `SetCwd` call sites).
- Shell resolution: `ShellPath`/`ShellArgs` per platform (`:106-126`) — shared
  across sessions, unchanged.
- Cleanup: `terminalCtrl` passed to `lifecycle_controller.CleanupIde`
  (`main.zia:352-355,660-662`) which calls `Stop()`.

## 3. Design

### 3.1 Session record

```zia
class TerminalInstance {
    expose terminal_session.TerminalSession sess;
    expose GUI.OutputPane pane;      // one pane per session, SetVisible toggled
    expose String title;             // "1: zsh", "2: zsh" ...
    expose String cwd;               // cwd at spawn
    expose Boolean manualStop;
    expose Boolean didFocus;
    expose Integer lastCols; expose Integer lastRows;
    expose String hiddenOutput;      // per-session replay buffer + flags
    expose Boolean hiddenOutputDropped;
    expose Boolean hiddenHadExit; expose Integer hiddenExitCode;
}
```

Move the per-session methods (`StartSession/RestartSession/EnsureStarted/
ResizeIfNeeded/BufferHiddenOutput/FlushHiddenOutput/PumpHiddenSession` —
`terminal_controller.zia:156-264`) onto `TerminalInstance` mostly verbatim.
`TerminalController` becomes the manager: `List instances`, `Integer activeIdx`,
selector UI, and a `Pump()` that:

1. pumps the ACTIVE instance exactly like today's visible path (`:267-305`),
2. background-drains every other instance (its `PumpHiddenSession`),
3. polls selector + New/Close/Stop/Restart buttons.

**Pane-per-session** (not one shared pane): each PTY's screen state (cursor
addressing, colors, partial escape sequences) lives in its pane's
terminal-mode state — sharing one pane across sessions would interleave state.
Panes are `GUI.OutputPane.New(terminalPanel)` with `SetTerminalMode(true)`,
`SetMaxLines(10000)`, `SetFlex(1.0)` (mirror `tool_panel_shell.zia:252-255`);
only the active one `SetVisible(true)`. The per-session hidden replay buffer
becomes unnecessary for *sessions with their own pane* — output can append to
the hidden pane directly (cheaper and preserves escape-sequence continuity);
keep the bounded-append guard by relying on the pane's own `SetMaxLines` ring.
Delete the string replay buffer entirely UNLESS appending to a hidden pane
proves expensive — measure; the pane append is C-side and bounded, expected fine.

### 3.2 Selector UI

Button row extension in the terminal panel
(`tool_panel_shell.zia:256-260` area): `[Dropdown: sessions] [+] [🗑 Close] [Stop] [Restart]`.

- `GUI.Dropdown` (exists — `vg_dropdown.c`); items = instance titles; selection
  change → `activeIdx` switch (hide old pane, show+focus new, flush nothing —
  panes carry their own content).
- `+` → new instance at `projMgr.RootPath()` (fallback: home dir), becomes
  active, starts immediately (it is visible).
- Close → stop PTY + destroy pane widget + remove instance; if it was the last,
  create a fresh one lazily on next show (preserve today's lazy-start-on-visible).
- Commands in `command_catalog.zia`: `newterminal` ("Terminal: New Terminal"),
  `closeterminal`, plus existing stop/restart continue to target the active
  instance. Handlers in `commands/view_commands.zia` or a new
  `commands/terminal_commands.zia` if view_commands is crowded (533 lines —
  new module preferred per the 300/500-line budget).

### 3.3 Behavior details

- `SetCwd(dir)` (project switch): stores default for FUTURE sessions only; the
  in-pane note (`:83-85`) now reads "new terminals will start in <dir>" and the
  `+` button makes acting on it one click.
- Exit of a non-active session: title gets a marker ("2: zsh (exited)");
  selecting it shows the exit notice already appended to its pane.
- Shutdown: `Stop()` iterates all instances (`lifecycle_controller` unchanged —
  it already calls `terminalCtrl.Stop()`).
- Panel hidden: drain ALL instances (today's hidden-drain rationale,
  `:251-264`, applies to each).

## 4. Implementation steps

1. Extract `TerminalInstance` (mechanical move of per-session state/methods);
   controller keeps a single-instance list — behavior identical. Run terminal
   probes (`terminal_probe`, `terminal_open_probe`, `terminal_render_probe`,
   `terminal_hidden_start_probe`) — must pass unchanged.
2. Pane-per-session + hidden-append-to-pane; delete the string replay buffer
   (or keep per measurement note in §3.1); re-run probes (hidden-start probe
   asserts replay behavior — update its expectations to "content present in
   pane on show" which is the same user-visible contract).
3. Selector UI + New/Close + commands + palette entries.
4. Multi-session pump (active full-pump, others drain).
5. Probe additions: extend `terminal_hidden_start_probe.zia` or new
   `terminal_multi_probe.zia`: create two sessions, write a distinguishing
   command to each (e.g. `echo one` / `echo two`), switch, assert each pane
   contains only its own output; close one; shutdown cleans both. Register
   `LABELS "zia;viperide;terminal"`.
6. Manual: two shells with different cwds; long output in a background session
   (no stall, no interleave); Stop/Restart target the right one; project
   switch + `+` spawns at new root; IDE exit leaves no orphan shells
   (`ps` check).
7. Full no-skip build + test run.

## 5. Files to modify

- `viperide/src/terminal/terminal_controller.zia` — manager refactor.
- `viperide/src/terminal/terminal_session.zia` — unchanged (verify no
  singleton assumptions).
- `viperide/src/ui/tool_panel_shell.zia` — selector row + pane host.
- `viperide/src/commands/command_catalog.zia` + new
  `viperide/src/commands/terminal_commands.zia` — commands.
- `viperide/src/main.zia` — Setup signature if widget wiring changes
  (`main.zia:220-222`), command dispatch entries.
- Probes per step 5; `src/tests/CMakeLists.txt` for new probe.
- `viperide/docs/status.md` — terminal section update (stay honest: still not
  a full emulator).

## 6. Testing

Existing four terminal probes are the regression net for single-session
behavior at every step; the new multi-session probe covers isolation and
lifecycle. Manual pass for interactive feel + orphan check.

## 7. Acceptance criteria

- Two+ concurrent shells with independent scrollback, cwd, and lifecycle;
  switching is instant and never mixes output.
- New Terminal spawns at the current workspace root; the old cwd-change
  annoyance is gone.
- Background sessions never back up PTY buffers (verified by long `yes | head`
  style output while hidden).
- All terminal probes green; clean shutdown of all sessions.

## 8. Repo rules (read before starting)

- Zia-only plan: rebuild with `./scripts/build_ide.sh`.
- Zia code binds namespace aliases; module headers per
  `viperide/docs/architecture.md`; process state stays in `src/terminal/`
  (ownership rules).
- Finish with a full no-skip `./scripts/build_viper_unix.sh` + test pass.
  Never commit. No CI changes.
