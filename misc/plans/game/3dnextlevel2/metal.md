# Metal (macOS) — platform-specific implementation & test checklist

> Companion to `roadmap.md` / `runtime-changes.md`, parallel to `directx.md`
> (D3D11/Windows) and `opengl.md` (OpenGL/Linux). This file lists only the work
> that is **Metal-/macOS-specific** and must be implemented and tested on a macOS
> dev machine. macOS is the **canonical/primary dev platform** (Apple Clang is
> the reference compiler per CLAUDE.md), so Metal is usually where a feature
> lands first and where the software-backend baseline diff is first taken.
> CPU/backend-neutral phases (3 spatial index, 8 physics, 9 nav, 10 animation
> logic) carry **no Metal-specific work** beyond confirming render parity; they
> are listed at the end for completeness.
>
> Backend files: `src/runtime/graphics/3d/backend/vgfx3d_backend_metal.m`
> (~4.3K), `vgfx3d_backend_metal_shared.c/.h`. Shaders are **MSL (Metal Shading
> Language)** held as an embedded source string and compiled at runtime via
> `[device newLibraryWithSource:options:error:]` (`vgfx3d_backend_metal.m:2095`
> for the main library, `:2560` for the post-FX library). Windowing/present is a
> **CAMetalLayer** drawable. Buffers are created `MTLResourceStorageModeShared`
> (`:1740`), which is cheap on Apple-Silicon unified memory.

## 0. The Metal capability profile (read first — it shapes Phases 7 and 11)

Metal is *more* capable than GL 3.3 (it has compute and modern buffer binding),
but it has one hard hardware split that the D3D11/GL paths do not: **the GPU
family determines which compressed texture formats exist**, and Apple-Silicon
GPUs do **not** support BC/DXT at all.

| Capability | Intel/AMD Mac (`MTLGPUFamilyMac2`) | Apple Silicon (`MTLGPUFamilyApple7`+) | Metal plan |
|---|---|---|---|
| **BC/DXT (BC1–7, BC6H)** | ✅ | ❌ **not supported** | pick at runtime; never assume |
| **ASTC** | ❌ usually | ✅ | the Apple-Silicon compressed path |
| **ETC2 / EAC** | ❌ usually | ✅ | available on Apple Silicon |
| **PVRTC** | ❌ | ✅ (legacy) | not a target; ASTC preferred |
| Compute shaders | ✅ | ✅ | usable for cluster build / Hi-Z (unlike GL 3.3) |
| Structured GPU buffers (`MTLBuffer`) | ✅ | ✅ | per-cluster light lists, bone palettes |
| Texture arrays | ✅ | ✅ | terrain layers, CSM cascades |
| Occlusion query (`MTLVisibilityResultMode`) | ✅ | ✅ | real occlusion + visibility result buffer |
| GPU timing (`commandBuffer.GPUStartTime/GPUEndTime`) | ✅ | ✅ | the perf lane |
| Layered render to array slice | ✅ | ✅ | CSM cascade render |

**Consequence for Phase 11:** the Metal compressed-texture path must branch on
`[device supportsFamily:...]` and choose **BC on Intel/AMD, ASTC/ETC2 on Apple
Silicon**. There is **no** universal GPU block format across Mac hardware, so
KTX2/precompressed assets that ship only BC blocks must software-decode to RGBA8
on Apple Silicon (and vice-versa) — this is the portable fallback. **Today the
backend has no GPU-family detection** (grep: no `supportsFamily`/`MTLGPUFamily`
in `vgfx3d_backend_metal.m`), so that probe is new Phase-11 work.

## 1. macOS environment & build

| ID | Task | Command / note |
|---|---|---|
| MTL-ENV-1 | Toolchain | Apple Clang + Xcode SDK; Metal framework; macOS 11+ for modern `MTLGPUFamily` queries |
| MTL-ENV-2 | Build + test | `./scripts/build_viper_mac.sh` (configures, builds, runs ctest) |
| MTL-ENV-3 | Demos | `./scripts/build_demos_mac.sh` |
| MTL-ENV-4 | 3D regression | `ctest --test-dir build -L graphics3d --output-on-failure` |
| MTL-ENV-5 | Completeness | `./scripts/check_runtime_completeness.sh` green |
| MTL-ENV-6 | Platform policy | no raw `__APPLE__` outside approved adapters; `./scripts/lint_platform_policy.sh`; `./scripts/run_cross_platform_smoke.sh` |
| MTL-ENV-7 | Sanitizers | TSan/ASan available via Apple Clang (`-fsanitize=thread`) — use TSan for the Phase-1 job system, including on Apple-Silicon ARM64 |
| MTL-ENV-8 | Force backend | run probes with the Metal backend selected and also `software` for the baseline diff; software baseline must not require a window |

## 2. Cross-cutting Metal concerns (apply to every new visual feature)

- **MSL parity.** Every new shader feature needs an MSL path inside the embedded
  shader source compiled by `newLibraryWithSource:` (plus the software
  reference). Preserve existing conventions documented in
  `docs/graphics3d-architecture.md`: matrices are **transposed to column-major**
  on upload, winding is `MTLWindingCounterClockwise`, the projection remaps
  OpenGL NDC depth `[-1,1]` → Metal `[0,1]` in the vertex shader, and texture
  sampling uses a **top-left origin** (matching software and D3D11). The MSL
  source is split into halves "to stay under the C99 string limit"
  (`vgfx3d_backend_metal.m:214`) — keep new shader code within that structure.
- **Argument/constant packing.** New per-draw or per-cluster data goes in
  `MTLBuffer`s (often `MTLResourceStorageModeShared`); respect 16-byte alignment
  for `float4`-packed structs shared with MSL, exactly as the D3D11 cbuffers do.
- **Threading model.** `MTLDevice` resource creation is **thread-safe** —
  `newTextureWithDescriptor:` (`:1090`/`:1227`), `newBufferWithBytes:`/`WithLength:`
  (`:1740`/`:1746`) may run on Phase-1 workers. But a `MTLRenderCommandEncoder`
  and the `CAMetalLayer` `nextDrawable`/present must stay on the main thread (one
  in-flight render encoder per command buffer). So workers produce CPU payloads
  and may create `MTLBuffer`/`MTLTexture`; binds/draws/encode/present stay on the
  main commit queue. On Apple-Silicon ARM64 use the existing `__dmb` barriers
  from `rt_platform.h` for the job system's lock-free paths.
- **Presentation model.** The backend already splits **direct mode** (no GPU
  post-FX → draw straight into the current `CAMetalLayer` drawable, `present()`
  just schedules it) from **overlay composition** (overlays blended after
  bloom/tonemap/SSAO/DOF/motion-blur), per `docs/graphics3d-architecture.md`
  §"Metal Window Presentation Model". New full-screen passes (occlusion depth,
  cluster debug, CSM) must respect that split so the no-post-FX path stays cheap.
- **Capabilities.** Add Metal capability strings as features land
  (`BackendSupports("rt-finalize"/"occlusion"/"clustered-lighting"/"shadow-csm"/"bc7"/"astc"/"etc2"/...)`),
  driven by `supportsFamily:` / feature-set checks; default `false` until the
  path exists. The Metal `.m` reports **no** capability strings today — wire them
  through the same dispatch the other backends use.

## 3. Per-phase Metal implementation + test checklist

### Phase C — carryover
| ID | Implement | Test |
|---|---|---|
| MTL-C-1 | `build_viper_mac.sh` green; `-L graphics3d` green on macOS (CO-1) | Full ctest run recorded on the Mac (Apple Silicon + Intel if available) |
| MTL-C-2 | Render-target finalization for the CAMetalLayer drawable **and** offscreen `MTLTexture` RTT (CO-4) | `ScreenshotFinal`/`Flip` no double-apply for RTT frames; `BackendSupports("rt-finalize")` honest |
| MTL-C-3 | GPU timing via `commandBuffer.GPUStartTime`/`GPUEndTime` for the perf lane (CO-11) | Frame GPU time recorded on reference Mac HW |
| MTL-C-4 | Confirm committed skinned character renders via the 256-bone palette buffer (CO-9) | Visual parity vs software within tolerance |
| MTL-C-5 | Metal robustness probe (CO-7): degenerate normals/tangents + zero-length skybox vector | `test_rt_canvas3d_gpu_paths` fake-Metal probe and `g3d_openworld_slice_gpu_smoke` platform-GPU pass render a degenerate-basis normal-mapped mesh; `test_vgfx3d_backend_metal_shared` guards safe-normal shader source and the `-Z` skybox zero-vector fallback |

### Phase 1 — concurrency
| ID | Implement | Test |
|---|---|---|
| MTL-1-1 | Worker pool on `rt_platform.h` pthreads; Apple-Clang atomics; Apple-Silicon `__dmb(_ARM64_BARRIER_ISH)` barriers | TSan-clean stress run on macOS |
| MTL-1-2 | Workers call `newTextureWithDescriptor:`/`newBufferWithBytes:`; encode/present on the main commit queue | `runFrames` parity pool on/off on macOS |

### Phase 2 — floating origin
| ID | Implement | Test |
|---|---|---|
| MTL-2-1 | Camera-relative model matrix in the column-major MSL uniform path | 50 km far-origin render matches near-origin within tolerance |

### Phase 4 — async asset + GPU upload
| ID | Implement | Test |
|---|---|---|
| MTL-4-1 | Worker `newTextureWithDescriptor:`/`newBufferWithBytes:`; main-thread blit/upload via blit command encoder or shared-storage write | async==blocking resource equality; no validation-layer errors under churn |
| MTL-4-2 | Mip/LOD upload + residency eviction releases `MTLTexture`/`MTLBuffer` | process/Metal allocation returns to baseline after churn |

### Phase 5 — world partition + terrain streaming
| ID | Implement | Test |
|---|---|---|
| MTL-5-1 | Per-tile textures + `MTLTextureType2DArray` splat layers; per-tile heightfield textures | tile stream in/out across layer resize; no seams |

### Phase 6 — visibility (occlusion + LOD)
| ID | Implement | Test |
|---|---|---|
| MTL-6-1 | Occlusion via `MTLVisibilityResultMode` + visibility result buffer on the render pass; optional Hi-Z via a **compute pass** (Metal has compute) | occluded-draw skip counts vs frustum-only baseline |
| MTL-6-2 | Impostor/HLOD render-to-`MTLTexture` (offscreen RTT) | LOD/impostor swap stability vs full-detail |

### Phase 7 — lighting (clustered + CSM)
| ID | Implement | Test |
|---|---|---|
| MTL-7-1 | CPU **or compute** cluster build → per-cluster light index in a `MTLBuffer`; MSL forward+ light loop | >16 lights correct vs naive forward |
| MTL-7-2 | CSM: depth `MTLTextureType2DArray` + `depth2d_array`/`sample_compare` in MSL; layered render to array slices or per-cascade passes; honor `Light3D.CastsShadows` | CSM stability (no acne/peter-panning beyond tolerance); fallback to 16-light forward |

### Phase 11 — asset pipeline (texture compression)
| ID | Implement | Test |
|---|---|---|
| MTL-11-1 | **GPU-family probe** (`supportsFamily:`) → choose `MTLPixelFormatBC7_RGBAUnorm`/BC* on Intel-AMD, `MTLPixelFormatASTC_*`/`ETC2_*` on Apple Silicon; `replaceRegion:` upload (no CPU decode) | compressed vs raw within tolerance; recorded VRAM/footprint reduction per GPU family |
| MTL-11-2 | KTX2/precompressed block upload matching the family's supported format; **software RGBA fallback when the file's blocks aren't supported on this GPU** (e.g. BC-only file on Apple Silicon) | KTX2/precompressed asset loads on both families; capability query honest; fallback path exercised |
| MTL-11-3 | glTF camera/multi-scene are loader-side (backend-neutral); Basis/Draco/meshopt remain optional Phase 11b | confirm imported camera/scene render under Metal |

### Phase 12 — vertical slice
| ID | Implement | Test |
|---|---|---|
| MTL-12-1 | Run `examples/3d/openworld_slice/` on the Metal backend | smoke + deterministic replay + recorded macOS perf baselines (Apple Silicon + Intel where available) |

## 4. CPU / backend-neutral phases (no Metal-specific code)

Phases **3** (spatial index), **8** (physics), **9** (navigation), **10**
(animation/IK logic) run on the shared C runtime. Metal work is limited to
confirming render parity (e.g. IK/skinning still upload correctly through the
existing 256-bone palette buffer). No new MSL/resource work expected.

## 5. macOS sign-off checklist

- [ ] `build_viper_mac.sh` + `build_demos_mac.sh` succeed (Apple Clang)
- [ ] `ctest -L graphics3d` green on macOS for every shipped phase
- [ ] each new feature has an MSL path + capability string + software-baseline diff
- [ ] async upload clean under churn (no Metal validation errors, no leak); TSan-clean job system on Apple Silicon
- [ ] CAMetalLayer resize + offscreen RTT finalization correct; direct/overlay split preserved
- [ ] compressed-texture path branches correctly by GPU family (BC on Intel/AMD, ASTC/ETC2 on Apple Silicon, software fallback otherwise)
- [ ] perf baseline recorded on named Mac reference hardware (note Apple Silicon vs Intel)
- [ ] no raw `__APPLE__` outside approved adapters (`lint_platform_policy.sh`)
