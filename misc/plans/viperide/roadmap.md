# ViperIDE Roadmap

## Goal

ViperIDE should become the default way to build Viper projects: a credible code editor for Zia and BASIC, a project-aware IDE with build/run/debug workflows, and a visual scene editor for Viper's game runtime.

The current app is a useful demo, but the hard work is not just adding panels. The gaps are document safety, structured workspace state, project-aware language services, async process control, scene data ownership, asset resolution, and interaction testing.

## Current Assessment

The IDE is a single-frame Zia app in `examples/apps/viperide/main.zia`. It registers shortcuts and command-palette entries near `main.zia:123-180`, dispatches commands through a linear if-chain around `main.zia:375-455`, tracks dirty state at `main.zia:557-568`, and watches only the active file at `main.zia:573-610`.

Strong pieces already exist:

- Multi-tab text documents, a file tree, tab bar, breadcrumb, find bar, minimap, settings, and status bar.
- Zia completion, diagnostics, hover, and symbols through `Viper.Zia.Completion.*ForFile`.
- A capable `CodeEditor` widget with existing runtime methods for multiple cursors (`runtime.def:8663-8671`).
- Runtime file writes that already aim to replace files atomically (`rt_file_ext.c` documents this), though ViperIDE does not yet provide good save error handling or conflict UX.

High-risk gaps:

- Exit and OS-window close do not sweep unsaved documents. `handleExit` destroys the shell directly, and the main loop exits on `shell.ShouldClose()` without using `App.SetPreventClose` / `WasCloseRequested`.
- `Document` is text-only. There is no document kind, scene document state, raw-source fallback, or non-code save path.
- Search, diagnostics, references, and build output use display strings or line-only data instead of structured locations.
- Build/run uses blocking shell strings (`Exec.ShellFull`) with a hard-coded `zia` command.
- Project loading eagerly walks the tree, has hardcoded excludes, and has no workspace manifest beyond a small `viper.project` parser.
- Zia language services are single-file oriented. BASIC has editor/build support but not equivalent IntelliSense.
- Scene editing depends on data and rendering contracts that are not yet settled: `Viper.Game.Scene` mutators, tile ID semantics, asset paths, diagnostics, scaled tilemap drawing, and save ownership.

## Corrected Decisions

1. Scene data is built on `Viper.Game.Scene`, not new `LevelData` mutators. `LevelData` may remain a legacy compatibility loader.
2. `DocumentKind` is necessary but insufficient. Every document kind must plug into dirty tracking, close prompts, save, reload, session restore, and external-change checks.
3. Runtime and IDE APIs should pass structured records or stable object handles. Do not add more tab-separated or colon-separated contracts unless there is no viable structured alternative.
4. Build/run/debug must move away from blocking shell strings. The minimum acceptable step is argument-vector execution and project run configs; the full goal is cancellable jobs with streamed output.
5. Scene editor implementation must be staged. A viewer that safely opens and saves scene files is a different increment from tile painting, object editing, property inspection, and play integration.
6. Accessibility, keyboard access, dark-theme contrast, and cross-platform behavior are acceptance criteria for each phase.

## Phases

### Phase 0 - Foundations and Data Safety

Prepare the app for new surfaces and long-lived work:

- Command registry that fixes existing command id drift and removes the scaling risk in `main.zia`.
- `DocumentKind` plus surface selection for code, scene, and future special documents.
- Close prevention and unsaved-document sweeps for File > Exit, OS-window close, tab close, and future close-all commands.
- Correct cursor/scroll persistence and active-document save state before every command that can switch, close, run, or save.
- Structured session restore, recent projects, and structured locations for search/diagnostics/output.
- Clickable project search without `path:line` parsing.
- File filters and language/kind detection for `.zia`, `.bas`, `.vb`, `.json`, `.scene`, and `.level`.

### Phase 1 - Code Editor and Project Intelligence

Turn the editor into a project-aware coding environment:

- Add an IDE-side language-service abstraction so Zia, BASIC, text, and scene source modes have explicit capabilities.
- Build a Zia project index with explicit root, source provider, dirty-buffer updates, reference ranges, and workspace-edit transactions.
- Replace string-parsed go-to-definition with structured definition/reference results.
- Add find references and rename only after reference-range and shadowing tests exist.
- Wire existing multi-cursor runtime methods into real commands and UX.
- Add structured signature help instead of parsing hover text.
- Add tree rename/delete/move with open-document updates and a documented `.gitignore` subset or a proper matcher.

### Phase 2 - Run, Console, and Debug

Close the inner loop in stages:

- Safe project-aware build/run commands using argument vectors, working directory, environment, run configs, and problem matchers.
- A real output console with structured clickable locations.
- A cancellable process/job model with streamed stdout/stderr if the runtime surface is not already sufficient.
- Debug architecture based on an external debug protocol or adapter unless an in-process VM host is proven safe.
- Debug UI only after launch configs, breakpoint persistence, source mapping, and process lifetime rules are defined.

### Phase 3 - Scene Data Foundation

Implement the runtime scene model described by `misc/plans/game/scene-system.md`, with IDE-specific prerequisites called out in [phase-3-scene-data.md](phase-3-scene-data.md):

- `Viper.Game.Scene` using the current `LoadJson` / `LoadFile` /
  `SaveFile` / `ToJson` surface, with optional shorter aliases only after they
  are registered.
- Canonical `.scene` JSON round-trip, including legacy scene-shape import.
- Live indexed object and tile/layer mutators that update scene-owned data, not
  a returned `Tilemap`.
- Typed scene and object property APIs.
- Optional `BuildTilemap()` render copy for viewport code.
- `DiagnosticRecords()` structured diagnostics, compatibility diagnostic strings,
  and last-error reporting for editor integration.
- Explicit extension and asset-path decisions.

### Phase 4 - Scene Viewport

Add a GUI widget for scene rendering and hit testing:

- Tilemap rendering with a proven scale strategy; `rt_tilemap_draw` alone only supports offsets.
- Pan/zoom, grid, markers, selection overlays, and tile hit testing without packed integer hacks.
- Runtime object lifetime checks appropriate for non-widget tilemap/scene pointers.
- Pixel tests against the mock graphics backend at multiple zoom levels.

### Phase 5 - Scene Editor UI

Deliver scene editing as smaller increments:

- 5A: scene viewer and load-error surface.
- 5B: scene document model, source/visual switching, save/reload/conflict handling.
- 5C: tile palette, layer list, paint/erase/fill, undo grouping.
- 5D: object placement, selection, gizmos, and inspector.
- 5E: play integration through Phase 2 run configs.

### Phase 6 - Hardening and Dogfood

Final pass over the entire product:

- End-to-end dogfood on a real Viper game, starting with a `.scene` version of
  Xenoscape's `buildDescent()` level and a spawn adapter in
  `examples/games/xenoscape/level.zia`.
- Accessibility, keyboard, command palette, theme, and high-contrast audit.
- Cross-platform smoke on macOS, Linux, and Windows.
- Docs and showcase updates that do not overclaim features.

## Verification Strategy

Every landed increment requires:

- `./scripts/build_viper.sh` or the platform-equivalent build command.
- Relevant `ctest` subset plus full `ctest --test-dir build --output-on-failure` before reporting a phase done.
- `./scripts/check_runtime_completeness.sh` after runtime API changes.
- Graphics-on and graphics-off builds for new graphics runtime bindings.
- Manual IDE interaction checklist for features that cannot yet be automated.

Specific gates:

- No unsaved-edit loss on tab close, File > Exit, OS close, project switch, or scene/code surface switch.
- No path parsing bugs on Windows drive-letter paths or filenames containing punctuation.
- No blocking UI for long builds, searches, indexing, scene loads, or game runs once Phase 2 job support exists.
- Scene edit -> save -> reload must round-trip through `Viper.Game.Scene`, not through a `Tilemap` render copy.
