# ViperIDE Runtime Integration

ViperIDE is implemented in Zia, but most platform and GUI behavior is supplied
by the Viper C runtime. This document records the runtime contracts the IDE uses
today and the important constraints at that boundary.

## Runtime Registry

Runtime APIs are registered through `src/il/runtime/runtime.def`. When adding or
changing runtime functions used by ViperIDE, update:

- The C runtime implementation and header.
- `runtime.def`.
- Structured class bindings if a method/property surface changes.
- Graphics and non-graphics stubs where applicable.
- Runtime completeness checks.
- ViperIDE docs and probes that cover the behavior.

Runtime ABI or cross-layer dependency changes require the repository's normal
ADR process.

## GUI Runtime

Main GUI families used by ViperIDE:

- `Viper.GUI.App`
- `Viper.GUI.Window`
- Menus, menu items, buttons, labels, boxes, splitters, tabs, list boxes,
  tree views, inputs, check boxes, sliders, command palette, and toasts.
- `Viper.GUI.CodeEditor`
- `Viper.GUI.OutputPane`
- `Viper.GUI.VirtualList`
- `Viper.GUI.VirtualTree`
- GUI test harness helpers used by probes.

### CodeEditor

`CodeEditor` is the primary editing widget. ViperIDE relies on:

- Text get/set and revision tracking.
- Cursor and selection state.
- Syntax/language mode.
- Line numbers, word wrap, minimap, folding, and gutter markers.
- Multi-cursor methods.
- Inline diagnostics/highlights.
- Performance counters used by probes.

IDE-side controllers should use editor revisions to avoid repeated full-buffer
copies. When adding semantic features, take a snapshot once per relevant
revision and reject stale results when the revision changes.

### OutputPane

`OutputPane` is used for:

- Build/run output.
- Terminal display and input capture in terminal mode.
- Some debug and textual panel surfaces.

Relevant methods include append, append line, append styled text, clear,
selection, select all, max-line limit, line count, font, auto-scroll,
terminal-mode toggle, and input draining.

OutputPane terminal mode is intentionally not a complete terminal emulator. It
supports line-oriented shell interaction and captured key input, but not the full
alternate-screen/cursor-addressing behavior required by applications like
full-screen editors.

### VirtualList And VirtualTree

`Viper.GUI.VirtualList` and `Viper.GUI.VirtualTree` are currently lightweight
runtime helpers for row ids, selection, counts, expansion, and visible-range
calculation. Most ViperIDE tool panels are still rendered through concrete
ListBox/OutputPane widgets and should not be documented as fully virtualized
until the UI uses virtual row realization end to end.

## Process Runtime

`Viper.System.Process` powers:

- Build jobs.
- Run jobs.
- Debug adapter process launch.

ViperIDE uses:

- `StartWithEnv(program, args, cwd, env)`.
- `ReadStdout()`.
- `ReadStderr()`.
- `WriteStdin()`.
- `IsRunning()`.
- `ExitCode()`.
- `Kill()`.
- `Destroy()`.

Important constraints:

- Jobs are started with explicit argv sequences, not shell strings.
- The process runtime does not search `PATH` for bare executables in the way a
  shell would, so ViperIDE resolves the `viper` path before launch.
- IDE-side retained output is bounded to avoid runaway memory usage.
- Runtime process buffers are finite. Excessive child output must be handled
  carefully by draining promptly and truncating at the IDE layer.

## PTY Runtime

`Viper.System.Pty` powers the integrated terminal.

ViperIDE uses:

- `Pty.IsSupported()`.
- `Pty.LastError()`.
- `Pty.Open(program, args, cwd, env, cols, rows)`.
- `PtySession.Read()`.
- `PtySession.Write()`.
- `PtySession.Resize()`.
- `PtySession.IsRunning()`.
- `PtySession.ExitCode()`.
- `PtySession.Kill()`.
- `PtySession.Destroy()`.

Platform behavior:

- POSIX uses the runtime PTY implementation.
- Windows uses the runtime ConPTY path when available.
- Unsupported platforms report through `IsSupported()` and `LastError()`.

IDE constraints:

- The terminal controller estimates columns and rows from OutputPane dimensions
  because font metrics are not exposed.
- Output deltas are bounded per update.
- Stop kills and destroys the PTY session.
- Restart destroys the previous session and opens a new one.

## Exec Runtime

`Viper.System.Exec` is used by the Git wrapper because the current Source
Control view is small and synchronous.

Current constraints:

- `Exec.CaptureArgs` captures stdout but does not provide a reliable exit code.
- `Exec.RunArgs` provides an exit code but lets child output go to inherited
  stdout.
- Source Control read commands infer success from captured output.
- Source Control write commands infer success from the exit code.

If Source Control becomes a polished product surface, prefer an async
`Viper.System.Process` model that captures stdout, stderr, and exit code
together without blocking the UI.

## Workspace Runtime

### FileIndex

`Viper.Workspace.FileIndex` is used for:

- Project tree exclusion checks.
- Quick Open cache population.
- Project search enumeration.
- Zia workspace indexing source discovery.

It applies hard excludes, `.gitignore` support implemented by the runtime, and
project manifest ignore/exclude patterns.

### Workspace.Edit

`Viper.Workspace.Edit.ApplyInRoot` is used for existing-file saves and
workspace-edit operations. ViperIDE depends on it to validate existing file
metadata, stage temporary writes, commit replacements, and report failure
without leaving a truncated target file.

## Language Runtime

### Zia

ViperIDE uses structured Zia runtime APIs for:

- Completion.
- Diagnostics/toolchain checks.
- Hover.
- Signature help.
- Symbols.
- Project indexing.
- Definition, references, call hierarchy, and rename edits.

Zia work should use structured records and handles. Avoid adding new
tab-separated, colon-separated, or display-row-parsed contracts when a map,
sequence, or stable handle can be returned.

### BASIC

BASIC currently provides in-process completion, diagnostics, hover, and document
symbols. There is no BASIC project index equivalent wired to ViperIDE, so
definition, references, rename, workspace symbols, and signature help remain
disabled for BASIC documents.

## Game Scene Runtime

`Viper.Game2D.SceneDocument` exists in the runtime and is covered by IDE-facing
probes for scene data behavior. Relevant scene runtime capabilities include loading,
saving, JSON round-trip, diagnostics, scene-owned mutators, properties, and
tilemap render-copy creation.

ViperIDE currently does not mount a visual scene editor. Runtime scene support is
available below the IDE, but the IDE still lacks:

- `Viper.GUI.SceneView`.
- A `scene_editor/` subsystem.
- `SceneDocumentState`.
- Per-document scene handles.
- Scene-specific save/reload/conflict flow.
- Tile palette, layer list, object tools, inspector, and play wiring.

## Cross-Platform Rules

Runtime and IDE changes must remain cross-platform:

- Use `src/common/PlatformCapabilities.hpp` or `src/runtime/rt_platform.h` for
  platform decisions in C/C++.
- Raw platform preprocessor checks belong only in approved adapter layers.
- Zia code should use runtime platform APIs such as `Viper.System.Machine`,
  `Viper.IO.Path`, and runtime process/PTY abstractions.
- Do not introduce external product dependencies.

Cross-platform-sensitive changes should run:

```sh
./scripts/lint_platform_policy.sh
```

## Runtime Boundary Checklist

When changing a runtime API for ViperIDE:

- Keep the IL/runtime spec and ADR requirements in mind.
- Update C implementation, headers, `runtime.def`, and stubs.
- Verify native and VM paths when both are relevant.
- Add focused runtime tests.
- Add or update a ViperIDE probe when the app depends on the behavior.
- Document limitations in this file.
