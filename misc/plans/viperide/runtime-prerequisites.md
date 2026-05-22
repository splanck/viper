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
- **R2 still open:** no generic `Viper.Job` wrapper yet; no GUI frame-loop regression test that proves a long-running process keeps ViperIDE rendering; Windows/Linux kill and streaming behavior still need CI or machine coverage beyond the current macOS run.
- **2026-05-22 - R3 structured diagnostics slice implementation landed.** Added `Viper.Zia.Toolchain` with `Check`, `CheckForFile`, `Compile`, and `CompileForFile` returning `Seq`/`Map` structured records instead of tab-delimited text. Diagnostic maps include normalized file, line/column range, severity, code, message, stage, and help. Compile maps include success, diagnostics, sourcePath, outputPath, and serialized IL on success. Weak runtime stubs now provide shape-compatible empty diagnostics/failed compile results when `fe_zia` is not linked. ViperIDE live diagnostics now consumes structured records directly, stores diagnostic path+line in list item data, and diagnostic navigation can open a different file before jumping to the diagnostic line. Runtime docs were added in `docs/viperlib/zia.md`.
- **Verified:** `cmake --build build --target test_rt_zia_completion_stub viper zia -j`, `ctest --test-dir build --output-on-failure -R '^test_rt_zia_completion_stub$'`, `ctest --test-dir build --output-on-failure -R '^test_runtime_classes_catalog$'`, `ctest --test-dir build --output-on-failure -R '^native_smoke_viperide_completion_arm64$'`, `ctest --test-dir build --output-on-failure -R '^zia_smoke_viperide_project_compile$'`, `ctest --test-dir build --output-on-failure -R '^test_linker_runtime_import_audit$'`, `ctest --test-dir build --output-on-failure -R '^test_runtime_surface_audit$'`, `./scripts/check_runtime_completeness.sh`, and `git diff --check`.
- **R3 still open:** no project-wide compile API yet; no persisted output path or bytecode/module handle; no BASIC-facing structured toolchain surface; no hosted VM/run/debug API. The delivered slice covers the first IDE-critical need: structured Zia diagnostics and single-buffer compile results.

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

Review:

- This is mandatory for the scene editor.
- The editor cannot safely mutate only a cached `Tilemap`; scene-owned normalized data must remain the serialization source of truth.

Add through `Viper.Game.Scene`:

- scene-owned tile/layer get/set/fill APIs
- layer visibility/name/order APIs if layers are editor-visible
- live object handles or scene-owned object mutators
- add/remove/reorder object APIs
- property get/set/delete APIs
- canonical save/to-json
- load diagnostics/last error
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
