# Direct3D 11 (Windows) — platform-specific implementation & test checklist

> Companion to `roadmap.md` / `runtime-changes.md`, sibling to `metal.md`
> (macOS) and `opengl.md` (Linux). This file lists only the work that is
> **D3D11-/Windows-specific** and must be implemented and tested on a
> Windows dev machine. CPU/backend-neutral phases (3 spatial index, 8 physics,
> 9 nav, 10 animation logic) carry **no D3D11-specific work** beyond confirming
> render parity; they are listed at the end for completeness.
>
> Backend files: `src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11.c`
> (~5.9K), `vgfx3d_backend_d3d11_shared.c/.h`. Shaders are HLSL compiled at
> runtime via `D3DCompile`. Target feature level **11_0** (compute, structured
> buffers, BC1–7 all available — none wired yet).

## 1. Windows environment & build

| ID | Task | Command / note |
|---|---|---|
| DX-ENV-1 | Toolchain | MSVC (cl.exe) + Windows SDK; clang-cl acceptable if used elsewhere |
| DX-ENV-2 | Build + test | `.\scripts\build_viper_win.cmd` (configures, builds, runs ctest) |
| DX-ENV-3 | Demos | `.\scripts\build_demos_win.cmd` |
| DX-ENV-4 | 3D regression | `ctest --test-dir build -L graphics3d --output-on-failure` |
| DX-ENV-5 | Completeness | `scripts\check_runtime_completeness.sh` (via Git Bash) green |
| DX-ENV-6 | Platform policy | no raw `_WIN32` outside approved adapters; `lint_platform_policy.sh` |
| DX-ENV-7 | Force backend | run probes with the D3D11 backend selected and also `software` for the baseline diff |

## 2. Cross-cutting D3D11 concerns (apply to every new visual feature)

- **HLSL parity.** Every new shader feature needs an `HLSL` path in
  `VSMain`/`PSMain` (plus the software reference). Preserve the existing
  conventions documented in `docs/graphics3d-architecture.md`: `row_major` matrix
  qualifier, `FrontCounterClockwise = FALSE` (Y-flip), depth remap
  `z = z*0.5 + w*0.5` (NDC→[0,1]), and clip/NDC→top-left texture-UV conversion
  for shadow/post-FX/motion sampling.
- **Constant-buffer packing.** New cbuffer data must use 16-byte-aligned `float4`
  packs (the backend already shares explicit packed layouts for morph/material/
  UV transforms); validate `ByteWidth` bounds before `CreateBuffer`.
- **Threading model.** `ID3D11Device` resource creation is free-threaded
  (do **not** create the device with `D3D11_CREATE_DEVICE_SINGLETHREADED`); the
  `ID3D11DeviceContext` immediate context is **not** thread-safe. So workers may
  create textures/buffers, but `Map`/`UpdateSubresource`/binds/draws stay on the
  main thread (or via deferred contexts). MSVC 64-bit CAS uses
  `_InterlockedCompareExchange64` (the generic `__atomic_*` compat is 32-bit only
  on MSVC).
- **Device-loss.** Handle `DXGI_ERROR_DEVICE_REMOVED`/`_RESET` on async upload and
  resize paths; the backend already restores prior render/depth targets on resize
  failure — keep that contract.
- **Capabilities.** Add D3D11 capability strings as features land
  (`BackendSupports("rt-finalize"/"occlusion"/"clustered-lighting"/"shadow-csm"/"bc7"/...)`);
  default to `false` until the path exists.

## 3. Per-phase D3D11 implementation + test checklist

### Phase C — carryover
| ID | Implement | Test |
|---|---|---|
| DX-C-1 | `build_viper_win.cmd` green; `-L graphics3d` green on Windows (CO-1) | Full ctest run recorded on the Windows machine |
| DX-C-2 | Render-target finalization for swapchain backbuffer **and** offscreen RTV (CO-4) | `ScreenshotFinal`/`Flip` no double-apply for RTT frames |
| DX-C-3 | GPU timing via `ID3D11Query` (`D3D11_QUERY_TIMESTAMP`/`_DISJOINT`) for the perf lane (CO-11) | Frame GPU time recorded on reference Windows HW |
| DX-C-4 | Confirm committed skinned character renders via 256-bone cbuffer (CO-9) | Visual parity vs software within tolerance |

### Phase 1 — concurrency
| ID | Implement | Test |
|---|---|---|
| DX-1-1 | Worker pool on `rt_platform.h` Win32 threads (`CRITICAL_SECTION`/`CONDITION_VARIABLE`/`InitOnceExecuteOnce`) | Stress + Application Verifier / debug-heap (TSan unavailable on MSVC) |
| DX-1-2 | Main-thread commit queue feeds the immediate context; workers only create resources | `runFrames` parity pool on/off on Windows |

### Phase 2 — floating origin
| ID | Implement | Test |
|---|---|---|
| DX-2-1 | Camera-relative model matrix in the HLSL `row_major` cbuffer | 50 km far-origin render matches near-origin within tolerance |

### Phase 4 — async asset + GPU upload
| ID | Implement | Test |
|---|---|---|
| DX-4-1 | Worker `CreateTexture2D`/`CreateBuffer` (with initial data); staging + `UpdateSubresource`/`CopyResource` on the main commit queue | async==blocking resource equality; no `DEVICE_REMOVED` under churn |
| DX-4-2 | Mip/LOD upload + residency eviction releases D3D11 resources | DXGI/process memory returns to baseline after churn |

### Phase 5 — world partition + terrain streaming
| ID | Implement | Test |
|---|---|---|
| DX-5-1 | Per-tile texture + `Texture2DArray` splat-layer uploads; per-tile heightfield textures | tile stream in/out across swapchain resize; no seams |

### Phase 6 — visibility (occlusion + LOD)
| ID | Implement | Test |
|---|---|---|
| DX-6-1 | Occlusion via `ID3D11Query` (`D3D11_QUERY_OCCLUSION_PREDICATE`) + `SetPredication`; optional Hi-Z via compute (CS 5.0) | occluded-draw skip counts vs frustum-only baseline |
| DX-6-2 | Impostor/HLOD render-to-texture (RTV) | LOD/impostor swap stability vs full-detail |

### Phase 7 — lighting (clustered + CSM)
| ID | Implement | Test |
|---|---|---|
| DX-7-1 | CPU cluster build → upload per-cluster light index via **structured buffer + SRV** (`t`-register); HLSL forward+ light loop | >16 lights correct vs naive forward |
| DX-7-2 | CSM: depth `Texture2DArray` + `SamplerComparisonState`; layered render via geometry shader or per-cascade passes; honor `Light3D.CastsShadows` | CSM stability (no acne/peter-panning beyond tolerance); fallback to 16-light forward |

### Phase 11 — asset pipeline (texture compression)
| ID | Implement | Test |
|---|---|---|
| DX-11-1 | BC1–3/BC6H/**BC7** upload via `DXGI_FORMAT_BC*_UNORM` + `CreateTexture2D` (no CPU decode) | compressed vs raw within tolerance; recorded VRAM reduction |
| DX-11-2 | KTX2/precompressed block upload → **BC7** on desktop; ASTC/ETC2 capability `false` on D3D11 | KTX2/precompressed asset loads and renders; capability query honest |
| DX-11-3 | glTF camera/multi-scene are loader-side (backend-neutral); Basis/Draco/meshopt remain optional Phase 11b | confirm imported camera/scene render under D3D11 |

### Phase 12 — vertical slice
| ID | Implement | Test |
|---|---|---|
| DX-12-1 | Run `examples/3d/openworld_slice/` on the D3D11 backend | smoke + deterministic replay + recorded Windows perf baselines |

## 4. CPU / backend-neutral phases (no D3D11-specific code)

Phases **3** (spatial index), **8** (physics), **9** (navigation), **10**
(animation/IK logic) run on the shared C runtime. D3D11 work is limited to
confirming render parity (e.g. IK/skinning still upload correctly through the
existing 256-bone cbuffer). No new HLSL/resource work expected.

## 5. Windows sign-off checklist

- [ ] `build_viper_win.cmd` + `build_demos_win.cmd` succeed (MSVC)
- [ ] `ctest -L graphics3d` green on Windows for every shipped phase
- [ ] each new feature has an HLSL path + capability string + software-baseline diff
- [ ] async upload clean under churn (no `DEVICE_REMOVED`, no leak)
- [ ] swapchain resize + RTT finalization correct
- [ ] perf baseline recorded on named Windows reference hardware
- [ ] no raw `_WIN32` outside approved adapters (`lint_platform_policy.sh`)
