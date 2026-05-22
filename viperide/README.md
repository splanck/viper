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
```

Arguments are split into an argument vector; quotes group tokens and `{target}`
is replaced with the project root or active file path.

## Features

- Code editor with syntax highlighting, line numbers, undo/redo, and minimap
- Revision-gated editor controllers for diagnostics, signature help, outline refresh, and idle project-index sync
- Zia IntelliSense using path-aware completion APIs, editor-focus-safe popup filtering, dot triggers, identifier debounce, and explicit `Ctrl+Space`
- Live diagnostics through structured toolchain records, plus an explicit Run Check Now command
- Hover tooltips for type and signature information
- Project-aware Zia definition, references, and rename through `Viper.Zia.ProjectIndex`; rename now has a preview/cancel step
- Language-service capability routing so BASIC/text/scene files do not invoke Zia semantic APIs
- File explorer with recursive tree view, right-click node targeting, Open/Open With Text Editor, duplicate, copy path, copy relative path, search in folder, run file, refresh, and OS-aware reveal
- File-tree rename/delete operations that update open documents
- Tabbed editing with modified indicators and close buttons
- Find/replace bar with match count and navigation
- Project search and folder-scoped search backed by structured location ids, not `path:line` parsing
- Quick Open by project file name/path fragment (`Ctrl+P`)
- Project-aware build/run configurations using argument-vector process jobs
- Streamed build/run output with cancellable jobs, append-preserving output updates, clickable diagnostics, and a lightweight Problems/Output tool strip
- Debug UI controls with persisted breakpoints wired to the current non-executing `Viper.Debug.Protocol` placeholder
- Command palette for keyboard-driven workflow
- Persistent settings in `~/.viperide/settings.ini`, with a larger default editor font and legacy tiny font-size migration
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
undo/redo, but remains stable for cursor-only movement. This is the first guard
against per-frame full-buffer polling in the IDE frame loop.

The Phase 0/1 regression probe is registered as `zia_viperide_phase0_phase1`.
It covers document kind detection, command id consistency, structured locations,
language-service capabilities, search matching, signature-call parsing,
session/recent-project persistence, active-tab close behavior, transactional
workspace edits for open and closed files, and project-index queries.

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
