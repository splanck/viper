# 3D Examples

## game3d_hello.zia

`game3d_hello.zia` is the smallest code-first Game3D starting point: a lit
world, walkable ground, first-person character, and one deterministic frame in
under 20 source lines without `Mat4`.

```sh
VIPER_3D_BACKEND=software build/src/tools/viper/viper run examples/3d/game3d_hello.zia
```

## walk_min.zia

`walk_min.zia` is the small code-first baseline sample for the Game3D plan. It
uses the normal C runtime `Viper.Game3D` surface over `Viper.Graphics3D`:

- `World3D` setup with default lighting, fog, quality, scene, physics, input, and effects
- a ground plane plus box, sphere, cylinder, and marker props as `Entity3D` objects
- static ground/prop colliders
- `FirstPersonController` driving a grounded `CharacterController3D`
- WASD walk, mouse look, Space jump, and Shift sprint
- CPU-safe post-FX
- final overlay recording through `BeginOverlay()` / `EndOverlay()`
- `ScreenshotFinal()` and grounded synthetic movement coverage in `walk_min_probe.zia`

Run the interactive sample with:

```sh
build/src/tools/viper/viper run examples/3d/walk_min.zia
```

Run the visual and movement probe with the software backend:

```sh
VIPER_3D_BACKEND=software build/src/tools/viper/viper run examples/3d/walk_min_probe.zia
```

## game3d_starter/

`game3d_starter/` is the recommended copyable starting point. It includes a
`viper.project`, package asset layout, source-tree and packaged
`Assets3D.LoadModelAsset` path, first-person character movement, and a
deterministic `test.zia`.

```sh
cd examples/3d/game3d_starter
../../../build/src/tools/viper/viper run main.zia
VIPER_3D_BACKEND=software ../../../build/src/tools/viper/viper run test.zia
../../../build/src/tools/viper/viper package . --target tarball --dry-run
```

## game3d_showcase/

`game3d_showcase/showcase.zia` is the full-stack Game3D integration sample. It
combines quality/post-FX/environment toggles, prefabs, a packaged glTF prop,
first-person/follow/orbit cameras, a character controller, physics bodies,
layers, triggers, collision events, animation events/root motion, positional
and attached audio, 2D audio, VFX particles/decals, final-frame HUD capture,
and deterministic replay.

```sh
VIPER_3D_BACKEND=software build/src/tools/viper/viper run examples/3d/game3d_showcase/showcase.zia
```
