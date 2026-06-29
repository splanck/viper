# ViperIDE Source Map

This document is a detailed tour of `viperide/src/`. It complements
[architecture.md](architecture.md) by naming the concrete files that own the
current behavior. Use it when deciding where a change belongs.

ViperIDE is organized around long-lived state objects that are created by
`main.zia` and pumped every frame. The source tree is not a plugin system and it
is not yet a highly decoupled workbench framework. It is a practical IDE
application with clear subsystem boundaries and several large coordinator files
that still need pressure taken off them over time.

## Root Entry Point

### `main.zia`

`main.zia` is the application bootstrap and frame loop. It creates the shell,
document manager, project manager, settings manager, session manager, editor
engine, editor controllers, build system, debug session, terminal controller,
SCM view, and command registry.

It also performs the top-level polling work:

- Read editor input and synchronize editor text back to the active document.
- Pump semantic controllers.
- Pump file and workspace watchers.
- Pump command palette, menus, toolbar buttons, context menus, and shortcuts.
- Dispatch commands to `commands/`.
- Update build/run/debug/terminal/SCM state.
- Maintain status bar and panel state.
- Save session state and clean up processes on exit.

`main.zia` is already beyond the preferred file-size budget. New behavior should
usually be added to a focused module and called from `main.zia`, not implemented
inline. If a change requires adding another large block to the frame loop, first
look for a controller extraction point.

Common reasons to touch `main.zia`:

- Wiring a new subsystem object.
- Adding a new command dispatch branch after adding metadata to the command
  catalog.
- Adding a new per-frame controller pump.
- Adding startup/shutdown integration.

Avoid touching `main.zia` for:

- Source parsing rules.
- File mutation logic.
- UI widget construction details.
- Command metadata.
- Runtime binding policy.

## `app/`

The `app/` directory contains orchestration helpers that sit near the frame loop
but do not own the core document model or persistent widget tree.

### `app/build_info.zia`

Reads build metadata written beside the generated IDE binary. The Help/About
surface uses this so users can identify the build they are running. This file is
the right place for build-info parsing, not for general version policy.

### `app/context_menus.zia`

Builds and updates editor/explorer context menu state. It bridges the current
document, selection, language service, debug session, and explorer selection
into enabled/disabled menu entries.

Touch this file when adding right-click behavior. Do not put command
implementation here; dispatch should still route to `commands/` or a controller.

### `app/dispatch_helpers.zia`

Contains small helpers for command triggering and disabled-state checks shared
by the main dispatch loop. This keeps repetitive menu/toolbar/palette checks out
of individual command modules.

### `app/command_palette_controller.zia`

State machine for command-palette modes. It restores command mode, opens the
command palette, and tracks when the palette temporarily contains Quick Open
file rows.

### `app/explorer_action_runner.zia`

Coordinates explorer actions that need multiple subsystems: document manager,
tabs, project manager, session manager, and AppShell. It is a deliberate bridge
between UI action requests and safe document/project mutations.

This is the right place to keep explorer workflows visible and auditable. File
creation, rename, duplicate, delete, and "open and record" behavior should stay
centralized here instead of being spread across the frame loop.

### `app/file_watch_controller.zia`

Owns active-file watcher state and timestamp polling for inactive open tabs.
`main.zia` calls its `Pump` method instead of carrying watcher variables inline.

### `app/settings_applier.zia`

Applies persisted settings to the shell and editor. Settings parsing belongs in
`core/settings.zia`; settings application belongs here when it requires widgets
or editor state.

### `app/workspace_watcher.zia`

Tracks workspace-level change notifications. It complements document-level file
watching in the main loop and should remain focused on detecting project-tree
changes, not mutating documents directly.

## `build/`

The `build/` directory owns process-backed build/run/debug state and diagnostic
models.

### `build/build_system.zia`

Owns build and run child processes. It resolves the `viper` executable, starts
`Viper.System.Process` jobs with argv sequences, streams stdout/stderr, bounds
retained output, parses structured JSON diagnostics, and falls back to legacy
diagnostic text parsing.

Important behavior:

- The IDE never runs build/run commands through a shell.
- `VIPER_BINARY`, colocated binaries, the build tree, `VIPER_BUILD_DIR`, and
  `PATH` are searched for `viper`.
- Output is bounded to avoid uncontrolled memory growth.
- Diagnostics are kept as structured `Diagnostic` objects for Problems/output
  navigation.

Touch this file for process lifecycle, compiler resolution, output retention, or
diagnostic parsing. Do not put UI row formatting here; that belongs in command
or shell code.

### `build/run_config.zia`

Builds a `RunConfig` from the active document and project manager. It parses
`viper.project` build/run overrides into argv tokens and expands `{target}`.

This file is the authority for:

- Current-file versus project-entry build/run.
- Custom build/run program and args.
- Working-directory selection.
- Problem matcher selection.

### `build/diagnostic.zia`

Small diagnostic data model shared by build, Problems, and output navigation.
Use this instead of display strings when a feature needs severity, path, line,
column, code, and message.

### `build/breakpoints.zia`

Stores persisted breakpoint records: path, line, condition, and log message.
Also owns breakpoint gutter marker constants. Debug command code and the debug
session both depend on this store.

### `build/debug_session.zia`

Controls the external VM debug adapter process. It launches:

```text
viper run --debug-adapter <file>
```

The IDE writes newline JSON commands to stdin and reads sentinel-prefixed JSON
events from stderr. Program stdout is kept as program output. The session tracks
stopped/running/terminated state, current stop path and line, locals, call
stack, evaluation results, program output, and debug console text.

Touch this file for debug protocol transport and session state. UI presentation
belongs in `commands/debug_commands.zia`, `ui/app_shell.zia`, or debug-specific
overlays.

## `commands/`

The `commands/` directory is where user-triggered behavior lives.

### `commands/command_catalog.zia`

Declarative command metadata: id, label, description, shortcut, capability, and
palette visibility. Add command ids here first. Command ids are stable API
inside the app; changing them can break shortcuts, probes, and dispatch.

### `commands/command_registry.zia`

Builds runtime command entries from the catalog, installs shortcuts, populates
the command palette, detects duplicate shortcuts, and computes language-service
availability. It does not implement feature behavior.

### `commands/file_commands.zia`

File and tab workflows: new file, open file, open folder, add folder, save,
save as, save all, close, reload, recent files, reopen closed files, and
project/file selection flows.

Use this file for user-facing file commands. Keep low-level document save logic
in `core/document_manager.zia`.

### `commands/edit_commands.zia`

Editor and semantic editing commands: navigation, references, call hierarchy,
rename, diagnostics navigation, code actions, multi-cursor, comments, duplicate
line, move line, refactors, trim whitespace, and formatting.

This file is large because it bridges many editor features. When adding a new
pure text transform, put the transform in `zia/` or `services/` and keep only
the command orchestration here.

### `commands/search_commands.zia`

Project search, folder search, Quick Open, workspace symbols, and related
result navigation. This module coordinates project file caches, search options,
result rows, and location ids.

Search result display strings should never be the only source of navigation
truth. Use `services/locations.zia` for stable location ids.

### `commands/build_commands.zia`

User-facing build/run command behavior. It performs save preflight, starts
build/run configs, updates running jobs, populates output rows, and publishes
diagnostics to the shell.

Process mechanics belong in `build/build_system.zia`; command workflow and
status messages belong here.

### `commands/debug_commands.zia`

User-facing debug commands: start, continue, step, pause, stop, restart, run to
cursor, breakpoint toggles, breakpoint metadata, evaluation, and UI publishing
from `DebugSession` state.

### `commands/view_commands.zia`

Workbench display commands: sidebar/status bar/theme/fullscreen/minimap,
font zoom, UI zoom, settings, keybindings, tool-row copy, output copy/clear,
output filter/wrap/autoscroll, recent files, and About.

## `core/`

The `core/` directory owns persistent IDE domain state.

### `core/document.zia`

The per-document record. It stores file path, display name, language, document
kind, text content, modified/new/read-only flags, disk metadata, cursor/scroll
state, and external-change notification state.

Do not add editor-widget references here. `Document` is model state, not UI.

### `core/document_manager.zia`

Owns the open document list and active document index. Responsibilities:

- Create untitled documents.
- Open files with deduplication.
- Detect language and document kind.
- Load text or read-only preview content.
- Save active documents.
- Save As.
- Save All.
- Close documents and maintain active index.
- Track recently closed paths.
- Use `Viper.Workspace.Edit` for safe existing-file replacement.

If a feature needs to mutate open documents, prefer going through this module or
a service that clearly documents how document state is updated.

### `core/project.zia`

Parses `viper.project` manifest data: project name, language, entry, build/run
programs, build/run args, working directory, problem matcher, and ignore rules.

Keep manifest parsing here. Runtime execution of those values belongs in
`build/run_config.zia`.

### `core/project_manager.zia`

Owns the primary project, additional workspace roots, explorer tree, tree-node
path data, Quick Open cache, incremental directory loading, ignore checks, and
project file cache refresh.

It uses `Viper.Workspace.FileIndex` for exclusion rules. Do not duplicate ignore
logic elsewhere unless the runtime surface cannot answer the question.

### `core/project_file_ops.zia`

Small filesystem policy helpers for project/explorer flows: safe child names,
project-root containment checks, `.zia` file classification, and reversible
delete trash destinations.

### `core/settings.zia`

Persistent settings manager. It handles defaults, platform-native settings
paths, legacy settings-path compatibility, validation on load, and read-modify-
write persistence so unrelated INI sections survive.

Settings are parsed here but applied elsewhere. Keep widget calls out of this
file except for theme routing that is explicitly part of settings behavior.

### `core/session.zia`

Session and recent history persistence. It stores project root, open files,
active file, active index, cursor/scroll, bounded recovery text for modified
text buffers, recent projects, and recent files.

Recovery text is intentionally capped and base64 encoded. This module should
remain conservative because it runs during startup/shutdown and protects user
work.

## `editor/`

The `editor/` directory adapts `Viper.GUI.CodeEditor` and language services into
IDE behavior.

### `editor/editor_engine.zia`

The adapter between `Document` and `CodeEditor`. It loads documents into the
editor, saves editor state back to documents, sets language/display options,
manages current file path/language, and caches full-text snapshots by editor
revision.

Use `GetTextSnapshot()` instead of reading full editor text repeatedly.

### `editor/editor_tabs.zia`

Owns tab widget behavior and tab metadata such as modified markers, tooltips,
active tab, and close signals. Document content belongs in `DocumentManager`;
visual tab state belongs here.

### `editor/language_service.zia`

Capability matrix for Zia, BASIC, text, and scene documents. The command
registry and semantic controllers use it to decide what should run.

This file is the first place to update when adding a language capability.

### `editor/scheduler.zia`

Priority and debounce model for editor background work. It names semantic job
kinds, default delays, and scheduling state. Use it to keep typing responsive
when adding more background work.

### `editor/completion.zia`

Completion controller. It handles automatic and explicit triggers, query
snapshots, async result tracking, popup population, filtering, acceptance,
commit characters, snippet cursor placement, and workspace-symbol completion
cache updates.

The file is large. Pure filtering helpers have already been split into
`completion_filter.zia`; new reusable completion-display logic should move
there or into another helper instead of growing the controller.

### `editor/completion_items.zia`

Typed completion candidate model and constructors. Use this when adding fields
that must travel together with a completion row, such as insert text, replacement
range, cursor offset, or commit characters.

### `editor/completion_filter.zia`

Pure completion filtering and display helpers. This module is small and easy to
probe. Prefer adding deterministic string/ranking helpers here rather than
inside `completion.zia`.

### `editor/diagnostics.zia`

Live diagnostics controller. It schedules structured toolchain checks, rejects
stale results, updates Problems, tracks active-file inline highlights, and
publishes diagnostic actions.

### `editor/quick_fixes.zia`

Helpers for interpreting diagnostic records into quick-fix actions, missing
runtime bind names, undefined identifier extraction, and known runtime bind
lines. This is the right place for deterministic diagnostic-to-action mapping.

### `editor/hover.zia`

Dwell-based hover controller. It schedules hover lookups, rejects stale results,
formats hover display text, and positions tooltips.

### `editor/signature.zia`

Signature-help controller. It detects call-site context, schedules structured
signature lookups, formats active overloads, and implements overload navigation.

### `editor/symbols.zia`

Document symbol outline and semantic fold-region support. It updates the
outline panel and derives fold regions for Zia symbols when appropriate.

### `editor/project_index.zia`

Lazy Zia project index integration. It owns the `Viper.Zia.ProjectIndex` handle,
syncs open documents, indexes workspace files cooperatively, enforces size and
per-frame byte limits, and exposes definition/reference/rename/call queries.

Changes here can affect responsiveness and semantic correctness. Keep file-size
caps, stale-data handling, and dirty-open-document behavior explicit.

### `editor/semantic_tokens.zia`

Semantic highlighting controller. It maps runtime token kinds to CodeEditor
highlight categories and applies semantic spans after debounce.

### `editor/inlay_hints.zia`

Conservative inlay hints. The current implementation uses IDE-side heuristics
for parameter names and inferred local types. It deliberately caps scanned lines
and hint count.

### `editor/zia_query.zia`

Helpers for wrapping source snippets before sending them to Zia runtime queries.
Use this when a semantic feature needs a parseable module wrapper around partial
editor text.

### `editor/perf_monitor.zia`

Optional perf logging for dogfood and probes. It records frame/controller/editor
counters when enabled by environment configuration.

### `editor/workbench_state.zia`

Small active-workbench-state model. Use it when command enablement or context
needs a structured snapshot rather than reading widgets ad hoc.

## `services/`

Services are leaf modules with shared rules and no ownership of AppShell.

### `services/file_utils.zia`

Extension-based language and document-kind detection plus open/save dialog
filters. This is the source of truth for `.zia`, `.bas`, `.vb`, `.scene`,
`.level`, text, IL, and unsupported binary detection.

### `services/locations.zia`

Structured location id storage for search, diagnostics, build output,
definitions, references, scene locations, and symbols. This prevents UI rows
from becoming the navigation contract.

When adding a clickable result type, add a structured location kind here rather
than parsing display text.

### `services/text_utils.zia`

Small deterministic text helpers: trailing CR stripping, leading whitespace,
right-trim, string-list joining, and string-list membership.

### `services/search_matcher.zia`

Pure project-search line matching. It owns literal matching, whole-word checks,
and the compact editor-search regex subset used by Search in Project.

### `services/search_paths.zia`

Search path normalization and include/exclude/ignore matching. Use it when a
feature needs to decide whether a project path belongs in search results.

### `services/workspace_edits.zia`

Preview, validation, conflict detection, application, and diagnostics formatting
for workspace edit sequences. Rename and refactor commands depend on this to
avoid partial or conflicting application.

## `terminal/`

### `terminal/terminal_session.zia`

Thin wrapper around `Viper.System.Pty.PtySession`. It starts a child, drains
bounded output, writes input, resizes, tracks exit, stores the latest startup
error, and destroys the PTY on stop.

### `terminal/terminal_controller.zia`

UI-side integrated terminal controller. It wires the shell-owned OutputPane,
sets terminal mode, lazily starts a shell when visible, resolves the platform
shell, estimates rows/columns, appends output, forwards captured input, handles
Stop and Restart, and updates the working directory for future sessions.

Do not treat this as a full terminal emulator. It is a PTY-backed shell surface
inside OutputPane.

## `scm/`

### `scm/scm_git.zia`

Synchronous Git command layer over `Viper.System.Exec`. It runs
`git -C <repo> ...`, parses porcelain v1 status, stages/unstages files, commits,
diffs, pushes, pulls, lists branches, and switches branches.

Known constraints are documented in [status.md](status.md) and
[runtime-integration.md](runtime-integration.md). If this becomes async, the
implementation should likely move toward `Viper.System.Process`.

### `scm/scm_view.zia`

Source Control view model and UI action state. It owns the current Git snapshot,
selected path, diff text, commit message, and refresh/operation behavior needed
by AppShell.

## `ui/`

The `ui/` directory owns persistent widgets, overlays, and display helpers.

### `ui/app_shell.zia`

Constructs the workbench: window, menu bar, toolbar, sidebar, explorer,
activity bar, editor container, tab bar, breadcrumb, find bar, bottom panels,
debug panels, terminal, Source Control widgets, preferences, overlays, status
bar, and helper methods for showing/hiding/updating those widgets.

This file is also too large. Add new UI state here only when it truly belongs to
the persistent shell. For complex new surfaces, prefer a dedicated controller or
view module that AppShell wires.

### `ui/activity_bar.zia`

Activity bar state and view ids. Currently covers Explorer, Search, Source
Control, Run/Debug, Extensions placeholder, Problems, Outline, and related view
selection behavior.

### `ui/debug_breakpoint_overlay.zia`

Overlay for breakpoint metadata entry: conditions and logpoints. It owns the
temporary input UI and action result consumed by debug command code.

### `ui/explorer_actions.zia`

Overlay for explorer create/rename/duplicate/delete workflows. It collects user
input and reports action intent; actual filesystem/document mutation happens in
`app/explorer_action_runner.zia`.

### `ui/ide_overlays.zia`

Shared overlay helpers used by command palette or workbench popups.

### `ui/tool_panel_text.zia`

Formatting constants and helpers for Problems/Output/Search/References rows:
colors, column widths, severity labels, truncation, diagnostics summary, and
language display names.

### `ui/tool_panel_model.zia`

Named ids and tab indexes for the bottom tool-panel strip. Use this instead of
magic integers when adding bottom-panel behavior.

## `zia/`

Pure Zia source-analysis and rewrite helpers live here. These modules should
not own GUI widgets or document-manager state.

### `zia/identifier_utils.zia`

Identifier and keyword rules used by refactors and source scanning. Unicode
identifier support should start here if added later.

### `zia/source_scan.zia`

Compatibility facade for source scanners. Existing callers can keep importing
`source_scan`, while new code should prefer the focused modules below.

### `zia/function_scan.zia`

Function lookup helpers: source-line access, function-name parsing, enclosing
function detection, and function end-line matching.

### `zia/trivia_scan.zia`

Trivia-aware line scanning for comment depth, brace/paren depth, continuation
indent, whitespace skipping, and identifier replacement outside strings/comments.

### `zia/call_scan.zia`

Outgoing call-token collection for simple call hierarchy support.

### `zia/delimiter_scan.zia`

Inline delimiter and block brace matching used by selection expansion.

### `zia/occurrence_scan.zia`

Whole-word next-occurrence search for multi-cursor commands.

### `zia/formatters.zia`

Zia and BASIC document/range formatting, whitespace trimming, indentation rules,
and line formatting helpers.

### `zia/bind_utils.zia`

Bind normalization and rewrite helpers. Explorer rename and missing-bind
commands use this kind of logic to keep source updates deterministic.

### `zia/refactors.zia`

Pure refactor transformations for inline local, extract local, and extract
function. The transforms are deliberately conservative and return unchanged
source when unsafe or unsupported.

## `probes/`

Probe files are focused executable checks. They are not examples for end-user
application structure; they are regression harnesses for IDE behavior.

Important probe groups:

- `smoke_probe.zia`: basic app smoke.
- `phase0_phase1_probe.zia`: core document/project/command/session behavior.
- `phase2_phase3_probe.zia`: build/run/debug boundary and scene data contracts.
- `editor_hot_path_probe.zia`: editor performance and copy/layout/index guards.
- `intellisense_probe.zia`: completion, diagnostics, hover, signature behavior.
- `console_search_probe.zia`: output/search/Quick Open helpers.
- `debug_probe.zia`: VM debug adapter integration.
- `terminal_*`: PTY terminal behavior and rendering.
- `scm_probe.zia`: Git Source Control behavior.

When adding a feature, add or extend a focused probe near the smallest surface
that can verify it. Do not rely only on a full IDE smoke test.

## `tests/`

`viperide/src/tests/` contains local GUI/runtime probe sources and small
experiments. CTest registration for IDE probes lives in the repository-level
`src/tests/CMakeLists.txt`.

## Where To Put New Code

Use this practical decision table:

| Change | Preferred location |
| --- | --- |
| New command id or shortcut | `commands/command_catalog.zia` |
| Command availability | `commands/command_registry.zia`, `editor/language_service.zia` |
| File open/save behavior | `core/document_manager.zia` |
| New document metadata | `core/document.zia` |
| Project manifest parsing | `core/project.zia` |
| Explorer tree/cache behavior | `core/project_manager.zia` |
| Editor widget synchronization | `editor/editor_engine.zia` |
| Zia semantic query scheduling | appropriate `editor/*` controller |
| Pure source scanning | focused `zia/*_scan.zia` module; keep `zia/source_scan.zia` as facade |
| Search matching/path rules | `services/search_matcher.zia`, `services/search_paths.zia` |
| Completion row data | `editor/completion_items.zia` |
| Pure source rewrite | `zia/refactors.zia` or `zia/bind_utils.zia` |
| Text formatting | `zia/formatters.zia` |
| Build/run process mechanics | `build/build_system.zia` |
| Build/run user command flow | `commands/build_commands.zia` |
| Debug transport/session state | `build/debug_session.zia` |
| Debug command behavior | `commands/debug_commands.zia` |
| Terminal PTY wrapper | `terminal/terminal_session.zia` |
| Terminal UI behavior | `terminal/terminal_controller.zia` |
| Git command execution | `scm/scm_git.zia` |
| Source Control view state | `scm/scm_view.zia` |
| Persistent shell widgets | `ui/app_shell.zia` |
| Shared display row formatting | `ui/tool_panel_text.zia` |
| Bottom panel ids/tab indexes | `ui/tool_panel_model.zia` |
| Structured clickable locations | `services/locations.zia` |
| Transactional workspace edits | `services/workspace_edits.zia` |
| Runtime API change | C runtime + `runtime.def` + docs/probes |

## Current Pressure Points

The source map should also make current weak spots explicit:

- `main.zia` and `ui/app_shell.zia` are oversized coordinator files.
- `commands/edit_commands.zia`, `commands/search_commands.zia`, and
  `editor/completion.zia` are also large and should be split when adding
  cohesive new behavior.
- Bottom panels are functional but not true virtualized workbench surfaces.
- Scene document kind exists without a scene editor subsystem.
- Source Control is synchronous and minimal.
- Terminal behavior depends on OutputPane terminal mode, not a complete terminal
  emulator.
- BASIC support is intentionally narrower than Zia.

New code should reduce these pressure points when it naturally can, but should
not perform unrelated refactors that make feature verification harder.
