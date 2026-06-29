# ViperIDE Current Status

Last reviewed against source: 2026-06-27.

This file is the current-state reference for ViperIDE. It intentionally avoids
future-phase language and records limitations in the same place as shipped
behavior.

## Summary

ViperIDE is usable as a code editor and project workbench for Zia projects, with
partial BASIC support, build/run integration, a VM-backed debugger path, an
integrated terminal, and lightweight Git operations.

It is not yet a polished product-complete IDE. The largest current gaps are:

- Scene documents are recognized but do not have a visual scene editor.
- Bottom tool surfaces are still largely listbox/output-pane based rather than
  fully virtualized, dockable workbench views.
- Source Control is synchronous and minimal.
- BASIC lacks project-index-backed semantic navigation and rename.
- Debugging lacks watch expressions as a first-class panel and rich variable
  trees.
- Some workflow prompts and overlays are still modal or command-palette based.
- The application source still has several oversized coordinator modules.

## Current Product Narrative

The most accurate way to describe ViperIDE today is "a functional Viper
workbench whose strongest path is Zia code editing." The editor can open real
projects, keep multiple files alive across sessions, run semantic services, and
drive the compiler/debugger toolchain. A Zia developer can use it for daily
editing in a small or medium project, especially when the workflow is edit,
search, build, run, diagnose, and debug one active program.

The roughness shows up when the workflow starts to look like a mature IDE. Some
panels are still row dumps instead of rich work surfaces. Some operations rely
on prompt-style input instead of integrated overlays. The debug substrate is
real, but the inspection UI is shallow. BASIC support is intentionally honest
but incomplete. Source Control is useful for basic Git actions but does not have
the responsiveness or error handling of a full client. Scene support exists in
the runtime, and the IDE recognizes scene file extensions, but no visual editor
is mounted.

This distinction matters for documentation and release notes. ViperIDE should
not be described as a complete scene editor, full terminal emulator, full SCM
client, or full multi-language IDE. It should be described as a growing IDE with
clear working slices and clearly documented gaps.

## What "Implemented" Means Here

In this document, "implemented" means that the feature has a user-visible path,
source ownership, and at least some regression coverage. It does not always mean
the feature is polished or complete by mature IDE standards.

"Partial" means that the feature has working pieces but should not be marketed
as complete. Partial features need explicit limitations in docs and UI.

"Not present" means that users may see related groundwork in code or runtime
APIs, but ViperIDE does not provide the user-facing workflow yet.

## User Impact Summary

For a Zia developer, the biggest strengths are:

- The editor understands Zia well enough for completion, diagnostics, hover,
  signature help, and navigation.
- Project search, Quick Open, workspace symbols, and recent files make normal
  navigation practical.
- Build/run/debug are wired to the Viper toolchain without leaving the IDE.
- Session restore and recovery reduce the risk of losing active work.

For a BASIC developer, the IDE is useful for editing and compiler feedback, but
not for project-wide semantic work.

For a game developer expecting scene tooling, the runtime data foundation exists
below the IDE, but the IDE user experience is not there yet.

For a user expecting a polished workbench, the rough areas are mostly around
panel density, icons, prompt flows, terminal fidelity, Source Control depth, and
debug inspection.

## Feature Matrix

| Area | Status | Notes |
| --- | --- | --- |
| Text editing | Implemented | Multi-tab CodeEditor, undo/redo, selections, comments, formatting, folding, minimap option. |
| Zia IntelliSense | Implemented with limits | Completion, diagnostics, hover, signature help, symbols, definition, references, rename, workspace symbols. |
| BASIC IntelliSense | Partial | Completion, diagnostics, hover, and document symbols only. No project index, definition, references, rename, or signature help. |
| Plain text | Implemented | Opens unknown/text-like files as text without semantic features. |
| Scene files | Partial | `.scene` and `.level` are detected, saved, restored, and filtered, but display as text. |
| Project explorer | Implemented with limits | Demand-loaded tree, multi-root support, Quick Open cache, file actions, ignores. |
| Search | Implemented | Project/folder search, literal/regex, case/word filters, include/exclude filters, grouped results. |
| Build/run | Implemented | Argument-vector jobs, project manifest overrides, streamed bounded output, JSON diagnostics. |
| Debugging | Implemented with UX gaps | External VM debug adapter, breakpoints, stepping, pause, run to cursor, locals, call stack, evaluate, conditions, logpoints. |
| Terminal | Partial | PTY-backed shell in OutputPane terminal mode. Not a full terminal emulator. |
| Source Control | Partial | Git status, stage/unstage, commit, diff, branch basics, push/pull. Blocking and minimal. |
| Settings | Implemented | Platform config path, theme, editor behavior, auto-save, save-before-build, session options. |
| Session restore | Implemented | Project, tabs, cursor/scroll, recent files/projects, bounded recovery text. |
| File watching | Implemented with limits | Active file watcher plus inactive document polling and workspace watcher. |
| Visual polish | Partial | Functional shell, activity bar, status, panels; still reads as utilitarian/prototype in places. |
| Cross-platform | Intended | Runtime adapters exist for process, PTY, GUI; display/runtime behavior still needs regular platform smoke. |

## Language Support

### Zia

Zia is the primary supported language. Current Zia features include:

- Syntax highlighting and semantic tokens.
- Completion with popup filtering, commit behavior, snippets, docs, runtime
  metadata, workspace-symbol completions, and stale-result rejection.
- Hover, signature help, and overload navigation.
- Live diagnostics and explicit "Run Check Now".
- Problems panel integration with diagnostic navigation.
- Fix-it application for supported structured diagnostics.
- Create Missing Bind for known runtime aliases and unambiguous project-file
  binds.
- Suppress Warning insertion for supported warnings.
- Definition, references, incoming calls, outgoing calls, and rename through
  `Viper.Zia.ProjectIndex`.
- Organize Binds.
- Extract Local Variable, Extract Function, and Inline Local Variable for
  deliberately conservative cases.
- Document formatting and selection formatting.

Known Zia limits:

- Workspace indexing is lazy and bounded by file size and per-frame budgets.
- Some project-wide semantic results can be incomplete while indexing is still
  warming up.
- Refactors are intentionally conservative and reject many legal programs.
- Some UI panels use string display rows even when the underlying location data
  is structured.

### BASIC

BASIC support is intentionally narrower:

- Completion.
- Diagnostics.
- Hover.
- Document symbols.
- Formatting for supported line forms.
- Build/run through the same `viper` toolchain path.

BASIC does not currently support:

- Go to Definition.
- Find References.
- Rename Symbol.
- Workspace Symbols.
- Signature Help.
- Project-wide call hierarchy.

The command registry marks unavailable commands with language-specific reasons.

### Text And IL

Plain text, Markdown, JSON, and IL open as text buffers. The IDE provides core
editing, search, save, session, and file-watcher behavior, but no semantic
language service for these file kinds.

### Scene Files

`.scene` and `.level` files are recognized as scene documents. They participate
in open/save/session/file-filter flows, but there is no visual scene document
surface. Today they open as text.

Runtime scene support exists in `Viper.Game2D.SceneDocument` and is covered by probes, but
ViperIDE does not yet keep a scene handle per open document, does not have
scene-specific dirty/save/reload logic, and does not provide tile/object tools.

## Workbench Status

### Explorer

The explorer supports:

- Open folder.
- Add folder to workspace.
- Demand-loaded folder expansion.
- Open selected files.
- Create file/folder.
- Rename.
- Delete/move-to-trash flow.
- Duplicate.
- Copy path and relative path.
- Search in folder.
- Run file.
- Set project entry.
- Refresh.
- Project-specific and runtime workspace ignore rules.

Known limits:

- Tree operations still rely on prompt-style input in some workflows.
- Ignore behavior is whatever `Viper.Workspace.FileIndex` supports; do not
  assume full Git ignore semantics beyond what the runtime implements.
- Very large workspaces depend on cooperative cache/index pumping.

### Bottom Panels

Current bottom panel tabs:

- Problems.
- Output.
- Search.
- References.
- Debug Console.
- Variables.
- Call Stack.
- Debug.
- Terminal.

Output and tool rows are bounded and mostly list-backed. This prevents runaway
UI memory in normal cases but is not the same as a fully virtualized workbench
surface for very large logs/search results.

### Debugger

The debugger uses the external VM debug adapter:

```text
viper run --debug-adapter <file>
```

Supported behavior:

- Launch active file.
- Set and persist breakpoints.
- Conditional breakpoints and logpoints.
- Continue, pause, step over, step in, step out.
- Run to cursor.
- Stop and restart.
- Current-line gutter marker.
- Locals and call stack at stop points.
- Expression evaluation while stopped.
- Debug console output.

Known debugger UX gaps:

- No dedicated watch-expression list.
- Variables are rendered as flat rows, not expandable trees.
- Breakpoint metadata editing exists, but the UX is still lightweight.
- Session state could be clearer when launching, running, stopping, and
  terminating.

### Terminal

The integrated terminal starts a platform shell in a PTY when the Terminal panel
is shown. It supports prompt output, raw typed input, line editing delegated to
the shell, resize, Stop, Restart, and workspace-root working directory selection
for new sessions.

Current limitations:

- The OutputPane terminal mode is not a full terminal emulator.
- Full-screen TUI programs that require cursor addressing or alternate-screen
  support are out of scope.
- Terminal dimensions are estimated from widget size because OutputPane does not
  expose font metrics.
- The controller pumps when the panel is visible; hidden-session behavior should
  be treated carefully when changing PTY buffering.

### Source Control

The Source Control view is a Git integration, not a general SCM abstraction.
It supports:

- Detecting whether the project root is a Git repository.
- Current branch display.
- Status entries.
- Stage one file.
- Unstage one file.
- Stage all.
- Commit staged changes with a message.
- Diff selected path.
- Push and pull.
- Switch branch basics.

Known limits:

- Git commands are synchronous.
- Push and pull can block the UI.
- Status parsing uses porcelain v1 and accepts path quoting/rename edge cases.
- Read commands infer success from output because the current Exec capture API
  does not return exit code and captured output together.
- Error feedback is minimal.

## Data Safety

Implemented protections:

- Modified tabs get close prompts through the document close flow.
- Save All skips untitled and read-only preview buffers.
- Existing-file saves use `Viper.Workspace.Edit.ApplyInRoot`.
- Save As uses same-directory temporary writes for new files.
- File watchers detect external changes.
- Session restore persists unsaved small text buffers as bounded base64 recovery
  data.
- Build/debug preflight can save all modified files before launching.

Known data-safety gaps:

- Scene files do not have a scene-specific document model yet.
- Recovery is intentionally capped and only applies to editable text buffers.
- Source Control write operations depend on Git command success and minimal
  error capture.

## Product Polish Gaps

These gaps are current documentation, not a plan commitment:

- Replace ambiguous toolbar glyphs with a coherent icon system.
- Move remaining prompt-style workflows into non-modal workbench overlays.
- Make tool panels resizable, virtualized, and more consistent.
- Add richer debugger watch/variable UX.
- Harden Source Control error reporting and async behavior.
- Add real scene editing before advertising scene editor functionality.
- Split oversized coordinator modules.
- Expand platform and display test coverage.

## Documentation Honesty Rules

Use these phrasing rules when updating user-facing docs:

- Say "Zia semantic navigation" only for Zia, not BASIC.
- Say "BASIC completion, diagnostics, hover, and symbols" instead of generic
  "full BASIC IntelliSense".
- Say "integrated PTY shell" instead of "full terminal".
- Say "Git Source Control view" instead of "SCM platform".
- Say "scene files are recognized and open as text" instead of "scene editor".
- Say "debug adapter supports stepping, breakpoints, locals, call stack, and
  evaluate" while still mentioning missing watch and variable-tree UX.

The goal is to make the app feel more trustworthy by making the docs less
optimistic than the code.
