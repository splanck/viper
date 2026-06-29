# Game3D Showcase

`showcase.zia` is the full-stack Game3D sample. It runs a short deterministic
mini-game loop and exits with `PASS` when the replay state and software final
frame checks match.

Run it from the repository root:

```sh
VIPER_3D_BACKEND=software build/src/tools/viper/viper run examples/3d/game3d_showcase/showcase.zia
```

The sample covers:

- `World3D` setup, quality, lighting, post-FX, fog, water, and runtime preset swaps
- prefabs, `Assets3D.LoadEntityAsset`, `BodyDef`, layers, masks, triggers, and collision events
- first-person, follow, and orbit camera controllers
- `CharacterController3D` plus a physics-driven ball
- `Animator3D` events and root-motion sync through a procedural skeleton
- positional, attached, and 2D audio
- collision-triggered particles and decals
- raw escape hatches through direct `Canvas3D`, `SceneNode`, and `Sound` calls
- final-overlay HUD pixels and structural scene assertions on the software backend

The software visual baseline is structural rather than a checked-in binary
image: the probe saves `/tmp/game3d_showcase_software_current.png`, verifies the
captured size, checks that final-overlay HUD pixels remain crisp, and checks
that the 3D scene keeps visible contrast. This keeps the flagship sample less
brittle while `walk_min_probe.zia` continues to provide an exact-tolerance image
baseline.

The animation path uses a small procedural skeleton so the sample does not need
large private art assets. Imported skinned-character art remains a content task,
not a Game3D API blocker.
