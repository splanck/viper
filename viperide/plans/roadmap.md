# ViperIDE Roadmap

## Goal

ViperIDE should become the default way to build Viper projects: a credible code editor for Zia and BASIC, a project-aware IDE with build/run/debug workflows, and a visual scene editor for Viper's game runtime.

The current app is useful, but the hard work is not just adding panels. The gaps are document safety, structured workspace state, project-aware language services, async process control, scene data ownership, asset resolution, and interaction testing.

## Current Assessment

**Roadmap reset:** the active plan is now
[editor-first-class-plan.md](editor-first-class-plan.md). Scene editor work is
blocked until ViperIDE is a fast, credible code editor and project IDE. Phases 0
through 3 produced useful infrastructure, but the product-level result is not
first-class yet: IntelliSense is basic, signature help is unreliable at call
sites, editor responsiveness has regressed, the console is not a real console
surface, the file tree and refactor UX need work, and the visual design still
reads like a demo.

The current app has a command registry, document kinds, close-prevention flow,
structured locations, session restore, language-service capability routing,
project-index-backed Zia definition/reference/rename plumbing, multi-cursor
commands, basic signature help, transactional workspace edits, configurable
project search, file-tree rename/delete handling, argument-vector build/run jobs,
streamed cancellable process output, clickable build diagnostics, persisted
breakpoints, headless placeholder debug-protocol controls, and a verified
`Viper.Game.Scene` data foundation for future scene editor work. That is
foundation, not product completion.

The debug protocol currently wired in ViperIDE does not execute real programs or
inspect real VM state. Phase 4 and later scene items below remain future roadmap
work and must be re-reviewed after the editor-first plan is complete.

**Phase 4/5 review update:** Scene data/runtime prerequisites are ready, but
the viewport and editor are not. There is no `Viper.GUI.SceneView` widget, and
ViperIDE's current `AppShell.SelectSurface(kind)` only records the document
kind while leaving the code editor visible. `.scene` / `.level` files are
recognized as scene documents, but they still open as text buffers; no
`Viper.Game.Scene` handle is cached by the IDE, no scene-specific dirty/save
path exists, and no visual tools are mounted.

The IDE is still a single-frame Zia app in `viperide/src/main.zia`, but the
Phase 0/1 work moved command metadata, session state, structured locations,
language-service capability checks, and project-index ownership into dedicated
modules under `viperide/src/commands`, `viperide/src/core`,
`viperide/src/editor`, and `viperide/src/services`.

Strong pieces already exist:

- Multi-tab text documents, a file tree, tab bar, breadcrumb, find bar, minimap, settings, and status bar.
- Zia completion, hover, symbols, and signature help through `Viper.Zia.Completion.*ForFile`, plus live diagnostics through structured `Viper.Zia.Toolchain.CheckForFile` records.
- A capable `CodeEditor` widget with existing runtime methods for multiple cursors (`runtime.def:8964-8976`).
- Runtime prerequisites now available for IDE integration: `Viper.System.Process`, `Viper.Zia.ProjectIndex`, `Viper.Workspace.FileIndex`, `Viper.Project.Manifest`, `Viper.Workspace.Edit`, `Viper.Game.Scene`, scaled `Tilemap` draw/hit-test primitives, GUI test/virtual-list helpers, fuzzy match, and debug protocol shells.
- Runtime file writes that already aim to replace files atomically (`rt_file_ext.c` documents this); ViperIDE now warns before overwriting files that changed on disk.

High-risk gaps:

- Editor typing, selection, and scrolling must not pay whole-buffer or semantic
  costs in the frame loop.
- Completion and signature help need structured, incremental, project-aware
  behavior before the editor can be considered serious.
- Refactor workflows need preview, conflicts, all-or-nothing application, and
  undo grouping.
- The project tree needs complete right-click workflows and path-safe operations.
- Console, Problems, Search, and Output need real work surfaces instead of
  listbox-style dumps.
- The settings and overall shell UX need a polish pass so the app no longer
  feels like a demo.
- Project loading eagerly walks the tree, has hardcoded excludes, and has no workspace manifest beyond a small `viper.project` parser.
- BASIC has editor/build support in ViperIDE, while the separate `vbasic-server` IntelliSense surface is not yet integrated.
- The debugger uses the existing headless `Viper.Debug.Protocol` placeholder inside the IDE process. It is useful for UI and command-state wiring, but it does not execute compiled code, evaluate expressions, or expose real VM frames. A crash-isolated external adapter/subprocess debugger or hosted VM debugger is still future work.
- Scene editing still needs IDE integration work beyond document-kind routing:
  a real scene surface, `SceneView`, `SceneDocumentState`, asset-resolution UX,
  undo/redo, save/reload/conflict handling, and Play wiring. The underlying
  `Viper.Game.Scene`, tile ID convention, diagnostics, asset descriptors,
  atomic save, typed properties, asset resolver, expanded manifest parser, and
  scaled tilemap primitives are available in the current runtime.

## Corrected Decisions

1. Scene data is built on `Viper.Game.Scene`, not new `LevelData` mutators. `LevelData` may remain a legacy compatibility loader.
2. `DocumentKind` is necessary but insufficient. Every document kind must plug into dirty tracking, close prompts, save, reload, session restore, and external-change checks.
3. Runtime and IDE APIs should pass structured records or stable object handles. Do not add more tab-separated or colon-separated contracts unless there is no viable structured alternative.
4. Build/run/debug must move away from blocking shell strings. The minimum acceptable step is argument-vector execution and project run configs; the full goal is cancellable jobs with streamed output.
5. Scene editor implementation must be staged. A viewer that safely opens and saves scene files is a different increment from tile painting, object editing, property inspection, and play integration.
6. Accessibility, keyboard access, dark-theme contrast, and cross-platform behavior are acceptance criteria for each phase.

## Phases

### Active Plan - First-Class Code Editor

Before any scene editor work, complete
[editor-first-class-plan.md](editor-first-class-plan.md):

- E0: editor responsiveness and revision-based scheduling.
- E1: completion and IntelliSense recovery.
- E2: structured signature help and hover.
- E3: diagnostics, Problems, and code actions.
- E4: project navigation and refactoring.
- E5: file tree and workspace UX.
- E6: console, search, and tool panels.
- E7: settings and preferences.
- E8: visual design and interaction polish.
- E9: BASIC and multi-language honesty.

Only after that plan's dogfood gate passes should this roadmap resume Phase 4/5
scene editor planning.

### Phase 0 - Foundations and Data Safety

Prepare the app for new surfaces and long-lived work:

- Command registry that fixes existing command id drift and removes the scaling risk in `viperide/src/main.zia`.
- `DocumentKind` plus surface selection for code, scene, and future special documents.
- Close prevention and unsaved-document sweeps for File > Exit, OS-window close, tab close, and future close-all commands.
- Correct cursor/scroll persistence and active-document save state before every command that can switch, close, run, or save.
- Structured session restore, recent projects, and structured locations for search/diagnostics/output.
- Clickable project search without `path:line` parsing.
- File filters and language/kind detection for `.zia`, `.bas`, `.vb`, `.json`, `.scene`, and `.level`.

### Phase 1 - Code Editor and Project Intelligence

Turn the editor into a project-aware coding environment:

- Add an IDE-side language-service abstraction so Zia, BASIC, text, and scene source modes have explicit capabilities.
- Integrate the existing `Viper.Zia.ProjectIndex` runtime API with explicit root, source provider, dirty-buffer updates, reference ranges, and workspace-edit transactions.
- Replace string-parsed go-to-definition with structured definition/reference results.
- Add find references and rename only after reference-range and shadowing tests exist.
- Wire existing multi-cursor runtime methods into real commands and UX.
- Add structured signature help instead of parsing hover text.
- Add tree rename/delete/move with open-document updates and a documented `.gitignore` subset or a proper matcher.

### Phase 2 - Run, Console, and Debug

Implemented for the current IDE/runtime boundary:

- Safe project-aware build/run commands using argument vectors, working directory, environment, run configs, and problem matchers.
- Output rows backed by structured clickable locations for diagnostics.
- IDE job ownership built on the existing `Viper.System.Process` runtime API, with streamed stdout/stderr, cancellation, and UI state.
- Persisted breakpoints with gutter toggling and debug toolbar/menu commands.
- Placeholder `Viper.Debug.Protocol` launch, continue, step-over, pause, stop, simulated stack/locals display, and crash/exit event reporting.
- Real debugging through an external process adapter or hosted VM remains future work.

### Phase 3 - Scene Data Foundation

Implemented and verified as the IDE-facing runtime data foundation described by `misc/plans/game/scene-system.md`, with IDE-specific prerequisites called out in [phase-3-scene-data.md](phase-3-scene-data.md):

- `Viper.Game.Scene` using the current `LoadJson` / `LoadFile` /
  `SaveFile` / `ToJson` surface.
- Canonical `.scene` JSON round-trip, including legacy scene-shape import.
- Live indexed object and tile/layer mutators that update scene-owned data, not
  a returned `Tilemap`.
- Typed scene and object property APIs.
- `BuildTilemap()` render copy for viewport code.
- `DiagnosticRecords()` structured diagnostics, compatibility diagnostic strings,
  and last-error reporting for editor integration.
- Explicit extension and asset-path decisions.

### Phase 4 - Scene Viewport

Planned, not implemented. Add a GUI widget for scene rendering and hit testing:

- `Viper.GUI.SceneView` / `vg_sceneview` widget and runtime binding.
- Tilemap rendering built on the existing scaled tilemap draw/count/hit-test primitives.
- Pan/zoom, grid, markers, selection overlays, and tile hit testing without packed integer hacks.
- Runtime object lifetime checks appropriate for non-widget tilemap/scene pointers.
- Real render/pixel tests at multiple zoom levels plus graphics-off stubs.

### Phase 5 - Scene Editor UI

Planned, not implemented. Deliver scene editing as smaller increments:

- 5A0: scene surface and `SceneDocumentState` skeleton; source fallback and structured load errors.
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

- `./scripts/build_viper_mac.sh`, `./scripts/build_viper_linux.sh`, `./scripts/build_viper_unix.sh`, or `scripts/build_viper_win.cmd` as appropriate for the platform.
- For ViperIDE app changes, `./scripts/build_ide.sh` or `scripts/build_ide_win.cmd`; generated IDE binaries should stay under ignored `viperide/bin/`.
- Relevant `ctest` subset plus full `ctest --test-dir build --output-on-failure` before reporting a phase done.
- `./scripts/check_runtime_completeness.sh` after runtime API changes.
- Graphics-on and graphics-off builds for new graphics runtime bindings.
- Manual IDE interaction checklist for features that cannot yet be automated.

Specific gates:

- No unsaved-edit loss on tab close, File > Exit, OS close, project switch, or scene/code surface switch.
- No path parsing bugs on Windows drive-letter paths or filenames containing punctuation.
- No blocking UI for long builds, searches, indexing, scene loads, or game runs once Phase 2 job support exists.
- Scene edit -> save -> reload must round-trip through `Viper.Game.Scene`, not through a `Tilemap` render copy.
