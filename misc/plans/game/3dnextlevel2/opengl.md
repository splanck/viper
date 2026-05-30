# OpenGL (Linux) — platform-specific implementation & test checklist

> Companion to `roadmap.md` / `runtime-changes.md`, sibling to `metal.md`
> (macOS) and `directx.md` (Windows). This file lists only the work that is
> **OpenGL-/Linux-specific** and must be implemented and tested on a Linux
> dev machine. CPU/backend-neutral phases (3 spatial index, 8 physics, 9 nav,
> 10 animation logic) carry **no OpenGL-specific work** beyond render parity and
> are listed at the end.
>
> Backend files: `src/runtime/graphics/3d/backend/vgfx3d_backend_opengl.c`
> (~5.2K), `vgfx3d_backend_opengl_shared.c/.h`. Shaders are **GLSL `#version 330`**
> compiled via `glCompileShader`; windowing/context is **GLX on X11**
> (`glXCreateContext*`). Cubemap seams use `GL_TEXTURE_CUBE_MAP_SEAMLESS`.

## 0. The OpenGL 3.3 ceiling (read first — it shapes every phase)

The backend targets desktop GL **3.3 core**. Several scale features rely on
capabilities introduced *after* 3.3, so the Linux implementation diverges from
D3D11. Decide per feature: raise the GL version requirement (capability-gated),
or use a 3.3-compatible fallback. Default below is **3.3-compatible fallback**.

| Capability | First in core GL | Available at 3.3? | Linux plan |
|---|---|---|---|
| Compute shaders | 4.3 | ❌ | CPU-side instead (cluster build, Hi-Z) or gate GPU path off |
| SSBO (shader storage) | 4.3 | ❌ | Use **UBO** (3.1) or **TBO** (3.1) for light-index/data buffers |
| Image load/store | 4.2 | ❌ | avoid; CPU/FBO paths |
| ETC2 / EAC core | 4.3 | ❌ (desktop HW often lacks) | capability-gate off; software-decode fallback |
| ASTC | ext only (mobile) | ❌ usually | capability-gate off; software-decode fallback |
| BPTC (BC6H/BC7) | 4.2 / `ARB_texture_compression_bptc` | ✅ via ext (widely present) | target for compressed textures |
| S3TC (BC1–3/DXT) | `EXT_texture_compression_s3tc` | ✅ via ext | target for compressed textures |
| Occlusion queries | 1.5 / 3.3 (`ANY_SAMPLES_PASSED`) | ✅ | use directly + conditional render |
| Timer query | 3.3 (`ARB_timer_query`) | ✅ | GPU timing for perf lane |
| Texture arrays | 3.0 | ✅ | terrain layers, CSM cascades |
| Geometry shaders | 3.2 | ✅ | layered CSM render (or per-cascade passes) |
| UBO | 3.1 | ✅ | bone palette, cluster lights (size-limited) |

**Runtime extension checks** are mandatory: query `GL_EXT_texture_compression_s3tc`
/ `GL_ARB_texture_compression_bptc` (and report capability strings) before using
compressed uploads; never assume.

## 1. Linux environment & build

| ID | Task | Command / note |
|---|---|---|
| GL-ENV-1 | Toolchain | GCC or Clang; X11 + GLX dev headers; mesa/driver with GL 3.3+ |
| GL-ENV-2 | Build + test | `./scripts/build_viper_linux.sh` (or `build_viper_unix.sh`) |
| GL-ENV-3 | Demos | `./scripts/build_demos_linux.sh` |
| GL-ENV-4 | 3D regression | `ctest --test-dir build -L graphics3d --output-on-failure` |
| GL-ENV-5 | Completeness | `./scripts/check_runtime_completeness.sh` green |
| GL-ENV-6 | Platform policy | no raw `__linux__` outside adapters; `lint_platform_policy.sh`; `run_cross_platform_smoke.sh` |
| GL-ENV-7 | Sanitizers | TSan/ASan available on Linux — use TSan for the Phase-1 job system |
| GL-ENV-8 | Display | X11/GLX (Wayland via XWayland). Headless software baseline must not need a display |

## 2. Cross-cutting OpenGL concerns (apply to every new visual feature)

- **GLSL 330 parity.** Every new shader feature needs a GLSL `#version 330` path
  (plus the software reference). Preserve existing conventions: upload matrices
  with `glUniformMatrix4fv(..., GL_TRUE, ...)` (transpose), `GL_CCW` winding (no
  Y-flip), native NDC depth `[-1,1]` (no remap), and **top-left texture origin**
  (flip RGBA rows before `glTexImage2D` / cubemap face upload).
- **Context thread-affinity.** A GL context is current on one thread only. For
  async work (Phase 4) either (a) create a **GLX shared context**
  (`glXCreateContextAttribsARB` with a shared display list) on the upload worker,
  or (b) keep all GL calls on the main thread and stream pixel data via **PBO**
  (`GL_PIXEL_UNPACK_BUFFER`) for DMA overlap. **Default: main-thread + PBO** (no
  per-driver shared-context fragility).
- **No SSBO/compute at 3.3.** Light-index and cluster data go through UBO/TBO;
  size limits cap cluster/light counts — expose that as a capability number.
- **Sampler objects.** The backend uses sampler objects so one texture serves
  multiple material slots; keep that for compressed + array textures.
- **Capabilities.** Add GL capability strings as features land
  (`BackendSupports("rt-finalize"/"occlusion"/"clustered-lighting"/"shadow-csm"/"bc7"/"s3tc"/...)`),
  driven by runtime extension/version checks; default `false`.

## 3. Per-phase OpenGL implementation + test checklist

### Phase C — carryover
| ID | Implement | Test |
|---|---|---|
| GL-C-1 | `build_viper_linux.sh` green; `-L graphics3d` green on Linux/X11 (CO-1) | Full ctest run recorded on the Linux machine |
| GL-C-2 | Render-target finalization for default framebuffer **and** FBO (CO-4) | `ScreenshotFinal`/`Flip` no double-apply for FBO frames |
| GL-C-3 | GPU timing via `ARB_timer_query` (`GL_TIME_ELAPSED`) for the perf lane (CO-11) | Frame GPU time recorded on reference Linux HW |
| GL-C-4 | Confirm committed skinned character renders via 256-bone UBO (CO-9) | Visual parity vs software within tolerance |

### Phase 1 — concurrency
| ID | Implement | Test |
|---|---|---|
| GL-1-1 | Worker pool on `rt_platform.h` pthreads (`PTHREAD_MUTEX_INITIALIZER`); GCC/clang atomics; ARM64 `__dmb` barriers where applicable | TSan-clean stress run |
| GL-1-2 | Main-thread GL upload (PBO) or GLX shared-context worker; workers produce CPU payloads | `runFrames` parity pool on/off on Linux |

### Phase 2 — floating origin
| ID | Implement | Test |
|---|---|---|
| GL-2-1 | Camera-relative model matrix (GLSL 330, `glUniformMatrix4fv` transpose) | 50 km far-origin matches near-origin within tolerance |

### Phase 4 — async asset + GPU upload
| ID | Implement | Test |
|---|---|---|
| GL-4-1 | PBO-based async texture/buffer upload on the main commit queue (preserve row-flip) | async==blocking resource equality; `glGetError` clean under churn |
| GL-4-2 | Mip/LOD upload + residency eviction (`glDeleteTextures`/buffers) | GPU/process memory returns to baseline after churn |

### Phase 5 — world partition + terrain streaming
| ID | Implement | Test |
|---|---|---|
| GL-5-1 | Per-tile `glTexImage2D` + `GL_TEXTURE_2D_ARRAY` splat layers; per-tile heightfield textures | tile stream in/out across GLX resize; no seams |

### Phase 6 — visibility (occlusion + LOD)
| ID | Implement | Test |
|---|---|---|
| GL-6-1 | Occlusion via `GL_SAMPLES_PASSED`/`GL_ANY_SAMPLES_PASSED` + `GL_CONDITIONAL_RENDER`; **Hi-Z compute unavailable at 3.3 → software occluder path is the Linux baseline** (gate Hi-Z off) | occluded-draw skip counts vs frustum-only baseline |
| GL-6-2 | Impostor/HLOD render-to-FBO | LOD/impostor swap stability vs full-detail |

### Phase 7 — lighting (clustered + CSM)
| ID | Implement | Test |
|---|---|---|
| GL-7-1 | CPU cluster build → upload per-cluster light index via **TBO** (preferred) or **UBO** (size-limited); GLSL 330 forward+ loop; expose cluster/light cap as capability | >16 lights correct vs naive forward |
| GL-7-2 | CSM: depth `GL_TEXTURE_2D_ARRAY` + `sampler2DArrayShadow`; layered render via geometry shader (3.2) or per-cascade passes; honor `Light3D.CastsShadows` | CSM stability; fallback to 16-light forward |

### Phase 11 — asset pipeline (texture compression)
| ID | Implement | Test |
|---|---|---|
| GL-11-1 | `glCompressedTexImage2D` for **S3TC (BC1–3)** + **BPTC (BC7/BC6H)** via runtime-checked extensions | compressed vs raw within tolerance; recorded VRAM reduction |
| GL-11-2 | KTX2/precompressed block upload → BC7/S3TC when extensions exist; **ETC2/ASTC capability `false`** on desktop, software-decode fallback | KTX2/precompressed asset loads; capability query honest; extension check before use |
| GL-11-3 | glTF camera/multi-scene are loader-side (backend-neutral); Basis/Draco/meshopt remain optional Phase 11b | confirm imported camera/scene render under OpenGL |

### Phase 12 — vertical slice
| ID | Implement | Test |
|---|---|---|
| GL-12-1 | Run `examples/3d/openworld_slice/` on the OpenGL backend | smoke + deterministic replay + recorded Linux perf baselines |

## 4. CPU / backend-neutral phases (no OpenGL-specific code)

Phases **3** (spatial index), **8** (physics), **9** (navigation), **10**
(animation/IK logic) run on the shared C runtime. OpenGL work is limited to
confirming render parity (e.g. IK/skinning still upload through the existing
256-bone UBO). No new GLSL/resource work expected.

## 5. Linux sign-off checklist

- [ ] `build_viper_linux.sh` + `build_demos_linux.sh` succeed (GCC/Clang)
- [ ] `ctest -L graphics3d` green on Linux/X11 for every shipped phase
- [ ] each new feature has a GLSL 330 path + runtime capability check + software-baseline diff
- [ ] GL-3.3-ceiling fallbacks chosen and documented per feature (compute/SSBO/ETC2/ASTC)
- [ ] async upload clean under churn (`glGetError` clean, no leak); TSan-clean job system
- [ ] GLX resize + FBO finalization correct; top-left texture origin preserved
- [ ] perf baseline recorded on named Linux reference hardware
- [ ] no raw `__linux__` outside approved adapters (`lint_platform_policy.sh`)
