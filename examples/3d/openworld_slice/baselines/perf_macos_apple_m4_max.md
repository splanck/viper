# Open World Slice Perf Baseline: macOS Apple M4 Max

Recorded on: 2026-05-29

Host:

- CPU: Apple M4 Max
- OS: macOS 26.5 (25F71)
- Arch: arm64
- Memory: 38654705664 bytes
- Logical CPUs: 14

Probe:

- Command directory: `examples/3d/openworld_slice`
- Script: `perf_probe.zia`
- Build type: Release (`build_release_perf`)
- Resolution: 192x108
- Warmup: 24 fixed frames
- Measurement: 120 fixed frames
- Per-frame work: `OpenWorldSlice.stepFixed(1.0 / 60.0)` plus `renderFrame()`
- CTest lane: `ctest --test-dir build_release_perf -R '^(g3d_openworld_slice_perf_probe|g3d_openworld_slice_gpu_smoke)$' --output-on-failure`

Measured results:

| Backend | Command | Setup ms | Frames | Elapsed ms | Avg ms | FPS | Draw count | Visible nodes | Entities | Bodies | Stream bytes | Resident cells | Resident tiles |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| software | `VIPER_3D_BACKEND=software ../../../build_release_perf/src/tools/viper/viper run perf_probe.zia` | 177.346 | 120 | 2000.485 | 16.671 | 59.985 | 14 | 7 | 15 | 5 | 327730 | 1 | 1 |
| metal | `VIPER_3D_BACKEND=metal ../../../build_release_perf/src/tools/viper/viper run perf_probe.zia` | 180.672 | 120 | 2000.443 | 16.670 | 59.987 | 14 | 7 | 15 | 5 | 327730 | 1 | 1 |

These numbers are a named local baseline, not a portable pass/fail threshold.
CTest verifies that the perf probe runs and reports valid counters; platform
perf gates should be added only on CI lanes with stable hardware and backend
configuration.

## NL3-013 Traversal Hitch/Memory Proof

Recorded on: 2026-05-31

Commands:

- `ctest --test-dir build_release_perf -R '^g3d_openworld_slice_(perf_probe|long_traversal)$' --output-on-failure -V`
- `VIPER_3D_BACKEND=metal ../../../build_release_perf/src/tools/viper/viper run long_traversal.zia`
- `VIPER_3D_BACKEND=metal ../../../build_release_perf/src/tools/viper/viper run perf_probe.zia`

The traversal visits all four streamed terrain/cell quadrants for eight rounds,
then repeats the same route and compares the replay checksum. Each visit settles
the deterministic `WorldStream3D.update` load budget before rendering, asserts
one resident cell/tile, one terrain collider, zero pending requests, bounded
resident bytes, and records the largest visit wall time as the hitch proxy.
`streamed_area_m2=18939904` is the four-tile authored terrain area.

| Backend | Visits per run | Replay | Max visit ms | Max stream bytes | Resident cells | Resident tiles | Pending requests | Streamed area m2 | No visible seams |
|---|---:|---|---:|---:|---:|---:|---:|---:|---|
| software | 32 + 32 | checksum match | 3.383 | 327730 | 1 | 1 | 0 | 18939904 | yes |
| metal | 32 + 32 | checksum match | 1.191 | 327730 | 1 | 1 | 0 | 18939904 | yes |

Associated perf probe rerun:

| Backend | Setup ms | Frames | Elapsed ms | Avg ms | FPS | Draw count | Visible nodes | Entities | Bodies | Stream bytes | Resident cells | Resident tiles |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| software | 170.166 | 120 | 1999.747 | 16.665 | 60.008 | 14 | 7 | 15 | 5 | 327730 | 1 | 1 |
| metal | 149.654 | 120 | 2000.205 | 16.668 | 59.994 | 14 | 7 | 15 | 5 | 327730 | 1 | 1 |

## NL3-016 Dense Visibility Reduction Proof

Recorded on: 2026-05-31

Command:

- `VIPER_3D_BACKEND=software ../../../build_release_perf/src/tools/viper/viper run visibility_dense_probe.zia`

The dense visibility probe authors a deterministic city/forest occlusion scene:
front city blocks plus a reachable portal alley remain visible, and dense forest
/ city zones behind an opaque blocker are assigned to unreachable visibility
zones. The script renders a no-PVS software baseline, then renders the PVS
version, compares sampled final-frame pixels, and reports `no_missing_geometry=1`
only when the visible image is unchanged.

| Backend | Authored nodes | Baseline draws | Optimized draws | Draw reduction | Draw reduction | Baseline fill proxy px | Optimized fill proxy px | Fill reduction px | Fill reduction | PVS culled | Zones | Portals | Baseline us | Optimized us | No missing geometry |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| software | 169 | 169 | 49 | 120 | 71.006% | 59040 | 29280 | 29760 | 50.407% | 120 | 4 | 2 | 1783 | 424 | yes |

## NL3-017 Clustered/Forward+ Lighting Proof

Recorded on: 2026-05-31

Commands:

- `VIPER_3D_BACKEND=metal ../../../build_release_perf/src/tools/viper/viper run gpu_smoke.zia`
- `ctest --test-dir build_release_perf -R '^g3d_openworld_slice_gpu_smoke$' --output-on-failure -V`

The GPU smoke now exercises the platform backend's many-light path after the
normal open-world render and shader robustness draw. It configures 24 point
lights, verifies the fixed forward fallback reports a 16-light budget, enables
`Canvas3D.SetClusteredLighting(true)`, verifies `MaxActiveLights=64`, renders
the same draw, and captures the final frame when the backend supports readback.

| Backend | Fallback max lights | Clustered max lights | Configured lights | Fallback us | Clustered us |
|---|---:|---:|---:|---:|---:|
| metal | 16 | 64 | 24 | 66 | 35 |

## NL3-018 Cascaded Shadow Map Proof

Recorded on: 2026-05-31

Commands:

- `VIPER_3D_BACKEND=metal ../../../build_release_perf/src/tools/viper/viper run gpu_smoke.zia`
- `ctest --test-dir build_release_perf -R '^g3d_openworld_slice_gpu_smoke$' --output-on-failure -V`

The GPU smoke now follows the clustered-lighting probe with an authored primary
directional-light CSM fixture. It enables `Canvas3D.SetShadowCascades(3)`,
renders a ground plane plus near/mid/far shadow casters into 1024px shadow maps,
checks the four main draws, and captures the final frame when the backend
supports readback.

| Backend | Cascades | Shadow map | Main draws | Direct csm us | CTest csm us |
|---|---:|---:|---:|---:|---:|
| metal | 3 | 1024 | 4 | 239 | 211 |

## NL3-026 NavAgent Crowd Target

Recorded on: 2026-05-31

Command:

- `ctest --test-dir build_release_perf -R '^test_rt_navagent3d$' --output-on-failure -V`

The NavAgent target authors 200 RVO-enabled agents in 100 opposing lanes,
updates 180 fixed frames, checks grid-vs-full-scan parity, records the minimum
sampled pair distance, and counts agents that crossed through the fixture.

| Build | Agents | Frames | Update us | Min pair distance | Crossed |
|---|---:|---:|---:|---:|---:|
| Release | 200 | 180 | 564686 | 1.142 | 170 |

## AC-003 SceneGraph Spatial-Index Cull Speedup

Recorded on: 2026-06-02

Command:

- `./build/src/tests/test_rt_scene3d` (telemetry line `SCENE3D_INDEX_SPEEDUP_TARGET`)

The 10k-node SceneGraph scaling fixture (`test_scene_spatial_index_10k_scaling_fixture`)
times an isolated `QueryAABB` point query that the BVH narrows to a single candidate,
then repeats it with `use_spatial_index = 0` so the same query falls back to a flat
O(N) sweep over all 10,000 node AABBs. The ratio is build-type independent (both
paths run under the same build), so the Debug measurement below is representative.

| Build | Nodes | Queries | Flat us | Indexed us | Speedup |
|---|---:|---:|---:|---:|---:|
| Debug | 10000 | 4000 | 20523643 | 11573 | 1773.41x |

This satisfies AC-003's "recorded speedup": indexed cull/query cost scales with the
result set (~1 node) rather than the total node count (10,000).
