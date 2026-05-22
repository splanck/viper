# Phase 4 - Scene Viewport Widget (`Viper.GUI.SceneView`)

## Current Review - 2026-05-22

Verdict: **not implemented yet**, but the runtime blockers are smaller than
they were when this plan was first written.

What exists now:

- Phase 3 landed `Viper.Game.Scene` with canonical load/save, structured
  diagnostics, asset descriptors, scene-owned layer/tile/object mutators, and
  `BuildTilemap()`.
- The runtime has scaled tilemap primitives:
  `Viper.Graphics.Tilemap.DrawScaled`, `CountDrawnVisibleScaled`, and
  `HitTestScaled`.
- `Viper.GUI.TestHarness` can exercise focus and synthetic widget regions, but
  it is not a real pixel-rendering harness. It cannot by itself prove that a
  `SceneView` actually painted a tilemap.

What is missing:

- There is no `Viper.GUI.SceneView` class or `vg_sceneview` widget.
- There are no GUI bindings, graphics-off stubs, widget lifetime checks, or
  SceneView-specific tests.
- ViperIDE only routes `.scene`/`.level` files to `DOC_KIND_SCENE`;
  `AppShell.SelectSurface(kind)` currently records the kind and still shows the
  code editor for every document kind.

Updated direction: Phase 4 should build a real GUI widget that uses the
existing scaled tilemap primitives internally. Do not add a generic
`DrawSurface` first unless SceneView proves it needs one. The first useful
increment is a tilemap-only `SceneView`; accepting a `Viper.Game.Scene` handle
can come after tilemap rendering, pan/zoom, hit testing, and overlays are
stable.

## 1. Summary and Objective

Add the GUI viewport that the scene editor uses to display tilemaps, grid overlays, object markers, selection, and pan/zoom. This phase must prove rendering and hit testing, not just create a widget shell.

The runtime prerequisite plan now exposes scaled tilemap draw/count/hit-test primitives. Phase 4 should build on those primitives or prove an equivalent SceneView-internal strategy with pixel and hit-test coverage.

## 2. Scope

In:

- `vg_sceneview` widget.
- Runtime binding `Viper.GUI.SceneView`.
- Integration into the runtime class catalog and graphics/non-graphics builds.
- Tilemap rendering with pan and zoom.
- Grid overlay.
- Object/point markers.
- Selection rectangle/gizmo overlay primitives needed by Phase 5.
- Tile hit testing without packed integer return values.
- Real graphics build, non-graphics stub, and SceneView-specific tests.

Out:

- Tile editing tools.
- Palette and inspector.
- Scene save logic.
- 3D viewport.

## 3. Rendering Strategy Decision

Decision for the current codebase:

Use the existing scaled `Tilemap.DrawScaled` / hit-test primitives inside the
widget. They already preserve tile IDs, support viewport culling, and have a
runtime contract for scaled drawing and hit testing. A separate intermediate
pixel buffer or generic draw surface is unnecessary for the first SceneView
increment.

`rt_tilemap_draw(tilemap, canvas, offsetX, offsetY)` alone is still insufficient for zoom.

Acceptance:

- SceneView paint tests show non-background tilemap output at zoom 0.5, 1.0,
  and 2.0. A blit-count-only tilemap unit test is not enough.
- Hit testing matches the rendered tile under pan and zoom.
- Tilemap primitives remain the source of tile draw math; SceneView owns
  widget-local pan/zoom/screen-to-world conversion.

## 4. Technical Requirements

### 4.1 Widget Model

Add:

- `src/lib/gui/include/vg_sceneview.h`
- `src/lib/gui/src/widgets/vg_sceneview.c`
- declarations in the GUI aggregate header used by runtime bindings
- runtime wrapper, likely `src/runtime/graphics/rt_gui_sceneview.c`
- graphics-off stubs alongside the other GUI graphics stubs
- `runtime.def` `RT_FUNC` and `RT_CLASS_BEGIN("Viper.GUI.SceneView", ...)`
  entries

State:

- parent/base widget
- tilemap handle
- optional scene handle
- pan x/y in world pixels
- zoom as `double` or fixed-point percent
- tile width/height cache
- grid visible
- markers list
- selection rectangle
- cached local mouse coordinates and last hit tile for editor tools

Do not use `vg_widget_is_live` for tilemap or scene handles unless they are actually GUI widgets. Use runtime object lifetime/typed-handle checks appropriate for `Viper.Graphics.Tilemap` and `Viper.Game.Scene`, or treat handles as borrowed and clear them when the owning document unloads.

The first implementation should treat tilemap/scene handles as borrowed. The
owning ViperIDE scene document must call `SetTilemap(null)` / `SetScene(null)`
when a document unloads or reloads.

### 4.2 Coordinate Model

SceneView owns its coordinate math:

```text
screen = (world - pan) * zoom + widget_origin
world = (screen - widget_origin) / zoom + pan
tile = floor(world / tile_size)
```

Do not rely on `Viper.Graphics.Camera.Zoom` unless its integer-percent API is intentionally part of the design. The widget API may expose `f64` zoom independently from the game camera API.

Clamp:

- zoom to a documented range, for example `0.125` to `8.0`
- pan to map bounds only when a tilemap is set and the map is larger than the viewport

### 4.3 Runtime Binding

Preferred API:

- `Viper.GUI.SceneView.New(parent) -> obj`
- `SetTilemap(view, tilemap)`
- `SetScene(view, scene)`
- `Clear(view)`
- `SetPan(view, x, y)`
- `GetPanX(view) -> i64`
- `GetPanY(view) -> i64`
- `SetZoom(view, zoom) -> void`
- `GetZoom(view) -> f64`
- `TileAtX(view, screenX, screenY) -> i64`
- `TileAtY(view, screenX, screenY) -> i64`
- `SetGridVisible(view, visible)`
- `ClearMarkers(view)`
- `AddMarker(view, id, x, y, color)`
- `SetSelectionRect(view, x, y, w, h)`
- `GetWorldX(view, screenX) -> i64`
- `GetWorldY(view, screenY) -> i64`
- `GetLocalMouseX(view) -> i64`
- `GetLocalMouseY(view) -> i64`
- `WasClicked(view) -> i1`
- `WasDragged(view) -> i1`

Use separate `TileAtX` and `TileAtY` returns instead of packing x/y into one integer. Outside the map returns `-1` for both.

Register all runtime functions and class methods in `runtime.def`.

### 4.4 Non-Graphics Stub

Non-graphics builds must link:

- constructors return null
- mutators no-op
- getters return safe defaults
- hit tests return `-1`

Add the stub in the same pattern used by other graphics runtime bindings.

### 4.5 Scene Integration

Phase 4 can be tested with a plain `Tilemap` or a `Scene.BuildTilemap()` render copy. If a `Scene` handle is accepted for markers, it must respect the Phase 3 ownership model. SceneView does not own scene data and does not mutate it.

Do not make SceneView responsible for loading `.scene` files or resolving
assets. It receives render-ready handles and editor overlay records from
ViperIDE. Phase 5 owns scene-document state, asset resolution, and mutation.

## 5. Error Handling

- Null tilemap: draw background/grid only.
- Null scene: hide scene markers, keep tilemap rendering.
- Invalid zoom: clamp.
- Invalid marker id or coordinates: ignore that marker.
- Tilemap freed/unloaded by owner: owner must call `SetTilemap(null)`; widget must tolerate null after unload.

## 6. Tests

- Create widget with null tilemap: paint no crash and hit tests return `-1`.
- Render 3x3 tilemap: pixel or framebuffer-backed test confirms
  non-background pixels. Synthetic `TestHarness.CaptureRegion` is not enough
  unless it is extended to reflect actual rendered pixels.
- Render at zoom 0.5, 1.0, 2.0: pixel dimensions change as expected.
- Hit test with pan and zoom: returns expected tile x/y.
- Outside hit: `TileAtX/Y` return `-1`.
- Marker draw: marker pixel appears at expected location.
- Selection rectangle draw: selection border appears at expected location under
  pan and zoom.
- Non-graphics build: links and methods return safe defaults.
- Runtime-completeness check covers every new `RT_FUNC` / `RT_METHOD`.

Suggested new gates:

- C runtime/widget test: `test_gui_sceneview`.
- Zia API smoke: `zia_rt_api_test_sceneview`.
- ViperIDE smoke after Phase 5 consumes the widget:
  `zia_viperide_phase4_sceneview`.

## 7. Manual Verification

- Open a probe that shows SceneView over a sample tilemap.
- Pan with drag.
- Zoom with wheel/keyboard.
- Toggle grid.
- Move selection marker and confirm it tracks pan/zoom.
