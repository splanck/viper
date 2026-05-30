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
