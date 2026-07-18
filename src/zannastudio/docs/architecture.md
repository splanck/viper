# Zanna Studio Architecture

This document describes the current Zanna Studio source layout and ownership rules.
It is a contributor guide, not a roadmap.

For a file-by-file tour, read [source-map.md](source-map.md). For procedural
change guidance, read [maintenance.md](maintenance.md). This document stays at
the architecture and ownership level; the other two documents go deeper into
current modules and common change workflows.

## Runtime Shape

Zanna Studio is a frame-driven GUI application written in Zia. `src/main.zia`
constructs long-lived subsystem objects, creates the shell widgets, restores
settings/session state, then polls controllers and command triggers each frame.
Most GUI controls expose polling methods such as "was clicked", "was changed",
or "take input"; the app uses those instead of callback registration.

The implementation depends on runtime classes supplied by the C runtime and
registered through `src/il/runtime/runtime.def`. Important runtime families are:

- `Zanna.GUI.*` for windows, menus, widgets, `CodeEditor`, `OutputPane`,
  command palette, and test/virtual-list helpers.
- `Zanna.System.Process` for build, run, debug-adapter, and Git child processes.
- `Zanna.System.Pty` for the integrated terminal.
- `Zanna.Workspace.FileIndex` for project tree/search enumeration.
- `Zanna.Workspace.Edit` for transactional text replacement.
- `Zanna.Zia.*` and `Zanna.Basic.*` for language services.
- `Zanna.Game2D.SceneDocument` for scene data loading/saving tests and future editor work.

## Source Layout

```text
zannastudio/src/
    main.zia        Application bootstrap, frame loop, dispatch glue.
    app/            App-level helpers that do not own widgets.
    build/          Build/run jobs, diagnostic model, breakpoints, debugger.
    commands/       Command catalog, registry, and feature handlers.
    core/           Documents, projects, settings, sessions.
    editor/         Code editor adapter, semantic controllers, indexing.
    probes/         Focused IDE probe entry points.
    scm/            Git command wrapper and Source Control view model.
    services/       Leaf helpers for file kinds, locations, text, edits.
    terminal/       PTY session wrapper and integrated-terminal controller.
    tests/          Local GUI/runtime probe sources.
    ui/             Shell widgets, panels, activity bar, overlays.
    zia/            Pure Zia parsing, formatting, bind, and refactor helpers.
```

The larger feature areas are intentionally split into small teaching modules:

- `app/command_palette_controller.zia` owns command-palette mode transitions.
- `app/file_watch_controller.zia` owns active-file watch state and open-tab
  timestamp polling.
- `services/search_matcher.zia` owns pure literal/regex line matching.
- `services/search_paths.zia` owns search include/exclude/ignore path rules.
- `services/workspace_file_index.zia` owns shared FileIndex-backed workspace
  membership and enumeration policy.
- `commands/command_context.zia` names the long-lived dependencies used by the
  command dispatcher.
- `commands/search_commands.zia` owns the docked search panel flow and consumes
  normalized requests for direct, cached, and legacy search paths.
- `commands/quick_open_commands.zia` owns Quick Open palette rows, id encoding,
  and deterministic file scoring.
- `commands/source_transform_commands.zia` owns UI command orchestration for
  format/refactor source transforms.
- `commands/diagnostic_edit_commands.zia` owns warning suppression, diagnostic
  fix-it, and missing-bind source edit flows.
- `ui/tool_panel_model.zia` names bottom-panel ids and tab indexes.
- `ui/output_cache.zia` owns pure build-output cache and wrapping policy.
- `editor/completion_items.zia` defines typed completion candidates while
  `editor/completion.zia` owns popup behavior.
- `editor/completion_workspace_source.zia` owns workspace source loading and
  detail formatting for completion's workspace symbol cache.
- `zia/function_scan.zia`, `trivia_scan.zia`, `call_scan.zia`,
  `delimiter_scan.zia`, and `occurrence_scan.zia` hold focused source scanners;
  `zia/source_scan.zia` remains a compatibility facade for older callers.

## Ownership Rules

Prefer this dependency direction:

```text
main -> app -> commands/ui/editor/core/build/services/zia
commands -> core/editor/services/zia/ui
editor -> core/services/zia
ui -> editor/core only when rendering state requires it
services/zia -> leaf helpers with no AppShell ownership
```

Rules:

- `main.zia` wires subsystems and dispatches events. Do not add parsing rules,
  filesystem mutation flows, formatting logic, or command metadata there.
- `commands/command_catalog.zia` owns command ids, labels, shortcuts,
  capability tags, and command-palette visibility.
- Command behavior belongs in the feature command modules:
  `file_commands.zia`, `edit_commands.zia`, `search_commands.zia`,
  `build_commands.zia`, `debug_commands.zia`, and `view_commands.zia`.
- Source-to-source transforms belong in `src/zia/`, not in UI or command code.
- Shared path, location, text, and workspace-edit rules belong in `src/services/`.
- Widget construction and persistent widget references belong in `src/ui/`.
- Process state belongs in `src/build/`, `src/terminal/`, or `src/scm/`
  depending on the feature.

Avoid cycles. When two modules need the same rule, move that rule down into
`services/` or `zia/`.

## Main Loop Responsibilities

`main.zia` currently coordinates:

- App shell creation and settings application.
- Document, tab, project, session, and recent-file state.
- Editor-to-document synchronization.
- File-watch and workspace-watch controllers.
- Command-palette controller, menu, toolbar, context menu, and shortcut dispatch.
- Editor controllers: completion, diagnostics, hover, signature help, symbols,
  inlay hints, semantic tokens, and project indexing.
- Build/run/debug/terminal/SCM panel updates.
- Close, save, project switch, and shutdown preflight.

This file is intentionally still too large. New behavior should usually be
introduced behind a subsystem object and called from the loop, not implemented
inline in the loop.

## Core Data Flow

### Documents

`core/document_manager.zia` owns the open document list and active index.
It deduplicates open files, tracks untitled documents, stores recently closed
paths, detects file kind and language by extension, and routes saves through
`Zanna.Workspace.Edit` when replacing existing files.

`core/document.zia` stores per-buffer state: file path, display name, language,
document kind, content, modified/new/read-only flags, disk metadata, cursor,
scroll, and external-change notification state.

Document kinds:

| Kind | Extensions | Current behavior |
| --- | --- | --- |
| Code | `.zia`, `.bas`, `.vb` | Full text editor; language-specific services. |
| Text | `.txt`, `.md`, `.json`, `.il`, unknown | Plain text editing or preview. |
| Scene | `.scene`, `.level` | Recognized as scene docs but opened as text. |
| Binary unsupported | common image extensions | Read-only preview placeholder. |

### Projects

`core/project_manager.zia` owns the file tree, primary project root, additional
workspace roots, incremental tree population, Quick Open cache, and project
exclusion checks. It delegates ignore matching to `Zanna.Workspace.FileIndex`
and uses absolute paths as tree-node data.

`core/project.zia` parses the small `zanna.project` manifest: project name,
language, entry, run/build overrides, working directory, problem matcher, and
ignore patterns.

### Sessions And Settings

`core/settings.zia` reads and writes platform-native settings paths and keeps
legacy `~/.zannastudio/settings.ini` compatibility. Settings are loaded once on
startup and updated by workbench commands.

`core/session.zia` stores the last project, open tabs, active tab, cursor/scroll
state, recent projects, recent files, and bounded base64 crash-recovery text for
modified editable buffers.

### Language Services

`editor/language_service.zia` routes capability checks by document language:

| Language | Completion | Diagnostics | Hover | Symbols | Definition/Refs/Rename | Signature |
| --- | --- | --- | --- | --- | --- | --- |
| Zia | yes | yes | yes | yes | yes | yes |
| BASIC | yes | yes | yes | yes | yes | yes |
| Text | no | no | no | no | no | no |
| Scene | no | no | no | no | no | no |

Commands with missing capabilities remain visible in the command palette when
that is useful, but are marked unavailable and report a status/toast reason.
BASIC semantic commands are scanner-backed rather than project-index-backed, so
large workspaces can produce incomplete results while cooperative scans warm up.

### Editor Controllers

`editor/editor_engine.zia` adapts `Zanna.GUI.CodeEditor` to IDE document state.
It caches full-text snapshots by editor revision so semantic jobs do not copy
the full buffer every frame.

Controllers under `editor/` own focused features:

- `completion.zia`: completion popup, filtering, commit behavior, workspace
  symbol completion cache.
- `diagnostics.zia`: live diagnostics, Problems rows, minimap/inline highlights.
- `hover.zia`: dwell-triggered hover requests.
- `signature.zia`: signature help and overload navigation.
- `symbols.zia`: document symbol outline.
- `project_index.zia`: lazy Zia workspace indexing for navigation/refactors.
- `semantic_tokens.zia`: semantic highlighting.
- `inlay_hints.zia`: conservative Zia hints.
- `scheduler.zia`: priority model for editor background work.
- `perf_monitor.zia`: optional frame/controller/editor counters.

### Build, Run, And Debug

`build/build_system.zia` starts argument-vector jobs through
`Zanna.System.Process`, streams stdout/stderr, bounds retained output, parses
structured JSON diagnostics when available, and falls back to legacy diagnostic
line parsing.

`build/run_config.zia` builds the command argv from the active document or
project manifest. It never invokes a shell.

`build/debug_session.zia` launches `zanna run --debug-adapter <file>` as an
external process, sends newline JSON commands on stdin, consumes sentinel-tagged
newline JSON debug events on stderr, and surfaces program stdout separately.
Restart is an asynchronous state transition: the active adapter is asked to
terminate, the frame pump waits until the process has actually exited, and only
then is the replacement adapter launched with the current breakpoint set.

### Terminal

`terminal/terminal_session.zia` wraps `Zanna.System.Pty.PtySession`.
`terminal/terminal_controller.zia` owns the UI side: lazy start, shell
resolution, terminal mode on `OutputPane`, raw key forwarding, output append,
cell-metric-based resize, bounded hidden-session draining, Stop, Restart, and
shutdown cleanup.

The terminal is intended for interactive shells and simple commands. OutputPane
terminal mode handles common cursor addressing and clear-screen redraws, but
full-screen TUI programs requiring complete alternate-screen and terminal-mode
semantics are out of scope.

### Source Control

`scm/scm_git.zia` is an async `Zanna.System.Process` wrapper around Git argv
sequences. It resolves `git`, captures stdout/stderr/exit code, parses
porcelain v2 status, and keeps blocking compatibility wrappers for probes and
older call sites. `scm_view.zia` pumps one active Git job at a time and
maintains the Source Control UI state, including conflict markers for porcelain
v2 unmerged rows. Active Git jobs expose cancellation, and stdout/stderr capture
uses the process read-result API so excessive output is reported as truncated
rather than trapping the workbench.

The Source Control view is intentionally lightweight. Push and pull are
long-running operations with basic progress/error text rather than rich
credential, conflict, and recovery workflows. Treat it as useful local Git
integration, not a complete Git client.

## UI Ownership

`ui/app_shell.zia` creates persistent widgets for the menu bar, toolbar,
activity bar, editor area, bottom tool panels, preferences, overlays, status bar,
debug panels, terminal, and Source Control view. Other subsystems receive
references to widgets owned by `AppShell`.

`ui/command_input.zia` owns reusable non-modal single-value command input for
Go To Line, Add Watch, output filtering, Workspace Symbols, Rename Symbol, and
source-extract names. Command modules still perform validation and effects.

Current tool panels include Problems, Output, Search, References, Debug Console,
Variables, Call Stack, Debug, and Terminal. Problems, Search, References, Debug
Console, Variables, and Call Stack share a bounded stable-row model; Output has
its own bounded row/raw-pane model. The concrete widgets are still
ListBox/OutputPane surfaces, so this is not yet a fully virtualized dockable
workbench, but rows now have a consistent data boundary and memory cap.

## File Size Budget

Use these as review triggers:

- 300 lines: check whether helpers should move out.
- 500 lines: require a clear reason the module is cohesive.
- 1000 lines: split before adding more behavior unless the file is generated or
  an intentionally exhaustive fixture.

Several current files exceed the budget. When touching them, prefer extracting
cohesive behavior rather than adding more inline logic.

## Comment Style

New Zia modules should start with a short module header explaining purpose,
ownership, and where the module fits. Public or nontrivial functions should use
`///` comments with `@brief`, parameters, return values where useful, and
`@details` for policy or safety assumptions.

Comments should explain intent and boundaries. Avoid restating the next line of
code.

## Adding A Feature

1. Add command metadata to `commands/command_catalog.zia` if the feature is
   user-triggered.
2. Put behavior in the appropriate command or subsystem module.
3. Keep shared parsing/path/edit logic in `services/` or `zia/`.
4. Route UI through `AppShell` or a focused controller instead of constructing
   widgets ad hoc in `main.zia`.
5. Add or extend a probe in `src/probes/`.
6. Register the probe in `src/tests/CMakeLists.txt` when it should be part of
   CTest.
7. Update the docs in this directory when the visible behavior or subsystem
   ownership changes.

For detailed recipes covering commands, language features, document kinds,
panels, runtime APIs, settings, session recovery, debugger, terminal, and Source
Control changes, use [maintenance.md](maintenance.md).
