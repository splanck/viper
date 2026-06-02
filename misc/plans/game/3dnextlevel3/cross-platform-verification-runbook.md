# Cross-Platform Verification Runbook (GATE-003 / AC-013)

This runbook records the exact steps to close the cross-platform verification gate
(`GATE-003`: every phase green on macOS/Windows/Linux) when Windows/Linux hardware is
available. On the current macOS Apple-Silicon host these lanes are **waived** (W2-002 /
W2-003): only Metal + software + `*_shared` backend translation units compile here, and
D3D11/OpenGL cores plus Windows/Linux `-L graphics3d` runs cannot be exercised locally.
No CI workflows are created or modified (project rule); these are local-host lanes.

## Host status matrix

| Lane | macOS (this host) | Windows | Linux |
|---|---|---|---|
| `-L graphics3d` build+run | ✅ green (79/79) | recorded green (Release 78/78, 2026-06-01) | ⏳ pending hardware (WSL2 lacks X11/ALSA → software lane only) |
| GPU backend core compile | Metal only | D3D11 (build script) | OpenGL (build script) |
| Software-baseline parity | ✅ | recorded | ⏳ |
| Named perf baseline | ✅ `perf_macos_apple_m4_max.md` | ✅ `perf_windows_shakylaptop_ryzen7940hs.md` | ⏳ `perf_linux_*` (blocked) |

## Procedure (run on the target host)

### Windows (x64, MSVC)
```cmd
.\scripts\build_viper_win.cmd
ctest --test-dir build -L graphics3d --output-on-failure
```
Pass criteria: graphics3d label green; `check_runtime_completeness.sh` (or the Windows
equivalent) clean; no UCRT/MSVC symbol-audit regressions.

### Linux (x64/arm64, clang)
```sh
./scripts/build_viper_linux.sh          # or build_viper_unix.sh
ctest --test-dir build -L graphics3d --output-on-failure
```
Note: a headless/WSL2 Linux box without X11/ALSA can still run the software lane; GPU
(OpenGL) requires a display + GL driver.

### Perf baseline (Release, named hardware)
```sh
ctest --test-dir build_release_perf -R '^(g3d_openworld_slice_perf_probe|g3d_openworld_slice_gpu_smoke)$' --output-on-failure
```
Record the result into `examples/3d/openworld_slice/baselines/perf_linux_<host>.md`
following the format of the existing macOS/Windows baselines.

## Gotcha — native AOT runtime archive (learned 2026-06-02)

Adding a `runtime.def` `RT_FUNC`/`RT_METHOD` creates **two** link surfaces:

1. `viper_runtime` — linked by unit tests. A targeted `cmake --build build --target test_X`
   refreshes it, so unit tests pass immediately.
2. The **native AOT runtime archive** — linked by compiled-to-native Zia programs. A
   partial/targeted build does **not** refresh it, leaving the new symbol undefined at
   native link time (e.g. `undefined symbol 'rt_navmesh3d_export'`).

After any `runtime.def` change, run a **full** `cmake --build build -j` (not just the test
target) before `./scripts/run_cross_platform_smoke.sh`. The native-link smoke
(`native_smoke_*`) is the gate that catches a stale native runtime archive.

## Staged backend work (Tier-C, needs on-device verification)

- **Sub-mip native upload slicing.** The slice-budget primitive
  `vgfx3d_upload_block_rows_for_budget` and `vgfx3d_pending_block_upload_bytes` exist in
  `vgfx3d_backend_utils.c` and are unit-proven (`test_vgfx3d_backend_utils`,
  monotonic sub-mip drain). The native compressed-mip upload loops of
  `vgfx3d_backend_metal.m`, `vgfx3d_backend_d3d11.c`, and
  `vgfx3d_backend_opengl.c` now track a per-mip block-row cursor and upload one
  budgeted block-row band per drain via backend partial-region texture updates.
  Remaining Tier-C proof is on-device: verify on each GPU under a tight
  `Canvas3D.SetTextureUploadBudget` that a large compressed mip uploads over
  multiple frames with bounded per-frame bytes and correct final sampling.

## Waivers (remain open until hardware lanes run)

- **W2-002** — cross-platform GPU interactive-framerate proof.
- **W2-003** — Windows/Linux Release software FPS baselines (Windows recorded; Linux pending).

GATE-003 / AC-013 close only when the Windows and Linux `-L graphics3d` lanes above run
green on real hosts.
