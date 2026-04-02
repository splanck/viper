# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

> **DRAFT** — This document is a preliminary draft for v0.2.4. Content is subject to change before
> the official release.

## Version 0.2.4 - Pre-Alpha (TBD) — DRAFT

### Release Overview

Version 0.2.4 is a game engine, asset system, rendering, codegen, linker, language features, media codecs, documentation, and showcase release. Highlights:

- **3D Engine Enhancements** — Procedural terrain generation (`Terrain3D.GeneratePerlin`), terrain LOD with frustum culling and multi-resolution chunks, Gerstner wave water simulation (`Water3D.AddWave`), new `Vegetation3D` instanced grass/foliage system with wind animation, and material shader hooks (`SetShadingModel` for Toon/Fresnel/Emissive effects).
- **3D Format Loaders** — From-scratch glTF 2.0 (.gltf/.glb), STL (binary + ASCII), OBJ .mtl material parser, FBX texture and morph target extraction. Scene3D.Save for JSON serialization.
- **Video Playback** — MJPEG/AVI video decoder with `VideoPlayer` runtime class (Open/Play/Pause/Stop/Seek/Update), MJPEG DHT injection for AVI compatibility, AVI RIFF container parser, Theora codec infrastructure (header parsing, YCbCr 4:2:0→RGB conversion, OGG multi-stream demux), GUI `VideoWidget` for Viper.GUI applications, and Image widget paint fix for the GUI library.
- **Graphics Backend Hardening** — Generation-aware texture/cubemap caching across all 4 backends (Metal/D3D11/OpenGL/SW), Canvas3D window resize handling, GPU screenshot readback, InstanceBatch3D memory safety, Mesh3D inverse-transpose normal transform, Pixels mutation tracking, and macOS default application menu.
- **Native Linker Hardening** — BranchTrampoline rewritten with boundary-based placement, SectionMerger VA logic extracted as shared API, SymbolResolver platform-aware dynamic symbol classification, RelocApplier range-checked REL32, multi-section COFF writer for function-level code sections, and Windows ARM64 native link gating.
- **Metal Backend: macOS 26 Compatibility** — Offscreen texture readback replaces CAMetalLayer direct presentation for macOS Tahoe compatibility. Backend vtable extended with `show/hide_gpu_layer` function pointers to fix software backend crash from duplicate global symbols.
- **Media Codec Suite** — From-scratch implementations of JPEG, GIF (animated), OGG Vorbis, and MP3 decoders. Extended PNG decoder to all color types, bit depths, interlacing, and transparency. Extended WAV loader to 24-bit and float32 PCM. Added OGG/MP3 music streaming with on-the-fly resampling. Added `Pixels.Load()` auto-detect, JPEG EXIF orientation, fast FFT-based IMDCT for Vorbis, and multi-pass residue decoding. 16 new runtime source files, 8 new tests.
- **Runtime Stub Audit & Fixes** — Comprehensive audit of all C/C++ runtime stubs. Fixed `rt_exc_is_exception()` type safety, OOP destructor chaining (derived→base), OOP refcount imbalance (NEW temporary leak), TLS RSA-PSS SHA-384/SHA-512 hashing, bytecode VM missing opcodes, POSIX process isolation timeout, and enabled Windows threading tests.
- **Native PE/COFF Linker Pipeline** — Full Windows native linking without clang. COFF archive (.lib) reader, symbol resolver, section merger, dead-strip pass, ICF, relocation applier with `IMAGE_REL_AMD64` support, and PE executable writer with proper `.idata` import tables. Combined with the v0.2.3 assembler, `viper build` now produces native Windows executables end-to-end with zero external tool dependencies.
- **Zia Language Features** — Seven new features: variadic parameters (`func sum(nums: ...Integer)`), type aliases (`type Name = TargetType;`), shift operators (`<<`, `>>`), compound bitwise assignments (`<<=`, `>>=`, `&=`, `|=`, `^=`), single-expression functions (`func f(x: T) -> R = expr;`), lambda expressions (`func(params) -> RetType { body }`), and polymorphic `is` expressions that check the full subclass hierarchy.
- **Metal Backend: Feature-Complete** — All 14 backend plans implemented, bringing Metal from 47% to 94% feature parity with the software renderer. GPU skinning, morph targets, shadow mapping, terrain splatting, post-processing, and instanced rendering.
- **D3D11 Backend: 20 Features Implemented** — All 20 D3D11 backend plans implemented in a 3,173-line HLSL+C backend rewrite. Diffuse textures, normal/specular/emissive maps, spot lights, fog, wireframe/cull, render-to-texture, GPU skinning, morph targets (with normal deltas), shadow mapping, instanced rendering, terrain splatting, post-processing (bloom, FXAA, tonemap, DOF, motion blur, SSAO), cubemap skybox, and environment reflections. Windows CI validation job added.
- **Software Renderer Upgrades** — Per-pixel terrain splatting (4-layer weight blend), bilinear filtering, vertex color support, shadow mapping, and material shader hooks (Toon/Fresnel/Emissive shading models).
- **Codegen Pipeline Decomposition** — Both x86_64 and AArch64 backends refactored from monolithic per-function pipelines into composable pass-based architectures. x86_64 exposes `legalizeModuleToMIR`/`allocateModuleMIR`/`optimizeModuleMIR`/`emitMIRToAssembly`/`emitMIRToBinary` public APIs. AArch64 uses `PassManager`-based composition with Scheduler and BlockLayout passes at O1+. EH-sensitive modules bypass IL optimizations. New `CodeSection::appendSection()` and `DebugLineTable::append()` for per-function section merging.
- **Windows x86_64 Codegen Hardening** — CoffWriter cross-section symbol resolution, X64BinaryEncoder runtime symbol mapping, operand materialisation for TESTrr/call.indirect, SETcc REX prefix for byte registers, SSE RIP-relative MOVSD encoding, unsafe spill slot reuse disabled, and process isolation hang fix. Windows native executables now assemble, link, and run correctly.
- **AArch64 Codegen Hardening** — Immediate utils extraction, binary encoder fixes, refcount injection bugfix, fastpath improvements, trap message forwarding, error field extraction via TLS bridge, Apple M-series scheduler latency tuning, and 10+ new codegen tests.
- **Zia Compiler Bug Fixes** — String bracket-index crash, `List[Boolean]` unboxing truncation, `catch(e)` binding via TLS message passing, `String.Contains()` method alias. New `ErrGetMsg` IL opcode and `rt_throw_msg_set/get` runtime functions for exception message propagation.
- **10 Game Engine APIs** — Entity (2D game object with built-in physics), Behavior (composable AI presets), Raycast2D (tilemap line-of-sight), LevelData (JSON level loader), SceneManager (multi-scene transitions), Camera.SmoothFollow (deadzone + lerp tracking), AnimStateMachine named states (play-by-name), MenuList.HandleInput (input convenience), Config.Load (JSON config), Tilemap.SetTileAnim (per-tile frame animation). 10 new runtime classes, 3,800+ LOC in C.
- **Asset Embedding System (VPA)** — Compile-time asset packaging via `embed`, `pack`, and `pack-compressed` project directives. VPA binary format with `AssetCompiler` and `VpaWriter` toolchain. `Assets.Load`/`LoadBytes`/`Exists`/`Mount` runtime API. Asset blobs injected into `.rodata` via the native assembler for zero-file-dependency executables. Cross-platform `Path.ExeDir()` for relative asset resolution.
- **XENOSCAPE Demo Rewrite** — Complete rewrite of the flagship Metroid-style sidescroller using all 10 new game engine APIs. 26 Zia files (13K LOC), 10 JSON level files, JSON-driven entity spawning via `LevelData`, composable enemy AI via `Behavior`, and scene management via `SceneManager`.
- **Native macOS Menu Bar** — 554-line Objective-C bridge (`rt_gui_macos_menu.m`) mirrors Viper GUI menubars to the native macOS application menu bar. Special item relocation (About → app menu, Preferences → app menu with Cmd+,, Quit → app menu with Cmd+Q). Keyboard accelerator translation, Services submenu, and Hide/Show All standard items.
- **Bytecode VM CALL_NATIVE Expansion** — Native function index widened from 8-bit to 16-bit (255 → 65,535 max native references) to accommodate the growing runtime. Bytecode format version bumped to v2.
- **Zia Runtime Extern Signatures** — `rtgen` now emits full parameter types (not just return types) for all `RT_FUNC` entries in `ZiaRuntimeExterns.inc`. Enables correct string equality comparison for runtime methods returning `str` (e.g., `LevelData.ObjectType() == "enemy"` now emits `Viper.String.Equals` instead of `ICmpEq`).
- **Zia Language: `entity`/`value` renamed to `class`/`struct`** — Mainstream keyword alignment across all source, tests, REPL, LSP, docs, and VS Code extension.
- **VAPS Packaging Overhaul** — 10 improvements, 57 new tests, Windows installer stub, symlink safety, dry-run mode.
- **GUI Runtime Hardening** — Theme ownership moved to per-app structs (no more mutating built-in dark/light singletons), modal dialog routing follows the real dialog stack, overlay timing uses wall-clock time, platform text input events (`VGFX_EVENT_TEXT_INPUT`) wired through macOS/Win32/X11 backends replacing ASCII key synthesis, dropdown placeholder ownership fix, notification compaction, and command palette UTF-8 query path.
- **IO Runtime Hardening** — SaveData migrated from raw C strings to GC-managed `rt_string` keys/values with versioned JSON format and migration support. Glob pattern matching extended with character classes (`[a-z]`, `[!0-9]`), case-insensitive matching on Windows, `**` recursive directory descent, and correct path separator handling. File watcher debounced event coalescing, single-file watch with directory monitoring, and Windows `OVERLAPPED` handle leak fix. TempFile atomic `O_CREAT|O_EXCL` creation with collision retry. Archive extraction path traversal validation.
- **Comprehensive Documentation Pass** — Substantial `@brief`/`@details` comments added to 100+ functions across GUI runtime (8 files), Graphics3D runtime (19 files), network subsystem (19 files), crypto subsystem (8 files), and IO subsystem (11 files). Rewrites broken auto-generated comments ("Should the close of the app", "Sphere the overlaps of the aabb3d", "Hash the with iterations of the password") into proper Viper documentation standard with why/how explanations, parameter contracts, and ownership semantics.
- **HTTP Server Runtime Bindings** — `HttpServer` class wired through bytecode VM and both Zia/BASIC frontends with `Listen`, `Accept`, `Respond`, `Close` methods and request property accessors (`Method`, `Path`, `Header`, `Body`).
- **Network & Crypto Hardening** — Comprehensive function documentation across TLS handshake, certificate verification, WebSocket framing, SSE chunked parsing, SMTP protocol, connection pooling, async sockets, AES/cipher/HKDF/password hashing modules. Two test fixes: IPv6 wildcard address acceptance, HTTP chunked encoding framing correction.
- **Comprehensive Documentation Review** — 39 stale files deleted, 70+ factual errors corrected across 30+ docs, Viper file headers on 100% of 2,706 source files, @brief Doxygen on 100% of runtime functions. Bible code audit across 12 chapters correcting struct field syntax, class method visibility, interface declarations, catch syntax, and collection API calls.

#### By the Numbers

| Metric | v0.2.3 | v0.2.4 | Delta |
|--------|--------|--------|-------|
| Commits | — | 72 | +72 |
| Source files | 2,671 | 2,795 | +124 |
| Production SLOC | ~348K | ~413K | +65K |
| Test count | 1,351 | 1,388 | +37 |

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

### Native Linker Hardening

Architectural improvements to the native linker pipeline improving correctness, maintainability, and platform awareness.

#### BranchTrampoline Rewrite

AArch64 branch trampolines rewritten with boundary-based placement. The previous implementation duplicated SectionMerger's VA reassignment algorithm (fragile coupling requiring manual synchronization). The new approach queries `.text` chunk boundaries directly via `collectChunkBoundaries()` and selects the nearest reachable boundary for trampoline insertion using `chooseReachableBoundary()`. `branch26Reachable()` validates ±128 MB displacement range at the instruction level.

#### SectionMerger VA Extraction

Virtual address assignment extracted from `mergeSections()` into the public `assignSectionVirtualAddresses()` API. This eliminates code duplication — BranchTrampoline and any future passes that need VA recomputation now call the shared function instead of maintaining parallel implementations. `imageBaseForPlatform()` and `permClass()` promoted from anonymous namespace lambdas to named functions.

#### Platform-Aware Symbol Resolution

`SymbolResolver::resolveSymbols()` now accepts a `LinkPlatform` parameter (defaulting to `detectLinkPlatform()`). Dynamic symbol prefix tables split into platform-specific arrays:
- **Common**: `__libc_`, `__stack_chk_` (all platforms)
- **macOS**: `CF`, `kCF`, `CG`, `NS`, `objc_`, `dispatch_`, `MTL`, `AudioObject`, etc.
- **Windows**: `__imp_`

This prevents macOS-specific prefixes from suppressing legitimate archive symbol resolution on Windows/Linux, and vice versa.

#### RelocApplier Range Checking

New `writeCheckedRel32()` helper validates that PC-relative relocations fit in a signed 32-bit range before patching. Out-of-range relocations now produce a diagnostic naming the object file, relocation kind, and target symbol instead of silently truncating. Applied to both COFF REL32 and generic PC-relative relocation paths.

#### Multi-Section COFF Writer

New `CoffWriter::write(path, vector<CodeSection>, rodata)` overload produces COFF objects with per-function `.text.funcName` sections. Each `CodeSection` gets its own COFF section header, symbol table entries, and relocations. Win64 unwind data (`xdataNameBase` parameter) accumulates across sections to produce correct cross-section `.xdata`/`.pdata` references. Single-section and empty inputs delegate to the existing single-section path.

#### ELF Symbol Types

ELF writer now emits `STT_OBJECT` for rodata symbols (previously `STT_FUNC`). New `elfSymbolType()` helper maps `SymbolSection::Text` → `STT_FUNC`, `SymbolSection::Rodata` → `STT_OBJECT`, and undefined/other → `STT_NOTYPE`, matching ELF spec conventions.

#### Windows ARM64 Gating

`nativeLink()` returns an early error for `LinkPlatform::Windows` + `LinkArch::AArch64`, with a diagnostic explaining that COFF object emission is supported but PE startup/import/unwind generation remains x86_64-specific. `native-assembler.md` updated to reflect "Object emission only" for AArch64 COFF.

#### New Tests

- `test_native_linker` — Windows ARM64 gating diagnostic (53 LOC)
- `test_branch_trampoline` — Updated for boundary-based placement, added relocation application verification and `countInsn()` helper
- `test_reloc_applier` — COFF AArch64 BRANCH26 relocation patching (54 LOC added)
- `test_symbol_resolver` — Platform-aware resolution tests (29 LOC added)
- `test_coff_writer` — Multi-section COFF output validation (49 LOC added)
- `test_elf_writer` — STT_OBJECT symbol type verification (47 LOC added)
- `test_pe_writer` — PE output structure tests (33 LOC added)

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
- **Branch relaxation** — Short JMP (`EB`, 2 bytes) and short Jcc (`75`/`74`/etc., 2 bytes) encodings for near branches, replacing always-near JMP (`E9`, 5 bytes) and always-long Jcc (`0F 8x`, 6 bytes) forms. Reduces code size for small functions with nearby branch targets.
- **Pipeline decomposition** — Monolithic `runFunctionPipeline` split into composable module-level phases: `legalizeModuleToMIR` (IL→MIR lowering + legalization), `allocateModuleMIR` (register allocation + frame lowering), `optimizeModuleMIR` (peephole), `emitMIRToAssembly` (text output), and `emitMIRToBinary` (native object output). `selectTarget()` made public. New `PeepholePass.cpp/hpp` (60 LOC) as a proper pass in the pass manager. Each phase can be invoked independently, enabling MIR inspection at any pipeline stage.

**AArch64 backend:**
- **`i1` parameter masking** — Boolean parameters masked with `AND 1` at function entry, matching return-value masking. Prevents upper-bit garbage corruption.
- **Remove redundant refcount injection** — `emitRefcountedStore` lambda stripped from instruction lowering. String ownership belongs in the IL layer.
- **Immediate utils extraction** — `A64ImmediateUtils.hpp` helper for immediate encoding, asm emitter hardening, binary encoder fixes, arithmetic/call fastpath improvements, regpool and symbol resolver fixes.
- **Trap message forwarding** — `TrapErr` now materialises the message string operand into x0 and passes it to `rt_trap()`, enabling catch handlers to display the user's throw message in native executables.
- **Error field extraction via TLS** — `ErrGetKind`, `ErrGetCode`, and `ErrGetLine` now call runtime TLS accessors (`rt_trap_get_kind/code/line`) instead of returning hardcoded 0. `rt_trap()` auto-classifies the trap kind from the message prefix. Enables typed catch (`catch(e: DivideByZero)`) in native code.
- **Apple M-series scheduler tuning** — Instruction latency model updated for Firestorm cores: FP divide 3→10 cycles, integer divide 3→7, FP multiply 3→4. Improves instruction scheduling for FP-heavy code.
- **Secondary scratch register (kScratchGPR2)** — X16 (IP0) formalized as `kScratchGPR2` for post-RA helper sequences that need a second temporary while `kScratchGPR` (X9) holds the base value. X16 excluded from register allocator pool. AsmEmitter and A64BinaryEncoder updated to use the named constant instead of hardcoded `PhysReg::X16`.
- **Pipeline decomposition** — `PassManager`-based pass composition replacing direct function calls in `runCodegenPipeline`. Scheduler and BlockLayout passes added to the O1+ pipeline (previously only peephole ran post-RA). EH-sensitive modules (`EhPush`/`EhPop`/`ResumeSame`/`ResumeNext` opcodes) bypass IL optimizations to avoid structural invariant violations. Virtual register space partitioned into three ranges: general vregs (`kFirstVirtualRegId`=1), phi-inserted vregs (`kPhiVRegStart`=40000), and cross-block spill keys (`kCrossBlockSpillKeyStart`=50000) with overflow guards.

**Common codegen infrastructure:**
- **`CodeSection::appendSection()`** — Merge two `CodeSection` objects with automatic symbol and relocation index rebasing. External symbols are deduped via `findOrDeclareSymbol`; defined symbols get offset-biased entries. Compact unwind and Win64 unwind entries are also rebased. Enables per-function binary emission followed by a single merged section for backward-compatible symbol extraction.
- **`DebugLineTable::append()`** — Merge debug line entries from another table with address bias and file index remapping. Both x86_64 and AArch64 `BinaryEmitPass` now emit per-function debug tables and merge them, producing correct DWARF `.debug_line` across function boundaries.
- **`seedDebugFiles()` helper** — Scans MIR for maximum file ID and populates debug table file entries from `debugSourcePath`, normalizing paths via `std::filesystem::path::lexically_normal()`. Replaces hardcoded `addFile("<source>")`.
- **`FrameLayout::ensureSpill` vreg widened** — Parameter type changed from `uint16_t` to `uint32_t` to accommodate the expanded virtual register space.

**Native linker:**
- `RtComponent::Game` — Game runtime classes link correctly via `libviper_rt_game.a` after directory reorganization.
- `-lshell32` — Added to Windows linker command for `DragQueryFile`/`DragAcceptFiles` GUI support.

**Runtime:**
- `SetErrorMode` + `_set_abort_behavior` added to `rt_init_stack_safety` on Windows to suppress crash/assert dialog boxes in natively compiled programs.
- **Trap IP recovery** — `rt_trap_set_ip()`/`rt_trap_get_ip()` store and retrieve the native instruction pointer associated with the most recent trap. `TrapGetIp` added to `runtime.def`.
- **Error code classification** — `rt_err_to_trap_kind()` maps `RtError` codes to trap kind integers (overflow=1, invalid cast=2, domain=3, bounds=4, file-not-found=5, etc.). `rt_trap_error_make()` and `rt_trap_raise_error()` convenience functions combine message storage, field classification, and trap invocation.

**Windows test infrastructure:**
- ProcessIsolation framework reworked: function pointers don't survive `CreateProcess`, so `registerChildFunction()` with indexed dispatch (`--viper-child-run=N`) replaces direct pointer passing. `dispatchChild()` added to `TEST_WITH_IL` macro and all 16 VM/conformance tests. Windows test failures reduced from 48 to 4.
- Codegen test assertions accept `.rdata` (Windows COFF) alongside `.rodata` (ELF), `cmovneq`→`cmovne` suffix fix, platform-adaptive paths for RTDiskFullTests, RTNetworkHardenTests, and test_vm_rt_trap_loc.

---

### 3D Graphics Engine Improvements

#### Terrain System

- **Procedural generation** — `Terrain3D.GeneratePerlin(noise, scale, octaves, persistence)` writes directly to the internal float heightmap from a PerlinNoise object, bypassing the Pixels intermediate for faster terrain generation on large grids
- **LOD (Level of Detail)** — Three resolution levels per chunk (step 1/2/4) selected by distance to camera. `SetLODDistances(near, far)` configures thresholds. Chunks use 578/162/50 vertices at LOD 0/1/2 respectively
- **Frustum culling** — Chunks outside the camera view frustum are skipped entirely using the existing `vgfx3d_frustum_t` infrastructure (Gribb-Hartmann plane extraction + AABB p-vertex/n-vertex test). Per-chunk AABBs computed during mesh generation
- **Skirt geometry** — Downward-facing skirt triangles along chunk edges hide T-junction cracks at LOD boundaries. `SetSkirtDepth(depth)` configures skirt size
- **16-bit heightmap** — R+G channels for 65536 height levels (was 256). `SetSplatMap` + 4 layer textures with per-layer UV tiling, baked blend fallback

#### Water System

- **Gerstner waves** — `Water3D.AddWave(dirX, dirZ, speed, amplitude, wavelength)` adds directional Gerstner waves (up to 8). Multi-wave sum produces realistic ocean-like displacement with proper derivative-based normals
- **Material wiring** — `SetTexture`, `SetNormalMap`, `SetEnvMap`, `SetReflectivity` forward to the underlying Material3D, enabling textured water with environment reflections using zero shader changes
- **Configurable resolution** — `SetResolution(n)` controls grid density (8-256, default 64, up from 32)

#### Vegetation System (New)

- **Vegetation3D** — New runtime class for instanced grass and foliage rendering
- **Cross-billboard blades** — Two perpendicular quads (8 vertices, 4 triangles) per blade instance, eliminating billboard popping from any angle
- **Terrain population** — `Populate(terrain, count)` scatters blades on terrain surface using LCG random, filtered by optional density map (R channel = spawn probability)
- **Wind animation** — Per-blade Y-axis shear via `sin(position + time)`. `SetWindParams(speed, strength, turbulence)` controls wind behavior
- **Distance LOD** — Progressive blade thinning between near/far thresholds + hard cull beyond far. `SetLODDistances(near, far)` configures
- **GPU instancing** — Uses `submit_draw_instanced` backend vtable for single-draw-call rendering of all visible blades. Software fallback for non-GPU backends

#### Material Shader Hooks

- **Shading models** — `Material3D.SetShadingModel(model)` selects per-material shading: 0=BlinnPhong (default), 1=Toon (quantized diffuse bands), 4=Fresnel (angle-dependent alpha), 5=Emissive (boosted glow)
- **Custom parameters** — `Material3D.SetCustomParam(index, value)` passes 8 float parameters to the shader for model-specific tuning (e.g., Toon band count, Fresnel power/bias, Emissive strength)
- **Cross-backend** — Implemented in Metal MSL fragment shader and software rasterizer `compute_lighting()`. OpenGL GLSL and D3D11 HLSL receive the uniforms (shader-side switch deferred)

#### 3D Format Loaders

- **glTF 2.0** — `.gltf` (JSON + external buffers) and `.glb` (single binary container). Mesh extraction with positions, normals, UVs, tangents. PBR metallic-roughness → Blinn-Phong material conversion. Skeletal animation and morph target support
- **STL** — Binary and ASCII auto-detection. Normal computation via `rt_mesh3d_recalc_normals`
- **OBJ .mtl** — Material parser with Kd/Ks/Ns/d properties, texture path resolution relative to OBJ directory, up to 64 materials per file
- **FBX enhancements** — Texture path extraction via Texture node parsing and connection tracing. Morph target extraction from BlendShape/Shape nodes with sparse position/normal deltas
- **Scene3D.Save** — JSON serialization of node hierarchy with transforms

#### Other 3D Improvements

- **Light3D.NewSpot** — Spot light with position, direction, inner/outer cone angles, and smoothstep attenuation
- **Camera3D.NewOrtho** — Orthographic camera for isometric/strategy games
- **Mesh3D.Clear()** — Reset vertex/index counts without freeing backing arrays (enables mesh reuse)
- **Sprite3D use-after-free fix** — Per-frame mesh/material allocation replaced with cached instances + GC temp buffer registration
- **Physics3D shape-specific collision** — Sphere-sphere radial + AABB-sphere closest-point narrow-phase (replaces AABB-only)
- **Physics3D character controller** — Slide-and-step movement replaces trivial velocity-set
- **Physics3D collision events** — `CollisionCount`, `GetCollisionBodyA/B`, `GetCollisionNormal/Depth` queue
- **DistanceJoint3D / SpringJoint3D** — Physics joint constraints with 6-iteration sequential impulse solver
- **Audio3D** — Per-voice `max_distance` tracking table (replaces shared global that caused cross-voice attenuation bugs)
- **SLERP domain clamp** — Prevent NaN from `acosf` with dot products > 1.0 due to rounding
- **Water3D/Decal3D finalizer leaks** — Free mesh/material on GC
- **Metal bone count cap** — Enforce 128-bone limit matching D3D11/OpenGL
- **Cubemap bilinear filtering** — 4-texel interpolation replaces nearest neighbor
- **VIPER_3D_BACKEND env var** — Set `VIPER_3D_BACKEND=software` to force software renderer
- **53 backend implementation plans** — Detailed plans across all 4 renderers (SW: 7, Metal: 14, OpenGL: 16, D3D11: 16) plus 4 additional D3D11 plans

#### Metal Backend: macOS 26 Compatibility

- **Offscreen rendering** — Metal now renders to a `MTLStorageModeManaged` offscreen texture and reads back BGRA pixels to the vgfx software framebuffer (BGRA→RGBA conversion). The existing `drawRect:` CGImage blit path displays the content. This replaces CAMetalLayer direct presentation which broke on macOS 26 (Tahoe)
- **Backend vtable dispatch** — `vgfx3d_show/hide_gpu_layer` moved from duplicate global symbols (Metal `.m` and software `.c` both defined them) to vtable function pointers in `vgfx3d_backend_t`. Fixes SIGSEGV crash when software backend was selected but Metal's version was linked
- **vgfx_set_gpu_present API** — New public API in `vgfx.h` for backends to signal GPU ownership of display (currently unused with offscreen approach but available for future backends)

---

### Graphics Backend Hardening

Cross-backend improvements to texture caching, Canvas3D robustness, instanced rendering safety, and mesh normal transformations.

#### Generation-Aware Texture Caching

All GPU backends (Metal, D3D11, OpenGL) now track a `generation` counter on `Pixels` and `CubeMap3D` objects. Every in-place mutation (Set, Fill, Clear, Copy, DrawBox, DrawDisc, FloodFill, BlendPixel, etc.) bumps the generation via `pixels_touch()`. GPU texture caches compare the stored generation against the current value — if they match, the cached GPU texture is reused; if stale, the texture is re-uploaded in place without allocating a new GPU resource. This replaces per-frame cache invalidation and fixes the "modified texture not updating on screen" bug class across all backends.

- `rt_pixels_internal.h`: `uint64_t generation` field added to `rt_pixels_impl`, `pixels_touch()` inline helper
- `vgfx3d_backend_utils.c/h`: `vgfx3d_get_pixels_generation()`, `vgfx3d_get_cubemap_generation()` (max across 6 faces)
- Metal: `VGFXMetalTextureCacheEntry` / `VGFXMetalCubemapCacheEntry` classes with generation field
- D3D11: `d3d_tex_cache_entry_t` / `d3d_cubemap_cache_entry_t` with generation field, stale SRV release + recreation
- OpenGL: `gl_texture_cache_entry_t` / `gl_cubemap_cache_entry_t` with generation field, in-place `glTexImage2D` update

#### Canvas3D Improvements

- **Window resize handling** — `vgfx_set_resize_callback()` propagates OS resize events to Canvas3D, which updates its width/height and calls `backend->resize()`. Resize events from `Poll()` also handled.
- **Screenshot GPU readback** — `rt_canvas3d_screenshot()` now supports three paths: render target color buffer direct copy, GPU backend readback via `backend->readback_rgba()`, and software framebuffer fallback. Previously only the software framebuffer path existed.

#### InstanceBatch3D Memory Safety

- **Allocation hardening** — `realloc` for `transforms`/`current_snapshot`/`prev_transforms` arrays replaced with `calloc` + `memcpy` + `free` pattern. If any allocation fails, all three are freed and the add is aborted (no partial state corruption).
- **Swap-remove correctness** — `rt_instbatch3d_remove()` now copies `current_snapshot` and `prev_transforms` slots alongside `transforms` during swap-remove, preventing stale motion data from persisting in the wrong instance slot. Snapshot/prev counts reset when the removed instance was within tracked range but its replacement was not.

#### Mesh3D Normal Transform

`rt_mesh3d_transform()` now uses the inverse-transpose of the upper 3x3 model matrix for normal transformation (via `vgfx3d_compute_normal_matrix4()`), replacing the previous incorrect direct-multiply approach. This fixes lighting artifacts on non-uniformly scaled meshes.

#### Metal Backend Enhancements

- **Skybox pipeline** — Dedicated `skyboxPipeline` render pipeline state with depth test enabled but depth write disabled, separate `skyboxDepthState`, pre-built `skyboxVertexBuffer` (36-vertex unit cube). Skybox drawn first in render pass before scene geometry.
- **Cubemap sampler** — Separate `cubeSampler` with linear min/mag/mip filtering for environment maps. `defaultCubemap` (1x1 black) serves as fallback when no cubemap is bound.
- **Separate view/projection matrices** — `_view[16]` and `_projection[16]` stored alongside combined `_vp[16]` for shader passes that need individual matrices (e.g., skybox strips translation from view).

#### macOS Default Application Menu

New `VGFXMacAppMenuDispatcher` class and `vgfx_macos_build_default_app_menu()` in `vgfx_platform_macos.m` (218 LOC). When a ViperGFX window is created, a standard macOS application menu is automatically generated with:

- About (`orderFrontStandardAboutPanel:`)
- Separator
- Services submenu
- Separator
- Hide (`Cmd+H`), Hide Others (`Cmd+Alt+H`), Show All
- Separator
- Quit (`Cmd+Q`) via `quitApplication:` which posts a `VGFX_EVENT_CLOSE` instead of hard-terminating

The app name is resolved from the window title, `CFBundleName`, or process name (in that order).

#### New Tests

- `test_rt_instterrain` — InstanceBatch3D + Terrain3D integration (46 LOC)
- `test_rt_canvas3d` — Canvas3D screenshot and resize handling (25 LOC added)
- `test_rt_canvas3d_gpu_paths` — GPU backend readback and texture cache invalidation (49 LOC added)
- `test_vgfx3d_backend_utils` — Pixels generation tracking, cubemap generation (28 LOC added)

---

### Game Engine Documentation

New `/docs/gameengine/` documentation section providing topical game engine guides organized by game development topic (not by runtime namespace):

- **README.md** — Landing page with quick start code (Zia + BASIC), feature summary table (17 systems), topical guide links (Rendering/Gameplay/Presentation/Infrastructure), 15-game example gallery, and API reference cross-links
- **getting-started.md** — Progressive 5-step tutorial building a paddle-bounce game from zero (window → movement → ball physics → sound → screen effects)
- **architecture.md** — System layer diagram, data flow per frame, zero-dependency manifesto, GPU backend selection, source code layout
- **examples/README.md** — Gallery of all 15 example games with engine feature coverage matrix

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

### Game Engine APIs (10 New Systems)

Ten new game engine systems providing high-level abstractions for 2D game development. All implemented as C runtime classes registered via `runtime.def`:

| API | Runtime Class | Key Functions | LOC |
|-----|---------------|---------------|-----|
| **Entity** | `Viper.Game.Entity` | `New`, `ApplyGravity`, `MoveAndCollide`, `UpdatePhysics`, `Overlaps`, `AtEdge`, `PatrolReverse` | 282 |
| **Behavior** | `Viper.Game.Behavior` | `AddPatrol`, `AddChase`, `AddGravity`, `AddEdgeReverse`, `AddShoot`, `AddSineFloat`, `Update` | 227 |
| **Raycast2D** | `Viper.Game.Raycast` | `HasLineOfSight`, `Collision.LineRect`, `Collision.LineCircle` | 124 |
| **LevelData** | `Viper.Game.LevelData` | `Load` (JSON), `ObjectCount`, `ObjectType`, `ObjectX/Y`, `PlayerStartX/Y`, `Theme` | 222 |
| **SceneManager** | `Viper.Game.SceneManager` | `Add`, `Switch`, `SwitchTransition`, `Update`, `Current`, `IsScene` | 183 |
| **Camera.SmoothFollow** | `Viper.Graphics.Camera` | `SmoothFollow(targetX, targetY, speed)`, `SetDeadzone(w, h)` | 45 |
| **AnimStateMachine** | `Viper.Game.AnimStateMachine` | `AddNamed`, `Play(name)`, `StateName`, `SetEventFrame`, `EventFired` | 76 |
| **MenuList.HandleInput** | `Viper.Game.UI.MenuList` | `HandleInput(up, down, confirm)` — returns selected index or -1 | — |
| **Config.Load** | `Viper.Game.Config` | `Load(path)`, `GetString`, `GetInt`, `GetBool`, `GetFloat`, `Has`, `Keys` | 114 |
| **Tilemap.SetTileAnim** | `Viper.Graphics.Tilemap` | `SetTileAnim`, `SetTileAnimFrame`, `UpdateAnims`, `ResolveAnimTile` | 76 |

**Entity** is a lightweight 2D game object with built-in position, velocity, direction, HP, AABB collision flags (ground/left/right/ceiling), and tilemap-aware physics (`MoveAndCollide` performs axis-separated sweep with solid tile detection). **Behavior** provides composable AI presets (patrol, chase, gravity, edge/wall reverse, shoot timer, sine float, anim loop) that can be stacked on any Entity. **LevelData** parses a JSON format with tilemap data, spawn objects (type/id/position), player start position, and theme string — enabling data-driven level design.

**GameButton** (`Viper.Game.UI.GameButton`) also added: styled button widget with customizable colors, border, and text for game menus. 132 LOC.

11 new test binaries covering all game engine APIs: `test_rt_entity`, `test_rt_behavior`, `test_rt_raycast_2d`, `test_rt_scene_manager`, `test_rt_animstate_named`, `test_rt_camera_enhance`, `test_rt_game_menu`, `test_rt_tilemap_anim`, `test_rt_vpa_format`, `test_rt_asset_manager`, `test_rt_path_exe_dir`.

---

### Asset Embedding System (VPA Format)

Compile-time asset packaging for zero-file-dependency native executables. Assets are compiled into a binary blob at build time and embedded into the executable's `.rodata` section.

**Project directives** in `viper.project`:
- `embed <path>` — Embed file as-is (raw bytes)
- `pack <path>` — Pack file with VPA container framing
- `pack-compressed <path>` — Pack with DEFLATE compression

**Toolchain components:**
- `AssetCompiler` (`src/tools/common/asset/AssetCompiler.cpp`, 233 LOC) — Reads project directives, resolves paths, invokes VpaWriter
- `VpaWriter` (`src/tools/common/asset/VpaWriter.cpp`, 230 LOC) — Produces VPA binary format: magic header, file table (name + offset + size + flags), concatenated file data, optional DEFLATE compression
- `VpaReader` (`src/runtime/io/rt_vpa_reader.c`, 375 LOC) — Reads VPA archives at runtime, supports mounting multiple archives
- Asset blob injection — Both x86_64 and AArch64 `CodegenPipeline` inject VPA blobs as `viper_asset_blob` / `viper_asset_blob_size` global symbols in `.rodata`
- `Path.ExeDir()` (`rt_path_exe.c`, 149 LOC) — Cross-platform executable directory resolution (`_NSGetExecutablePath` on macOS, `/proc/self/exe` on Linux, `GetModuleFileName` on Windows)

**Runtime API** (`Viper.IO.Assets`):

| Method | Signature | Description |
|--------|-----------|-------------|
| `Assets.Load(name)` | `obj(str)` | Load asset as String |
| `Assets.LoadBytes(name)` | `obj(str)` | Load asset as Bytes |
| `Assets.Exists(name)` | `i64(str)` | Check if asset exists |
| `Assets.Size(name)` | `i64(str)` | Get asset size in bytes |
| `Assets.List()` | `obj()` | List all asset names |
| `Assets.Mount(path)` | `i64(str)` | Mount additional VPA archive |
| `Assets.Unmount(path)` | `i64(str)` | Unmount VPA archive |

Asset resolution order: embedded blob → mounted VPA archives → filesystem fallback relative to executable directory. 9 VPA format tests + 9 asset manager tests.

---

### Native macOS Menu Bar Bridge

New 554-line Objective-C module (`rt_gui_macos_menu.m`) mirrors Viper GUI menubars to the native macOS application menu bar, providing standard macOS application behavior:

- **Special item relocation** — "About" items move to the app menu (prefixed with app name), "Preferences" moves to app menu with Cmd+, accelerator, "Quit"/"Exit" moves to app menu with Cmd+Q
- **Standard app menu items** — Auto-generated About, Services submenu, Hide/Hide Others/Show All, and Quit entries
- **Keyboard accelerator translation** — Viper `Ctrl+` accelerators map to `Cmd+` on macOS; full function key, arrow key, and modifier mapping via `rt_gui_macos_key_equivalent_for_key()`
- **Menu bar suppression** — When `native_main_menu` is active, the Viper-rendered menubar collapses to zero height (no measure/paint/event handling), eliminating double menu bars
- **Key equivalent routing** — macOS event loop now forwards `NSEventTypeKeyDown` to `[mainMenu performKeyEquivalent:]` before Viper input processing, enabling native Cmd+key shortcuts (Cmd+N, F5, etc.)
- **Window and Help menu registration** — Menus titled "Window" and "Help" are registered with `NSApp` for standard macOS behavior (window list, Help search)

Infrastructure changes: `vg_menu_item_t` gains `parent_menu` pointer, `vg_menu` gains `owner_menubar` pointer, `vg_menubar_t` gains `native_main_menu` flag. Accelerator table memory leak fixed in `menubar_destroy`.

---

### Bytecode VM CALL_NATIVE Expansion

The `CALL_NATIVE` instruction encoding widened to support the growing runtime library:

- **Before:** `[opcode:8][nativeIdx:8][argCount:8]` — max 255 native functions
- **After:** `[opcode:8][argCount:8][nativeIdx:16]` — max 65,535 native functions

Bytecode format version bumped from 1 to 2. Both `BytecodeVM.cpp` (switch dispatch) and `BytecodeVM_threaded.cpp` (threaded dispatch) updated. `BytecodeCompiler` now validates argument count ≤ 255 and native index ≤ 65,535 with clear error messages.

---

### Zia Frontend Improvements

- **Runtime extern parameter types** — `rtgen` now generates full ABI-shaped parameter signatures (not just return types) for all `RT_FUNC` entries in `ZiaRuntimeExterns.inc`. New `ilParamTypeToZiaType()` mapper handles `str`, `i64`, `f64`, `i1`, `obj`, `ptr`, and optional types. This enables Zia sema to correctly identify string-returning runtime methods and emit `Viper.String.Equals` for `==` comparisons instead of `ICmpEq` (which would compare pointer values).
- **`List[Object]` Push ABI fix** — `List.Push` on user-defined class instances now emits the correct 2-argument `(obj, obj)` extern signature. Verified by new `ListEntityPushUsesAbiArity` lowerer test.

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
- **Game engine documentation** — New `/docs/gameengine/` section with landing page (feature table, quick start, topical guide links), getting-started tutorial (5-step paddle game in Zia + BASIC), architecture overview (system layers, data flow, zero-dependency design), and example games gallery (15 games with feature coverage matrix). `/docs/README.md` updated with Game Engine section.
- **Native assembler doc corrections** — AArch64 COFF marked as "Object emission only" (PE startup/import/unwind generation remains x86_64-specific). Object-file matrix clarified.
- **3D graphics guide corrections** — `SetShadingModel` model 2 marked as "Reserved" (forward-compatible, falls back to Blinn-Phong). `FromOBJ` description clarified: `.mtl`/`usemtl`/`g`/`o` directives parsed and flattened but do not create per-material submeshes. Unlit model 3 documented.

---

### Demo Games

- **XENOSCAPE** — Complete rewrite of the flagship Metroid-style sidescroller using all 10 new game engine APIs. 26 Zia files (13K LOC) + 10 JSON level files with data-driven entity spawning, composable AI via Behavior, scene management, smooth camera tracking, and named animation states. 10 interconnected levels, 30+ enemy types, 4 boss fights, ability-gated progression, save system, and achievement tracking.
- **Asset Demo** — Minimal example (`examples/apps/asset_demo/`) demonstrating `embed` and `pack` project directives with `Assets.Load()` at runtime.

---

### Video Playback

Video playback for game cutscenes and GUI media applications. All codecs implemented from scratch with zero external dependencies.

#### VideoPlayer Runtime Class

| Member | Description |
|--------|-------------|
| `Open(path)` | Load video file (`.avi` or `.ogv`) |
| `Play()` / `Pause()` / `Stop()` | Playback control |
| `Seek(seconds)` | Seek to time position |
| `Update(dt)` | Advance by delta time (call each game frame) |
| `SetVolume(vol)` | Audio volume [0.0-1.0] |
| `Width` / `Height` | Frame dimensions (read-only) |
| `Duration` / `Position` | Total and current time in seconds (read-only) |
| `IsPlaying` | Playback active flag (read-only) |
| `Frame` | Current decoded Pixels frame (read-only) |

#### AVI/MJPEG Decoder

- **AVI RIFF container parser** — Walks RIFF chunk tree: `hdrl` (stream headers), `strl`/`strf` (video/audio format), `movi` (interleaved A/V data), `idx1` (index). Identifies video (`XXdc`) and audio (`XXwb`) chunks by FOURCC.
- **MJPEG DHT injection** — AVI MJPEG frames typically omit Huffman tables (DHT markers). Decoder detects missing `0xFFC4` markers and injects the 420-byte standard JPEG Annex K tables (2 DC + 2 AC) before the SOS marker. This enables decoding of MJPEG frames using the existing `rt_jpeg_decode_buffer()`.
- **JPEG buffer decode refactor** — Extracted `rt_jpeg_decode_buffer(data, len)` from file-based `rt_pixels_load_jpeg()` for in-memory frame decoding without file I/O.

#### Theora Codec Infrastructure

- **Header parsing** — Identification (0x80), comment (0x81), and setup (0x82) headers parsed from OGG packets. Extracts frame dimensions, FPS, color space, loop filter limits.
- **YCbCr 4:2:0 → RGBA conversion** — BT.601 integer-only matrix with chroma upsampling. Separate utility (`rt_ycbcr.c`) reusable by future codecs.
- **OGG multi-stream demux** — VideoPlayer detects Theora vs Vorbis streams by packet header signature, routing packets to the correct decoder.
- **Reference frame buffers** — Y/Cb/Cr planes allocated for current, reference, and golden frames. Full DCT/motion compensation decode documented as follow-up.

#### VideoWidget (GUI)

- `VideoWidget.New(parent, path)` — Creates image widget in the GUI widget tree, loads video file internally via VideoPlayer.
- `VideoWidget.Update(dt)` — Advances playback and refreshes the image widget with the current decoded frame.
- Converts Viper Pixels (`uint32 0xRRGGBBAA`) to byte-order RGBA for the `vg_image_set_pixels` GUI path.
- **GUI Image widget paint fix** — Added `image_paint()` vtable function to `vg_image.c` with nearest-neighbor scaled blit to the vgfx framebuffer. Previously, the Image widget stored pixel data but had no rendering code — this fixes `Image.SetPixels()` for all GUI users.

#### Supported Formats

| Container | Video Codec | Audio Codec | Extension | Status |
|-----------|-------------|-------------|-----------|--------|
| AVI (RIFF) | MJPEG | PCM WAV | `.avi` | Full decode |
| OGG | Theora | Vorbis | `.ogv` | Infrastructure (headers + YCbCr) |

---

### 3D Graphics Demos

Eight new demo programs in `examples/apiaudit/graphics3d/`:

| Demo | Features Demonstrated |
|------|----------------------|
| `minimal_3d_test.zia` | Canvas3D, Camera3D, Mesh3D, Material3D, Light3D — basic 3D rendering validation |
| `procedural_terrain_demo.zia` | PerlinNoise → Terrain3D.GeneratePerlin, splat maps, FPS camera, terrain-locked movement |
| `terrain_lod_demo.zia` | 256x256 terrain with SetLODDistances, frustum culling, SetSkirtDepth, free flight |
| `water_demo.zia` | Terrain island + Water3D with 3 Gerstner waves, fog, directional lighting |
| `vegetation_demo.zia` | Terrain + Vegetation3D with 5000 procedural grass blades, wind animation, LOD thinning |
| `shading_demo.zia` | 4 spheres with BlinnPhong, Toon, Fresnel, and Emissive shading models side-by-side |
| `video_demo.zia` | MJPEG AVI playback via VideoPlayer + Canvas.Blit |
| `video_gui_demo.zia` | VideoWidget in a Viper.GUI application with looping |

---

### GUI Runtime Hardening

Architectural improvements to the GUI subsystem for correctness and platform fidelity across macOS, Windows, and Linux.

#### Theme & App State Management
- **Per-app theme ownership** — Each `rt_gui_app_t` now owns a private scaled theme copy. Previously, `rt_gui_refresh_theme` mutated the built-in `vg_theme_dark()`/`vg_theme_light()` singletons in place, meaning multiple apps or rapid theme switches would corrupt shared state. Theme copies are rebuilt only when the base theme or HiDPI scale changes.
- **Widget runtime state save/restore** — Focus, keyboard capture, and tooltip state are saved per-app and restored on activation, enabling correct multi-app contexts.
- **Modal dialog stack** — Dialog routing now follows the actual `dialog_stack` array with `rt_gui_sync_modal_root`, replacing the old parallel event path that could desync from the widget tree.

#### Platform Text Input
- **`VGFX_EVENT_TEXT_INPUT`** — New event type carries translated Unicode text from the OS input method. Wired through macOS (`interpretKeyEvents:`/`insertText:`), Win32 (`WM_CHAR`), and X11 (`XLookupString`) backends.
- **GUI `KEY_CHAR` delivery** — `vg_event_from_platform` converts text-input events to `VG_EVENT_KEY_CHAR`, replacing the old US-layout ASCII key synthesis that broke on non-QWERTY keyboards and dead keys.

#### Widget Contract Fixes
- Dropdown placeholder strings copied instead of borrowing freed temporaries
- Dismissed notifications compacted immediately instead of accumulating stale entries
- Command palette placeholder and UTF-8 query path completed
- MessageBox prompt/builder flows honor default/cancel button semantics
- Font inheritance applied consistently at construction for all text-bearing widgets

---

### Comprehensive Documentation Pass

Substantial function-level documentation added across four major runtime subsystems, rewriting broken auto-generated comments into proper Viper `@brief`/`@details`/`@param`/`@return` annotations that explain the *why* and *how* for each function.

| Subsystem | Files | Functions Documented | Key Topics |
|-----------|-------|---------------------|------------|
| GUI Runtime | 8 | ~100 | App lifecycle, widget constructors, menu system, command palette, tooltips, toasts, drag-and-drop, find/replace bar |
| Graphics3D | 19 | ~130 | Canvas3D render pipeline, Camera3D (perspective/ortho/FPS/orbit/shake), Mesh3D procedural generators, Material3D PBR, Light3D types, skeletal animation, physics joints, raycasting, spline paths, terrain, water, decals, sprites, instanced batching, cubemap reflections, FBX/glTF loaders |
| Network | 19 | ~120 | TCP/UDP sockets, TLS handshake, certificate verification, HTTP client/server, WebSocket framing, SSE streaming, SMTP protocol, REST client, connection pooling, async sockets, DNS resolution |
| Crypto/Text | 8 | ~50 | AES-CBC/CTR, cipher abstraction, HKDF key derivation, password hashing (bcrypt/PBKDF2/Argon2), CSPRNG |
| IO | 11 | ~60 | Archive extraction, binary files, directories, glob matching, line reader, path utilities, save data, streams, temp files, file watcher, JSON streaming |

**Examples of rewritten comments:**
- `"Should the close of the app"` → `"Query whether the application's window has been closed"`
- `"Sphere the overlaps of the aabb3d"` → `"Test whether an AABB and a sphere overlap"`
- `"Hash the with iterations of the password"` → `"Hash a password with a custom PBKDF2 iteration count"`
- `"Play the at of the audio3d"` → `"Play a sound at a 3D position with distance-based attenuation"`

---

### HTTP Server Runtime Bindings

New `HttpServer` runtime class wired through the bytecode VM and both language frontends (Zia and BASIC), enabling Zia programs to serve HTTP requests:

| Component | Changes |
|-----------|---------|
| `runtime.def` | `HttpServer` class with `Listen`, `Accept`, `Respond`, `Close`, `Method`, `Path`, `Header`, `Body` |
| `NetworkRuntime.cpp` | VM native function implementations (130 LOC) |
| `BytecodeVM.cpp` | CALL_NATIVE dispatch entries for all 8 HttpServer methods |
| `Lowerer_Expr_Call.cpp` | Zia frontend lowering for HttpServer method calls |
| `Lower_OOP_MethodCall.cpp` | BASIC frontend lowering for HttpServer method calls |
| Tests | `RTHighLevelNetworkTests.cpp` (SSE + SMTP + HttpServer), `HttpServerRuntimeTests.cpp`, `TestHttpServerBinding.cpp`, `test_zia_http_server.cpp` |

---

### IO Runtime Hardening

Correctness and robustness improvements across the filesystem IO subsystem.

#### SaveData
- **GC-managed keys and values** — Migrated `SaveEntry` from raw `char*` fields to `rt_string`, eliminating manual `malloc`/`free` lifetime tracking and aligning with the GC-managed string model used everywhere else.
- **JSON parse error forwarding** — `rt_json_stream_error()` now surfaces parse errors to callers instead of silently producing empty data from malformed save files.
- **Versioned format support** — Save format includes version metadata for future migration paths.

#### Glob Pattern Matching
- **Character classes** — `[a-z]`, `[0-9]`, `[!abc]` (negated) bracket expressions with range support.
- **Case-insensitive matching on Windows** — `glob_char_eq` normalizes via `tolower` on Windows, preserving case sensitivity on POSIX.
- **`**` recursive descent** — Double-star matches across directory boundaries for deep file discovery.
- **Correct path separator handling** — `*` does not cross `/` (or `\` on Windows) boundaries.

#### File Watcher
- **Debounced event coalescing** — Rapid file modifications (e.g., editor save) coalesced into a single event to prevent callback storms.
- **Single-file watch** — Watches the parent directory and filters events by leaf name, since OS APIs (inotify, FSEvents, ReadDirectoryChangesW) only accept directory handles.
- **Windows `OVERLAPPED` handle leak** — `CloseHandle(overlapped.hEvent)` added to finalizer to prevent kernel handle exhaustion.

#### TempFile
- **Atomic creation** — Uses `O_CREAT|O_EXCL` (POSIX) / `CREATE_NEW` (Windows) to atomically fail if the path already exists, with collision retry using a fresh random ID. Prevents TOCTOU race conditions.

#### Archive Extraction
- **Path traversal validation** — Archive entry paths are checked for `../` directory escape before extraction, preventing zip-slip attacks.

#### New Tests
- `RTArchiveTests.cpp` — Archive extraction, path traversal guard, round-trip integrity (+75 lines)
- `RTGlobTests.cpp` — Character classes, recursive descent, case sensitivity (+21 lines)
- `RTSaveDataTests.cpp` — GC string entries, versioned format, parse error handling (+82 lines)
- `RTWatcherTests.cpp` — Event coalescing, single-file watch, cleanup (+74 lines)
- `RTFileExtTests.cpp` — Edge case extensions (+6 lines)

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
- Mach-O linker: `rt_audio_shutdown` exported as dynamic symbol but defined as weak stub, causing link failures on some configurations
- Metal backend: CAMetalLayer presentation broken on macOS 26 Tahoe — replaced with offscreen texture readback
- Metal backend clear color alpha: was 0.0 when PostFX inactive (transparent layer), now always 1.0
- Software 3D backend crash: `vgfx3d_show_gpu_layer` duplicate symbol — Metal version called with software context, sending `metalLayer` objc message to garbage memory (SIGSEGV). Fixed via vtable dispatch
- `drawRect:` CGImage blit covered Metal layer content every frame on macOS — software framebuffer overwriting GPU-rendered content
- Water3D wave normals: single sine wave normal used `dydz = dydx` (identical derivatives), producing incorrect normals for diagonal wave propagation. Gerstner model computes per-direction derivatives correctly
- GUI Image widget: `vg_image_t` stored pixel data via `SetPixels()` but had no vtable paint function — image content never rendered. Added `image_paint()` with nearest-neighbor scaled blit to framebuffer
- MJPEG AVI decode: frames missing DHT Huffman tables caused JPEG decode failure (returned NULL). Added automatic injection of standard Annex K DHT tables before SOS marker
- Zia runtime extern signatures only included return type — string-returning methods like `LevelData.ObjectType()` compared as pointer equality instead of string equality. `rtgen` now emits full parameter types
- Zia `List[Object].Push()` emitted wrong extern arity for user-defined class instances
- GUI menubar accelerator table leaked on destroy (missing free loop in `menubar_destroy`)
- macOS key equivalents (Cmd+N, F5) not consumed by native menu bar — arrow keys triggered system beep for unhandled navigation
- GPU texture caches (Metal/D3D11/OpenGL) served stale textures when Pixels content was modified in-place — generation-based invalidation now detects mutations across all backends
- InstanceBatch3D `realloc` could leave partial state (transforms allocated, snapshot NULL) on allocation failure — replaced with `calloc`+copy+free pattern that aborts cleanly
- InstanceBatch3D swap-remove only moved `transforms` array, leaving `current_snapshot` and `prev_transforms` pointing at the removed instance's data — now copies all three arrays
- Mesh3D `rt_mesh3d_transform()` used direct model matrix multiply for normals, producing incorrect lighting on non-uniformly scaled meshes — now uses inverse-transpose of upper 3x3
- Canvas3D `rt_canvas3d_screenshot()` returned NULL when using a GPU backend (only software framebuffer path existed) — now supports render target readback and `backend->readback_rgba()` GPU path
- Canvas3D did not propagate OS window resize events — added resize callback and `VGFX_EVENT_RESIZE` handling in `Poll()`
- macOS ViperGFX windows had no application menu (no About, no Cmd+Q) — default app menu now generated automatically with standard items
- GUI theme singletons mutated in-place: `rt_gui_refresh_theme` modified the built-in `vg_theme_dark()`/`vg_theme_light()` constants, corrupting shared state across apps — now creates private scaled copies per-app
- GUI modal dialog routing used a parallel event path that could desync from the widget tree — now follows the real dialog stack via `rt_gui_sync_modal_root`
- GUI overlay timing (tooltips, toasts) used last-input-event timestamps, causing animations to freeze while idle — now uses wall-clock time via `rt_gui_now_ms`
- GUI dropdown placeholder used freed temporary C string (use-after-free) — now copies the string into owned storage
- GUI notification manager accumulated dismissed entries without compacting — stale notifications never freed
- Platform text input on non-QWERTY keyboards produced wrong characters — old US-layout ASCII key synthesis replaced with OS text-input events (`VGFX_EVENT_TEXT_INPUT`)
- Network test `test_rt_network` failed on macOS: `getaddrinfo(NULL, ...)` with `AF_UNSPEC` prefers IPv6, storing `"::"` instead of expected `"0.0.0.0"` — test now accepts either wildcard
- Network test `test_rt_network_highlevel` SSE chunked mock server: wrong hex chunk size (`0x14` for 23-byte payload, should be `0x17`) and missing mandatory trailing `\r\n` after chunk data per RFC 7230 §4.1
- SaveData `SaveEntry` used raw `char*` for keys/values — manual `malloc`/`free` lifetime tracking leaked on error paths and didn't integrate with the GC. Migrated to `rt_string` with `rt_string_unref` cleanup
- SaveData silently produced empty results from malformed JSON save files — now forwards parse errors via `rt_json_stream_error()`
- Glob pattern `*` incorrectly matched path separators (`/`, `\`) — `*` now stops at directory boundaries, `**` matches across them
- Glob matching was case-sensitive on Windows — file systems are case-insensitive on Windows, so glob comparison now uses `tolower` normalization
- File watcher leaked Windows `OVERLAPPED.hEvent` kernel handle — `CloseHandle` added to finalizer
- File watcher single-file watch returned empty event paths — now reconstructs full paths from directory + leaf name components
- TempFile creation had a TOCTOU race: checked for existence then created — replaced with atomic `O_CREAT|O_EXCL` (POSIX) / `CREATE_NEW` (Windows) with collision retry
- Archive extraction accepted paths containing `../` — could write outside the target directory (zip-slip). Now validates and rejects path-traversal entries
- Cipher `rt_cipher_decrypt` did not fall back gracefully when PBKDF2-derived key failed authentication — now tries legacy HKDF derivation before trapping
