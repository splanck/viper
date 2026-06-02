# NL3-033 Software Baseline Closure

Date: 2026-05-31

## Rule

Software remains the correctness baseline for visual features. GPU paths are
capability-gated and must either pass a parity/smoke probe against that baseline
or report a clean skip on unsupported hardware.

## Evidence Matrix

| Visual area | Software correctness baseline | GPU / capability gate | Status |
|---|---|---|---|
| Compressed textures / KTX2 | `test_rt_canvas3d` covers RGBA8 KTX2 fallback binding, BC3/BC7 software decode, representative ETC2/ASTC decode, mip residency, and native-only asset forwarding; `g3d_openworld_slice_probe` renders the committed BC7/KTX2 panel through the software slice baseline | `g3d_openworld_slice_streaming_hitch_native_compressed_probe` runs the native lane when a GPU backend advertises BC7/ASTC/ETC2, otherwise skips; Metal/ASTC local run recorded native upload/tolerance | Closed locally |
| Clustered / forward+ lighting | `test_rt_canvas3d` proves the software backend advertises the many-light baseline and submits beyond the 16-light forward cap | `test_rt_canvas3d` and `test_rt_canvas3d_production` prove capability names/fallbacks; `g3d_openworld_slice_gpu_smoke` records the Metal 24-light clustered fixture | Closed locally |
| Cascaded shadow maps | `test_rt_canvas3d` covers CSM capability gating and software support | `test_rt_canvas3d_gpu_paths` validates cascade payload/pass layout; `g3d_openworld_slice_gpu_smoke` records the 3-cascade Metal fixture | Closed locally |
| Occlusion and portal/PVS | `test_rt_canvas3d` covers CPU occlusion reduction; `test_rt_scene3d` covers BVH-fed occlusion candidates and portal/PVS reachability; `g3d_openworld_slice_visibility_dense_probe` compares optimized software pixels against a no-PVS software baseline | GPU occlusion/Hi-Z remains an optional accelerator; software PVS/occlusion is the portable correctness path | Closed locally |
| Runtime LOD / impostors / HLOD-related paths | `test_rt_scene3d` covers LOD/impostor draw behavior and nonresident LOD fallback; `g3d_openworld_slice_visibility_dense_probe` records the dense-scene draw/fill reduction without missing software pixels | `test_rt_canvas3d_production` guards `BackendSupports("hlod")`; backend-baked HLOD and automatic simplification remain stretch scope | Closed locally |
| Open-world visual slice and new visible content | `g3d_openworld_slice_probe` compares the full software final frame to the committed baseline while covering terrain, KTX2/BC7 texture panel, skinned character/IK, and terrain-foot IK markers | `g3d_openworld_slice_gpu_smoke` is capability-gated and covers normal-map robustness, clustered lighting, and CSM on the platform GPU backend | Closed locally |

## Local Gate

```sh
ctest --test-dir cmake-build-debug -R '^(test_rt_canvas3d|test_rt_canvas3d_gpu_paths|test_rt_canvas3d_production|test_rt_scene3d|test_vgfx3d_backend_utils|g3d_openworld_slice_probe|g3d_openworld_slice_streaming_hitch_native_compressed_probe|g3d_openworld_slice_visibility_dense_probe|g3d_openworld_slice_gpu_smoke)$' --output-on-failure -V
```

Local result on macOS/Metal: 9/9 passed.

Windows x64/MSVC closure on 2026-06-01:

```powershell
cmake --build build --config Debug -j $env:NUMBER_OF_PROCESSORS
ctest --test-dir build -C Debug -L graphics3d --output-on-failure -j $env:NUMBER_OF_PROCESSORS
cmake --build build --config Release -j $env:NUMBER_OF_PROCESSORS
ctest --test-dir build -C Release -L graphics3d --output-on-failure -j $env:NUMBER_OF_PROCESSORS
```

Windows results: Debug 78/78 `graphics3d` passed; Release 78/78
`graphics3d` passed. The named Windows Release software and D3D11 baselines are
recorded in
`examples/3d/openworld_slice/baselines/perf_windows_shakylaptop_ryzen7940hs.md`.

Key emitted evidence:

- `g3d_openworld_slice_streaming_hitch_native_compressed_probe`: `native_compressed_upload=1`, `native_backend=metal`, `native_format=astc`, `native_zero_pending_bytes=16`, `native_upload_bytes=16`, `native_raw_rgba_bytes=64`, `native_compressed_bytes=16`, `native_tolerance_checked=1`.
- `g3d_openworld_slice_visibility_dense_probe`: `baseline_draws=169`, `optimized_draws=49`, `pvs_culled=120`, `no_missing_geometry=1`.
- `g3d_openworld_slice_gpu_smoke`: `configured_lights=24`, `clustered_max_lights=64`, `cascades=3`, `draws=4`, `PASS: metal`.
- Windows D3D11 direct Release run:
  `native_compressed_upload=1`, `native_backend=d3d11`, `native_format=bc7`,
  `native_tolerance_max_diff=0`, `configured_lights=24`,
  `clustered_max_lights=64`, `cascades=3`, and `PASS: d3d11`.

## Runtime Fix Captured By The Gate

The first local NL3-033 run exposed a stale pending-upload path in the native
compressed GPU backend: while a native upload was paused by zero texture budget,
the material resolver also started an RGBA fallback upload. Once the native
texture became ready, that fallback upload was no longer used but still counted
as pending. Metal, D3D11, and OpenGL material resolvers now keep native-supported
`TextureAsset3D` values on the native upload path until it completes, avoiding
abandoned fallback uploads and preserving the software fallback for unsupported
native formats.
