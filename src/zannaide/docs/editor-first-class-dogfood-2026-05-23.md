# Archived ZannaIDE Dogfood Report - 2026-05-23

This is an archived performance dogfood report from 2026-05-23. It is retained
as historical evidence for one editor hot-path pass, not as the current product
status. See [status.md](status.md), [workflows.md](workflows.md), and
[testing.md](testing.md) for current documentation.

## Scope At The Time

- Build: `./scripts/build_ide.sh` from the repository root.
- Runtime: macOS 26.5, production `zannaide/bin/zannaide`.
- Profile isolation: temporary `HOME=/tmp/zannaide-dogfood-home.Omfc79`.
- Perf log: `ZANNAIDE_PERF_LOG=/tmp/zannaide-dogfood-final2.log`.
- Project: `examples/apps/zannasql`.
- Restored files:
  - `engine/executor.zia`: 6380 source lines, minimap enabled.
  - `parser/parser.zia`: 3906 source lines, opened through Quick Open.
- Settings: font size 15, autosave off, diagnostics on, code folding on, line
  numbers on, word wrap off, minimap on, session restore on.

## Manual Checklist Run

- Launch/session restore restored the `zannasql` project and two source tabs
  from isolated settings.
- Large-file editing typed into `executor.zia`, triggered completion from a
  `Zanna.` prefix, accepted with Tab, then undid the edit.
- Navigation/panels toggled symbol outline, ran diagnostics explicitly, opened
  `parser.zia` through Quick Open, and scrolled the active editor.
- Tool surfaces exercised Problems/diagnostics, Search/Quick Open, Outline,
  Output/build shortcut, Preferences, status chrome, minimap, and tab switching.
- Crash/stall check found no crash, stuck process, or post-interaction CPU spin.

## Perf Summary From That Run

- Perf windows: 35.
- Frames recorded: 2046.
- Worst frame: 45.688 ms during startup/session restore.
- Worst frame after startup: 36.413 ms while the symbol outline was visible.
- Steady-state windows stayed around 16.7-17.6 ms per frame.
- Full-text copies: 5 total, all tied to explicit edit/semantic/file-switch
  work.
- Full-text bytes copied: 1,247,982 total.
- Layout scans: 0.
- Project-index updates: 1 startup/open-document sync, 153,606 bytes, then 0.
- Idle sample: 1602 of 1687 main-thread samples in `nanosleep`; `top` reported
  0.0% CPU after interactions.

## Issues Found Then

The dogfood pass found that the old Quick Open command used a blocking modal
prompt, producing a 627 ms frame while the prompt was open. Quick Open was moved
to the in-app command-palette overlay with cached project files.

A separate follow-up found command-palette/Quick Open filtering risk for large
projects. The native command-palette UTF-8 decoder was changed to avoid
`strlen()` for every decoded character during fuzzy matching, and native tests
were added for thousands of long path-shaped labels.

## Historical Follow-Ups

At the time of the report, several lower-frequency commands still used modal
prompts, and richer debugger, BASIC semantic, SCM, split/diff, refactor,
formatter, and column-native result widgets were outside that specific
performance gate.

Current status has changed since this report. In particular, ZannaIDE now has
VM-backed debug-adapter integration, an integrated terminal, and a lightweight
Source Control view; it still has the limitations documented in
[status.md](status.md).
