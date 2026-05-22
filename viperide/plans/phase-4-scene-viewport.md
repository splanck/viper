# Phase 4 - Scene Viewport Widget (`Viper.GUI.SceneView`)

## 1. Summary and Objective

Add the GUI viewport that the scene editor uses to display tilemaps, grid overlays, object markers, selection, and pan/zoom. This phase must prove rendering and hit testing, not just create a widget shell.

The runtime prerequisite plan now exposes scaled tilemap draw/count/hit-test primitives. Phase 4 should build on those primitives or prove an equivalent SceneView-internal strategy with pixel and hit-test coverage.

## 2. Scope

In:

- `vg_sceneview` widget.
- Runtime binding `Viper.GUI.SceneView`.
- Tilemap rendering with pan and zoom.
- Grid overlay.
- Object/point markers.
- Selection rectangle/gizmo overlay primitives needed by Phase 5.
- Tile hit testing without packed integer return values.
- Real graphics build and non-graphics stub.

Out:

- Tile editing tools.
- Palette and inspector.
- Scene save logic.
- 3D viewport.

## 3. Rendering Strategy Decision

Before implementation, choose one:

1. Use the existing scaled `Tilemap.DrawScaled` / hit-test primitives.
2. Render the tilemap to an intermediate pixel buffer and scale/blit it into the widget.
3. Use an existing canvas transform only if one exists and is proven with tests.

`rt_tilemap_draw(tilemap, canvas, offsetX, offsetY)` alone is still insufficient for zoom.

Acceptance:

- Pixel tests show a nonblank tilemap at zoom 0.5, 1.0, and 2.0.
- Hit testing matches the rendered tile under pan and zoom.

## 4. Technical Requirements

### 4.1 Widget Model

Add:

- `src/lib/gui/include/vg_sceneview.h`
- `src/lib/gui/src/widgets/vg_sceneview.c`

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

Do not use `vg_widget_is_live` for tilemap or scene handles unless they are actually GUI widgets. Use runtime object lifetime/typed-handle checks appropriate for `Viper.Graphics.Tilemap` and `Viper.Game.Scene`, or treat handles as borrowed and clear them when the owning document unloads.

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

## 5. Error Handling

- Null tilemap: draw background/grid only.
- Null scene: hide scene markers, keep tilemap rendering.
- Invalid zoom: clamp.
- Invalid marker id or coordinates: ignore that marker.
- Tilemap freed/unloaded by owner: owner must call `SetTilemap(null)`; widget must tolerate null after unload.

## 6. Tests

- Create widget with null tilemap: paint no crash.
- Render 3x3 tilemap: pixel test confirms non-background pixels.
- Render at zoom 0.5, 1.0, 2.0: pixel dimensions change as expected.
- Hit test with pan and zoom: returns expected tile x/y.
- Outside hit: `TileAtX/Y` return `-1`.
- Marker draw: marker pixel appears at expected location.
- Non-graphics build: links and methods return safe defaults.

## 7. Manual Verification

- Open a probe that shows SceneView over a sample tilemap.
- Pan with drag.
- Zoom with wheel/keyboard.
- Toggle grid.
- Move selection marker and confirm it tracks pan/zoom.
