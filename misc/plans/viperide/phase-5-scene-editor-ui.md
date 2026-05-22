# Phase 5 - Scene Editor UI

## 1. Summary and Objective

Build the docked visual scene editor inside ViperIDE. This is a staged program, not one oversized increment. Each sub-phase must preserve document safety, save/reload behavior, keyboard access, and cross-platform behavior.

The scene editor works with `Viper.Game.Scene` as the source of truth. The cached `Tilemap` is only a render view unless Phase 3 deliberately changes that ownership model.

## 2. Dependencies

Required before any visual editing:

- Phase 0 document kinds, surface switching, close/save safety, structured locations, session restore.
- Phase 3 scene load/save, diagnostics, and mutators required by the specific tool.
- Phase 4 SceneView rendering, hit testing, and marker/selection APIs.

Required before Play:

- Phase 2 run configs and safe process/job model.

## 3. Scope by Sub-Phase

### 5A - Scene Viewer and Load Errors

In:

- `scene_editor` module mounted by `AppShell.SelectSurface(KIND_SCENE)`.
- Open `.scene`/`.level`/canonical scene files as scene documents.
- Load through `Viper.Game.Scene.Load`.
- Show SceneView with tilemap if load succeeds.
- Show an in-surface error with diagnostics if load fails.
- Provide "Open as Source" or source/visual toggle.

Out:

- Mutating scene data.
- Save through scene runtime.

Acceptance:

- Switching between code and scene tabs preserves code cursor and scene pan/zoom.
- Bad scene file shows error and allows source inspection.

### 5B - Scene Document Model and Save/Reload

In:

- `SceneDocumentState` owned by the IDE document or a side table keyed by document id.
- Dirty tracking for scene changes separate from raw text editor dirty state.
- Save via `scene.Save(doc.filePath)`.
- Save As with canonical scene extension.
- Reload from disk with dirty conflict prompt.
- External-change handling for active and inactive scene docs.
- Session restore for scene tabs, including pan/zoom and selected tool if practical.

Acceptance:

- Load -> Save -> Reload preserves canonical scene JSON.
- Dirty scene blocks tab close, File > Exit, and OS close.
- External disk change warns before overwrite.

### 5C - Tile Palette, Layers, and Brushes

In:

- Asset resolution for tileset descriptors using project root and `Viper.IO.Assets.Load`.
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
- Property edits use live scene-backed `SceneObject` handles or scene mutators.
- Undo/redo for object/property edits.

Acceptance:

- Add object -> Save -> Reload persists.
- Move object with gizmo updates inspector and scene data.
- Invalid property value is rejected with a useful message.

### 5E - Play Integration

In:

- Run the current project with the active scene path passed through a run config.
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
- Do not nest scene editor panels inside decorative cards; this is a tool surface.
- Toolbar and command palette update by active surface.

### 4.2 Source/Visual Duality

Scene files remain text files. The editor must support at least one of:

- source/visual toggle in the same tab, or
- "Open as Source" command that opens a code/text view of the same file.

Load diagnostics should point to source line/column when available.

### 4.3 Asset Resolution

Define one editor asset resolver:

1. scene-relative path
2. project-root relative path
3. mounted asset path through `Viper.IO.Assets`

The chosen order must be documented and tested. Missing assets should not prevent tile-id editing.

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

- Scene load fails: show diagnostic surface and source fallback.
- Scene save fails: keep dirty flag and show path/reason.
- Missing tileset: placeholder palette and warning.
- Asset decode fails: placeholder and warning.
- Edit without active layer/tile: no-op plus status message.
- Tool command active on code document: no-op disabled command.
- Scene runtime missing required mutator: editor hides that tool.

## 6. Tests

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

## 7. Manual Verification

- Create/open a scene.
- Toggle source/visual.
- Paint tiles, switch layers, undo/redo, save, reload.
- Place and move objects, edit properties, save, reload.
- Close with unsaved edits and cancel.
- Play the current scene and stop the game.
- Repeat in dark and light themes.
