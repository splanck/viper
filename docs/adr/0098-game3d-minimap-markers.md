---
status: active
audience: contributors
last-verified: 2026-07-11
---

# ADR 0098: Minimap3D, Compass, and World Markers

Date: 2026-07-11

## Status

Accepted

## Context

Every adventure demo hand-rolls HUD blips: manual world-to-screen affine
math, ad-hoc quest pointers, no compass. The runtime has all the primitives
(Canvas3D overlay rect/image/text, `Camera3D.WorldToScreen`) but no
component gluing them into standard HUD furniture.

## Decision

- **`Minimap3D.New(world, sizePx)`** (Game3D component, game-owned and
  explicitly drawn from the HUD pass like Debug3D): an authored **north-up**
  map image with a world-rect affine (`SetMapImage(pixels, minX, minZ, maxX,
  maxZ)`; a dark panel when no image is set), `SetViewport`, and
  `SetTrackedEntity` for the player chip + heading tick (camera yaw derived
  from the forward vector, so any camera driver works — `fps_yaw` is only
  valid under the FPS controller).
- **Markers (≤64, trap past):** `AddMarker(entity, icon, color)` /
  `AddMarkerAt(point, icon, color)` returning ids; per-marker `EdgeClamp`
  (default on — off-map markers clamp to the viewport rim along the ray
  from the map center), `Scale`, `OnCompass`, `ObjectiveIndicator`. Stale
  entities drop their markers fail-closed at draw time.
- **Compass strip:** `SetCompass(enabled, widthPx)` — top-center bar with
  cardinal letters positioned by relative bearing (±90° span) and
  on-compass marker chips; bearing convention: −Z = north, +X = east.
- **Objective indicators:** flagged markers draw at their
  `WorldToScreen` position when visible, else clamp to the screen border
  along the horizontal bearing — the standard quest pointer.
- **Test surface:** `MapX/MapY(worldX, worldZ)` expose the raw affine
  (also useful for games compositing their own overlays), and
  `MarkerCount` observes fail-closed drops.

## Consequences

- Deferred (recorded): live top-down RT capture mode (`SetLiveCapture` —
  requires the limited-pass re-render machinery; authored maps are the
  recommended mode regardless), circular masking, map-image rotation with
  camera yaw (needs a rotated blit primitive; markers/compass already
  rotate), and the `MapTexture` escape hatch that rides the capture mode.
- Fog-of-war stays game-side by design.
- Test: `g3d_test_game3d_minimap_probe` — affine corners/center/off-map,
  marker registration, draw smoke with compass + objective indicator +
  rim-clamped far marker, stale-entity drop, RemoveMarker. VM == native.

## Links

- misc/plans/thirdpersonupgrade/28-minimap-markers.md
- src/runtime/graphics/3d/rt_game3d_minimap.c
- ADR 0097 (persistence), ADR 0086 (dialogue/WorldToScreen)
