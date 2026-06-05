# Game3D Open World Showcase

`game3d-showcase` is the games-folder showcase for the 3D runtime. It uses the
higher-level `Viper.Game3D` world surface together with direct
`Viper.Graphics3D` terrain, water, skybox, and overlay rendering.

## Run

```sh
viper run examples/games/game3d-showcase/
```

Quick validation:

```sh
viper run examples/games/game3d-showcase/smoke_probe.zia
```

## Features

Built on the higher-level `Viper.Game3D` world surface plus direct `Viper.Graphics3D`
rendering, the scene exercises a broad slice of the 3D API:

- **Imported models** — optional `MapleTree_1.fbx` loaded with `Model3D.Load` when present
  on disk and instanced into a deterministic forest, placed by terrain slope/height
  (avoids cliffs and water)
- **PBR materials** — stone/metal landmarks use `NewPBR` with procedural albedo + normal maps
  and skybox environment reflections
- **Instanced vegetation** — wind-animated grass populated from the terrain's grass splat channel
- **Spatial audio** — synthesized beacon hums, footsteps, and activation/victory stingers with
  listener-follow attenuation
- **Dynamic lights** — beacons emit point lights when linked; a soft point light tracks the player
- **Particles + decals** — drifting fireflies, lakeside mist, sprint dust, and beacon scorch marks
- **Day/night cycle** — animated sun direction/colour, fog, and ambient (starts at noon)
- **Skeletal animation** — procedurally-rigged "sentinel" creatures animated by a bone skeleton
- **Raycast scanner** — a centre-screen reticle names the landmark/beacon under the crosshair;
  camera shake punctuates beacon activation
- **Authored post-FX** — bloom, tonemap, colour-grade, vignette, FXAA (SSAO where supported),
  plus a runtime quality toggle and a diagnostics overlay
- procedural terrain (splat-mapped grass/rock/dirt), reflective animated water, a generated
  cubemap skybox, and a final-overlay HUD/minimap
- **F11 fullscreen toggle** (`Canvas3D.ToggleFullscreen`), rigid-body physics props (a dynamic
  boulder pile settling on a static plinth), **wind-swayed reeds** (`Canvas3D.DrawMeshWind`), a
  **render-to-image minimap** (`Canvas3D.DrawImage2D`), **AnimController3D** idle/move sentinels
  that wander the meadow (skinned via `Canvas3D.DrawMeshSkinned`), bark/canopy split tree
  materials (`SceneNode3D` traversal), a sprint **FOV kick** with depth-of-field + motion blur,
  and a "return home" **Trigger3D** objective

> The forest treats `MapleTree_1.fbx` as an optional local asset. If the file is
> missing, the demo logs a notice and continues without trees; native demo builds
> therefore do not embed it as a required asset.

## Controls

- `WASD` or arrows: move
- `Mouse`: look
- `Wheel`: zoom camera
- `Shift`: sprint
- `F11`: toggle fullscreen (starts windowed)
- `Space`: cycle render quality (performance / balanced / cinematic)
- `Ctrl`: toggle the diagnostics overlay
- `Esc`: quit
