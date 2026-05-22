# ViperIDE

ViperIDE is the IDE application for Zia and Viper BASIC. It is built in Zia on top of the `Viper.GUI.*` runtime classes.

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

## Features

- Code editor with syntax highlighting, line numbers, undo/redo, and minimap
- Zia IntelliSense using path-aware completion APIs
- Live diagnostics through structured toolchain records
- Hover tooltips for type and signature information
- Project-aware Zia definition, references, and rename through `Viper.Zia.ProjectIndex`
- Language-service capability routing so BASIC/text/scene files do not invoke Zia semantic APIs
- File explorer with recursive tree view and context menus
- File-tree rename/delete operations that update open documents
- Tabbed editing with modified indicators and close buttons
- Find/replace bar with match count and navigation
- Project search backed by structured location ids, not `path:line` parsing
- Build/run integration with compiler diagnostics
- Command palette for keyboard-driven workflow
- Persistent settings in `~/.viperide/settings.ini`
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

## Phase 0/1 Test Gate

The Phase 0/1 regression probe is registered as `zia_viperide_phase0_phase1`.
It covers document kind detection, command id consistency, structured locations,
language-service capabilities, search matching, signature-call parsing,
session/recent-project persistence, active-tab close behavior, transactional
workspace edits for open and closed files, and project-index queries.
