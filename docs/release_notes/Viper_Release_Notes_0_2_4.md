# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

> **DRAFT** — This document is a preliminary draft for v0.2.4. Content is subject to change before
> the official release.

## Version 0.2.4 - Pre-Alpha (TBD) — DRAFT

### Release Overview

Version 0.2.4 is a rendering, codegen, and showcase release. Highlights:

- **Metal Backend: Feature-Complete** — All 14 backend plans implemented, bringing Metal from 47% to 94% feature parity with the software renderer. GPU skinning, morph targets, shadow mapping, terrain splatting, post-processing, and instanced rendering.
- **Software Renderer Upgrades** — Per-pixel terrain splatting (4-layer weight blend), bilinear filtering, vertex color support, and shadow mapping.
- **AArch64 Codegen Hardening** — Immediate utils extraction, binary encoder fixes, refcount injection bugfix, fastpath improvements, and 10+ new codegen tests.
- **XENOSCAPE Demo Game** — Flagship Metroid-style sidescroller expanded from 720 LOC to 17K LOC across 26 files with 10 interconnected levels, 30+ enemy types, boss fights, save system, achievement tracking, procedural music, and ability-gated progression.
- **Zia Language: `entity`/`value` renamed to `class`/`struct`** — Mainstream keyword alignment across all source, tests, REPL, LSP, docs, and VS Code extension.
- **VAPS Packaging Overhaul** — 10 improvements, 57 new tests, Windows installer stub, symlink safety, dry-run mode.
- **Documentation Pass** — Viper file headers on 100% of 2,668 source files, @brief doxygen on 98% of runtime functions.

#### By the Numbers

| Metric | v0.2.3 | v0.2.4 | Delta |
|--------|--------|--------|-------|
| Commits | — | 21 | +21 |
| Source files | 2,671 | 2,700 | +29 |
| Production SLOC | ~348K | ~382K | +34K |
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

**Parity score: 47% → 94%** (32/34 features). Remaining gaps: VBO pooling optimization.

Shared infrastructure changes:
- `vgfx3d_draw_cmd_t` extended with `bone_palette`, `bone_count`, `morph_deltas`, `morph_weights`, `morph_shape_count`
- `vgfx3d_backend_t` gains optional `submit_draw_instanced` vtable entry
- `vgfx3d_postfx_snapshot_t` export API decouples GPU backends from private PostFX structs
- `InstanceBatch3D` dispatches through GPU hook when available, falls back to N individual draws

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
- **AArch64: `i1` parameter masking** — Boolean parameters masked with `AND 1` at function entry, matching return-value masking. Prevents upper-bit garbage corruption.
- **AArch64: remove redundant refcount injection** — `emitRefcountedStore` lambda stripped from instruction lowering. String ownership belongs in the IL layer; the codegen backend should not inject phantom `rt_str_retain_maybe`/`rt_str_release_maybe` calls.
- **AArch64: immediate utils extraction** — `A64ImmediateUtils.hpp` helper for immediate encoding, asm emitter hardening, binary encoder fixes, arithmetic/call fastpath improvements, regpool and symbol resolver fixes.
- **Native linker: `RtComponent::Game`** — Game runtime classes link correctly via `libviper_rt_game.a` after directory reorganization.

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
- **53 backend implementation plans** — Detailed plans across all 4 renderers (SW: 7, Metal: 14, OpenGL: 16, D3D11: 16) with verified feature parity matrix

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

- **File headers**: Viper license header on 100% of 2,668 source files (257 newly added)
- **Doxygen**: `@brief`/`@param`/`@return` comments on 98% of runtime `.c` files, 100% of runtime `.h` files
- **Clang-format**: `BreakBeforeBraces` switched from Allman to Attach across all 2,669 source files
- **SLOC script**: `scripts/count_sloc.sh` with `--summary`, `--subsystem`, `--all`, `--json` modes
- **3D architecture docs**: Metal shader feature table, backend parity matrix, terrain splat pipeline

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

- **Platformer Reference** — 10-file teaching codebase (2K LOC) demonstrating 25+ runtime APIs with thorough inline documentation

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
