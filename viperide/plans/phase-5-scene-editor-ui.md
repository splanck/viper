# Phase 5 - Scene Editor UI

## Current Review - 2026-05-22

Verdict: **not implemented yet**. Phase 5 should remain a future plan.

What exists now:

- ViperIDE recognizes `.scene` and `.level` as `DOC_KIND_SCENE`.
- File filters include `.scene` / `.level`, search includes scene files, and
  scene documents route through the same tab/session/close safety model as
  text documents.
- The language-service layer correctly gives scene files no Zia semantic
  capabilities.
- Phase 2 run configs and cancellable `Viper.System.Process` jobs are available
  for future Play integration.
- Phase 3 landed the runtime data model: `Viper.Game.Scene.LoadFile`,
  `SaveFile`, `DiagnosticRecords`, asset descriptors, scene-owned tile/layer
  and object mutators, typed properties, and `BuildTilemap()`.
- Runtime primitives also exist for asset resolution
  (`Viper.Assets.Resolver.Resolve`) and richer project manifest parsing
  (`Viper.Project.Manifest.ParseFile`).

What is missing:

- There is no `viperide/src/scene_editor/` implementation.
- `AppShell.SelectSurface(kind)` currently records the active kind but still
  shows the code editor for every document. There is no docked scene surface.
- Opening a `.scene` file still loads it as text content through
  `DocumentManager.OpenFile`; the IDE does not create or cache a
  `Viper.Game.Scene` handle.
- There is no `SceneDocumentState`, no scene dirty flag separate from editor
  text, no scene save/reload path, no scene undo/redo, no asset-resolution UX,
  no tile palette/layer/object tools, and no Play command that passes the active
  scene.
- Phase 4 `Viper.GUI.SceneView` is also still missing, so Phase 5 cannot ship
  a real visual editor yet.

Updated direction: split Phase 5 more sharply. The first IDE increment should
be a surface/state skeleton that can open a scene, show structured load errors,
and round-trip save through `Viper.Game.Scene` without visual editing. Visual
tools come only after Phase 4 provides a real SceneView.

## 1. Summary and Objective

Build the docked visual scene editor inside ViperIDE. This is a staged program, not one oversized increment. Each sub-phase must preserve document safety, save/reload behavior, keyboard access, and cross-platform behavior.

The scene editor works with `Viper.Game.Scene` as the source of truth. Any `Tilemap` returned by `BuildTilemap()` is a render copy unless Phase 3 deliberately adds a tested live-view ownership model.

## 2. Dependencies

Required before any visual editing:

- Phase 0 document kinds, surface switching, close/save safety, structured locations, session restore.
- Phase 3 scene load/save, `DiagnosticRecords()` diagnostics, and mutators
  required by the specific tool.
- Phase 4 SceneView rendering, hit testing, and marker/selection APIs.
- A real ViperIDE scene surface, not the current placeholder
  `SelectSurface(kind)` implementation that always leaves the code editor
  visible.

Required before Play:

- Phase 2 run configs and the IDE job model built on `Viper.System.Process`.

Useful runtime dependencies already available:

- `Viper.Assets.Resolver.Resolve` for scene/project/asset-root/mounted lookup.
- `Viper.Project.Manifest.ParseFile` for asset roots, scene roots, default
  scene, and run/build config metadata.

## 3. Scope by Sub-Phase

### 5A0 - Scene Surface and State Skeleton

In:

- Create `viperide/src/scene_editor/` with a small controller/state module.
- Add a real scene area to `AppShell` and make
  `AppShell.SelectSurface(DOC_KIND_SCENE)` hide the code editor and show the
  scene surface.
- Keep source-mode fallback available for scene files until visual editing is
  mature.
- Add a `SceneDocumentState` side table keyed by document path or future
  document id.
- Load `.scene` and `.level` through `Viper.Game.Scene.LoadFile` into that
  state, but keep `Document.content` as the source text fallback.
- Preserve code cursor/scroll state when switching to/from scene tabs.

Acceptance:

- Opening a scene tab shows the scene surface, not the code editor.
- Switching code -> scene -> code preserves editor cursor/scroll.
- Closing an unmodified scene participates in the same tab/exit close paths as
  existing documents.
- A bad scene file produces structured diagnostics and allows source fallback.

### 5A - Scene Viewer and Load Errors

In:

- Scene-editor modules under `viperide/src/scene_editor/`, mounted by `AppShell.SelectSurface(KIND_SCENE)`.
- Open `.scene`/`.level`/canonical scene files as scene documents.
- Load through `Viper.Game.Scene.LoadFile`.
- Show SceneView with a scene-owned tile view or `Scene.BuildTilemap()` render copy if load succeeds.
- Show an in-surface error from `scene.DiagnosticRecords()` if load fails.
- Provide "Open as Source" or source/visual toggle.

Out:

- Mutating scene data.
- Save through scene runtime.

Acceptance:

- Switching between code and scene tabs preserves code cursor and scene pan/zoom.
- Bad scene file shows error and allows source inspection.
- No edit/save feature is claimed in this sub-phase.

### 5B - Scene Document Model and Save/Reload

In:

- `SceneDocumentState` owned by the IDE document or a side table keyed by document id.
- Dirty tracking for scene changes separate from raw text editor dirty state.
- Save via `scene.SaveFile(doc.filePath)`.
- Save As with canonical scene extension.
- Reload from disk with dirty conflict prompt.
- External-change handling for active and inactive scene docs.
- Session restore for scene tabs, including pan/zoom and selected tool if practical.
- Updating `Document.isModified` / tab modified indicators from scene-state
  dirty changes so existing close prompts work.
- A single `SaveStateToDocument`/surface flush path that dispatches to code or
  scene state before commands that switch, close, run, or save.

Acceptance:

- Load -> Save -> Reload preserves canonical scene JSON.
- Dirty scene blocks tab close, File > Exit, and OS close.
- External disk change warns before overwrite.
- Source fallback text and scene runtime state cannot silently diverge; when
  source text changes, the scene state is either reloaded or marked stale with a
  clear status.

### 5C - Tile Palette, Layers, and Brushes

In:

- Asset resolution for scene asset descriptors using project root, scene path, asset roots, and `Viper.Assets.Resolver.Resolve`.
- Missing asset placeholder.
- Tile palette grid with selected tile id.
- Layer list with active layer and visibility.
- Paint, erase, and fill tools.
- Brush edits call scene-owned tile/layer mutators from Phase 3.
- Undo/redo grouping: one drag stroke is one undo transaction; one fill is one transaction.

Out:

- Object editing.
- Rich property inspector beyond tile id/layer basics.

Acceptance:

- Paint -> Save -> Reload persists.
- Erase uses the documented empty tile id.
- Fill respects boundaries and map bounds.
- Large fills do not store wasteful full-scene copies if an operation log is sufficient.

### 5D - Objects, Selection, Gizmos, Inspector

In:

- Object palette/type list. If no schema exists yet, provide a minimal object creation dialog for `type`, `id`, `x`, and `y`.
- Place, select, move, duplicate, and delete objects.
- Selection markers and gizmos via SceneView overlay APIs.
- Inspector for scene, layer, tile, and object scalar properties.
- Property edits use indexed scene-owned object/property mutators from `Viper.Game.Scene`.
- Undo/redo for object/property edits.

Acceptance:

- Add object -> Save -> Reload persists.
- Move object with gizmo updates inspector and scene data.
- Invalid property value is rejected with a useful message.

### 5E - Play Integration

In:

- Run the current project with the active scene path passed through a run config
  argument or environment variable. The exact contract must be documented in
  `viper.project`.
- Save or prompt before Play.
- Stream output to Phase 2 console.
- Clickable runtime errors open source or scene diagnostics where possible.

Acceptance:

- Play runs the configured game against the active scene.
- Missing run config produces a clear setup prompt.
- Long-running game can be stopped.

## 4. Technical Requirements

### 4.1 Surface Mounting

- `AppShell.SelectSurface(KIND_SCENE)` shows the scene editor and hides the code editor.
- `KIND_CODE` restores the code editor.
- `KIND_TEXT` may use the code editor as a text surface.
- `KIND_BINARY_UNSUPPORTED` should get a read-only unsupported-file surface
  rather than loading binary content into the code editor.
- Do not nest scene editor panels inside decorative cards; this is a tool surface.
- Toolbar and command palette update by active surface.
- The minimap should hide for scene visual mode unless/until it has a meaningful
  scene overview role.

### 4.2 Source/Visual Duality

Scene files remain text files. The editor must support at least one of:

- source/visual toggle in the same tab, or
- "Open as Source" command that opens a code/text view of the same file.

Load diagnostics should point to source line/column and JSON path when
available.

### 4.3 Asset Resolution

Define one editor asset resolver:

1. scene-relative path
2. project-root relative path
3. each manifest `asset-root`
4. mounted asset path through `Viper.IO.Assets`

Use `Viper.Assets.Resolver.Resolve` instead of recreating this logic in
ViperIDE. Missing assets should not prevent tile-id editing.

### 4.4 Undo/Redo

Scene undo is separate from CodeEditor undo.

Rules:

- Coalesce drag paint into one operation.
- Store operation deltas, not full scene copies, except for small safe cases.
- Clear redo on new edit.
- Mark document dirty on edit and clean after successful save.

### 4.5 Object Schemas

Before claiming a rich inspector, choose one:

- project-level object schema/templates
- runtime-provided known object types
- minimal free-form scalar property editor

Do not build a polished inspector that has no source of truth for valid object types and fields.

## 5. Error Handling

- Scene load fails: show `DiagnosticRecords()` diagnostic surface and source
  fallback.
- Scene save fails: keep dirty flag and show path/reason.
- Missing tileset: placeholder palette and warning.
- Asset decode fails: placeholder and warning.
- Edit without active layer/tile: no-op plus status message.
- Tool command active on code document: no-op disabled command.
- Scene runtime missing required mutator: editor hides that tool.

## 6. Tests

- Surface skeleton: code -> scene -> code preserves editor state and toggles
  visible surfaces.
- Surface swap: code -> scene -> code preserves state.
- Bad scene: error surface appears; source fallback works.
- Save/reload: scene dirty flag clears only after save.
- External change: conflict prompt blocks overwrite.
- Tile paint/erase/fill persistence.
- Undo/redo for paint and fill.
- Object add/move/delete persistence.
- Inspector property edit validation.
- Asset resolver order and missing asset placeholder.
- Play config passes active scene path.

Suggested new gates:

- `zia_viperide_phase5_scene_surface` for 5A0/5A.
- `zia_viperide_phase5_scene_save` for 5B.
- `zia_viperide_phase5_scene_editing` for 5C/5D.
- Keep these separate from Phase 4 SceneView runtime/widget tests so failures
  point to the right layer.

## 7. Manual Verification

- Create/open a scene.
- Toggle source/visual.
- Paint tiles, switch layers, undo/redo, save, reload.
- Place and move objects, edit properties, save, reload.
- Close with unsaved edits and cancel.
- Play the current scene and stop the game.
- Repeat in dark and light themes.
