# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

> **DRAFT** — This document is a preliminary draft for v0.2.4. Content is subject to change before
> the official release.

## Version 0.2.4 - Pre-Alpha (TBD) — DRAFT

### Release Overview

Version 0.2.4 is a rendering, codegen, language features, media codecs, documentation, and showcase release. Highlights:

- **Media Codec Suite** — From-scratch implementations of JPEG, GIF (animated), OGG Vorbis, and MP3 decoders. Extended PNG decoder to all color types, bit depths, interlacing, and transparency. Extended WAV loader to 24-bit and float32 PCM. Added OGG/MP3 music streaming with on-the-fly resampling. Added `Pixels.Load()` auto-detect, JPEG EXIF orientation, fast FFT-based IMDCT for Vorbis, and multi-pass residue decoding. 16 new runtime source files, 8 new tests.
- **Runtime Stub Audit & Fixes** — Comprehensive audit of all C/C++ runtime stubs. Fixed `rt_exc_is_exception()` type safety, OOP destructor chaining (derived→base), OOP refcount imbalance (NEW temporary leak), TLS RSA-PSS SHA-384/SHA-512 hashing, bytecode VM missing opcodes, POSIX process isolation timeout, and enabled Windows threading tests.
- **Native PE/COFF Linker Pipeline** — Full Windows native linking without clang. COFF archive (.lib) reader, symbol resolver, section merger, dead-strip pass, ICF, relocation applier with `IMAGE_REL_AMD64` support, and PE executable writer with proper `.idata` import tables. Combined with the v0.2.3 assembler, `viper build` now produces native Windows executables end-to-end with zero external tool dependencies.
- **Zia Language Features** — Seven new features: variadic parameters (`func sum(nums: ...Integer)`), type aliases (`type Name = TargetType;`), shift operators (`<<`, `>>`), compound bitwise assignments (`<<=`, `>>=`, `&=`, `|=`, `^=`), single-expression functions (`func f(x: T) -> R = expr;`), lambda expressions (`func(params) -> RetType { body }`), and polymorphic `is` expressions that check the full subclass hierarchy.
- **Metal Backend: Feature-Complete** — All 14 backend plans implemented, bringing Metal from 47% to 94% feature parity with the software renderer. GPU skinning, morph targets, shadow mapping, terrain splatting, post-processing, and instanced rendering.
- **D3D11 Backend: 20 Features Implemented** — All 20 D3D11 backend plans implemented in a 3,173-line HLSL+C backend rewrite. Diffuse textures, normal/specular/emissive maps, spot lights, fog, wireframe/cull, render-to-texture, GPU skinning, morph targets (with normal deltas), shadow mapping, instanced rendering, terrain splatting, post-processing (bloom, FXAA, tonemap, DOF, motion blur, SSAO), cubemap skybox, and environment reflections. Windows CI validation job added.
- **Software Renderer Upgrades** — Per-pixel terrain splatting (4-layer weight blend), bilinear filtering, vertex color support, and shadow mapping.
- **Windows x86_64 Codegen Hardening** — CoffWriter cross-section symbol resolution, X64BinaryEncoder runtime symbol mapping, operand materialisation for TESTrr/call.indirect, SETcc REX prefix for byte registers, SSE RIP-relative MOVSD encoding, unsafe spill slot reuse disabled, and process isolation hang fix. Windows native executables now assemble, link, and run correctly.
- **AArch64 Codegen Hardening** — Immediate utils extraction, binary encoder fixes, refcount injection bugfix, fastpath improvements, trap message forwarding, error field extraction via TLS bridge, Apple M-series scheduler latency tuning, and 10+ new codegen tests.
- **Zia Compiler Bug Fixes** — String bracket-index crash, `List[Boolean]` unboxing truncation, `catch(e)` binding via TLS message passing, `String.Contains()` method alias. New `ErrGetMsg` IL opcode and `rt_throw_msg_set/get` runtime functions for exception message propagation.
- **XENOSCAPE Demo Game** — Flagship Metroid-style sidescroller expanded from 720 LOC to 17K LOC across 26 files with 10 interconnected levels, 30+ enemy types, boss fights, save system, achievement tracking, procedural music, and ability-gated progression.
- **Zia Language: `entity`/`value` renamed to `class`/`struct`** — Mainstream keyword alignment across all source, tests, REPL, LSP, docs, and VS Code extension.
- **VAPS Packaging Overhaul** — 10 improvements, 57 new tests, Windows installer stub, symlink safety, dry-run mode.
- **Comprehensive Documentation Review** — 39 stale files deleted, 70+ factual errors corrected across 30+ docs, Viper file headers on 100% of 2,706 source files, @brief Doxygen on 98% of runtime functions. Bible code audit across 12 chapters correcting struct field syntax, class method visibility, interface declarations, catch syntax, and collection API calls.

#### By the Numbers

| Metric | v0.2.3 | v0.2.4 | Delta |
|--------|--------|--------|-------|
| Commits | — | 50 | +50 |
| Source files | 2,671 | 2,736 | +65 |
| Production SLOC | ~348K | ~398K | +50K |
| Test count | 1,351 | 1,370 | +19 |

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

### Media Codec Suite

From-scratch implementations of 4 new media decoders plus major extensions to existing loaders, all with zero external dependencies. Viper now handles the most common image and audio formats natively.

#### Image Formats

| Format | Capability | Details |
|--------|-----------|---------|
| **JPEG** | Load | Baseline DCT, 8-bit, YCbCr/grayscale, 4:4:4/4:2:0/4:2:2, EXIF orientation (auto-rotate), restart markers |
| **PNG** | Load + Save | All 5 color types (grayscale, RGB, indexed, gray+alpha, RGBA), 1/2/4/8/16-bit depths, PLTE/tRNS transparency, Adam7 interlace, proper 16→8 bit rounding |
| **GIF** | Load | GIF87a/89a, LZW decompression, multi-frame animation (up to 64 frames), all 4 disposal methods, interlacing, per-frame delay, transparency, local color tables |
| **BMP** | Load + Save | 24-bit uncompressed (unchanged) |

New API: `Pixels.Load(path)` auto-detects format from magic bytes (PNG, JPEG, BMP, GIF). `Sprite.FromFile(path)` extended to support all 4 formats; animated GIFs load all frames with timing.

#### Audio Formats

| Format | Sound FX | Music Stream | Details |
|--------|----------|-------------|---------|
| **WAV** | Yes | Yes | 8/16/24/32-bit PCM + 32-bit IEEE float. Any sample rate (resampled to 44100 Hz) |
| **OGG Vorbis** | Yes (full decode) | Yes (streaming) | Vorbis I codec: codebook VQ, floor type 1, multi-pass residue (types 0/1/2), FFT-based IMDCT, stereo coupling. Music streamed with triple-buffer on-the-fly decode |
| **MP3** | Yes (full decode) | Yes (streaming) | MPEG-1/2/2.5 Layer III: Huffman decode (tree-walk for tables 1-6), scalefactors, requantization, anti-alias, IMDCT, polyphase synthesis, MS stereo, bit reservoir. ID3v2 + ID3v1 tag handling. Music streamed per-frame |
| **VAF** | Yes | Block-based | IMA ADPCM 4:1 compression (unchanged) |

Music streaming for OGG and MP3 uses the same triple-buffer architecture as WAV (~96 KB buffer memory regardless of track length). Seamless looping supported for all formats.

New files: `rt_ogg.c/h` (OGG container parser, 276 LOC), `rt_vorbis.c/h` (Vorbis codec, 1100+ LOC), `rt_mp3.c/h` (MP3 decoder, 1000+ LOC), `rt_mp3_tables.h` (spec constants), `rt_gif.c/h` (GIF decoder, 420 LOC).

---

### Runtime Stub Audit & Bug Fixes

Comprehensive audit of all C/C++ runtime source files identified 21 stubbed or incomplete implementations. Key fixes:

| Issue | Severity | Fix |
|-------|----------|-----|
| `rt_exc_is_exception()` returned true for any non-null pointer | P0 | Proper `rt_obj_class_id() == RT_EXCEPTION_CLASS_ID` check |
| Destructor chaining missing (derived dtor never called base dtor) | P0 | `emitClassDestructor` now emits `call @Base.__dtor` |
| OOP refcount imbalance (NEW objects never reached refcount 0) | P0 | Release NEW temporary after assignment in `lowerLet` |
| TLS RSA-PSS only supported SHA-256 content hashing | P1 | Added SHA-384/SHA-512 via CommonCrypto (macOS) and dlopen'd EVP_Digest (Linux) |
| Bytecode VM missing LOAD_I32, LOAD_STR_MEM, STORE_STR_MEM | P1 | Added threaded-dispatch labels |
| POSIX process isolation had no timeout (blocking waitpid) | P1 | WNOHANG poll loop with clock_gettime + SIGKILL on timeout |
| Windows Viper.Threads test disabled but runtime implemented | P1 | Removed `#ifdef _WIN32 return 0` guard |

---

### Software Renderer Upgrades

- **Per-pixel terrain splatting** — Full 4-layer weight-blended terrain renderer (572+ LOC) sampling splat map RGBA channels with per-layer UV tiling scales
- **NavMesh3D optimization** — Adjacency build from O(n^2) to O(n) via edge hash map
- **Canvas3D framebuffer clear** — uint32 writes instead of per-byte loop (~4x faster at 1080p)
- **Animation crossfade** — TRS decomposition + quaternion SLERP replaces raw matrix lerp (eliminates shear artifacts)

---

### Native PE/COFF Linker Pipeline

Full Windows native linking pipeline, eliminating the clang/link.exe dependency for producing Windows executables. `viper build` now goes from Zia source to `.exe` with zero external tools on all three platforms.

| Component | Description |
|-----------|-------------|
| **ArchiveReader** | Reads COFF `.lib` archives, extracts object files and symbol tables |
| **SymbolResolver** | Resolves symbols across object files and archives with on-demand archive member inclusion |
| **SectionMerger** | Merges `.text`, `.rdata`, `.data`, `.bss` sections across objects with alignment padding |
| **DeadStripPass** | Removes unreferenced sections via transitive reachability from entry point |
| **ICF** | Identical Code Folding — deduplicates sections with matching content and relocations |
| **RelocApplier** | Applies `IMAGE_REL_AMD64_*` relocations (ADDR64, ADDR32NB, REL32, SECTION, SECREL) |
| **PeExeWriter** | Produces PE32+ executables with `.idata` import tables, proper section alignment, and optional headers |

CodegenPipeline extended with native link mode: assembles COFF `.obj`, discovers runtime `.lib` archives via `RuntimeComponents`, and invokes the native linker pipeline. Comprehensive tests for all linker passes and PE output validation.

---

### Zia Language Features

Seven new language features expanding Zia's operator, declaration, and parameter surface:

- **Type alias declarations** — `type Name = TargetType;` creates compile-time aliases resolved during semantic analysis. No runtime representation; `typeAliases_` map in Sema, lookup integrated into `resolveNamedType()`.
- **Variadic parameters** — `func sum(nums: ...Integer)` accepts zero or more arguments, collected as `List[Integer]` inside the function body. The lowerer packs excess call-site arguments into a runtime List using `kListNew` + `kListAdd`. Only the last parameter may be variadic.
- **Shift operators** — `<<` (left shift) and `>>` (arithmetic right shift) with correct precedence between additive and comparison. New `parseShift()` precedence level, lowered to `Shl`/`AShr` IL opcodes.
- **Compound bitwise assignments** — `<<=`, `>>=`, `&=`, `|=`, `^=` follow the existing compound assignment desugaring pattern (read-op-store).
- **Single-expression functions** — `func f(x: Integer) -> Integer = x * 2;` desugars to a `ReturnStmt` wrapping the body expression. Works for both top-level functions and class methods.
- **Lambda expressions** — `func(params) -> RetType { body }` parsed in expression position when `func` is followed by `(`. Supports typed parameters and optional return type annotation.
- **Polymorphic `is` expressions** — `obj is Base` now returns true when `obj`'s runtime type is `Base` or any subclass of `Base`. `collectDescendants()` walks the class hierarchy, emitting an OR chain of `ICmpEq` comparisons (single comparison optimized for the no-subclass case).

---

### Compiler & Codegen

**Zia frontend:**
- **`entity` → `class`, `value` → `struct`** — Full rename across lexer, parser, sema, lowerer, REPL, LSP, runtime GUI, tests, docs, VS Code extension, and website.
- **Runtime property setter resolution** — Property assignments on runtime class instances (e.g., `ctrl.VY = value`) now call the setter function via symbol lookup. Previously fell through to invalid direct memory writes.
- **String bracket-index crash** — Added `String` case to `lowerIndex()`, emitting `Substring(base, idx, 1)` instead of falling through to the List path.
- **`List[Boolean]` unboxing** — Added `Trunc1` after `kUnboxI1` in `emitUnbox()` to narrow the i64 result back to i1.
- **`catch(e)` binding** — New `rt_throw_msg_set`/`rt_throw_msg_get` TLS runtime functions and `ErrGetMsg` IL opcode. `throw` stores the message, `catch` reads it as a String binding.
- **`String.Contains()` method** — Added `Contains` method alias to the String class mapping to the existing `StrHas` implementation.

**x86_64 backend:**
- **CoffWriter cross-section symbol resolution** — Rodata symbols (`.LC_str_*`) were emitted as undefined in the COFF symbol table, causing LNK2001 linker errors. Rodata symbols now processed first with text relocations redirected to defined entries.
- **X64BinaryEncoder runtime symbol mapping** — External calls used raw IL names (`Viper.Terminal.PrintStr`) instead of C runtime names (`rt_print_str`). Added `mapRuntimeSymbol()` matching the AArch64 pattern.
- **Operand materialisation** — Immediate operands for `TESTrr` (select/cond_br) and `call.indirect` callee now materialised into registers to prevent `bad_variant_access` crashes.
- **SETcc REX prefix** — Correct REX prefix emission for SPL/BPL/SIL/DIL byte registers.
- **SSE RIP-relative loads** — Added `MOVSD` encoding for xmm loads from RIP-relative labels.
- **Spill slot reuse disabled** — Interval analysis did not account for cross-block liveness, causing values still live in successor blocks to be overwritten. All three reuse sites now use the safe `ensureSpillSlot` path.
- **Binary encoder diagnostics** — `bad_variant_access` handler wraps encoding with instruction context for cleaner error reporting.
- **Pipeline error handling** — `pipeline.run()` wrapped in try/catch for cleaner error reporting.

**AArch64 backend:**
- **`i1` parameter masking** — Boolean parameters masked with `AND 1` at function entry, matching return-value masking. Prevents upper-bit garbage corruption.
- **Remove redundant refcount injection** — `emitRefcountedStore` lambda stripped from instruction lowering. String ownership belongs in the IL layer.
- **Immediate utils extraction** — `A64ImmediateUtils.hpp` helper for immediate encoding, asm emitter hardening, binary encoder fixes, arithmetic/call fastpath improvements, regpool and symbol resolver fixes.
- **Trap message forwarding** — `TrapErr` now materialises the message string operand into x0 and passes it to `rt_trap()`, enabling catch handlers to display the user's throw message in native executables.
- **Error field extraction via TLS** — `ErrGetKind`, `ErrGetCode`, and `ErrGetLine` now call runtime TLS accessors (`rt_trap_get_kind/code/line`) instead of returning hardcoded 0. `rt_trap()` auto-classifies the trap kind from the message prefix. Enables typed catch (`catch(e: DivideByZero)`) in native code.
- **Apple M-series scheduler tuning** — Instruction latency model updated for Firestorm cores: FP divide 3→10 cycles, integer divide 3→7, FP multiply 3→4. Improves instruction scheduling for FP-heavy code.

**Native linker:**
- `RtComponent::Game` — Game runtime classes link correctly via `libviper_rt_game.a` after directory reorganization.
- `-lshell32` — Added to Windows linker command for `DragQueryFile`/`DragAcceptFiles` GUI support.

**Runtime:**
- `SetErrorMode` + `_set_abort_behavior` added to `rt_init_stack_safety` on Windows to suppress crash/assert dialog boxes in natively compiled programs.

**Windows test infrastructure:**
- ProcessIsolation framework reworked: function pointers don't survive `CreateProcess`, so `registerChildFunction()` with indexed dispatch (`--viper-child-run=N`) replaces direct pointer passing. `dispatchChild()` added to `TEST_WITH_IL` macro and all 16 VM/conformance tests. Windows test failures reduced from 48 to 4.
- Codegen test assertions accept `.rdata` (Windows COFF) alongside `.rodata` (ELF), `cmovneq`→`cmovne` suffix fix, platform-adaptive paths for RTDiskFullTests, RTNetworkHardenTests, and test_vm_rt_trap_loc.

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
- **Bible code audit** — 12 chapters corrected: struct fields `name: Type` → `expose Type name` (Ch 11, 14–18), class methods `func` → `expose func` (Ch 14–18), interface methods reverted from incorrect `expose` (Ch 16–18), `catch e {}` → `catch {}` (Ch 10), `Split()[0]` → `Split().Get(0)` (Ch 08, 09, 12, 23), `[val; count]` repeat syntax → loop with `Push` (Ch 06).
- **File headers** — Viper license header on 100% of 2,706 source files (257 newly added)
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
- Zia string bracket-index crash: `lowerIndex()` missing String case fell through to List path
- Zia `List[Boolean]` unboxing: `kUnboxI1` returned i64, missing `Trunc1` to narrow to i1
- Zia `catch(e)` binding empty: throw now stores message via TLS, catch reads it as String
- Zia `String.Contains()` missing: added method alias to existing `StrHas` implementation
- Windows x86_64 CoffWriter: rodata symbols emitted as undefined causing LNK2001 linker errors
- Windows x86_64 BinaryEncoder: external calls used raw IL names instead of C runtime names
- Windows x86_64 operand materialisation: TESTrr and call.indirect immediate operands caused `bad_variant_access`
- Windows x86_64 SETcc REX prefix: incorrect encoding for SPL/BPL/SIL/DIL byte registers
- Windows x86_64 SSE RIP loads: missing MOVSD encoding for xmm loads from RIP-relative labels
- Windows x86_64 spill slot reuse: interval analysis missed cross-block liveness, overwriting live values
- Windows `CrossLayerArithTests`: missing `dispatchChild()` guard caused infinite process recursion
- Windows crash dialogs suppressed via `SetErrorMode` + `_set_abort_behavior` in `rt_init_stack_safety`
- AArch64 TrapErr: message string operand was discarded, native `throw "msg"` produced empty diagnostics
- AArch64 ErrGetKind/Code/Line: returned hardcoded 0, typed catch (`catch(e: DivideByZero)`) always fell through
- AArch64 scheduler: FP divide modeled at 3 cycles instead of 10, integer divide at 3 instead of 7 — suboptimal instruction ordering
- DllImport aggregate initializer missing `importNames` field (pre-existing `-Wmissing-field-initializers` warning)
- Stale comment in `rt_safe_i64.c` claiming Windows SafeI64 "not yet implemented" (it IS fully implemented)
- `rt_exc_is_exception()` accepted any non-null pointer as an exception (type safety violation)
- OOP destructor chaining: derived class destructors never called base class destructors
- OOP refcount imbalance: `NEW` objects had refcount 2 after assignment (creation ref never released), preventing destruction
- TLS CertificateVerify: RSA-PSS with SHA-384/SHA-512 failed because content hash was always SHA-256
- Bytecode VM: `LOAD_I32`, `LOAD_STR_MEM`, `STORE_STR_MEM` opcodes routed to unimplemented trap handler
- POSIX `ProcessIsolation::runIsolated()` blocked forever on hanging tests (no timeout implemented)
- PNG 16-bit sample downscaling discarded LSB (now uses round-to-nearest)
- MP3 ID3v1 tags at end of file could corrupt last decoded frame
- `vg_image_load_file()` GUI widget stub always returned false (now wired to `rt_pixels` decoders)
