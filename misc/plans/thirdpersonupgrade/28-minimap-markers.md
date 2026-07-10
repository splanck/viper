# Plan 28 — `Minimap3D`, Compass, and World Markers

## 1. Objective & scope

Standard adventure HUD furniture in one component: a minimap (live top-down capture or authored map image), entity markers with edge clamping, a compass strip, and off-screen objective indicators. All rendering rides the existing overlay pass; the only new render machinery is a throttled ortho capture.

**In scope:** (a) `Minimap3D` with two map sources (RT capture / authored image + affine); (b) markers bound to entities or points, with icons, colors, edge clamp; (c) compass strip + bearing markers; (d) screen-space objective indicators (reuses `Camera3D.WorldToScreen`, plan 25).
**Out of scope:** fog-of-war/exploration reveal (game-side mask composition), zoomable full-screen map UI (GameUI/game-side; the map texture is exposed for it).

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **No map/marker helpers exist** in the runtime; every demo hand-rolls HUD blips.
- **Capture path:** `RenderTarget3D` off-screen render (`rt_rendertarget3d.c`) + `rt_camera3d_new_ortho` (`rt_camera3d.c` fn list) — a top-down ortho scene render is assembly, not new tech. `Material3D.SetAlbedoRenderTarget` precedent shows RT-as-texture consumption (runtime.def Material3D).
- **Overlay drawing:** final-overlay images/rects/text post-post-FX (`game3d.md` §Debug3D path; `Canvas3D` window/image helpers, `rendering3d.md` §Canvas3D Window, Image, and Foliage Helpers — verify the exact overlay image-blit entry at write time).
- **Projection helper:** plan 25 adds `Camera3D.WorldToScreen` — the objective-indicator primitive.
- **Pixels:** icons are `Pixels` objects (`Graphics.Pixels`), same as other overlay imagery.
- **World frame data:** camera yaw for compass/map rotation (`rt_camera3d_get_yaw`); entity world positions from the registry.

## 3. Design

### 3.1 Map sources

New C `src/runtime/graphics/3d/rt_game3d_minimap.c`. `Minimap3D.New(world, sizePx)`:

- **Authored mode (recommended):** `setMapImage(pixels, worldMinX, worldMinZ, worldMaxX, worldMaxZ)` — a hand-authored/baked map with its world-rect affine. Zero per-frame render cost.
- **Capture mode:** `setLiveCapture(radiusM, everyNFrames)` — ortho camera straight down at the tracked center, radius half-extent, rendered into an owned `RenderTarget3D` every N frames (default 30; the capture renders the scene with a stripped feature set: no shadows/post-FX — a dedicated lightweight pass flag on the canvas draw call, same machinery Water3D's reflection pass uses for its resolution-limited re-render).
- Tracked center: `setTrackedEntity(entity)` (player); map view rotates with camera yaw when `setRotateWithCamera(true)` (default) else north-up.

### 3.2 Markers

`addMarker(entity | vec3, iconPixels, r, g, b) -> i64`; `removeMarker(id)`; per-marker `setMarkerEdgeClamp(id, bool)` (default true: off-map markers clamp to the minimap rim with the correct bearing — the objective-compass behavior) and `setMarkerScale(id, f64)`. Marker world → map: affine (authored) or center-relative (capture), then rotation. Stale entities drop their markers (fail-closed registry pattern).

### 3.3 Compass + objective indicators

- `setCompass(enabled, widthPx)` — a horizon strip at top-center: cardinal letters + degree ticks scrolled by camera yaw; markers project onto it by bearing (shared marker list, `setMarkerOnCompass(id, bool)`).
- `setObjectiveIndicator(id, bool)`: for flagged markers, when the target is on-screen draw the icon at `WorldToScreen` position (+ distance text option); off-screen, clamp to screen edge along the bearing — the standard quest pointer.

### 3.4 Drawing

`draw()` called from the game's overlay callback (explicit, like `Debug3D` — games own HUD layout); draws map disc/rect (circular mask option `setCircular(true)`), player arrow, markers, compass, indicators via overlay image/rect/text calls. Position/size props (`setViewport(x, y, w, h)`).

## 4. Implementation steps

1. Component + authored-mode affine + marker registry + overlay draw (rect map, icons, player arrow); C math tests (world→map affine incl. rotation) + Zia structural capture test.
2. Edge clamping geometry (rim intersection by bearing) + tests.
3. Compass strip + bearing projection.
4. Objective indicators over `WorldToScreen` (on-screen + edge-clamped cases).
5. Capture mode: throttled ortho pass into an RT + map-source swap; perf note (capture cost visible in `FrameGpuTimeUs` only every Nth frame).
6. runtime.def + audits + ADR + docs (`game3d.md` new §Minimap And Markers with an authored-map recipe).
7. Zia probe `g3d_test_game3d_minimap_probe`: authored map + 3 markers (one off-map clamped, one objective off-screen), synthetic camera yaw sweep, capture asserts + deterministic replay.

## 5. Public API changes (runtime.def)

```
RT_FUNC(Game3DMinimapNew, rt_game3d_minimap_new, "Viper.Game3D.Minimap3D.New", "obj(obj,i64)")
RT_CLASS_BEGIN("Viper.Game3D.Minimap3D", Game3DMinimap3D, "obj", Game3DMinimapNew)
    RT_METHOD("setMapImage","void(obj,obj,f64,f64,f64,f64)",…)
    RT_METHOD("setLiveCapture","void(obj,f64,i64)",…)
    RT_METHOD("setTrackedEntity","void(obj,obj<Viper.Game3D.Entity3D>)",…)
    RT_METHOD("setRotateWithCamera","void(obj,i1)",…) RT_METHOD("setCircular","void(obj,i1)",…)
    RT_METHOD("setViewport","void(obj,f64,f64,f64,f64)",…)
    RT_METHOD("addMarker","i64(obj,obj,obj,f64,f64,f64)",…)   /* entity|vec3, icon pixels, rgb */
    RT_METHOD("removeMarker","void(obj,i64)",…)
    RT_METHOD("setMarkerEdgeClamp","void(obj,i64,i1)",…) RT_METHOD("setMarkerScale","void(obj,i64,f64)",…)
    RT_METHOD("setMarkerOnCompass","void(obj,i64,i1)",…) RT_METHOD("setObjectiveIndicator","void(obj,i64,i1)",…)
    RT_METHOD("setCompass","void(obj,i1,f64)",…)
    RT_METHOD("draw","void(obj)",…)
    RT_PROP("MapTexture","obj",get)     /* owned RT/pixels escape hatch for full-screen map UIs */
RT_CLASS_END()
```

Leaf `Minimap3D` unique. New file → source-health; ADR `00xx-game3d-minimap.md`.

## 6. Tests

- **Affine (C unit):** world corners map to map-rect corners; camera-rotation mode rotates a +Z-north marker to screen-up at yaw 0 and screen-right at yaw 90° (fail-before: no API).
- **Edge clamp:** far marker clamps to the rim at the correct bearing (circular and rect modes).
- **Compass:** yaw sweep scrolls cardinal positions by the expected pixel offsets; a bearing-45° marker sits between N and E ticks.
- **Indicator:** on-screen target draws at `WorldToScreen` position; behind-camera clamps to the correct screen edge.
- **Capture mode:** RT refreshes exactly every N frames (frame-counter assert); tracked center follows the entity.
- **Stale:** despawned marker entity removes its marker without a trap.

## 7. Verification gates

Full build + ctest; `-L graphics3d`; surface audits; `-L slow`. Capture-mode perf recorded on the openworld perf harness (every-30-frames capture ≤ budgeted cost).

## 8. Risks & constraints

- **Capture-pass feature stripping** must reuse the existing limited-pass machinery (Water3D reflection precedent) — never fork a second scene-draw path.
- **Overlay call availability:** confirm the overlay image-blit primitive covers scaled/tinted icon draws; if not, the tiny gap (tinted blit) lands as a Canvas3D overlay addition in step 1.
- Marker counts are HUD-scale (≤64, trap past) — fixed arrays, no per-frame allocation.
- Fog-of-war explicitly out: expose `MapTexture` so games composite their own reveal mask.
