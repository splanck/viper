# 3D World

Small outdoor starter 3D game written in Zia on top of `Viper.Graphics3D`.

It is intentionally simple, but it now feels like a real exterior space:

- procedural terrain with layered grass / dirt / rock texturing
- generated seamless gradient skybox
- third-person follow camera that stays above the terrain
- `WASD` movement
- mouse look
- `Q` / `E` camera orbit
- four ridge beacons to light while hiking the hills
- 2D HUD drawn after the 3D pass

## Run

```sh
viper run examples/games/3dworld/
```

Or run the local probe:

```sh
viper run examples/games/3dworld/smoke_probe.zia
```

## Files

- `main.zia`: project entry point
- `config.zia`: constants and tuning
- `game.zia`: terrain generation, skybox generation, game loop, rendering, and HUD
- `smoke_probe.zia`: one-frame render probe for quick validation

## Controls

- `WASD`: move
- `Mouse`: look around
- `Shift`: sprint
- `Q` / `E`: rotate camera
- `R`: reset world
- `Esc`: quit

## Notes

- Terrain is generated from Perlin noise in `game.zia`.
- The sky is a procedural cubemap built from `Pixels` faces.
- The interactive game loop captures the mouse for third-person look and releases it when the app exits.
- The example is self-contained inside this folder and is meant to be a good starting point for a larger outdoor game.
