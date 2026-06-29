# 3D Examples

## game3d_hello.zia

`game3d_hello.zia` is the smallest code-first Game3D starting point: a lit
world, walkable ground, first-person character, and one deterministic frame in
under 20 source lines without `Mat4`. The `g3d_game3d_common_no_mat4` CTest
keeps this sample and the starter/common probes on the Game3D transform-helper
path instead of direct matrix composition.

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
- bounded-scene no-regression coverage in `bounded_no_regression_probe.zia`,
  which compares exact final-frame pixels and runtime state against the default
  path with scale flags explicitly off

Run the interactive sample with:

```sh
build/src/tools/viper/viper run examples/3d/walk_min.zia
```

Run the visual and movement probe with the software backend:

```sh
VIPER_3D_BACKEND=software build/src/tools/viper/viper run examples/3d/walk_min_probe.zia
```

Run the bounded no-regression probe with the software backend:

```sh
VIPER_3D_BACKEND=software build/src/tools/viper/viper run examples/3d/bounded_no_regression_probe.zia
```

## game3d_starter/

`game3d_starter/` is the recommended copyable starting point. It includes a
`viper.project`, package asset layout, source-tree and packaged
`Assets3D.LoadEntityAsset` path, first-person character movement, and a
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

## openworld_slice/

`openworld_slice/` is the Phase 12 streaming vertical-slice project. It mounts
cell and terrain manifests for a >4 km² world stand-in, swaps resident quadrants
by stream center across all four cells, keeps the resident set bounded, exposes
rendered heightmapped `Terrain3D` tile payloads, completes an async model load through
`AssetHandle3D`, records a zero-upload-budget streaming hitch probe, renders KTX2/BC7 texture assets, runs a
first-person character, physics, a synthetic skinned glTF agent with
`Idle`→`Wave` crossfade plus `IKSolver3D.LookAt`, a terrain-sampled
`IKSolver3D.TwoBone` foot-plant proof with visible markers, committed GLB and WAV package-asset
fixture loads, and local-avoidance nav agents
from a world-scoped navmesh bake that includes streamed terrain, reads `World3D` runtime counters, compares the
final frame against a committed software baseline, then verifies deterministic
replay in `test.zia`. Stream-center teleports settle the deterministic
`WorldStream3D.update` load budget over a few ticks while unit coverage checks
`pendingRequestCount` between staged loads. `gpu_smoke.zia` requests the platform GPU backend and
reports a clean skip when it is unavailable; when a GPU backend is active it
also renders a degenerate-basis normal-mapped mesh to keep backend shader
fallbacks covered. `perf_probe.zia` records the
software frame loop metrics used by the named local perf baseline, and
`long_traversal.zia` repeats all-quadrant stream churn with deterministic replay
checks.

```sh
cd examples/3d/openworld_slice
VIPER_3D_BACKEND=software ../../../build/src/tools/viper/viper run test.zia
VIPER_3D_BACKEND=software ../../../build/src/tools/viper/viper run perf_probe.zia
VIPER_3D_BACKEND=software ../../../build/src/tools/viper/viper run long_traversal.zia
VIPER_3D_BACKEND=metal ../../../build/src/tools/viper/viper run gpu_smoke.zia
../../../build/src/tools/viper/viper package . --target tarball --dry-run
```
