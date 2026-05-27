# 3D Examples

## walk_min.zia

`walk_min.zia` is the Phase 0B code-first baseline sample for the Game3D plan.
It uses only the normal `Viper.Graphics3D` runtime surface:

- explicit `Canvas3D.SetDefaultLighting()`
- a ground plane plus box, sphere, cylinder, and marker props
- first-person/free-fly camera controls with WASD, mouse look, Q/E climb, and Shift sprint
- CPU-safe post-FX
- final overlay recording through `BeginOverlay()` / `EndOverlay()`
- `ScreenshotFinal()` coverage in `walk_min_probe.zia`

Run the interactive sample with:

```sh
build/src/tools/viper/viper run examples/3d/walk_min.zia
```

Run the visual probe with the software backend:

```sh
VIPER_3D_BACKEND=software build/src/tools/viper/viper run examples/3d/walk_min_probe.zia
```
