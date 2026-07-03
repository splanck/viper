# Ridgebound

Ridgebound is a playable open-world Game3D sample about restoring five beacon
sites across a forested mountain basin. It opens on an in-world title menu and
uses the higher-level
`Viper.Game3D` world surface together with direct `Viper.Graphics3D` terrain,
water, skybox, and overlay rendering.

## Run

```sh
viper run examples/games/ridgebound/
```

Quick validation:

```sh
viper run examples/games/ridgebound/smoke_probe.zia
```

## File Map

- `main.zia` - entry point and packaged `--smoke` path.
- `game.zia` - thin lifecycle, input, player movement, camera, and frame orchestration.
- `assets.zia` - shared meshes, PBR/procedural materials, and environment reflections.
- `terrain.zia` - terrain generation, splat/texture helpers, vegetation, and shared terrain math.
- `water_sky.zia` - procedural cubemap skybox, animated water, and wind-swayed reeds.
- `forest.zia` - optional imported maple model loading, deterministic placement, and tree materials.
- `critters.zia` - procedurally rigged sentinels, wander targets, animation, and drawing.
- `audio.zia` - synthesized clips plus spatial footstep, beacon, victory, and ambient playback.
- `postfx.zia` - post-processing chain, particles, sprint dust, and scorch decals.
- `worldsim.zia` - landmarks, beacons, day cycle, dynamic lights, scanner, objective, and physics props.
- `minimap.zia` - pre-rendered terrain minimap and live player/beacon markers.
- `hud.zia` - overlay text, reticle readout, minimap placement, and debug panel.
- `menu3d.zia` - in-world title, pause, options, and controls menu.
- `config.zia` - all user-tunable constants used by the subsystems.
- `smoke_probe.zia` - one-frame validation probe for render/HUD/diagnostic regressions.

## Features

Built on the higher-level `Viper.Game3D` world surface plus direct
`Viper.Graphics3D` rendering, Ridgebound exercises a broad slice of the 3D API:

- **Imported models** — optional `MapleTree_1.fbx` loaded with `SceneAsset.LoadResult` when present
  on disk and instanced into a deterministic forest, placed by terrain slope/height
  (avoids cliffs and water)
- **PBR materials** — stone/metal landmarks use `PBR` with procedural albedo + normal maps
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
- **3D menu shell** — title/pause/options/controls screens drawn over the live
  world with menu pylons, selection glow, FOV/look/quality settings, and no external assets
- procedural terrain (splat-mapped grass/rock/dirt), reflective animated water, a generated
  cubemap skybox, and a final-overlay HUD/minimap
- **F11 fullscreen toggle** (`Canvas3D.ToggleFullscreen`), rigid-body physics props (a dynamic
  boulder pile settling on a static plinth), **wind-swayed reeds** (`Canvas3D.DrawMeshWind`), a
  **render-to-image minimap** (`Canvas3D.DrawImage2D`), **AnimController3D** idle/move sentinels
  that wander the meadow (skinned via `Canvas3D.DrawMeshSkinned`), bark/canopy split tree
  materials (`SceneNode` traversal), a sprint **FOV kick** with depth-of-field + motion blur,
  and a "return home" **Trigger3D** objective

> The forest treats `MapleTree_1.fbx` as an optional local asset. If the file is
> missing, the demo logs a notice and plants a procedural fallback forest; native
> demo builds therefore do not embed it as a required asset.

## Controls

- `WASD` or arrows: move
- `Mouse`: look
- `Wheel`: zoom camera
- `Shift`: sprint
- `Space`: jump
- `E`: hold to link a targeted beacon
- `V`: toggle first-person/orbit camera
- `Right mouse`: cycle render quality
- `F11`: toggle fullscreen
- `Ctrl`: toggle the diagnostics overlay
- `Esc`: pause during gameplay; back/quit from menus
