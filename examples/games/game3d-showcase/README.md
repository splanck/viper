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

- `World3D` frame loop, input snapshots, quality policy, lighting, post-FX, and effects
- procedural open terrain with splat-mapped grass, dirt, and rock layers
- generated cubemap skybox plus reflective animated water
- third-person exploration with mouse look, wheel zoom, and sprint
- ridge beacons, emissive materials, particles, and final-overlay HUD/minimap
- scene-graph landmarks drawn through Game3D entities plus direct terrain/water passes

## Controls

- `WASD` or arrows: move
- `Mouse`: look
- `Wheel`: zoom camera
- `Shift`: sprint
- `Esc`: quit
