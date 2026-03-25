# Game Development — Top 10 High-Value Improvements

## Overview

These plans were reviewed against the current Viper runtime on 2026-03-24. The
biggest correction is that several items are not greenfield features anymore:
`SpriteBatch`, `Audio3D`, `RenderTarget3D`, `BitmapFont`, `StateMachine`,
`Tween`, `SpriteAnimation`, and substantial `Tilemap` support already exist.

The plans below are therefore written as:
- extensions of the current runtime
- compatible with the current `runtime.def` naming style
- compatible with the current source tree layout
- explicit about prerequisites when a feature cannot be implemented cleanly in one pass

## Feature List

| # | Feature | Effort | Plan |
|---|---------|--------|------|
| 1 | [2D Texture Atlas + SpriteBatch Enhancements](01-sprite-batching.md) | 3-5 days | Extend existing `SpriteBatch` with atlas/named-region workflow |
| 2 | [Animation State Machine](02-anim-state-machine.md) | 2-4 days | Frame-based wrapper around existing `StateMachine` + `SpriteAnimation` |
| 3 | [TrueType / Vector Font Rendering](03-truetype-fonts.md) | 5-8 days | Add scalable anti-aliased text alongside existing `BitmapFont` |
| 4 | [User-Facing Shader API](04-shader-api.md) | 30-40 days | Shared-IR custom shading with day-one parity across all four backends |
| 5 | [3D Physics Rotation + Constraints](05-physics-rotation.md) | 8-12 days | Angular dynamics, orientation API, joints |
| 6 | [Spatial Audio + Reverb Zones](06-spatial-audio.md) | 4-8 days | Extend existing `Audio3D`; gate reverb on mixer support |
| 7 | [Tiled/TMX Map Import](07-tiled-import.md) | 3-5 days | Add TMX import to existing `Tilemap` runtime |
| 8 | [Tween Sequence Chaining](08-tween-sequences.md) | 2-3 days | Frame-based sequencing on top of current `Tween` semantics |
| 9 | [2D Render Targets](09-render-targets.md) | 4-7 days | Off-screen 2D surfaces built around `Pixels` + shared draw path |
| 10 | [Animation Events](10-animation-events.md) | 2-4 days | Frame/time notifies for existing 2D and 3D animation players |

## Priority Order

**Quick wins (≤5 days):** #1, #2, #7, #8, #10
**Medium effort (6-12 days):** #3, #5, #6, #9
**Major feature (20+ days):** #4

## Current Runtime Baseline

The current runtime already has:
- 2D canvas, pixels, sprite, sprite sheet, scene, camera, tilemap, and `SpriteBatch`
- 3D canvas, materials, scene graph, `RenderTarget3D`, physics, audio helpers, and texture atlases
- `StateMachine`, `Tween`, and `SpriteAnimation` in `src/runtime/collections`
- `BitmapFont` text rendering for BDF/PSF fonts
- XML, JSON, and file I/O helpers needed by import-style features

That existing surface changes how these plans should be implemented:
- prefer extending current runtime classes over introducing replacement APIs
- keep timing semantics consistent with the existing frame-based gameplay helpers
- use `src/runtime/CMakeLists.txt` for runtime build wiring
- use current docs paths such as `docs/viperlib/game/animation.md`,
  `docs/viperlib/game/physics.md`, `docs/viperlib/graphics/pixels.md`,
  `docs/viperlib/graphics/fonts.md`, `docs/viperlib/audio.md`, and
  `docs/graphics3d-guide.md`

## Recommended Delivery Order

If these are implemented as product work rather than just plans, the lowest-risk order is:
1. `TextureAtlas` + `SpriteBatch` extensions
2. `AnimStateMachine`
3. TMX import
4. Tween sequences
5. Animation events
6. TrueType fonts
7. 3D physics rotation/joints
8. 2D render targets
9. Audio3D extensions
10. User-facing shader API

That ordering front-loads the features that:
- fit the current runtime with minimal architectural change
- are easiest to validate with existing demos
- do not depend on backend or mixer refactors
