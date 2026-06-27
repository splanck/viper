# ViperIDE

ViperIDE is the repository IDE application for editing, building, running, and
debugging Viper projects. It is written in Zia and runs on the `Viper.GUI.*`,
`Viper.System.*`, `Viper.Workspace.*`, and language-service runtime surfaces.

This directory is application code, not a design archive. The current
documentation is:

- [docs/status.md](docs/status.md): current feature status and known gaps.
- [docs/workflows.md](docs/workflows.md): user and developer workflows.
- [docs/architecture.md](docs/architecture.md): source layout, ownership, and
  layering rules.
- [docs/source-map.md](docs/source-map.md): detailed module-by-module source
  guide.
- [docs/runtime-integration.md](docs/runtime-integration.md): runtime APIs used
  by the IDE and the constraints they impose.
- [docs/maintenance.md](docs/maintenance.md): how to change ViperIDE safely.
- [docs/testing.md](docs/testing.md): probes, CTest entries, manual checks, and
  coverage gaps.

## Current Scope

ViperIDE is currently a project-aware code editor and workbench for Zia and
Viper BASIC. It includes:

- Multi-tab text editing backed by `Viper.GUI.CodeEditor`.
- Zia completion, diagnostics, hover, signature help, document symbols,
  definition, references, rename, and selected refactors.
- BASIC completion, diagnostics, hover, and document symbols.
- Project explorer, multi-root workspace support, Quick Open, project search,
  workspace symbols, and recent files.
- Build and run jobs through `Viper.System.Process`.
- VM-backed debug adapter launch through `viper run --debug-adapter`.
- Integrated PTY terminal through `Viper.System.Pty`.
- Lightweight Git source-control view.
- Session restore, settings persistence, external-change detection, and
  crash-recovery snapshots for small modified text buffers.

The app is not yet a full visual scene editor. `.scene` and `.level` files are
recognized as scene documents, but they still open in the text editor. There is
no `Viper.GUI.SceneView`, no scene-specific document state, and no visual tile
or object-editing surface in ViperIDE today.

## Reading The Documentation

Start with [docs/status.md](docs/status.md) when deciding whether a feature is
real, partial, or absent. That document is deliberately blunt about limitations
because ViperIDE has historically had documentation that sounded more complete
than the implementation.

Use [docs/workflows.md](docs/workflows.md) when you are using the app or trying
to reproduce a user-visible workflow. It explains how project opening, editing,
search, build/run, debugging, terminal, Source Control, settings, and recovery
behave from the user's point of view.

Use [docs/architecture.md](docs/architecture.md) for the high-level ownership
model and [docs/source-map.md](docs/source-map.md) for the practical module
tour. The architecture document explains the layering rules; the source map
names the files that currently implement each subsystem and the reasons to touch
them.

Use [docs/runtime-integration.md](docs/runtime-integration.md) before changing
anything that crosses the Zia/C runtime boundary. ViperIDE is unusually
dependent on runtime APIs for editor widgets, processes, PTYs, workspace edits,
language services, and future scene work.

Use [docs/maintenance.md](docs/maintenance.md) before adding features. It
documents the common change paths: adding commands, document kinds, language
features, panels, runtime APIs, settings, probes, and docs.

## Directory Layout

```text
viperide/
    README.md                  This overview.
    CMakeLists.txt             Native menu/runtime probe target.
    viper.project              Project manifest; entry point is src/main.zia.
    bin/                       Generated IDE binaries; ignored by git.

    docs/
        architecture.md         Contributor-facing source map.
        runtime-integration.md  Runtime API boundary.
        status.md               Current feature matrix and known gaps.
        testing.md              Automated and manual verification.
        workflows.md            Build, run, edit, debug, terminal, and SCM use.
        editor-first-class-...  Archived performance dogfood report.

    src/
        main.zia                App bootstrap, frame loop, and dispatch glue.
        app/                    Frame-loop helpers and shared app services.
        build/                  Build/run jobs, diagnostics, breakpoints, debug.
        commands/               Command catalog, registry, and handlers.
        core/                   Documents, projects, settings, session state.
        editor/                 Editor engine, semantic controllers, indexing.
        probes/                 Focused IDE probe entry points.
        scm/                    Git command layer and Source Control view.
        services/               File kinds, locations, text, workspace edits.
        terminal/               PTY session and integrated-terminal controller.
        tests/                  Local GUI/runtime probe source.
        ui/                     App shell, panels, activity bar, overlays.
        zia/                    Pure Zia formatting, scanning, bind/refactor code.
```

## Build

Build the full repository first when runtime or compiler changes are involved:

```sh
./scripts/build_viper_mac.sh
./scripts/build_viper_linux.sh
scripts\build_viper_win.cmd
```

Build only the IDE native binary from the repository root:

```sh
./scripts/build_ide.sh
```

On Windows:

```bat
scripts\build_ide_win.cmd
```

The IDE build scripts write `viperide/bin/viperide` or
`viperide\bin\viperide.exe` by default. They also write
`viperide.buildinfo` beside the generated binary and refresh a compatibility
copy under `build/viperide/` unless `VIPER_IDE_SKIP_COMPAT_COPY=1` is set.

Useful IDE build variables:

| Variable | Effect |
| --- | --- |
| `VIPER_IDE_OUTPUT` | Override the output binary path. |
| `VIPER_IDE_COMPAT_OUTPUT` | Override the compatibility-copy path. |
| `VIPER_IDE_SKIP_COMPAT_COPY=1` | Skip the compatibility copy. |
| `VIPER_BINARY` | Force ViperIDE build/run jobs to use a specific `viper` binary. |
| `VIPER_BUILD_DIR` | Help ViperIDE locate a developer build tree. |

## Run

Run the app through the repository toolchain:

```sh
viper run viperide/
```

Or from inside `viperide/`:

```sh
viper run .
```

After `./scripts/build_ide.sh`, run the native binary directly:

```sh
./viperide/bin/viperide
```

## Project Configuration

ViperIDE reads a small `viper.project` manifest for project metadata, file
associations, build/run overrides, and ignore rules. The IDE's own manifest is
`viperide/viper.project`.

Supported build/run override keys:

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

`build-args` and `run-args` are split into an argument vector. Quoted spans are
preserved, backslash escapes are handled, and `{target}` is replaced with the
project root or active file path. Commands are not run through a shell.

Project ignore keys (`ignore`, `ignore-patterns`, `exclude`) are forwarded to
the runtime workspace index and apply to the explorer, Quick Open, search, and
workspace indexing.

## Settings And Session Files

Settings are stored in a platform config directory:

- Windows: `%APPDATA%\ViperIDE\settings.ini`
- macOS: `~/Library/Application Support/ViperIDE/settings.ini`
- Linux: `$XDG_CONFIG_HOME/viperide/settings.ini` or
  `~/.config/viperide/settings.ini`

If no platform-native settings file exists, existing legacy
`~/.viperide/settings.ini` files are still read.

The same INI file stores:

- `[settings]` editor and workbench settings.
- `[session]` last project, open tabs, active tab, cursor, and scroll state.
- `[session.file.N]` per-file session entries and bounded crash-recovery text.
- `[recent]` recent projects.
- `[recentFiles]` recent files.

## Verification

The most relevant focused tests are documented in [docs/testing.md](docs/testing.md).
Common targeted runs after a repository build are:

```sh
ctest --test-dir build -R zia_smoke_viperide_project_compile --output-on-failure
ctest --test-dir build -L viperide --output-on-failure
ctest --test-dir build -R zia_viperide_terminal --output-on-failure
ctest --test-dir build -R zia_viperide_debug --output-on-failure
```

Display-dependent probes are labelled `requires_display`. Run them only in an
environment with the GUI runtime available.
