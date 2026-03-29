# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

> **DRAFT** — This document is a preliminary draft for v0.2.4. Content is subject to change before
> the official release.

## Version 0.2.4 - Pre-Alpha (TBD) — DRAFT

### Release Overview

Version 0.2.4 is a rendering, codegen, documentation, and showcase release. Highlights:

- **Metal Backend: Feature-Complete** — All 14 backend plans implemented, bringing Metal from 47% to 94% feature parity with the software renderer. GPU skinning, morph targets, shadow mapping, terrain splatting, post-processing, and instanced rendering.
- **D3D11 Backend: 20 Features Implemented** — All 20 D3D11 backend plans implemented in a 3,173-line HLSL+C backend rewrite. Diffuse textures, normal/specular/emissive maps, spot lights, fog, wireframe/cull, render-to-texture, GPU skinning, morph targets (with normal deltas), shadow mapping, instanced rendering, terrain splatting, post-processing (bloom, FXAA, tonemap, DOF, motion blur, SSAO), cubemap skybox, and environment reflections. Windows CI validation job added.
- **Software Renderer Upgrades** — Per-pixel terrain splatting (4-layer weight blend), bilinear filtering, vertex color support, and shadow mapping.
- **Windows x86_64 Native Assembler Fixes** — CoffWriter cross-section symbol resolution, X64BinaryEncoder runtime symbol mapping, and process isolation hang fix. Windows native executables now link and run correctly.
- **AArch64 Codegen Hardening** — Immediate utils extraction, binary encoder fixes, refcount injection bugfix, fastpath improvements, and 10+ new codegen tests.
- **XENOSCAPE Demo Game** — Flagship Metroid-style sidescroller expanded from 720 LOC to 17K LOC across 26 files with 10 interconnected levels, 30+ enemy types, boss fights, save system, achievement tracking, procedural music, and ability-gated progression.
- **Zia Language: `entity`/`value` renamed to `class`/`struct`** — Mainstream keyword alignment across all source, tests, REPL, LSP, docs, and VS Code extension.
- **VAPS Packaging Overhaul** — 10 improvements, 57 new tests, Windows installer stub, symlink safety, dry-run mode.
- **Comprehensive Documentation Review** — 39 stale files deleted, 70+ factual errors corrected across 30+ docs, Viper file headers on 100% of 2,705 source files, @brief Doxygen on 98% of runtime functions.

#### By the Numbers

| Metric | v0.2.3 | v0.2.4 | Delta |
|--------|--------|--------|-------|
| Commits | — | 33 | +33 |
| Source files | 2,671 | 2,705 | +34 |
| Production SLOC | ~348K | ~388K | +40K |
| Test count | 1,351 | 1,358 | +7 |

---

### Metal Backend — Feature-Complete (14 Plans)

All 14 Metal implementation plans shipped in this release, making Metal the first GPU backend with near-complete feature parity:

| Plan | Feature | Summary |
|------|---------|---------|
| MTL-01 | Lit texture | `baseColor` sampled before lit/unlit branch — textured meshes now properly lit |
| MTL-02 | Spot lights | Smoothstep cone attenuation via `inner_cos`/`outer_cos` in MSL Light struct |
| MTL-03 | Texture cache | Per-frame `NSMutableDictionary` keyed by Pixels pointer; shared sampler created once |
| MTL-04 | Normal map | TBN perturbation with Gram-Schmidt orthonormalization, degenerate tangent guard |
| MTL-05 | Specular map | Per-texel specular modulation (texture slot 2) |
| MTL-06 | Emissive map | Additive emissive sampling (texture slot 3) |
| MTL-07 | Fog | Linear distance fog in PerScene cbuffer, per-pixel blend in fragment shader |
| MTL-08 | Wireframe | `setTriangleFillMode:MTLTriangleFillModeLines` per draw call |
| MTL-09 | GPU skinning | 4-bone weighted skinning in vertex shader via bone palette buffer(3) |
| MTL-10 | GPU morph targets | Vertex shader delta accumulation via buffer(4-5), respects 4KB inline limit |
| MTL-11 | Post-processing | Separate fullscreen quad pipeline: bloom, FXAA, Reinhard/ACES tonemap, vignette, color grading |
| MTL-12 | Shadow mapping | Depth-only render pass + `Depth32Float` comparison sampler with PCF filtering |
| MTL-13 | Instanced rendering | `submit_draw_instanced` vtable hook, shared VB/IB with per-instance model matrix |
| MTL-14 | Terrain splat | 4-layer weight blend via texture slots 5-9, per-layer UV tiling |

**Metal parity score: 47% → 94%** (32/34 features). Remaining gaps: VBO pooling optimization.

Shared infrastructure changes:
- `vgfx3d_draw_cmd_t` extended with `bone_palette`, `bone_count`, `morph_deltas`, `morph_weights`, `morph_shape_count`
- `vgfx3d_backend_t` gains optional `submit_draw_instanced` vtable entry
- `vgfx3d_postfx_snapshot_t` export API decouples GPU backends from private PostFX structs
- `InstanceBatch3D` dispatches through GPU hook when available, falls back to N individual draws

---

### D3D11 Backend — 20 Features Implemented

All 20 D3D11 backend features implemented in a ground-up rewrite of `vgfx3d_backend_d3d11.c` (3,173 LOC). The D3D11 backend now has full feature parity with Metal, bringing both GPU backends to near-complete coverage of the software renderer:

| Plan | Feature | Summary |
|------|---------|---------|
| D3D-01 | Diffuse texture | SRV creation + PS texture slot binding for `baseColor` sampling |
| D3D-02 | Normal matrix | Inverse-transpose `normalMatrix` in per-object cbuffer |
| D3D-03 | Texture cache | Per-frame `ID3D11ShaderResourceView` cache keyed by Pixels pointer |
| D3D-04 | Spot lights | Smoothstep cone attenuation in HLSL pixel shader |
| D3D-05 | Wireframe / cull | `D3D11_FILL_WIREFRAME` rasterizer state, per-material cull mode |
| D3D-06 | Fog | Linear distance fog in per-scene constant buffer |
| D3D-07 | Normal map | TBN perturbation with Gram-Schmidt in HLSL, texture slot 1 |
| D3D-08 | Specular / emissive | Per-texel specular (slot 2) and additive emissive (slot 3) maps |
| D3D-09 | Render-to-texture | Offscreen `ID3D11RenderTargetView` + resolve for RTT passes |
| D3D-10 | Skinning / morph | 4-bone vertex skinning + morph target delta accumulation in VS |
| D3D-11 | Post-processing | Fullscreen quad pipeline: bloom, FXAA, tonemap, vignette, color grading |
| D3D-12 | VBO optimization | Shared vertex/index buffer pooling with sub-allocation |
| D3D-13 | HRESULT checks | Systematic `FAILED(hr)` validation on all D3D11 API calls |
| D3D-14 | Shadow mapping | Depth-only pass + `D3D11_COMPARISON_LESS_EQUAL` sampler with PCF |
| D3D-15 | Instanced rendering | Per-instance model matrix via structured buffer SRV |
| D3D-16 | Terrain splat | 4-layer weight blend via texture slots 5-9 |
| D3D-17 | Cubemap / skybox | `TextureCube` SRV + dedicated skybox VS/PS, drawn first with depth write disabled |
| D3D-18 | Environment reflections | Cubemap sampling in PS using reflected view vector, fresnel blend |
| D3D-19 | Morph normal deltas | Normal + tangent delta channels in morph target buffer |
| D3D-20 | Advanced post-FX | Depth-of-field, motion blur, SSAO, chromatic aberration |

HLSL shader features: unified uber-shader with skinning (4-bone vertex weights via `BonesCurrent`/`BonesPrevious` cbuffers), morph targets (position + normal delta accumulation from `Buffer<float>` SRVs), instanced rendering (per-instance model/normal/prev matrices via vertex stream), shadow sampling (PCF with comparison sampler), terrain splatting (4-layer RGBA weight blend), environment reflections (cubemap sampling with fresnel), and motion vectors (`currClip`/`prevClip` for temporal effects).

Infrastructure:
- `vgfx3d_backend_utils.c/.h` — shared backend utility functions (216+ LOC)
- Windows-only `windows-d3d11` CI job for focused D3D11 build and test validation
- GPU path unit tests (`test_rt_canvas3d_gpu_paths` — 595 LOC, `test_vgfx3d_backend_utils` — 224 LOC)

---

### Software Renderer Upgrades

- **Per-pixel terrain splatting** — Full 4-layer weight-blended terrain renderer (572+ LOC) sampling splat map RGBA channels with per-layer UV tiling scales
- **NavMesh3D optimization** — Adjacency build from O(n^2) to O(n) via edge hash map
- **Canvas3D framebuffer clear** — uint32 writes instead of per-byte loop (~4x faster at 1080p)
- **Animation crossfade** — TRS decomposition + quaternion SLERP replaces raw matrix lerp (eliminates shear artifacts)

---

### Compiler & Codegen

- **Zia: `entity` → `class`, `value` → `struct`** — Full rename across lexer, parser, sema, lowerer, REPL, LSP, runtime GUI, tests, docs, VS Code extension, and website. 1358/1358 tests passing after migration.
- **Zia sema: runtime property setter resolution** — Property assignments on runtime class instances (e.g., `ctrl.VY = value`) now call the setter function via symbol lookup. Previously fell through to invalid direct memory writes.
- **Windows x86_64: CoffWriter cross-section symbol resolution** — Rodata symbols (`.LC_str_*`) were emitted as undefined in the COFF symbol table, causing LNK2001 linker errors. Rodata symbols now processed first with text relocations redirected to defined entries.
- **Windows x86_64: X64BinaryEncoder runtime symbol mapping** — External calls used raw IL names (`Viper.Terminal.PrintStr`) instead of C runtime names (`rt_print_str`). Added `mapRuntimeSymbol()` matching the AArch64 pattern.
- **Windows x86_64: ProcessIsolation hang fix** — `CrossLayerArithTests` missing `dispatchChild()` guard caused infinite process recursion via `CreateProcess` self-relaunch.
- **AArch64: `i1` parameter masking** — Boolean parameters masked with `AND 1` at function entry, matching return-value masking. Prevents upper-bit garbage corruption.
- **AArch64: remove redundant refcount injection** — `emitRefcountedStore` lambda stripped from instruction lowering. String ownership belongs in the IL layer; the codegen backend should not inject phantom `rt_str_retain_maybe`/`rt_str_release_maybe` calls.
- **AArch64: immediate utils extraction** — `A64ImmediateUtils.hpp` helper for immediate encoding, asm emitter hardening, binary encoder fixes, arithmetic/call fastpath improvements, regpool and symbol resolver fixes.
- **Native linker: `RtComponent::Game`** — Game runtime classes link correctly via `libviper_rt_game.a` after directory reorganization.
- **Native linker: `-lshell32`** — Added to Windows linker command for `DragQueryFile`/`DragAcceptFiles` GUI support.

---

### 3D Graphics Engine Improvements

- **Light3D.NewSpot** — Spot light with position, direction, inner/outer cone angles, and smoothstep attenuation
- **Camera3D.NewOrtho** — Orthographic camera for isometric/strategy games
- **Mesh3D.Clear()** — Reset vertex/index counts without freeing backing arrays (enables mesh reuse)
- **Sprite3D use-after-free fix** — Per-frame mesh/material allocation replaced with cached instances + GC temp buffer registration
- **Physics3D shape-specific collision** — Sphere-sphere radial + AABB-sphere closest-point narrow-phase (replaces AABB-only)
- **Physics3D character controller** — Slide-and-step movement replaces trivial velocity-set
- **Physics3D collision events** — `CollisionCount`, `GetCollisionBodyA/B`, `GetCollisionNormal/Depth` queue
- **DistanceJoint3D / SpringJoint3D** — Physics joint constraints with 6-iteration sequential impulse solver
- **Audio3D** — Per-voice `max_distance` tracking table (replaces shared global that caused cross-voice attenuation bugs)
- **Terrain3D** — `SetSplatMap` + 4 layer textures with per-layer UV tiling, baked blend fallback
- **53 backend implementation plans** — Detailed plans across all 4 renderers (SW: 7, Metal: 14, OpenGL: 16, D3D11: 16) plus 4 additional D3D11 plans (cubemap, env reflections, morph normals, advanced post-FX)

---

### New Game Runtime Classes

Five additions to the `Viper.Game` namespace:

- **Timer ms-mode** — `StartMs(durationMs)`, `UpdateMs(dt)`, `ElapsedMs`, `RemainingMs` for delta-time-independent cooldowns
- **Lighting2D** — Darkness overlay with pulsing player light and pooled dynamic point lights
- **PlatformerController** — Jump buffering, coyote time, variable jump height, ground/air acceleration curves, apex gravity bonus
- **AchievementTracker** — Up to 64 achievements with bitmask tracking, 32 stat counters, animated slide-in popups
- **Typewriter** — Character-by-character text reveal with configurable ms-per-character rate

**Runtime directory reorganization**: Game engine classes moved from `src/runtime/collections/` to `src/runtime/game/` (36 files). No API changes.

---

### VAPS Packaging System

Comprehensive overhaul with 10 improvements:
- Shared `PkgUtils.hpp` (readFile, name normalizers, safeDirectoryIterate)
- Warn on missing icons/assets/invalid versions (eliminate silent failures)
- Symlink safety with root-escape detection in all directory traversals
- `package-category` and `package-depends` manifest directives
- ARM64 Windows PE support (machine type 0xAA64)
- `--dry-run` and `--verbose` CLI modes for package content preview
- Post-build verification (`PkgVerify`) for ZIP, .deb, and PE structural checks
- `.lnk` LinkInfo with VolumeID + LocalBasePath for reliable shortcut resolution
- Windows installer stub architecture (InstallerStubGen x86-64 emitter, IAT wiring, ZipReader)
- Fix DEFLATE double-free crash and `.lnk` missing LinkInfo

57 new packaging tests.

---

### Documentation

- **Comprehensive review** — 39 stale/obsolete doc files deleted (-24K lines), 70+ factual errors corrected across 30+ documents. Deleted files include resolved bug trackers, historical stress tests, completed plans, and deprecated specs.
- **Factual corrections** — `entity`→`class` terminology in 15 files, IL version 0.1→0.2.0, AArch64 status updates (all demos, 89 tests, coalescer/peephole decomp), Win64 ABI marked as implemented (not missing), Linux/Windows graphics backends marked as implemented (not stubs), Metal winding corrected to `MTLWindingCounterClockwise`, 6 missing IL pass docs added (GVN, LICM, EHOpt, LoopRotate, Reassociate, SiblingRecursion).
- **Test & runtime accuracy** — Test label counts updated to actual values (codegen 121, runtime 352, zia 99, il 196, total 1358), `runtime.def` counts updated (225→293 functions, 3129→3965 entries), GC cycle detection documented (trial-deletion algorithm), `fcmp` opcode mnemonics corrected to underscore format.
- **File headers** — Viper license header on 100% of 2,705 source files (257 newly added)
- **Doxygen** — `@brief`/`@param`/`@return` comments on 98% of runtime `.c` files, 100% of runtime `.h` files. Includes `rt_output.c`, `rt_memory.c`, `rt_platform.h`, `rt_string.h`, `rt_pool.c`, `rt_gc.c`.
- **Codemap updates** — File counts refreshed (il-core 24, il-transform 72, runtime 522, tools 113, zia 74, basic 278), 3D Graphics Engine section added (122 files), lsp-common and vbasic-server entries.
- **Clang-format** — `BreakBeforeBraces` switched from Allman to Attach across all 2,669 source files
- **SLOC script** — `scripts/count_sloc.sh` with `--summary`, `--subsystem`, `--all`, `--json` modes
- **3D architecture docs** — Metal shader feature table, backend parity matrix, terrain splat pipeline, D3D11 backend guide

---

### Demo Games

- **XENOSCAPE** — Flagship Metroid-style action exploration sidescroller:
  - 10 interconnected levels: Crash Site, Fungal Caverns, Crystal Depths, Surface Ruins, Underground Lake, Thermal Vents, Overgrowth, Frozen Abyss, Corrupted Ruins, The Core
  - 30+ enemy types across 5 biome families (fungal, crystal, thermal, frozen, corrupted) with unique AI and procedural sprites
  - 4 boss fights: Spore Mother, Crystal Wyrm, Magma Core, The Architect
  - Ability system: wall jump, double jump, dash, charge shot, ground pound, grapple hook (progression gates)
  - Save system (3 slots), world map, lore terminals (30+ entries), achievement tracking (32 achievements)
  - Procedural music via MusicGen, dynamic lighting, biome crossfade transitions via Color.Lerp
  - Color.RGB() palette system with constructor injection for theme-swapping
  - Runtime API migration: PlatformerController, CollisionRect, Tilemap, ObjectPool, Quadtree, Camera, Lighting2D, AchievementTracker, Typewriter
  - 17,023 LOC across 26 files (was 720 LOC / 5 files)

---

### Bug Fixes

- Particle emitter renders with zero alpha from `Color.RGB()` values (alpha byte = 0 treated as opaque)
- Runtime property setter calls on runtime classes (Zia sema symbol lookup)
- `i1` boolean parameter corruption in AArch64 native codegen
- Native linker missing `libviper_rt_game.a` archive (symbol-not-found crash)
- AArch64 string store redundant refcount injection (ownership belongs in IL layer)
- PlatformerController velocity desync after damage knockback, death bounce, enemy stomp
- Sidescroller expansion systems not wired into game loop
- Sprite3D use-after-free: per-frame allocation → cached instances + GC temp buffer
- Metal lit texture bug: diffuse texture only sampled in unlit path
- Metal spot lights fall through to ambient (no cone attenuation)
- Metal texture + sampler recreated every draw call (now cached per-frame)
- Zia `hide final` in class bodies reports clear error instead of generic parse error
- Zia non-constant `final` initializers report V3202 instead of silent drop
- DEFLATE double-free crash in VAPS packaging
- `.lnk` shortcuts missing LinkInfo structure
- Windows x86_64 CoffWriter: rodata symbols emitted as undefined causing LNK2001 linker errors
- Windows x86_64 BinaryEncoder: external calls used raw IL names instead of C runtime names
- Windows `CrossLayerArithTests`: missing `dispatchChild()` guard caused infinite process recursion
