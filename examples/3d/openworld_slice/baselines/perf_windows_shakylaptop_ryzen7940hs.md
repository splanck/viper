# Open World Slice Perf Baseline: Windows SHAKYLAPTOP Ryzen 9 7940HS

Recorded on: 2026-06-01

Host:

- Machine: SHAKYLAPTOP
- CPU: AMD Ryzen 9 7940HS w/ Radeon 780M Graphics
- OS: Microsoft Windows 11 Home 10.0.26200 (build 26200)
- Arch: x64
- Memory: 16335958016 bytes
- Logical CPUs: 16

Build:

- Build directory: `build`
- Generator: Visual Studio 18 2026
- Compiler: MSVC 19.51.36246.0
- Config: Release
- Graphics backend availability: Win32 platform with D3D11 3D backend
- Audio backend availability: WASAPI

Gate:

```powershell
cmake --build build --config Release -j $env:NUMBER_OF_PROCESSORS
ctest --test-dir build -C Release -L graphics3d --output-on-failure -j $env:NUMBER_OF_PROCESSORS
```

Result: 78/78 `graphics3d` tests passed.

The Debug MSVC Windows lane was also run:

```powershell
cmake --build build --config Debug -j $env:NUMBER_OF_PROCESSORS
ctest --test-dir build -C Debug -L graphics3d --output-on-failure -j $env:NUMBER_OF_PROCESSORS
```

Result: 78/78 `graphics3d` tests passed.

## Perf Probe

Run from `examples/3d/openworld_slice`:

```powershell
$env:VIPER_3D_BACKEND='software'; ..\..\..\build\src\tools\viper\Release\viper.exe run perf_probe.zia
$env:VIPER_3D_BACKEND='d3d11'; ..\..\..\build\src\tools\viper\Release\viper.exe run perf_probe.zia
```

Measured results:

| Backend | Setup ms | Frames | Elapsed ms | Avg ms | FPS | Draw count | Visible nodes | Entities | Bodies | Stream bytes | Resident cells | Resident tiles |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| software | 2248.097 | 120 | 3676.263 | 30.636 | 32.642 | 17 | 10 | 18 | 5 | 263074 | 1 | 1 |
| d3d11 | 11337.783 | 120 | 1999.638 | 16.664 | 60.011 | 17 | 10 | 18 | 5 | 263074 | 1 | 1 |

These numbers are a named local baseline, not a portable pass/fail threshold.
CTest verifies that the perf probe runs and reports valid counters; platform
perf gates should be added only on stable CI hardware.

## Traversal Hitch/Memory Proof

Run from `examples/3d/openworld_slice`:

```powershell
$env:VIPER_3D_BACKEND='software'; ..\..\..\build\src\tools\viper\Release\viper.exe run long_traversal.zia
$env:VIPER_3D_BACKEND='d3d11'; ..\..\..\build\src\tools\viper\Release\viper.exe run long_traversal.zia
```

| Backend | Visits per run | Replay | Max visit ms | Max stream bytes | Max draws | Max bodies | Max entities | Resident cells | Resident tiles | Pending requests | Streamed area m2 | No visible seams |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| software | 32 + 32 | checksum match | 9.400 | 263074 | 8 | 5 | 17 | 1 | 1 | 0 | 18939904 | yes |
| d3d11 | 32 + 32 | checksum match | 3.312 | 263074 | 8 | 5 | 17 | 1 | 1 | 0 | 18939904 | yes |

## D3D11 Native Compressed Upload

Run from `examples/3d/openworld_slice`:

```powershell
$env:VIPER_3D_BACKEND='d3d11'
$env:VIPER_OPENWORLD_NATIVE_COMPRESSED_PROBE='1'
..\..\..\build\src\tools\viper\Release\viper.exe run streaming_hitch_probe.zia
```

Evidence:

- `native_compressed_upload=1`
- `native_backend=d3d11`
- `native_format=bc7`
- `native_zero_pending_bytes=16`
- `native_upload_bytes=16`
- `native_raw_rgba_bytes=64`
- `native_compressed_bytes=16`
- `native_ram_reduction_pct=75`
- `native_vram_reduction_pct=75`
- `native_tolerance_checked=1`
- `native_tolerance_max_diff=0`

Full line:

```text
HITCH: blocking_us=606 async_create_us=2 first_observe_us=307 zero_budget_pending=1 release_us=293 blocking_resident_bytes=7033 async_resident_bytes=7033 native_compressed_upload=1 native_backend=d3d11 native_format=bc7 native_zero_frame_us=472 native_zero_pending_bytes=16 native_release_us=197 native_upload_bytes=16 native_raw_rgba_bytes=64 native_compressed_bytes=16 native_ram_reduction_pct=75 native_vram_reduction_pct=75 native_tolerance_checked=1 native_tolerance_max_diff=0
```

## D3D11 Clustered Lighting And Cascaded Shadows

Run from `examples/3d/openworld_slice`:

```powershell
$env:VIPER_3D_BACKEND='d3d11'; ..\..\..\build\src\tools\viper\Release\viper.exe run gpu_smoke.zia
```

| Backend | Fallback max lights | Clustered max lights | Configured lights | Fallback us | Clustered us |
|---|---:|---:|---:|---:|---:|
| d3d11 | 16 | 64 | 24 | 76 | 22 |

| Backend | Cascades | Shadow map | Main draws | CSM us |
|---|---:|---:|---:|---:|
| d3d11 | 3 | 1024 | 4 | 621 |

Full lines:

```text
CLUSTERED_LIGHTING: backend=d3d11 fallback_max_lights=16 clustered_max_lights=64 configured_lights=24 fallback_us=76 clustered_us=22
CSM_SHADOWS: backend=d3d11 cascades=3 shadow_map=1024 draws=4 csm_us=621
PASS: d3d11
```

## Linux Note

The local WSL2 image is not a Linux graphics reference host: the existing
`build-wsl-debug` CMake cache has `VIPERGFX_AVAILABLE=OFF` and
`VIPERAUD_AVAILABLE=OFF` because X11 and ALSA development headers are missing.
Passwordless sudo is unavailable in that image, so Linux Release graphics proof
remains external to this Windows machine.
