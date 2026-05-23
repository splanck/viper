# ViperIDE

ViperIDE is the IDE application for Zia and Viper BASIC. It is built in Zia on top of the `Viper.GUI.*` runtime classes.

Current status: ViperIDE has useful IDE infrastructure, but the code-editor
experience is not yet first-class. The active correction plan is
[`plans/editor-first-class-plan.md`](plans/editor-first-class-plan.md), which
blocks scene-editor work until editor responsiveness, IntelliSense, refactoring,
project explorer UX, console/search/problems surfaces, preferences, and visual
polish are brought up to product quality.

## Layout

```text
viperide/
    README.md                  This overview
    viper.project              Project manifest; entry is src/main.zia
    CMakeLists.txt             Native test target for the menu/runtime probe
    bin/                       Generated IDE binaries (ignored)

    src/
        main.zia               Application entry point and frame loop
        smoke_probe.zia        CI smoke probe for shell/editor construction

        build/                 Build/run integration and diagnostics model
        commands/              File, edit, view, build, and search commands
        core/                  Documents, projects, settings, and persistence
        editor/                Code editor integration and language services
        services/              Shared utility services
        ui/                    Window shell, menus, toolbar, panels, overlays
        tests/                 Local GUI probes and C runtime/menu test source

    plans/                     Roadmap, phase plans, and runtime prerequisites
    docs/
        plans/                 Historical implementation notes
```

Root-level files are project metadata or documentation. Zia and C source files live under `src/`.

## Build

From the repository root:

```bash
./scripts/build_ide.sh
```

On Windows:

```bat
scripts\build_ide_win.cmd
```

Both scripts write generated binaries under `viperide/bin/` by default.

## Run

```bash
viper run viperide/
```

Or from inside this directory:

```bash
viper run .
```

## Project Run Config

`viper.project` can override the default `viper build <project-root>` and
`viper run <project-root>` commands:

```text
build-program viper
build-args build "{target}"
run-program viper
run-args run "{target}"
working-directory .
problem-matcher viper
ignore build-output/
ignore *.tmp
```

Arguments are split into an argument vector; quotes group tokens and `{target}`
is replaced with the project root or active file path.
`ignore`, `ignore-patterns`, or `exclude` entries add project-specific file-tree
and search exclusions on top of hard excludes and `.gitignore`.

## Features

- Code editor with syntax highlighting, line numbers, undo/redo, smart Home/End,
  pointer selection drag, auto-indent, bracket/quote pair insertion, line
  and block comment toggle, matching-pair highlight, duplicate line, move line
  up/down, expand/shrink selection, semantic fold regions for Zia symbols,
  Organize Binds, word wrap, and minimap
- Revision-gated editor controllers with background semantic jobs for
  completion, diagnostics, signature help, hover, and outline refresh, plus idle
  project-index sync
- Zia IntelliSense using path-aware structured completion/signature/hover APIs,
  editor-focus-safe popup filtering, dot triggers, identifier debounce, snippet
  cursor placement metadata, callable commit characters, workspace-symbol
  completion from the project file cache, stale-result rejection, and explicit
  `Ctrl+Space`
- Live diagnostics through structured toolchain records and background checks,
  plus an explicit Run Check Now command
- Hover tooltips for type and signature information, delayed until dwell and
  resolved through background jobs
- Project-aware Zia definition, references, and rename through `Viper.Zia.ProjectIndex`; rename now has a preview/cancel step
- Dedicated References bottom-panel tab with grouped file headers and preview rows
- Language-service capability routing so BASIC/text/scene files do not invoke Zia semantic APIs
- File explorer with recursive tree view, right-click node targeting, Open/Open With Text Editor, duplicate, copy path, copy relative path, search in folder, run file, set project entry, refresh, keyboard commands for common tree actions, project-specific ignore patterns, and OS-aware reveal
- File-tree rename/delete operations that update open documents; rename previews
  and rewrites affected quoted Zia file binds
- Tabbed editing with modified indicators and close buttons
- Find/replace bar with match count and navigation
- Project search and folder-scoped search with grouped file headers,
  cooperative file discovery/content scanning, cached project file state when
  available, and structured location ids instead of `path:line` parsing
- Quick Open by project file name/path fragment (`Ctrl+P`), plus recent-file
  and recently closed-tab reopen workflows
- Workspace symbol search by project symbol fragment (`Ctrl+T`) with structured
  result locations and dirty-open-document awareness
- Project-aware build/run configurations using argument-vector process jobs
- Streamed build/run output with cancellable jobs, append-preserving output
  updates, filter/wrap/copy/clear commands, selected-row/range copy,
  auto-scroll lock, severity-colored rows, clickable diagnostics, and lightweight
  Problems/Output/Search/References/Debug Console tabs
- Debug UI controls with persisted breakpoints and a dedicated Debug Console tab
  wired to the current non-executing `Viper.Debug.Protocol` placeholder
- Command palette for keyboard-driven workflow
- Persistent settings in `~/.viperide/settings.ini`, with editor behavior
  controls, auto-save, save-time whitespace controls, IntelliSense delay
  controls, configurable editor font size, per-section default resets, and
  legacy tiny font-size migration
- Keyboard-shortcuts view with duplicate shortcut detection
- Session restore for open files, active tab, cursor/scroll state, and last project
- File watcher for external changes
- Dark and light themes

## Architecture

ViperIDE is organized as a small layered app:

- `src/core/` owns domain state such as documents, projects, and settings.
- `src/editor/` adapts `Viper.GUI.CodeEditor` into IDE-level editing features.
- `src/build/` invokes compiler/run commands and parses diagnostics.
- `src/ui/` constructs the shell widgets and overlays.
- `src/commands/` contains user-facing command handlers.
- `src/services/` contains shared file-kind, location, and workspace-edit helpers.
- `src/main.zia` wires the layers together and runs the frame loop.

## Phase Test Gates

The editor performance probe is registered as `zia_viperide_editor_hot_path`.
It verifies that `Viper.GUI.CodeEditor.Revision` changes on content edits and
undo/redo, remains stable for cursor-only movement, avoids repeated open-document
index syncs, and enforces large-buffer cursor-movement copy/timing budgets. The
native `test_vg_codeeditor_perf` target also guards 50k-line navigation,
folded/wrapped layout caches, highlight span indexing, typing bursts, and large
selection paths against full-buffer copies and layout scans, with wall-clock
budgets for 5k/20k typing-plus-paint, 50k scroll/paint, pointer selection drag,
and minimap paint.

The Phase 0/1 regression probe is registered as `zia_viperide_phase0_phase1`.
It covers document kind detection, command id consistency, structured locations,
language-service capabilities, search matching, signature-call parsing,
session/recent-project persistence, active-tab close behavior, transactional
workspace edits for open and closed files, recent-file persistence, recently
closed tab tracking, and project-index queries.

The console/search regression probe is registered as
`zia_viperide_console_search`. It covers output append behavior, partial-line
rebuild behavior, output filter/wrap/copy/clear helpers, whole-word search, and
Quick Open ranking.

The Phase 2/3 regression probe is registered as `zia_viperide_phase2_phase3`.
It covers run-config argument vectors, project command overrides, real
`viper run <project-root>` entry execution, streamed and cancellable
`Viper.System.Process` jobs, diagnostic parsing for paths with spaces/colons,
persisted breakpoints, the current placeholder debug protocol's
stop/step/locals/terminate/crash event shape, and `Viper.Game.Scene`
load/save/mutator/diagnostic/tilemap contracts.

The current debug protocol used by ViperIDE is a source-text placeholder. It
does not execute compiled code, evaluate expressions, or produce real VM stack
frames. It exists to wire breakpoint persistence, command routing, and debug UI
state before a crash-isolated subprocess debugger or hosted VM debugger is
available.
