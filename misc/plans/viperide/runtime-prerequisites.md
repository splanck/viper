# Runtime Prerequisites for ViperIDE

## Context

ViperIDE should stay mostly Zia application code, but several planned features become brittle if the IDE has to compensate for missing runtime/toolkit primitives. This document reviews the useful prerequisites and expands them into a prioritized backlog.

This is not a mandatory phase. Each item should land only when it is the cheapest way to make a ViperIDE phase correct. The goal is to avoid building complex IDE behavior on shell strings, display-string parsing, single-file analysis, or one-off GUI widgets.

## Priority Tiers

- **Tier 0 - Audit first:** cheap checks that prevent false assumptions.
- **Tier 1 - Hard blockers:** features that block safe run/build, project intelligence, and scene editing.
- **Tier 2 - Strong accelerators:** not strictly required, but they shrink IDE code and reduce future rework.
- **Tier 3 - Nice accelerators:** useful after the core editor and scene editor are stable.

## Implementation Progress

- **2026-05-22 - R1 implementation landed.** Added `Viper.GUI.CodeEditor.ScrollTopLine` as a typed get/set property, including GUI widget helpers, runtime bridge functions, headless stubs, runtime catalog registration, and a runtime regression test. Added indexed selection range getters (`GetSelectionStartLineAt`, `GetSelectionStartColAt`, `GetSelectionEndLineAt`, `GetSelectionEndColAt`) with runtime regression coverage. Added widget-level `Shift`+click selection extension and `Ctrl`/`Cmd`+click secondary cursor placement with runtime regression coverage. ViperIDE now restores scroll using the property and saves live cursor/scroll state into the document instead of relying on stale cached cursor fields. The ViperIDE smoke CTest now probes Zia-facing hit-testing and cursor/scroll restoration across a simulated tab switch. Runtime docs were updated in `docs/viperlib/gui/containers.md` and `docs/bible/appendices/d-runtime-reference.md`.
- **Verified:** `cmake --build build --target test_rt_gui_runtime -j`, `cmake --build build --target viper zia -j`, `./scripts/check_runtime_completeness.sh`, `ctest --test-dir build --output-on-failure -R '^test_rt_gui_runtime$'`, `ctest --test-dir build --output-on-failure -R '^test_runtime_classes_catalog$'`, `ctest --test-dir build --output-on-failure -R '^zia_smoke_viperide_project_compile$'`, `ctest --test-dir build --output-on-failure -R '^zia_smoke_viperide$'`, and `git diff --check`.
- **R1 still open:** no known Phase 0/1 blockers. Future GUI automation should still add event-level interaction tests once the GUI test harness from R10 exists.
- **2026-05-22 - R2 process slice implementation landed.** Added `Viper.System.Process` and `Viper.System.Process.Handle` with args-based direct process start, cwd/env support, non-blocking incremental stdout/stderr reads, polling, running state, exit-code retrieval, termination, wait, destroy, GC finalization, runtime catalog registration, native-link component/import policy mapping, and public runtime docs in `docs/viperlib/system.md`. Fixed the existing `Viper.System.Exec.*Args` path to accept Zia/Object-ABI boxed strings in argument sequences instead of only raw C `rt_string` entries. Added focused runtime CTest coverage for streaming stdout/stderr before exit, cwd/env propagation, empty non-blocking reads, termination, and boxed-string args.
- **Verified:** `cmake --build build --target test_rt_exec viper zia -j`, `cmake --build build --target test_linker_runtime_import_audit viper zia -j`, `./scripts/check_runtime_completeness.sh`, `ctest --test-dir build --output-on-failure -R '^test_rt_exec$'`, `ctest --test-dir build --output-on-failure -R '^test_runtime_classes_catalog$'`, `ctest --test-dir build --output-on-failure -R '^zia_smoke_viperide_project_compile$'`, `ctest --test-dir build --output-on-failure -R '^(native_smoke_viperide_completion_arm64|test_linker_runtime_import_audit)$'`, and `git diff --check`.
- **R2 accepted follow-ups:** the required process prerequisite is implemented. A generic `Viper.Job` wrapper remains optional product polish above `Process`/`Threads.Future`; Windows/Linux kill and streaming behavior still need CI or machine coverage beyond the current macOS run.
- **2026-05-22 - R3 structured diagnostics slice implementation landed.** Added `Viper.Zia.Toolchain` with `Check`, `CheckForFile`, `Compile`, and `CompileForFile` returning `Seq`/`Map` structured records instead of tab-delimited text. Diagnostic maps include normalized file, line/column range, severity, code, message, stage, and help. Compile maps include success, diagnostics, sourcePath, outputPath, and serialized IL on success. Weak runtime stubs now provide shape-compatible empty diagnostics/failed compile results when `fe_zia` is not linked. ViperIDE live diagnostics now consumes structured records directly, stores diagnostic path+line in list item data, and diagnostic navigation can open a different file before jumping to the diagnostic line. Runtime docs were added in `docs/viperlib/zia.md`.
- **Verified:** `cmake --build build --target test_rt_zia_completion_stub viper zia -j`, `ctest --test-dir build --output-on-failure -R '^test_rt_zia_completion_stub$'`, `ctest --test-dir build --output-on-failure -R '^test_runtime_classes_catalog$'`, `ctest --test-dir build --output-on-failure -R '^native_smoke_viperide_completion_arm64$'`, `ctest --test-dir build --output-on-failure -R '^zia_smoke_viperide_project_compile$'`, `ctest --test-dir build --output-on-failure -R '^test_linker_runtime_import_audit$'`, `ctest --test-dir build --output-on-failure -R '^test_runtime_surface_audit$'`, `./scripts/check_runtime_completeness.sh`, and `git diff --check`.
- **R3 accepted follow-ups:** the required structured Zia diagnostics/compile prerequisite is implemented. Project-wide compile, persisted output/module handles, BASIC-facing structured toolchain surface, and hosted VM run/debug remain future expansions outside the current runtime prerequisite gate.
- **2026-05-22 - R5/R8/R9/R13/R14 implementation landed.** Added `Viper.Workspace.FileIndex`, `Viper.Workspace.Watcher.PollBatch`, `Viper.Assets.Resolver`, `Viper.Project.Manifest`, `Viper.Workspace.Edit`, and `Viper.Text.FuzzyMatch`. These cover recursive workspace inventory with hard excludes and `.gitignore` subset support, per-frame watcher event batching, scene/project/mounted asset resolution with diagnostics, backward-compatible plus expanded `viper.project` parsing, all-or-nothing multi-file text edit validation/application, and reusable quick-open scoring/highlight ranges. Added `test_rt_ide_workspace`, `test_rt_fuzzy_match`, and the Zia-facing `zia_rt_api_test_viperide_primitives` probe.
- **2026-05-22 - R6/R7 implementation landed.** Added `Viper.Game.Scene` as an editable scene document with tile layers, object mutators, custom properties, JSON load/save, diagnostics, and asset path descriptors. Added scaled `Viper.Graphics.Tilemap.DrawScaled`, `CountDrawnVisibleScaled`, and `HitTestScaled` for scene viewports. Added `test_rt_scene_editor`.
- **2026-05-22 - R10/R11/R15 implementation landed.** Added `Viper.GUI.TestHarness`, `Viper.GUI.VirtualList`, `Viper.GUI.VirtualTree`, `Viper.GUI.CommandState`, and `Viper.GUI.Accessibility`. These provide deterministic widget lookup/input/focus/pixel snapshot helpers, large-list visible range and stable-id selection, lazy tree expansion/refresh, command enabled/checked/accessibility snapshots, contrast checks, and high-contrast tokens. Added `test_rt_gui_ide`.
- **2026-05-22 - R12 implementation landed.** Added `Viper.Debug.Protocol` and `Viper.Debug.Protocol.Session` as a headless debug command/event boundary with launch, terminate, pause, continue, step over, breakpoint management, stack frames, locals, event history, running state, and crash-as-event behavior. Added `test_rt_debug_protocol`.
- **Documentation updated:** `docs/viperlib/io/files.md`, `docs/viperlib/io/assets.md`, `docs/viperlib/text/README.md`, `docs/viperlib/text/patterns.md`, `docs/viperlib/game.md`, `docs/viperlib/game/README.md`, `docs/viperlib/graphics/pixels.md`, `docs/viperlib/graphics/tilemaps2d.md`, `docs/viperlib/gui/README.md`, `docs/viperlib/gui/application.md`, `docs/viperlib/diagnostics.md`, and `docs/release_notes/Viper_Release_Notes_0_2_6.md`.
- **Verified:** `cmake --build build --target test_rt_ide_workspace test_rt_fuzzy_match test_rt_scene_editor test_rt_gui_ide test_rt_debug_protocol viper_rt_io_fs test_runtime_classes_catalog test_linker_runtime_import_audit test_runtime_surface_audit zia -j`, `./scripts/check_runtime_completeness.sh`, `ctest --test-dir build --output-on-failure -R '^(test_rt_ide_workspace|test_rt_fuzzy_match|test_rt_scene_editor|test_rt_gui_ide|test_rt_debug_protocol|test_runtime_classes_catalog|test_linker_runtime_import_audit|test_runtime_surface_audit|zia_rt_api_test_viperide_primitives)$'`, `cmake --build build --target viper zia -j`, `ctest --test-dir build --output-on-failure -R '^(native_smoke_viperide_completion_arm64|zia_smoke_viperide_project_compile|zia_smoke_viperide)$'`, and `git diff --check`.
- **Remaining cross-platform validation:** graphics-off, Windows, and Linux CI coverage still needs to run on those build configurations. The macOS arm64 runtime, catalog, native-import, and Zia-surface checks are the local gate for this pass.

## R1 - CodeEditor API Audit

Tier: 0.

Review:

- This is one of the most useful items because it is cheap and corrects prior assumptions.
- Many useful APIs are already exposed through `Viper.GUI.CodeEditor`: highlights, gutter icons, fold regions, and multiple cursors.
- The missing work is mostly verifying state APIs before Phase 0 and Phase 1 claim cursor persistence, scroll restore, breakpoint placement, or modifier-click multi-cursor UX.

Audit and add only if missing:

- live primary cursor line/column getters: **already present** as `CursorLine` and `CursorCol`.
- scroll/top-line getter and setter: **implemented** as `ScrollTopLine`.
- selection range enumeration: **implemented** through indexed selection range getters.
- mouse coordinate to line/column hit testing: **covered** by runtime pixel helper tests and the ViperIDE Zia smoke probe.
- modifier-click state needed for extra cursor placement: **implemented** for `Ctrl`/`Cmd`+click with `Shift`+click selection extension.
- stable scroll/cursor behavior across tab switches: **covered** by the ViperIDE Zia smoke probe.

Unblocks:

- Phase 0 cursor/scroll restore.
- Phase 1 multi-cursor UX.
- Phase 2 breakpoint placement.

Verification:

- Small runtime/Zia probe that moves cursor, scrolls, switches buffers, and reads back state.
- Multi-cursor probe using existing exposed cursor methods.

## R2 - Process and Job Runtime

Tier: 1.

Review:

- This is the most immediately useful prerequisite for IDE quality.
- `Viper.System.Exec.*Args` avoids shell quoting bugs, but it is still blocking.
- A generic job wrapper is helpful, but it cannot replace child-process control for run/debug. ViperIDE needs process start, streaming output, exit code, and kill.

Add:

- `Viper.System.Process`:
  - start with program, args, cwd, env
  - poll status
  - read stdout incrementally
  - read stderr incrementally
  - kill/terminate
  - exit code
- Optional `Viper.Job` wrapper above process jobs and pure background jobs:
  - status
  - progress message
  - cancellation token
  - result/error

Unblocks:

- Phase 2 output console and stop button.
- Long-running run/debug sessions.
- Responsive build jobs.
- Responsive search/indexing if reused for background work.
- Phase 5 Play integration.

Verification:

- Run a process whose path contains spaces.
- Stream stdout/stderr while the process is still running.
- Kill an infinite process on macOS, Linux, and Windows.
- Confirm the GUI frame loop continues ticking while the process runs.

## R3 - Structured Toolchain and Diagnostics API

Tier: 1, but incremental.

Review:

- This is valuable, but it should not block the simpler Phase 2 improvement of moving from shell strings to args/process APIs.
- The first useful slice is structured compile diagnostics. Hosted in-process run/debug is larger and should be a separate decision.

Add:

- `Viper.Toolchain` or language-specific compiler APIs:
  - compile file
  - compile project
  - success flag
  - structured diagnostics
  - output path or IL/bytecode handle
  - source path normalization
- Diagnostic record:
  - file path
  - line
  - column
  - end line
  - end column
  - severity
  - code
  - message

Unblocks:

- Phase 0/1 structured diagnostics.
- Phase 2 build correctness.
- Cleaner Problems panel and output-console links.
- Future in-process Play/debug if a hosted VM is added later.

Verification:

- Compile a valid file and an invalid file.
- Invalid compile returns structured diagnostics without regex parsing.
- Diagnostic file paths open correctly from ViperIDE.

## R4 - Project Language Index

Tier: 1.

Status: Implemented as a runtime/tooling prerequisite on 2026-05-22.

Progress:

- Added `Viper.Zia.ProjectIndex` and opaque `Viper.Zia.ProjectIndex.Handle`.
- Added explicit lifecycle calls: `New`, `IsValid`, `UpdateFile`, `RemoveFile`, `Clear`, and `Destroy`.
- Added semantic `Definition`, `References`, and `RenameEdits` queries returning `Map`/`Seq` structures.
- Added an import source-provider hook to the Zia analyzer so indexed dirty buffers can satisfy relative `bind` imports without stale disk reads.
- Reference collection now lexes identifiers, excluding comments and string literals, and resolves symbols through Sema before reporting a match.
- Rename returns workspace edit maps and detects visible-name collisions before reporting success.
- Added `test_zia_project_index` coverage for two-file definition, shadowed references, rename collision, and dirty-buffer replacement.
- Documented the runtime API in `docs/viperlib/zia.md`.

Remaining IDE integration:

- ViperIDE still needs to own a long-lived project index, update it from document-open/save/edit events, and replace the old hover-plus-symbols go-to-definition fallback.
- Rename application UX is still pending: preview, conflict reporting, multi-file buffer edits, undo grouping, and save prompts.
- This is intentionally in-process and synchronous; larger workspaces may need batching/caching once the workspace file index lands.

Review:

- This is required for real go-to-definition, references, and rename.
- Existing Zia completion/check/hover/symbol APIs are source-driven single-file services. `Sema::findSymbolAtPosition` is not enough by itself for references or safe rename.

Add:

- `Viper.Zia.ProjectIndex` with explicit lifetime:
  - create for project root
  - update file from supplied source
  - remove file
  - clear/dispose
  - definition query
  - references query
  - rename edits query
- The source provider must accept dirty open-buffer text from the IDE.
- Results should be structured objects if practical. If bridge serialization is unavoidable, the IDE must immediately parse it into structured records.

Critical requirements:

- Reference collection is semantic, not string search.
- Shadowed locals, members, imports, and globals are distinguishable.
- Comments and strings are excluded.
- Rename returns a workspace edit, not already-applied disk changes.

Unblocks:

- Phase 1 definition.
- Phase 1 find references.
- Phase 1 rename.

Verification:

- Two-file definition test.
- Shadowed-symbol references test.
- Rename collision test.
- Dirty-buffer index update test.

## R5 - Workspace File Index, Ignore Rules, and Watcher Service

Tier: 1.
Status: Implemented for runtime/tooling use on 2026-05-22.

Progress:

- Added `Viper.Workspace.FileIndex.Enumerate` and `ShouldIgnore`.
- Added hard excludes plus a documented `.gitignore` subset with negation support.
- Added `Viper.Workspace.Watcher.PollBatch` over `Viper.IO.Watcher`.
- Added CTest coverage for ignore rules, file inventory, watcher batching, asset resolution, manifest parsing, and workspace edits.

Remaining:

- `Viper.IO.Watcher` is still non-recursive; ViperIDE should create per-directory watchers or rescan through the file index after overflow.
- Windows path/case behavior needs CI-machine validation.

Review:

- Viper has `Viper.IO.Glob` and `Viper.IO.Watcher`, but ViperIDE needs workspace-level behavior: recursive file inventory, ignore rules, and change batching.
- Without this, project tree, search, language indexing, and session restore will each invent their own partial filesystem logic.

Add:

- `Viper.Workspace.FileIndex` or equivalent runtime/toolkit helper:
  - enumerate files under root
  - include/exclude by extension and kind
  - apply hardcoded excludes
  - apply `.gitignore` or a clearly documented subset
  - produce stable file ids or canonical paths
- Recursive watcher service:
  - batch events per frame
  - report create/modify/delete/rename with paths
  - handle overflow by requesting a rescan

Unblocks:

- Project tree scalability.
- Project search.
- Project index invalidation.
- External-change detection for inactive files.
- Scene asset reload later.

Verification:

- Ignore pattern tests, including directories and negation if supported.
- Create/modify/delete/rename events under a project root.
- Overflow/rescan behavior.
- Windows path and case behavior.

## R6 - Scene Runtime Editor Primitives

Tier: 1.
Status: Implemented for editable scene documents on 2026-05-22.

Progress:

- Added the first `Viper.Game.Scene` editable-document baseline: scene-owned tile layers, indexed object mutators, string custom properties, asset path extraction, diagnostics, and JSON load/save.
- Added CTest coverage for the baseline tile/object/property mutation path, save/load, JSON round-trip, asset path extraction, and tile ID convention.

Remaining:

- Align the baseline with `misc/plans/game/scene-system.md`: canonical v1 `.scene` schema, safe non-trapping `DiagnosticRecords()` load diagnostics, atomic save, typed scene/object properties, object-property persistence, structured asset descriptors, schema fixture coverage, and Zia/BASIC smoke coverage.

Review:

- This is mandatory for the scene editor.
- The editor cannot safely mutate only a `Tilemap` render copy; scene-owned normalized data must remain the serialization source of truth.

Add through `Viper.Game.Scene`:

- scene-owned tile/layer get/set/fill APIs
- layer visibility/name/order APIs if layers are editor-visible
- live object handles or scene-owned object mutators
- add/remove/reorder object APIs
- property get/set/delete APIs
- canonical v1 `SaveFile` / `ToJson`
- `DiagnosticRecords()` load diagnostics plus compatibility strings/last error
- asset path descriptors
- documented tile ID convention

Unblocks:

- Phase 5 tile tools.
- Phase 5 layer panel.
- Phase 5 object editor.
- Phase 5 inspector.
- Reliable scene save/reload.

Verification:

- Mutate tile, object, and property, save, reload, and compare.
- Invalid scene returns useful diagnostics.
- Tile ID edge cases match the documented convention.

## R7 - Scene Rendering and GUI Draw Surface Primitives

Tier: 1.
Status: Implemented as scaled Tilemap draw/count/hit-test primitives on 2026-05-22.

Progress:

- Added `Viper.Graphics.Tilemap.DrawScaled`, `CountDrawnVisibleScaled`, and `HitTestScaled`.
- Added CTest coverage for pan/zoom hit testing and scene-editor tile math.

Remaining:

- A reusable `Viper.GUI.DrawSurface` widget is still optional; the implemented primitive covers the current scene viewport blocker without adding a new widget class.
- Graphics-off build validation still needs a dedicated CI run.

Review:

- `Viper.Graphics.Tilemap.Draw` currently takes offsets but no scale.
- `Viper.Graphics.Canvas` exists, but it is a standalone graphics surface, not a child GUI widget inside ViperIDE.
- SceneView needs a widget-local draw/input surface or a custom widget with equivalent tested behavior.

Add one of:

- scaled tilemap draw APIs
- a reusable `Viper.GUI.DrawSurface` widget with local input coordinates
- SceneView-internal scaled blitting with tests

Also expose:

- local mouse position
- wheel/pan input
- widget size
- pixel/screenshot access in tests

Unblocks:

- Phase 4 SceneView.
- Scene editor overlays and hit testing.
- Future custom graph/preview/editor widgets.

Verification:

- Nonblank render test at zoom 0.5, 1.0, and 2.0.
- Hit test under pan and zoom.
- Graphics-on and graphics-off builds.

## R8 - Asset Resolver for Editor and Runtime

Tier: 1 for scene editing, Tier 2 otherwise.
Status: Implemented on 2026-05-22.

Progress:

- Added `Viper.Assets.Resolver.Resolve` with absolute, scene-relative, project-root, asset-root, and mounted-asset lookup.
- Missing assets return structured diagnostics instead of trapping.
- Added CTest coverage for scene-relative, asset-root, and missing-asset paths.

Remaining:

- Watch-driven hot reload remains ViperIDE integration work.

Review:

- `Viper.IO.Assets.Load` exists, but editor workflows need a resolver, not just a load call.
- Scenes need to reference tilesets and other assets in a way that works from the project tree, packaged assets, and later build outputs.

Add:

- `Viper.Assets.Resolver` or project-level asset resolver:
  - scene-relative lookup
  - project-root relative lookup
  - mounted asset lookup through `Viper.IO.Assets`
  - existence check
  - canonical display path
  - missing-asset diagnostic
- Optional watch integration for asset reload.

Unblocks:

- Phase 5 tile palette.
- Missing asset placeholders.
- Project packaging/build correctness later.

Verification:

- Resolve the same asset through each supported root.
- Missing asset returns structured diagnostic.
- Spaces and case differences work cross-platform.

## R9 - Project Manifest and Run Configuration Model

Tier: 2.
Status: Implemented parser/runtime model on 2026-05-22.

Progress:

- Added `Viper.Project.Manifest.ParseText` and `ParseFile`.
- Supports current simple `viper.project` directives plus source globs, excludes, asset roots, scene roots/default scene, and `[run.*]`/`[build.*]` sections.
- Invalid content returns diagnostics and safe defaults.

Remaining:

- ViperIDE still needs manifest authoring UX and migration policy if the file format grows beyond line directives.

Review:

- The current `viper.project` parser is small and only covers basic metadata.
- A first-class IDE needs a source of truth for project entry, language, source globs, excludes, asset roots, build tasks, run configs, and scene defaults.

Add:

- Runtime/toolkit parser for an expanded `viper.project` or a new project manifest format.
- Structured fields:
  - project name
  - language
  - entry file
  - source globs
  - excludes
  - asset roots
  - build configs
  - run configs
  - scene roots/default scene
- Backward compatibility with current simple `viper.project`.

Unblocks:

- Phase 2 run configs.
- Phase 3/5 scene asset roots.
- Project search and indexing scope.

Verification:

- Parse old and new manifests.
- Invalid manifest returns diagnostics and safe defaults.

## R10 - GUI Test and Automation Hooks

Tier: 2.
Status: Implemented headless harness primitives on 2026-05-22.

Progress:

- Added `Viper.GUI.TestHarness` for named widget registration, lookup by id/name/type, key/mouse dispatch, frame ticking, focus inspection, region capture, and nonblank snapshot assertion.
- Added CTest and Zia rt-api coverage.

Remaining:

- Full ViperIDE smoke tests should now migrate from construction-only probes to interaction flows using these helpers.

Review:

- Current ViperIDE smoke tests mostly prove construction, not interaction.
- IDE work needs repeatable click/key/selection/pixel tests without relying only on manual testing.

Add:

- GUI test helpers:
  - find widget by id/name/type
  - send key/mouse/wheel events
  - tick frames
  - inspect focus and selection
  - capture widget or window pixels
  - assert nonblank regions
- A stable way for Zia probes to name important widgets.

Unblocks:

- Command palette regression tests.
- Search-result click tests.
- Close prompt tests.
- SceneView render/hit tests.
- Scene editor smoke automation.

Verification:

- Automated test opens ViperIDE, triggers a command, clicks a list result, and checks state.
- Pixel assertion detects blank SceneView.

## R11 - Virtualized List/Tree/Result Widgets

Tier: 2.
Status: Implemented runtime model primitives on 2026-05-22.

Progress:

- Added `Viper.GUI.VirtualList` with visible range calculation and stable selection by id.
- Added `Viper.GUI.VirtualTree` with lazy expand/collapse, visible row generation, selection, and dirty subtree refresh.
- Added 10k-row list and lazy-tree CTest coverage.

Remaining:

- Rendering adapters for concrete ViperIDE widgets are application work above these runtime models.

Review:

- Current `ListBox` and `TreeView` are enough for small demos, but IDE use will produce thousands of files, search results, diagnostics, and references.
- Without virtualization, the IDE may solve performance by over-filtering or by blocking UI while rebuilding widgets.

Add:

- Virtual list model:
  - row count callback/model
  - visible row rendering
  - stable selection by id
  - incremental updates
- Virtual or lazy tree expansion:
  - populate children on expand
  - refresh subtree
  - preserve expansion/selection

Unblocks:

- Large project trees.
- Search and references results.
- Problems panel.
- Asset browser and tile palette if large.

Verification:

- 10k row list remains interactive.
- Deep tree lazy expansion avoids full recursive build.

## R12 - Debug Protocol Surface

Tier: 2 until Phase 2 debugger work starts.
Status: Implemented headless command/event boundary on 2026-05-22.

Progress:

- Added `Viper.Debug.Protocol` and `Viper.Debug.Protocol.Session`.
- Supports launch, terminate, pause, continue, step over, breakpoints, stack frames, locals, events, running state, and crash-as-event behavior.
- Added CTest coverage for breakpoint stop, stepping, locals/frames, normal exit, and crash event.

Remaining:

- Hosted VM/subprocess adapter integration can reuse the same protocol shape when the full debugger starts.

Review:

- VM debug internals exist, but a live IDE debugger needs a protocol and process/session model.
- Direct `Viper.Debug.*` bindings inside the target are not enough if ViperIDE drives a child process.

Add:

- Debug adapter/protocol boundary, either in-process-hosted or subprocess-based.
- Commands/events:
  - launch
  - terminate
  - restart
  - set/clear breakpoints
  - continue
  - pause
  - step over/into/out
  - stopped event
  - stack frames
  - locals/scopes
  - stdout/stderr
  - target exit/crash

Critical requirement:

- A target crash must not crash ViperIDE.

Unblocks:

- Phase 2 integrated debugger.

Verification:

- Headless debug protocol test hits a breakpoint, steps, reads locals, and terminates.

## R13 - Structured Workspace Edit API

Tier: 2.
Status: Implemented on 2026-05-22.

Progress:

- Added `Viper.Workspace.Edit.Validate` and `Apply`.
- Supports ordered text replacements with 1-based ranges, optional `expectedMtime`, non-overlap validation, and all-or-nothing apply with rollback.
- Added CTest coverage for multi-file apply and overlapping-range rejection.

Remaining:

- Preview/diff formatting remains an IDE UX layer.

Review:

- Rename and refactor features should not each reinvent transactional multi-file edit application.
- This can be an IDE library in Zia, but a runtime helper is useful if file timestamp checks and atomic writes should be shared.

Add:

- `WorkspaceEdit` model:
  - file edits with ordered ranges
  - text replacement
  - file create/delete/rename
  - expected file version/timestamp
- Validation:
  - non-overlapping ranges
  - unchanged external timestamp
  - all-or-nothing apply
- Optional preview/diff formatting.

Unblocks:

- Phase 1 rename.
- File tree rename/move.
- Future code actions.

Verification:

- Apply multi-file edit.
- Reject overlapping ranges.
- Abort when one target file changed externally.

## R14 - Reusable Fuzzy Match and Quick Open Primitives

Tier: 3.
Status: Implemented on 2026-05-22.

Progress:

- Added `Viper.Text.FuzzyMatch.Score` and `Match`.
- Scoring is case-insensitive with bonuses for separators, camel-case/acronym hits, and consecutive matches.
- `Match` returns highlight ranges for UI rendering.
- Added CTest and Zia rt-api coverage.

Review:

- Useful, but not foundational.
- Command-palette style matching should be reusable for quick-open, go-to-symbol, file search, and palette ranking.

Add:

- `Viper.Text.FuzzyMatch`:
  - score
  - matched ranges
  - stable tie breakers
  - optional acronym/camel-case behavior

Unblocks:

- Go to file.
- Go to symbol.
- Better command palette ranking.

Verification:

- Scoring tests for common file/symbol queries.
- Matched range output highlights the expected characters.

## R15 - GUI Accessibility and Command State Primitives

Tier: 3, but audit continuously.
Status: Implemented first runtime slice on 2026-05-22.

Progress:

- Added `Viper.GUI.CommandState` enabled/checked state and accessibility metadata snapshots.
- Added `Viper.GUI.Accessibility.ContrastRatio`, `MeetsContrast`, and `HighContrastTokens`.
- Covered through `test_rt_gui_ide`.

Remaining:

- Keyboard-only smoke paths through the real ViperIDE screens should be added as the phase UIs land.

Review:

- Accessibility and keyboard access are product requirements, not polish.
- Some work belongs in ViperIDE, but common widget/toolkit state should be centralized.

Add:

- command enabled/disabled/checked state helpers
- focus traversal inspection
- accessible label/description metadata for widgets
- high-contrast theme tokens for editor/scene overlays
- keyboard navigation helpers for lists, trees, palettes, and toolbars

Unblocks:

- Per-phase accessibility acceptance.
- Command palette correctness.
- Scene editor keyboard workflow.

Verification:

- Keyboard-only smoke path through open, search, build, and scene tool selection.
- Theme contrast audit for core widgets and editor overlays.

## Suggested Order

1. R1 CodeEditor API audit.
2. R2 Process and Job Runtime, or at minimum args-based build/run plus a process API design.
3. R3 Structured Toolchain and Diagnostics API, diagnostics first.
4. R5 Workspace File Index, Ignore Rules, and Watcher Service.
5. R4 Project Language Index.
6. R6 Scene Runtime Editor Primitives, in parallel with R4 if scene editor is urgent.
7. R7 Scene Rendering and GUI Draw Surface Primitives.
8. R8 Asset Resolver.
9. R9 Project Manifest and Run Configuration Model.
10. R10 GUI Test and Automation Hooks.
11. R11 Virtualized List/Tree/Result Widgets as soon as large-project tests show pressure.
12. R12 Debug Protocol Surface after process/session ownership is settled.
13. R13 Structured Workspace Edit API before rename ships.
14. R14 Fuzzy Match when quick-open enters scope.
15. R15 Accessibility/command-state primitives continuously, with formal audit in Phase 6.

## Keep Out of This File

These are important but should stay in IDE phase specs unless runtime need is proven:

- exact ViperIDE panel layout
- command names and keybindings
- scene editor tool UX details
- user-facing docs and release notes
- project-specific examples

## Verification Rules

Every runtime prerequisite requires:

- build and relevant `ctest`
- runtime completeness check after `runtime.def` edits
- graphics-on and graphics-off builds for GUI/graphics APIs
- Windows, macOS, and Linux path/process checks where relevant
- at least one IDE-facing Zia probe proving the primitive can be consumed
- one fail-before/pass-after regression where practical
