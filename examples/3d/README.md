# 3D Examples

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
