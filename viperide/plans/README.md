# ViperIDE Implementation Plans

These plans describe how to turn ViperIDE into a first-class code editor, IDE, and scene editor for Viper. They are intentionally critical: each phase must remove real product risk, not just add visible features.

## Conventions

- Each phase is implemented as one or more always-green increments. A sub-increment may land before the whole phase is complete, but every landed increment must build, test, and preserve existing behavior.
- Runtime or GUI API work must update `runtime.def`, structured class bindings, graphics and non-graphics builds where applicable, and runtime-completeness checks.
- IDE features must use structured state. Do not encode paths, line numbers, diagnostics, references, or session state as ad hoc `path:line` strings.
- Data-loss prevention is a release gate for every phase. New document surfaces must participate in close prompts, dirty tracking, save, reload, session restore, and external-change handling.
- Cross-platform behavior, accessibility, keyboard access, and dark-theme contrast are per-phase acceptance criteria, not Phase 6 cleanup.
- UI smoke tests are not enough. Every phase needs at least one automated regression where practical plus a short manual interaction checklist.
- ViperIDE app source lives under `viperide/src/`. New Zia and C source files for the IDE should be added under that tree; keep the root `viperide/` directory for project metadata, docs, plans, and generated `bin/` output.

## Active Correction Plan

The active roadmap is now [editor-first-class-plan.md](editor-first-class-plan.md).
It freezes scene editor work until ViperIDE is a fast, credible code editor and
project IDE. The earlier phase files remain useful historical/context documents,
but their implementation status must not be read as product completion.

## Phase Map

| # | Document | Goal | Depends on |
|---|----------|------|------------|
| 0 | [phase-0-foundations.md](phase-0-foundations.md) | IDE safety, command dispatch, document surfaces, structured session/results | none |
| 1 | [phase-1-editor-depth.md](phase-1-editor-depth.md) | Project-aware code intelligence, multi-cursor UX, signature help, tree operations | 0 |
| 2 | [phase-2-run-debug.md](phase-2-run-debug.md) | Safe run/build jobs, output console, launch configs, debugger protocol | 0 |
| 3 | [phase-3-scene-data.md](phase-3-scene-data.md) | `Viper.Game.Scene` round-trip runtime data foundation | none |
| 4 | [phase-4-scene-viewport.md](phase-4-scene-viewport.md) | `Viper.GUI.SceneView` rendering, hit testing, pan/zoom, markers | Phase 3 scene runtime where scene handles are used; tilemap-only work can start from scaled tilemap primitives |
| 5 | [phase-5-scene-editor-ui.md](phase-5-scene-editor-ui.md) | Staged docked scene editor: viewer, document model, tile tools, objects, inspector, play | 0, Phase 2 run-config/process integration for Play, Phase 3 acceptance criteria, 4 |
| 6 | [phase-6-polish.md](phase-6-polish.md) | Final hardening, dogfood, docs, platform/accessibility audit | all |

Supporting plan: [runtime-prerequisites.md](runtime-prerequisites.md) lists runtime/toolkit investments that can shrink the IDE phases when implemented first.

## Sequencing

Phase 0 is the first dependency because the current main loop, document model, close behavior, and stringly typed navigation data will not scale. Phase 1 and Phase 2 were the original code-editor and IDE loop tracks, but the current product bar has been reset by the editor-first plan. Phase 3 runtime/data work may remain available as a dependency, but Phase 4 and Phase 5 scene editor work are blocked until the editor-first release gates pass.

## Status

- **Phase 0:** Implemented in the IDE app. The app now has a shared command
  registry, document kinds, close prevention, structured location ids,
  session restore, recent-project persistence, safer save/close paths,
  configurable search options with navigation through `LocationStore`, and
  file-kind filters for code, text, scene, and unsupported binary files.
- **Phase 1:** Infrastructure landed, but the code editor is not product-complete.
  The app has capability routing, a `Viper.Zia.ProjectIndex`, structured
  definition/reference plumbing, rename edit application, multi-cursor commands,
  basic signature help, and some file-tree operations. It still needs the
  editor-first correction plan for responsiveness, real completion behavior,
  structured signature help, refactor preview, project explorer UX, and daily
  coding quality.
- **Phase 2:** Partially implemented for the current IDE/runtime boundary. Build and run
  now use `RunConfig` argument vectors, project manifest overrides,
  `Viper.System.Process` streaming, cancellable jobs, close/save-aware
  preflight saves, clickable output diagnostics, active-job stop prompts, and
  stop controls. The IDE also persists breakpoints, paints breakpoint gutter
  icons, and drives the existing headless `Viper.Debug.Protocol` placeholder
  with start/continue/step-over/pause and stop commands. That protocol is not a
  real program debugger: it interprets source text heuristically and does not
  execute compiled code, evaluate expressions, or expose VM stack frames. A
  separate external debug-adapter process or hosted VM debugger remains future
  work. The output console remains below product quality and is covered by the
  editor-first plan.
- **Phase 3:** Implemented as an IDE integration gate over the current
  `Viper.Game.Scene` runtime surface. The focused probe verifies canonical
  `.scene` save/load, scene-owned tile/layer/object/property mutators,
  `BuildTilemap()` render-copy creation, and non-trapping structured load
  diagnostics before Phase 5 scene-editor UI depends on them.
- **Phase 4:** Not implemented. Runtime prerequisites now exist
  (`Tilemap.DrawScaled`, `CountDrawnVisibleScaled`, `HitTestScaled`, and
  `Scene.BuildTilemap()`), but there is no `Viper.GUI.SceneView`, no
  `vg_sceneview` widget, no runtime binding/stub, and no SceneView render or
  hit-test gate.
- **Phase 5:** Not implemented. ViperIDE recognizes `.scene` / `.level`
  document kinds and routes them through the general document/session/close
  machinery, but there is no `viperide/src/scene_editor/`, no real scene
  surface, no `SceneDocumentState`, no scene save/reload path, no asset UX,
  no tile/object tools, and no Play integration.
- **CTest:** `zia_viperide_phase0_phase1` is the focused regression gate for
  Phases 0/1. `zia_viperide_phase2_phase3` is the focused gate for run/debug
  jobs, project-entry execution, stop/kill behavior, persisted breakpoints, the
  placeholder debug protocol boundary, and scene data contracts.
  `zia_smoke_viperide_project_compile` remains the project compile smoke gate.

Later phases remain plans until the editor-first plan is complete and their phase
documents are re-reviewed from the corrected baseline.
