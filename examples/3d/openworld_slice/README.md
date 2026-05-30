# Open World Slice

`openworld_slice/` is the Phase 12 vertical-slice smoke project. It combines a
manifest-driven >4 km² world stand-in, cell and terrain residency, async model
loading, KTX2 texture-asset residency, a first-person character, physics,
local-avoidance nav agents, a synthetic skinned glTF agent that crossfades from
`Idle` to `Wave` with a bound `IKSolver3D.LookAt`, committed GLB and WAV
package-asset fixture loads, final overlay capture,
`World3D` runtime counter checks, a world-scoped navmesh bake, deterministic
all-four-quadrant bounded-residency traversal, replay coverage, and a committed
software final-frame baseline comparison.

Run from this directory:

```sh
../../../build/src/tools/viper/viper run main.zia
VIPER_3D_BACKEND=software ../../../build/src/tools/viper/viper run test.zia
VIPER_3D_BACKEND=software ../../../build/src/tools/viper/viper run perf_probe.zia
../../../build/src/tools/viper/viper run streaming_hitch_probe.zia
VIPER_3D_BACKEND=metal ../../../build/src/tools/viper/viper run gpu_smoke.zia
../../../build/src/tools/viper/viper package . --target tarball --dry-run
```

Release perf baselines should be recorded from the Release build output, for
example `../../../build_release_perf/src/tools/viper/viper run perf_probe.zia`.
CTest also registers `g3d_openworld_slice_perf_harness`, which wraps the same
probe, validates the required `PERF:` counters, and emits a stable `HARNESS:`
summary for CI logs.

The current terrain stream instantiates and renders heightmapped `Terrain3D`
payloads from `assets/world/terrain.vscn` plus `assets/world/terrain/*.height`;
`assets/textures/` includes a tiny RGBA8 KTX2
material fallback and a BC7 KTX2 metadata/residency fixture;
`assets/models/skinned_agent.gltf` plus `skinned_agent.bin` are the committed
redistributable skinned animation fixture; `assets/models/triangle.glb` is the
committed binary glTF fixture; `assets/audio/jump.wav` is the committed audio
asset fixture loaded through `Sound3D.loadAsset`;
`assets/baselines/openworld_slice_software.png` is the software visual
baseline used by `test.zia`; `baselines/perf_macos_apple_m4_max.md` records the
current named local perf baseline for `perf_probe.zia`. The probe also builds a
small three-bone foot chain, samples the resident terrain tile height, and
asserts `IKSolver3D.TwoBone` plants the foot on that terrain target. Per-tile
heightfield collider residency and terrain nav-bake inclusion are verified
through streamed terrain collider/source nodes. Scripted quadrant visits settle
the deterministic `WorldStream3D.update` load budget across a few ticks, and
the runtime unit tests assert `pendingRequestCount` while a terrain request is
deferred. `long_traversal.zia` churns all four streamed quadrants repeatedly and
replays the same route to verify deterministic residency churn.
`streaming_hitch_probe.zia` records blocking-vs-async model-template timing,
proves zero upload budget keeps positive-cost async commit work pending, then
checks the shared `Assets3D.GetResidentBytes` counter returns to zero after
clear. Native
compressed texture upload and a visible foot-planted skinned character
remain tracked in the 3D next-level plan.
`gpu_smoke.zia` also runs under CTest with the platform GPU
backend (`metal`, `d3d11`, or `opengl`) and skips cleanly if that backend is
unavailable. The smoke includes a small degenerate-normal/tangent normal-map
draw so GPU shader basis fallbacks are exercised with the rest of the slice.
