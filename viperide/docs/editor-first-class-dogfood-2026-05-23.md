# ViperIDE Editor-First-Class Dogfood Report - 2026-05-23

## Scope

- Build: `./scripts/build_ide.sh` from the repository root.
- Runtime: macOS 26.5, production `viperide/bin/viperide`.
- Profile isolation: temporary `HOME=/tmp/viperide-dogfood-home.Omfc79`.
- Perf log: `VIPERIDE_PERF_LOG=/tmp/viperide-dogfood-final2.log`.
- Project: `examples/apps/vipersql`.
- Restored files:
  - `engine/executor.zia` - 6380 source lines, minimap enabled.
  - `parser/parser.zia` - 3906 source lines, opened through Quick Open.
- Settings: font size 15, autosave off, diagnostics on, code folding on, line
  numbers on, word wrap off, minimap on, session restore on.

## Manual Checklist

- Launch/session restore: restored the `vipersql` project and two source tabs
  from the isolated settings file without touching user settings.
- Large-file editing: typed into `executor.zia`, triggered completion from a
  `Viper.` prefix, accepted with Tab, then undid the edit.
- Navigation/panels: toggled the symbol outline, ran diagnostics explicitly,
  opened `parser.zia` through the in-app Quick Open palette, and scrolled the
  active editor.
- Tool surfaces: exercised Problems/diagnostics, Search/Quick Open, Outline,
  Output/build shortcut, Preferences, status chrome, minimap, and tab switching.
- Crash/stall check: no crash, no stuck process, no post-interaction CPU spin.

## Perf Summary

- Perf windows: 35.
- Frames recorded: 2046.
- Worst frame: 45.688 ms during startup/session restore.
- Worst frame after startup: 36.413 ms while the symbol outline was visible.
- Steady-state windows stayed around 16.7-17.6 ms per frame.
- Full-text copies: 5 total, all tied to explicit edit/semantic/file-switch
  work; no per-frame copy churn.
- Full-text bytes copied: 1,247,982 total across the run.
- Layout scans: 0.
- Project-index updates: 1 startup/open-document sync, 153,606 bytes, then 0.
- Idle sample: 1602 of 1687 main-thread samples in `nanosleep`; `top` reported
  0.0% CPU after interactions.

## Top Measured Offenders

1. Render/vsync cadence dominates the perf log at roughly one second of render
   time per one-second window. The sample shows the process sleeping between
   frames rather than burning CPU.
2. Symbol outline refresh is the largest controller cost when visible, peaking
   at 24.840 ms total in one perf window.
3. Diagnostics after edit/check peaked at 11.393 ms total in one perf window.
4. Completion after the `Viper.` edit peaked at 3.450 ms total in one perf
   window and did not steal editor focus.
5. Build/output polling peaked at 14.041 ms total in one perf window during the
   build shortcut smoke.

## Fix From Dogfood

The first dogfood pass found that the old Quick Open command used a blocking
`MessageBox.Prompt`, which produced a single 627 ms frame while the modal prompt
was open. Quick Open now reuses the in-app command-palette overlay populated
with cached project files, so `Ctrl+P` is non-modal, fuzzy-filtered, and no
longer blocks the editor frame loop.

After the first rebuilt binary was tested interactively, a separate beachball
report pointed at command-palette/Quick Open filtering risk for large projects.
The native command-palette UTF-8 decoder no longer calls `strlen()` for every
decoded character during fuzzy matching, and `test_vg_tier3_fixes` now covers
thousands of long path-shaped labels.

## Known Follow-Ups

- Several lower-frequency commands still use modal prompts: project search,
  workspace symbols, Go To Line, rename/refactor names, and tree create/rename.
  They are not in the editor typing hot path, but should move to workbench
  overlays after this gate.
- Real debugger execution, BASIC semantic language services, split/diff editors,
  and SCM integration remain explicitly out of scope for this plan.
- Richer AST-backed refactors/formatter, deeper semantic scoring, and fully
  column-native result widgets remain future polish beyond the release gate.
