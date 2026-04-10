# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

> **DRAFT** — This document is a preliminary draft for v0.2.4. Content is subject to change before
> the official release.

## Version 0.2.4 - Pre-Alpha (TBD) — DRAFT

### Release Overview

Version 0.2.4 is a game engine, asset system, rendering, 3D physics, PBR materials, asset import pipeline, codegen optimization, native linker, cross-platform hardening, toolchain packaging foundation, language features, media codecs, IL optimizer, IDE intelligence, typed runtime metadata, and showcase release. Highlights:

- **3D Engine Enhancements** — Procedural terrain generation (`Terrain3D.GeneratePerlin`), terrain LOD with frustum culling and multi-resolution chunks, Gerstner wave water simulation (`Water3D.AddWave`), new `Vegetation3D` instanced grass/foliage system with wind animation, and material shader hooks (`SetShadingModel` for Toon/Fresnel/Emissive effects).
- **3D Format Loaders** — From-scratch glTF 2.0 (.gltf/.glb), STL (binary + ASCII), OBJ .mtl material parser, FBX texture and morph target extraction. Scene3D.Save for JSON serialization.
- **Video Playback** — MJPEG/AVI video decoder with `VideoPlayer` runtime class (Open/Play/Pause/Stop/Seek/Update), MJPEG DHT injection for AVI compatibility, AVI RIFF container parser, Theora codec infrastructure (header parsing, YCbCr 4:2:0→RGB conversion, OGG multi-stream demux), GUI `VideoWidget` for Viper.GUI applications, and Image widget paint fix for the GUI library.
- **Graphics Backend Hardening** — Generation-aware texture/cubemap caching across all 4 backends (Metal/D3D11/OpenGL/SW), morph target generation tracking with `rt_morphtarget3d_get_payload_generation()` and `morph_revision` field on draw commands, Canvas3D PostFX state latching at frame boundaries (`canvas3d_latch_gpu_postfx_state`/`apply_gpu_postfx_state`), Canvas3D window resize handling, GPU screenshot readback, InstanceBatch3D memory safety, Mesh3D inverse-transpose normal transform, Pixels mutation tracking, and macOS default application menu.
- **D3D11 / OpenGL Shared Backend Helpers** — Extracted `vgfx3d_backend_d3d11_shared.{c,h}` (180 + 117 LOC) and `vgfx3d_backend_opengl_shared.{c,h}` (126 + 77 LOC) factoring out per-object/per-material/per-scene constant buffer layouts, packed morph weight encoding, blend mode and target kind enums, color format enums, and OpenGL frame-history tracking (`vgfx3d_opengl_update_frame_history` for motion blur and temporal effects). The main `vgfx3d_backend_d3d11.c` and `vgfx3d_backend_opengl.c` files were rewritten against these shared headers (~900 lines changed each), and new unit tests (`test_vgfx3d_backend_d3d11_shared.c`, `test_vgfx3d_backend_opengl_shared.c`) directly exercise the shared helpers without requiring a live GPU context.
- **Native Linker Hardening** — BranchTrampoline rewritten with boundary-based placement, SectionMerger VA logic extracted as shared API, SymbolResolver platform-aware dynamic symbol classification, RelocApplier range-checked REL32, multi-section COFF writer for function-level code sections, Windows ARM64 native link gating, Mach-O ObjC section flags and symbol normalization for framework auto-linking.
- **Metal Backend: macOS 26 Compatibility** — Offscreen texture readback replaces CAMetalLayer direct presentation for macOS Tahoe compatibility. Backend vtable extended with `show/hide_gpu_layer` function pointers to fix software backend crash from duplicate global symbols.
- **Media Codec Suite** — From-scratch implementations of JPEG, GIF (animated), OGG Vorbis, and MP3 decoders. Extended PNG decoder to all color types, bit depths, interlacing, and transparency. Extended WAV loader to 24-bit and float32 PCM. Added OGG/MP3 music streaming with on-the-fly resampling. Added `Pixels.Load()` auto-detect, JPEG EXIF orientation, fast FFT-based IMDCT for Vorbis, and multi-pass residue decoding. 16 new runtime source files, 8 new tests.
- **Audio Streaming Overhaul** — OGG reader extended with `ogg_reader_next_packet_ex()` providing per-packet serial number, granule position, and BOS/EOS flags. Music streaming now selects the correct Vorbis logical stream in multi-stream OGG containers (e.g., `.ogv` files with both Theora video and Vorbis audio). Unified `vaud_music_seek_output_frame()` handles seek/rewind/loop-restart across all formats (WAV/OGG/MP3). `source_sample_rate` field separates file sample rate from mixer rate for correct duration reporting and resampling.
- **Runtime Stub Audit & Fixes** — Comprehensive audit of all C/C++ runtime stubs. Fixed `rt_exc_is_exception()` type safety, OOP destructor chaining (derived→base), OOP refcount imbalance (NEW temporary leak), TLS RSA-PSS SHA-384/SHA-512 hashing, bytecode VM missing opcodes, POSIX process isolation timeout, and enabled Windows threading tests.
- **Native PE/COFF Linker Pipeline** — Full Windows native linking without clang. COFF archive (.lib) reader, symbol resolver, section merger, dead-strip pass, ICF, relocation applier with `IMAGE_REL_AMD64` support, and PE executable writer with proper `.idata` import tables. Combined with the v0.2.3 assembler, `viper build` now produces native Windows executables end-to-end with zero external tool dependencies.
- **Native Exception Handling Lowering** — New `NativeEHLowering` pass (683 LOC) rewrites structured IL EH markers (`EhPush`/`EhPop`/`ResumeSame`/`ResumeNext`/`TrapErr`) into ordinary calls and branches before backend lowering, enabling cross-platform native exception handling on x86_64 and AArch64.
- **Zia Language Features** — Seven new features: variadic parameters (`func sum(nums: ...Integer)`), type aliases (`type Name = TargetType;`), shift operators (`<<`, `>>`), compound bitwise assignments (`<<=`, `>>=`, `&=`, `|=`, `^=`), single-expression functions (`func f(x: T) -> R = expr;`), lambda expressions (`(x: T) => expr`), and polymorphic `is` expressions that check the full subclass hierarchy.
- **Metal Backend: Feature-Complete** — All 14 backend plans implemented, bringing Metal from 47% to 94% feature parity with the software renderer. GPU skinning, morph targets, shadow mapping, terrain splatting, post-processing, and instanced rendering.
- **D3D11 Backend: 20 Features Implemented** — All 20 D3D11 backend plans implemented in a 3,173-line HLSL+C backend rewrite. Diffuse textures, normal/specular/emissive maps, spot lights, fog, wireframe/cull, render-to-texture, GPU skinning, morph targets (with normal deltas), shadow mapping, instanced rendering, terrain splatting, post-processing (bloom, FXAA, tonemap, DOF, motion blur, SSAO), cubemap skybox, and environment reflections. Windows CI validation job added.
- **Software Renderer Upgrades** — Per-pixel terrain splatting (4-layer weight blend), bilinear filtering, vertex color support, shadow mapping, and material shader hooks (Toon/Fresnel/Emissive shading models).
- **Codegen Pipeline Decomposition** — Both x86_64 and AArch64 backends refactored from monolithic per-function pipelines into composable pass-based architectures. x86_64 exposes `legalizeModuleToMIR`/`allocateModuleMIR`/`optimizeModuleMIR`/`emitMIRToAssembly`/`emitMIRToBinary` public APIs. AArch64 uses `PassManager`-based composition with Scheduler and BlockLayout passes at O1+. EH-sensitive modules bypass IL optimizations. New `CodeSection::appendSection()` and `DebugLineTable::append()` for per-function section merging.
- **Windows x86_64 Codegen Hardening** — CoffWriter cross-section symbol resolution, X64BinaryEncoder runtime symbol mapping, operand materialisation for TESTrr/call.indirect, SETcc REX prefix for byte registers, SSE RIP-relative MOVSD encoding, unsafe spill slot reuse disabled, and process isolation hang fix. Windows native executables now assemble, link, and run correctly.
- **AArch64 Codegen Hardening** — Register allocator protected-use eviction (prevents source operand clobbering during def allocation), operandRoles fix for immediate-ALU instructions, FPR load/store classification, spill slot reuse for clean values across calls, immediate utils extraction, binary encoder fixes, refcount injection bugfix, fastpath improvements, trap message forwarding, error field extraction via TLS bridge, Apple M-series scheduler latency tuning, and 10+ new codegen tests.
- **IL Optimizer Correctness** — EarlyCSE and GVN passes now enforce textual block ordering when replacing across dominator-ordered blocks, preventing use-before-def violations where a dominating definition appears later in the block list.
- **Zia Compiler Bug Fixes** — String bracket-index crash, `List[Boolean]` unboxing truncation, `catch(e)` binding via TLS message passing, `String.Contains()` method alias. New `ErrGetMsg` IL opcode and `rt_throw_msg_set/get` runtime functions for exception message propagation.
- **10 Game Engine APIs** — Entity (2D game object with built-in physics), Behavior (composable AI presets), Raycast2D (tilemap line-of-sight), LevelData (JSON level loader), SceneManager (multi-scene transitions), Camera.SmoothFollow (deadzone + lerp tracking), AnimStateMachine named states (play-by-name), MenuList.HandleInput (input convenience), Config.Load (JSON config), Tilemap.SetTileAnim (per-tile frame animation). 10 new runtime classes, 3,800+ LOC in C.
- **Asset Embedding System (VPA)** — Compile-time asset packaging via `embed`, `pack`, and `pack-compressed` project directives. VPA binary format with `AssetCompiler` and `VpaWriter` toolchain. `Assets.Load`/`LoadBytes`/`Exists`/`Mount` runtime API. Asset blobs injected into `.rodata` via the native assembler for zero-file-dependency executables. Cross-platform `Path.ExeDir()` for relative asset resolution.
- **Native macOS Menu Bar** — 554-line Objective-C bridge (`rt_gui_macos_menu.m`) mirrors Viper GUI menubars to the native macOS application menu bar. Special item relocation (About → app menu, Preferences → app menu with Cmd+,, Quit → app menu with Cmd+Q). Keyboard accelerator translation, Services submenu, and Hide/Show All standard items.
- **Bytecode VM CALL_NATIVE Expansion** — Native function index widened from 8-bit to 16-bit (255 → 65,535 max native references) to accommodate the growing runtime. Bytecode format version bumped to v2.
- **Zia Runtime Extern Signatures** — `rtgen` now emits full parameter types (not just return types) for all `RT_FUNC` entries in `ZiaRuntimeExterns.inc`. Enables correct string equality comparison for runtime methods returning `str` (e.g., `LevelData.ObjectType() == "enemy"` now emits `Viper.String.Equals` instead of `ICmpEq`).
- **Zia Language: `entity`/`value` renamed to `class`/`struct`** — Mainstream keyword alignment across all source, tests, REPL, LSP, docs, and VS Code extension.
- **VAPS Packaging Overhaul** — InstallerStub rewrite with full Windows .exe generation, WindowsPackageBuilder improvements, ZipWriter enhancements, PkgVerify expansion, symlink safety, dry-run mode, 57+ new tests.
- **GUI Runtime Hardening** — Theme ownership moved to per-app structs (no more mutating built-in dark/light singletons), modal dialog routing follows the real dialog stack, overlay timing uses wall-clock time, platform text input events (`VGFX_EVENT_TEXT_INPUT`) wired through macOS/Win32/X11 backends replacing ASCII key synthesis, dropdown placeholder ownership fix, notification compaction, and command palette UTF-8 query path.
- **IO Runtime Hardening** — SaveData migrated from raw C strings to GC-managed `rt_string` keys/values with versioned JSON format and migration support. Glob pattern matching extended with character classes (`[a-z]`, `[!0-9]`), case-insensitive matching on Windows, `**` recursive directory descent, and correct path separator handling. File watcher debounced event coalescing, single-file watch with directory monitoring, and Windows `OVERLAPPED` handle leak fix. TempFile atomic `O_CREAT|O_EXCL` creation with collision retry. Archive extraction path traversal validation.
- **HTTP Server Runtime Bindings** — `HttpServer` class wired through bytecode VM and both Zia/BASIC frontends with `Listen`, `Accept`, `Respond`, `Close` methods and request property accessors (`Method`, `Path`, `Header`, `Body`).
- **Graphics3D Ownership Hardening** — CubeMap3D, Material3D, Decal3D, Sprite3D, InstanceBatch3D, and Water3D now properly retain/release their texture, mesh, and material references. Prevents GC from collecting assets still in use by the renderer.
- **Zia Completion Runtime APIs** — Three new runtime bindings expose the Zia compiler's analysis pipeline directly to Zia programs: `Viper.Zia.Completion.Check(source)` returns serialized diagnostics, `.Hover(source, line, col)` returns type/signature info at a cursor position, `.Symbols(source)` returns all top-level symbols. Implemented via `parseAndAnalyze()` for error-tolerant partial compilation.
- **Zia `final` Enforcement** — The Zia semantic analyzer now rejects reassignment of `final` variables, for-in loop variables, and match pattern bindings at compile time. Lowerer safety net prevents SSA value corruption if enforcement is bypassed.
- **AArch64 Join-Phi Coalescing** — New peephole pass replaces stack round-trips at CFG join blocks with ordered register-to-register moves. Analyzes predecessor stores and successor loads to prove all edges materialize the same values in physical registers, then eliminates the loads and substitutes topologically-ordered copies (cycles bail out). CBR terminator lowering generalized to emit edge blocks whenever branch arguments are present, not only for same-target branches.
- **Typed Return Metadata** — `runtime.def` signatures annotated with concrete return types (`obj<ClassName>`, `seq<str>`) for 100+ factory/conversion/collection methods. Both frontends now infer the exact runtime class returned, enabling chained method calls without losing type information. New `concreteRuntimeReturnClassQName` API and BASIC `inferObjectClassQName` recursive expression type tracer.
- **3D Runtime File Decomposition** — `rt_canvas3d.c` split into core + `rt_canvas3d_overlay.c` (screen-space overlay, screenshot, debug-draw). `rt_scene3d.c` split into core + `rt_scene3d_vscn.c` (.vscn save/load serialization) with shared `rt_scene3d_internal.h`.
- **Collider3D Runtime Class** — New reusable 3D collision shape system with 7 shape types: box, sphere, capsule, convex hull, triangle mesh, heightfield, and compound (parent with child transforms). Shapes decouple collision geometry from physics bodies — `Body3D.SetCollider` attaches a shape, AABB and narrow-phase dispatch use the attached collider.
- **Physics3D Expansion** — Quaternion-based orientation with angular velocity/torque/impulse, dynamic/static/kinematic body modes, linear and angular damping, sleep system (configurable thresholds, manual Wake/Sleep), continuous collision detection (CCD) via substep sweeps, and a mass-only `Body3D.New` constructor.
- **ELF Dynamic Linking** — Native linker now produces dynamically-linked Linux executables: PT_INTERP, PT_DYNAMIC, .dynsym, .dynstr, .hash (SYSV), .rela.dyn with R_X86_64_GLOB_DAT. Linux x86_64 programs can natively link against libc/libm/libpthread/libX11/libasound without system linker fallback.
- **Native Linker Overhaul** — Table-driven macOS/Windows import plans replacing ad-hoc if/else chains, DynamicSymbolPolicy extraction, proper S_ZEROFILL (Mach-O) and SHT_NOBITS (ELF) for BSS sections, string dedup section compaction, dead-strip applied to all objects (not just archives), ICF cross-object address-taken resolution.
- **Model3D Asset Import** — Unified 3D asset container with `Load` (routes by extension: .vscn/.fbx/.gltf/.glb), `Instantiate` (clones node hierarchy with shared resources), and `InstantiateScene` (creates full Scene3D). Imported meshes, materials, skeletons, and animations are shared across instances.
- **AnimController3D** — High-level animation state controller with named states, crossfade transitions, play/pause/stop, speed control, loop modes, and event frame callbacks. `SceneNode3D.BindAnimator` + `Scene3D.SyncBindings(dt)` for automatic skeleton-driven pose updates.
- **PBR Materials** — `Material3D.NewPBR(metallic, roughness, ao)` with metallic-roughness workflow. Albedo, metallic-roughness, AO, normal, and emissive texture map slots. `Clone`/`MakeInstance` for shared-base + per-instance overrides. AlphaMode (opaque/mask/blend), DoubleSided, NormalScale properties. Cook-Torrance BRDF with GGX distribution and Schlick fresnel implemented across all 4 GPU backends.
- **NavAgent3D** — Autonomous pathfinding agent on NavMesh3D surfaces. A* path query, string-pulling corridor smoothing, steering with configurable speed/acceleration/stopping distance, node binding for automatic SceneNode3D sync.
- **3D Audio Objects** — `AudioListener3D` (position/forward/velocity with node and camera binding) and `AudioSource3D` (inner/outer cone, min/max distance, rolloff, looping, pitch, gain). `Audio3D.SyncBindings(dt)` batch-updates spatial positions. Distance attenuation and stereo panning in the audio mixer.
- **Cross-Platform Hardening** — Shared `PlatformCapabilities.hpp` with `VIPER_HOST_*`/`VIPER_CAN_*` capability macros replacing scattered raw `_WIN32`/`__APPLE__`/`__linux__` checks. CMake capability summary gate (`VIPER_GRAPHICS_MODE`/`VIPER_AUDIO_MODE` with AUTO/REQUIRE/OFF modes). Generated `RuntimeComponentManifest` for machine-checked archive names. Platform import planners split from monolithic NativeLinker.cpp. Unified `build_viper_unix.sh` replacing near-identical mac/linux scripts. Platform policy lint script. CTest `SKIP_RETURN_CODE 77` for visible test skips.
- **Toolchain Packaging Foundation** — New `viper install-package` CLI command with `ToolchainInstallManifest` as the shared data model for packaging Viper itself from a staged `cmake --install` tree (separate from the existing app packaging `viper package`). Windows/macOS/Linux package builders gain toolchain entry points. `build_installer.sh`/`.cmd` wrapper scripts. Installer plans revised with reuse-first architecture, prerequisite fixes, and a verification matrix (plans at `misc/plans/installer/`).
- **Installed Runtime Library Discovery** — `LinkerSupport.cpp` gains layered search for runtime archives: `VIPER_LIB_PATH` env → exe-relative `../lib` → platform standard paths → build-tree fallback. Companion library resolvers (vipergfx, viperaud, vipergui) threaded through both x86_64 and AArch64 codegen pipelines so an installed Viper can compile native executables without a build tree. Prerequisite for shipping installers.
- **Demos & Documentation** — XENOSCAPE sidescroller (17K LOC), 3D bowling (3.1K LOC), ViperSQL (10 SQL features, runtime API migration), ViperIDE professional IDE (live diagnostics, hover, go-to-def, search, symbol outline, 21 files / 7 dirs), Chess (pre-rendered sprites, core/engine/ui), 8 Graphics3D API demos, 6 app/game smoke probes; comprehensive 186-file docs audit (130 with frontmatter date-bumped to 2026-04-09; ~15 substantive content fixes including arithmetic-semantics / il-{guide,reference,quickstart,passes} / frontend-howto opcode policy corrections — plain `add`/`sub`/`mul`/`sdiv`/`udiv`/`srem`/`urem` are verifier-rejected for signed integers per `SpecTables.cpp:55-152` and only `iadd.ovf` / `sdiv.chk0` / etc. are legal; BASIC `DISPOSE`→`DELETE`, RESUME 0 description, TIMER return type, frontend-howto post-`std::move` Instr UB, missing IRBuilder methods, debugging.md `eh.push`/`eh.pop` model corrections, dependencies.md runtime path reorganization); 700+ Doxygen comments, prior 70+ factual error pass; full per-file worklist at `misc/plans/docs-audit-20260409.md`.

#### By the Numbers

| Metric | v0.2.3 | v0.2.4 | Delta |
|--------|--------|--------|-------|
| Commits | — | 108 | +108 |
| Source files | 2,671 | 2,866 | +195 |
| Production SLOC | ~348K | ~445K | +97K |
| Test count | 1,351 | 1,442 | +91 |

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

#### Audio Streaming Overhaul

Significant rework of the music streaming subsystem across all three compressed formats (OGG Vorbis, MP3, WAV):

- **Multi-stream OGG support** — `ogg_reader_next_packet_ex()` returns per-packet `ogg_packet_info_t` with serial number, granule position, and BOS/EOS flags. Vorbis stream selection now identifies the first Vorbis BOS packet and filters all subsequent reads by serial, enabling correct audio extraction from mixed OGG containers (`.ogv` with Theora + Vorbis).
- **Unified seek/rewind** — New `vaud_music_seek_output_frame()` handles seek, rewind, and loop-restart for all formats. Resets format-specific decoder state (OGG: rewinds reader, re-parses headers for the selected serial; MP3: rewinds stream; WAV: fseek to data offset), primes buffer 0, and leaves the stream ready for playback.
- **Source sample rate separation** — `source_sample_rate` field tracks the original file sample rate independently from `sample_rate` (always mixer rate). Fixes duration reporting (`frame_count` now represents output frames) and resampling calculations for all three formats.
- **OGG duration from granule** — Last-page granule scan during load provides accurate `frame_count` for Vorbis streams (previously 0/unknown).
- **Loop restart consolidation** — Mixer loop restart code (previously duplicated per-format in `mix_music`) now delegates to `vaud_music_seek_output_frame(0)`, eliminating 40+ lines of format-specific restart logic.

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

- `test_native_linker` — Windows ARM64 gating diagnostic (53 LOC), macOS ObjC framework linking with section flag and symbol validation
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
- **Lambda expressions** — Parenthesized lambdas use `=>` with typed parameters, for example `(x: Integer) => x * 2`. Zero-argument lambdas use `() => expr`.
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
- **`final` variable enforcement** — Semantic analyzer now emits `error[V3000]: Cannot reassign final variable 'x'` when code attempts to reassign a `final` local variable, for-in loop variable, or match pattern binding. Covers all 10 compound assignment operators (`+=`, `-=`, etc.) via parser desugaring. Lowerer safety net prevents SSA value corruption as a defense-in-depth measure.
- **Completion runtime APIs** — Three new `RT_FUNC` bindings expose the compiler's analysis pipeline to Zia programs: `Viper.Zia.Completion.Check(source)` runs error-tolerant semantic analysis and returns serialized diagnostics (severity, line, col, code, message); `.Hover(source, line, col)` resolves the identifier at the cursor via `findSymbolAtPosition()` and returns kind + type; `.Symbols(source)` enumerates all top-level symbols with kind, type, and line number. All three use `parseAndAnalyze()` for error-tolerant partial compilation.
- **`main()` entry point parity** — `main()` is now treated as an entry point alongside `start()` for interface itable initialization (`__zia_iface_init`) and global variable initializer emission. Previously only `start()` received these, causing uninitialized globals and missing vtables in `main()`-based programs.
- **Optional struct boxing** — `emitOptionalWrap` now boxes struct payloads to the heap (via `emitBoxValue`) instead of returning a raw stack pointer. `emitOptionalUnwrap` correspondingly unboxes. Fixes dangling stack pointer when returning `Struct?` from a function.

**BASIC frontend:**
- **Constant folding for builtins** — `FoldBuiltins.cpp` evaluates `ABS`, `INT`, `SGN`, `SQR`, `LOG`, `EXP`, `SIN`, `COS`, `TAN`, `ATN`, `ASC`, `CHR$`, `LEN`, `LEFT$`, `RIGHT$`, `MID$`, `STR$`, `VAL`, `STRING$`, `SPACE$`, `LCASE$`, `UCASE$`, and `LTRIM$`/`RTRIM$`/`TRIM$` at compile time when arguments are constant. Eliminates runtime calls for constant expressions.

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
- **Call ABI refactor** — `CallArgLayout` extracted as shared utility for SysV x86-64 call argument classification. `FrameLayoutUtils` for common frame lowering patterns. New `test_x86_call_abi.cpp` tests.

**AArch64 backend:**
- **`i1` parameter masking** — Boolean parameters masked with `AND 1` at function entry, matching return-value masking. Prevents upper-bit garbage corruption.
- **Remove redundant refcount injection** — `emitRefcountedStore` lambda stripped from instruction lowering. String ownership belongs in the IL layer.
- **Immediate utils extraction** — `A64ImmediateUtils.hpp` helper for immediate encoding, asm emitter hardening, binary encoder fixes, arithmetic/call fastpath improvements, regpool and symbol resolver fixes.
- **Trap message forwarding** — `TrapErr` now materialises the message string operand into x0 and passes it to `rt_trap()`, enabling catch handlers to display the user's throw message in native executables.
- **Error field extraction via TLS** — `ErrGetKind`, `ErrGetCode`, and `ErrGetLine` now call runtime TLS accessors (`rt_trap_get_kind/code/line`) instead of returning hardcoded 0. `rt_trap()` auto-classifies the trap kind from the message prefix. Enables typed catch (`catch(e: DivideByZero)`) in native code.
- **Register allocator protected-use eviction** — `protectedUseGPR_`/`protectedUseFPR_` sets prevent the LRU and furthest-use victim selectors from evicting source operands of the currently-allocating instruction. Fixes the pattern where materializing a GEP destination register could evict the GEP base register, corrupting subsequent field stores (broke BowlingGame.init in the 3D bowling demo).
- **OperandRoles fix for immediate-ALU** — `isUseDefImmLike` opcodes (AddRI, SubRI, etc.) now correctly classify operand 0 as DEF-only instead of USE+DEF. Prevents `computeNextUses` from recording spurious use positions for destination registers.
- **FPR load/store classification** — `isMemLd` and `isMemSt` now include `LdrFprFpImm`, `LdrFprBaseImm`, `StrFprFpImm`, `StrFprBaseImm`, and `StrFprSpImm`. Fixes floating-point operand tracking in the register allocator.
- **Clean FPR spill slot reuse** — Values loaded from memory (e.g., rodata FP constants) that are not marked dirty still get assigned spill slots when they survive a call in caller-saved FPR registers. Previously, clean loads were dropped without spilling, causing reloads from uninitialized frame offsets.
- **Dead vreg early release** — When `getNextUseDistance` returns `UINT_MAX` (no future uses), the physical register is released immediately without generating a spill store, reducing unnecessary memory traffic.
- **Apple M-series scheduler tuning** — Instruction latency model updated for Firestorm cores: FP divide 3→10 cycles, integer divide 3→7, FP multiply 3→4. Improves instruction scheduling for FP-heavy code.
- **Secondary scratch register (kScratchGPR2)** — X16 (IP0) formalized as `kScratchGPR2` for post-RA helper sequences that need a second temporary while `kScratchGPR` (X9) holds the base value. X16 excluded from register allocator pool. AsmEmitter and A64BinaryEncoder updated to use the named constant instead of hardcoded `PhysReg::X16`.
- **Pipeline decomposition** — `PassManager`-based pass composition replacing direct function calls in `runCodegenPipeline`. Scheduler and BlockLayout passes added to the O1+ pipeline (previously only peephole ran post-RA). EH-sensitive modules (`EhPush`/`EhPop`/`ResumeSame`/`ResumeNext` opcodes) bypass IL optimizations to avoid structural invariant violations. Virtual register space partitioned into three ranges: general vregs (`kFirstVirtualRegId`=1), phi-inserted vregs (`kPhiVRegStart`=40000), and cross-block spill keys (`kCrossBlockSpillKeyStart`=50000) with overflow guards.
- **Join-phi coalescing peephole** — New `coalesceJoinPhiLoads` pass (Pass 4.88) eliminates stack round-trips at CFG join blocks by proving all predecessor edges store the same physical registers to the same frame offsets, then replacing the successor's load prefix with topologically-ordered register moves. Handles both single-predecessor forwarding and multi-predecessor consensus. Copy ordering avoids clobbering via dependency analysis; cycles cause conservative bailout. `forwardSinglePredPhiLoads` (Pass 4.86) prepared but disabled pending nested control-flow validation. Existing Pass 4.8 store→load forwarding reworked for correct copy ordering and cycle handling.
- **CBR edge block generalization** — Terminator lowering now emits edge blocks (`Ledge_true_N`/`Ledge_false_N`) whenever branch arguments are present on either the true or false arm, not only when both arms target the same block. Previously, different-target CBRs with branch arguments silently dropped the argument copies, causing miscompilation in branch-heavy control flow (e.g., multi-level if/else ladders with phi values).
- **Loop phi-spill multi-block fix** — `eliminateLoopPhiSpills` (LoopOpt.cpp) now correctly handles multi-block loops where the latch block (not the split body) carries phi values on the back-edge. Refactored into `insertEdgeMoves` and `removeStores` lambdas applied to whichever block actually contains the phi-slot stores.
- **Register allocator live-out spill fix** — New `isLiveOut()` query prevents premature register release for vregs that have no remaining uses in the current block but are live-out to successors. End-of-block spill insertion scan corrected from reverse to forward to find the first terminator insertion point.
- **CBR compare lowering generalized** — Compare-and-branch optimization now fires in all blocks (not just entry), using `materializeValueToVReg()` for operands. Includes floating-point compares (`FCmpEQ`/`FCmpLT`/etc.) via `FCmpRR` + `BCond`.
- **Frame address resolution** — New `resolveFrameAddress()` walks AddrOf/GEP chains to compute stack offsets, enabling direct frame-relative loads/stores without materializing pointer temporaries.
- **AArch64 branch relaxation infrastructure** — Iterative label offset computation (`computeFunctionLabelOffsets`) with `measureInstructionSize` and `estimateFunctionSize`. New `A64CondBr19` relocation kind for conditional branches (BCond/Cbz/Cbnz) that exceed ±1 MB displacement.

**x86_64 backend (new):**
- **Regalloc carry-through** — `canCarryIntoNextBlock()` skips forced spills at straight-line single-predecessor fallthrough edges, keeping values in registers across simple block boundaries. `spillActiveValue()` consolidates spill logic; `crossBlockSpillVRegs_` tracks vregs requiring safe (non-reuse) spill slots.
- **Branch relaxation hardening** — `measureInstructionSize` now takes `currentOffset` for accurate RIP-relative displacement sizing. `estimateFunctionSize()` and `verifyPredictedLabelOffset()` detect drift between predicted and actual label positions.
- **Rodata-before-functions** — Rodata emission moved before function encoding so rodata symbol references are available during `encodeFunction`.
- **System linker shell-out removed** — ~330 lines of `cc`/`ld` invocation code removed from x86_64 `CodegenPipeline`; native linker is the default path. `--system-link` deprecated.

**Native Exception Handling Lowering:**
- **`NativeEHLowering` pass** (`src/codegen/common/NativeEHLowering.cpp`, 683 LOC) — Rewrites structured IL exception handling markers into ordinary IL calls and branches before backend lowering. Transforms `EhPush`/`EhPop` scope markers into `rt_eh_push`/`rt_eh_pop` runtime calls, converts `TrapErr` into `rt_trap()` invocations with message operand materialisation, and lowers `ResumeSame`/`ResumeNext` into control flow jumps. Both x86_64 and AArch64 `CodegenPipeline` invoke the pass before MIR lowering. 171-line unit test validates all EH opcode transformations.

**Common codegen infrastructure:**
- **`CodeSection::appendSection()`** — Merge two `CodeSection` objects with automatic symbol and relocation index rebasing. External symbols are deduped via `findOrDeclareSymbol`; defined symbols get offset-biased entries. Compact unwind and Win64 unwind entries are also rebased. Enables per-function binary emission followed by a single merged section for backward-compatible symbol extraction.
- **`DebugLineTable::append()`** — Merge debug line entries from another table with address bias and file index remapping. Both x86_64 and AArch64 `BinaryEmitPass` now emit per-function debug tables and merge them, producing correct DWARF `.debug_line` across function boundaries.
- **`seedDebugFiles()` helper** — Scans MIR for maximum file ID and populates debug table file entries from `debugSourcePath`, normalizing paths via `std::filesystem::path::lexically_normal()`. Replaces hardcoded `addFile("<source>")`.
- **`FrameLayout::ensureSpill` vreg widened** — Parameter type changed from `uint16_t` to `uint32_t` to accommodate the expanded virtual register space.

**IL optimizer:**
- **EarlyCSE textual ordering guard** — CSE replacements now track the defining block for each available expression via `AvailableExpr{value, block}`. `isTextuallyAvailable()` checks that the defining block appears at or before the use block in the function's block list, preventing replacements that would create textual use-before-def violations in the IL.
- **GVN textual ordering guard** — Same `isTextuallyAvailable()` guard applied to both expression value-numbering and redundant load elimination. Available values stored as `vector<AvailableValue>` (searched most-recent-first) to preserve dominator-scoped lookup correctness while filtering by textual order.

**Native linker:**
- `RtComponent::Game` — Game runtime classes link correctly via `libviper_rt_game.a` after directory reorganization.
- `-lshell32` — Added to Windows linker command for `DragQueryFile`/`DragAcceptFiles` GUI support.
- **PeExeWriter hardening** — Import table construction and section alignment fixes for Windows native executables.
- **NativeLinker improvements** — Enhanced platform detection and link-time error diagnostics.
- **Mach-O ObjC section flags** — ObjC metadata sections now emit proper Mach-O section types: `S_CSTRING_LITERALS` for `__objc_classname`/`__objc_methname`/`__objc_methtype`, `S_LITERAL_POINTERS` for `__objc_selrefs`, `S_ATTR_NO_DEAD_STRIP` for class/category/protocol lists, and `S_COALESCED` for `__objc_protolist`. Section alignment emitted as log2 power-of-two.
- **Mach-O symbol name fallback** — `findWithMachoFallback` now searches bidirectionally: plain name, underscore-stripped name, and underscore-prefixed name. `SymbolResolver` uses this for both undefined symbol lookup and defined symbol resolution, fixing cross-object symbol matching between Mach-O and ELF naming conventions.
- **ObjC framework symbol normalization** — `normalizeMacFrameworkSymbol()` strips `OBJC_CLASS_$_`, `OBJC_METACLASS_$_`, and `OBJC_EHTYPE_$_` prefixes for framework rule matching. `isObjcClassLookupSymbol()` recognizes ObjC class references regardless of leading underscore count, enabling flat-namespace lookup for classes whose defining framework can't be determined from the symbol prefix alone.
- **Windows ARM64 import stubs** — `generateWindowsImports()` now emits AArch64 `ADRP`/`LDR`/`BR` sequences for import thunks alongside x86_64 `JMP [rip+disp32]` stubs, with correct COFF ARM64 relocation types (`kPageRel21`, `kPageOff12L`). Machine type set to `0xAA64` for ARM64 COFF objects.
- **ELF dynamic linking** — `ElfExeWriter` rewritten from static-only to full dynamic-link support: PT_INTERP (`/lib64/ld-linux-x86-64.so.2`), PT_DYNAMIC, `.dynamic` section with DT_NEEDED/DT_HASH/DT_STRTAB/DT_SYMTAB/DT_RELA, `.dynsym`, `.dynstr`, `.hash` (SYSV ELF hash), `.rela.dyn` with `R_X86_64_GLOB_DAT` relocations for GOT slots. Linux x86_64 programs can now natively link against shared libraries without falling back to the system linker.
- **Linux import plan** — `planLinuxImports()` classifies dynamic symbols into libc, libm, libdl, libpthread, libX11, and libasound shared libraries via prefix/exact-match tables. `generateDynStubsX8664()` emits 6-byte `jmpq *__got_sym(%rip)` stubs with 8-byte GOT slots for each dynamic symbol.
- **Table-driven import plans** — macOS import plan rewritten with `MacImportRule` structs mapping dylib paths to symbol prefix/exact-match tables (libSystem, CoreFoundation, Foundation, AppKit, Metal, CoreGraphics, AudioToolbox, Security, IOKit, CoreText, CoreServices, SystemConfiguration). Windows `dllForImport` similarly restructured. `DynamicSymbolPolicy.hpp` extracted from `SymbolResolver` — archive symbol filtering moved downstream to import plan builders.
- **Proper zerofill sections** — `OutputSection::zeroFill` flag propagated through section merging, dead-strip, and ICF. Mach-O emits `S_ZEROFILL` for BSS and `S_THREAD_LOCAL_ZEROFILL` for TLS BSS. ELF emits `SHT_NOBITS` with zero file size. Eliminates unnecessary file backing for zero-initialized data, reducing executable size.
- **Dead-strip all objects** — `deadStrip()` now applies liveness analysis to all input objects (not just archive extracts). Only entry points, TLS, ObjC metadata, and runtime roots are unconditional. User `.o` sections that are unreachable from roots are stripped.
- **String dedup compaction** — After dedup aliasing, cstring sections where every byte is covered by a symbolized string are physically compacted by removing duplicate bytes (previously only symbol aliasing, no size reduction).
- **ICF cross-object resolution** — Address-taken detection in `foldIdenticalCode()` now resolves through the global symbol table for cross-object relocation targets, instead of only checking local section indices.
- **COFF writer hardening** — `validateCoffRelocationAddend()` diagnostic for unsupported addends. `.pdata` function length field now emitted (was 0). Defined rodata symbols pre-indexed for correct cross-section resolution.

**Runtime:**
- `SetErrorMode` + `_set_abort_behavior` added to `rt_init_stack_safety` on Windows to suppress crash/assert dialog boxes in natively compiled programs.
- **Trap IP recovery** — `rt_trap_set_ip()`/`rt_trap_get_ip()` store and retrieve the native instruction pointer associated with the most recent trap. `TrapGetIp` added to `runtime.def`.
- **Error code classification** — `rt_err_to_trap_kind()` maps `RtError` codes to trap kind integers (overflow=1, invalid cast=2, domain=3, bounds=4, file-not-found=5, etc.). `rt_trap_error_make()` and `rt_trap_raise_error()` convenience functions combine message storage, field classification, and trap invocation.
- **Caps Lock platform query** — `Keyboard.CapsLock` now queries real OS state on every call: `GetKeyState(VK_CAPITAL)` on Windows, `CGEventSourceFlagsState` on macOS, `XkbGetIndicatorState` on Linux. Replaces the previous key-event toggle tracker which could desync from the actual LED state. Test hooks (`rt_input_set_caps_lock_query_hook`) enable deterministic unit testing without a live window.
- **Mouse cursor warp** — `Mouse.SetPos(x, y)` now warps the OS cursor via `vgfx_warp_cursor` platform bridge: `CGWarpMouseCursorPosition` on macOS, `SetCursorPos` on Windows, `XWarpPointer` on Linux. Previously only updated the internal tracking state without moving the actual cursor.

**Windows test infrastructure:**
- ProcessIsolation framework reworked: function pointers don't survive `CreateProcess`, so `registerChildFunction()` with indexed dispatch (`--viper-child-run=N`) replaces direct pointer passing. `dispatchChild()` added to `TEST_WITH_IL` macro and all 16 VM/conformance tests. Windows test failures reduced from 48 to 4.
- Codegen test assertions accept `.rdata` (Windows COFF) alongside `.rodata` (ELF), `cmovneq`→`cmovne` suffix fix, platform-adaptive paths for RTDiskFullTests, RTNetworkHardenTests, and test_vm_rt_trap_loc.
- `rtgen` wraps generated `ZiaRuntimeExterns.inc` calls in per-namespace lambdas to prevent MSVC Debug stack overflow (4338 inline `defineExternFunction` calls exceeded the default 1 MB Windows stack).
- `WinDialogSuppress.c` linked into viper/vbasic/zia executables to suppress Windows crash dialog boxes in CI.
- `test_cf_stress` assertions made ABI-agnostic — accepts any `jcc` (jl, jg, etc.) instead of requiring `testq+jne`, since Windows x64 fuses comparison+branch into `cmpq+jcc`.
- `test_native_asm` `EquivBasicReturn` test skipped when system assembler (clang/cc) is not in PATH.
- Assorted Windows build/compat fixes in runtime graphics, networking, and text hashing.

**Linux native build:**
- Split `build_viper.sh` into platform-specific `build_viper_linux.sh` and `build_viper_mac.sh` scripts for cleaner CI. New `build_demos_linux.sh` for native x86_64 demo compilation on Linux.
- ELF writer extended with GOT/PLT-style dynamic relocation support for shared library imports on Linux.
- ELF object writer, COFF writer, and Mach-O writer hardened for cross-platform emission edge cases (alignment, section flags, symbol ordering).
- OpenGL 3D backend: Linux-specific initialization fixes, PBR uniform forwarding, shader compilation guards.
- Software 3D backend: platform-gated overrides to prevent non-graphics builds from pulling GPU symbols.
- x86_64 LowerDiv: IDIV encoding fix for Linux ABI (RDX clobber handling).
- Runtime: ALSA audio backend, crypto/TLS, threading, and network test portability fixes for Linux.

**Smoke probes:**
- 6 new Zia smoke probe tests (`zia_smoke_paint`, `zia_smoke_viperide`, `zia_smoke_vipersql`, `zia_smoke_3dbowling`, `zia_smoke_chess`, `zia_smoke_xenoscape`) exercise real example app/game module stacks with deterministic probes that verify core subsystem wiring without requiring a display.

**AArch64 codegen tests:**
- 4 new CBR edge block tests: different-target edge blocks, branch ladder correctness (run-native), branch ladder asm quality (join reload elimination), mixed fallthrough/join (run-native).
- Loop phi test expectations updated for unconditional backedge after loop optimization.

**Cross-platform infrastructure:**
- `PlatformCapabilities.hpp` (`src/common/`) — shared C++ header with `VIPER_HOST_WINDOWS`/`VIPER_HOST_MACOS`/`VIPER_HOST_LINUX`, `VIPER_COMPILER_MSVC`/`VIPER_COMPILER_CLANG`/`VIPER_COMPILER_GCC`, and capability macros (`VIPER_CAN_FORK`, `VIPER_HAS_X11`, `VIPER_HAS_ALSA`, `VIPER_NATIVE_LINK_X86_64`, etc.). Replaces ad-hoc raw `_WIN32`/`__APPLE__`/`__linux__` checks in codegen, tools, and tests.
- CMake capability gate — `VIPER_GRAPHICS_MODE` and `VIPER_AUDIO_MODE` cache variables with AUTO/REQUIRE/OFF modes. AUTO (default) uses the feature if deps are available, REQUIRE fails configure if deps are missing (with install instructions), OFF explicitly disables. Replaces silent `return()` in library CMakeLists.txt that could produce broken binaries without warning.
- Generated `RuntimeComponentManifest.hpp` — machine-checked archive name → component mapping generated from `runtime.def`, replacing hand-maintained string tables in `RuntimeComponents.hpp`. Drift between runtime.def and linker discovery is now a build error.
- Platform import planners — NativeLinker.cpp monolith (~1100 lines removed) split into `PlatformImportPlanner.hpp`, `MacImportPlanner.cpp`, `LinuxImportPlanner.cpp`, `WindowsImportPlanner.cpp`. Each planner owns its platform's symbol → dylib/DLL classification.
- Unified `build_viper_unix.sh` — replaces near-identical `build_viper_mac.sh` and `build_viper_linux.sh` with platform detection via `uname -s`. Old scripts retained as thin wrappers. Standardized env vars (`VIPER_BUILD_DIR`, `VIPER_BUILD_TYPE`, `VIPER_SKIP_INSTALL`). Demo scripts similarly consolidated.
- `lint_platform_policy.sh` — flags raw `_WIN32`/`__APPLE__`/`__linux__` usage outside approved adapter files listed in `platform_policy_allowlist.txt`.
- `run_cross_platform_smoke.sh` — detects host capabilities, runs the appropriate ctest label slice and example smoke probes, reports skips explicitly.
- `PlatformSkip.h` + CTest `SKIP_RETURN_CODE 77` — test skips are now visible in CI output ("131 passed, 19 skipped" instead of "150 passed"). `viper_add_ctest()` sets skip return code centrally.
- Audio surface link tests (`RTAudioSurfaceLinkTests.cpp`) — verifies disabled-audio builds link correctly against stubs.

**Toolchain packaging foundation:**
- `ToolchainInstallManifest` (`src/tools/common/packaging/ToolchainInstallManifest.hpp/cpp`) — shared data model for packaging Viper itself. Consumed by every platform builder; preserves the staged relative install layout underneath the platform-specific install root. Distinct from `PackageConfig` (which is for apps built WITH Viper).
- `viper install-package` CLI (`cmd_install_package.cpp`) with `--target`, `--arch`, `--stage-dir`, `--build-dir`, `--verify-only`, `--no-verify`, `--keep-stage-dir`, `--stage-only`, and `--metadata-file` flags.
- Platform builders extended with toolchain entry points: `buildWindowsToolchainInstaller` reuses PE+ZIP overlay, `buildMacOSToolchainPkg` builds on `.app` bundle knowledge, `buildLinuxToolchainPackages` extends the `.deb` pipeline.
- `scripts/build_installer.sh` and `scripts/build_installer.cmd` — thin wrappers that stage via `cmake --install`, then invoke the CLI.
- `InstallPackageTarballSmoke.cmake` and `InstalledViperConfigSmoke.cmake` — CTest integration tests validating the staged install tree and the installed `ViperConfig.cmake`/`ViperTargets.cmake` export.
- Installer plans revised at `misc/plans/installer/` with prerequisite fixes, verification matrix, and Phase 7 signing/release scaffolding.

**Installed runtime library discovery:**
- `LinkerSupport.cpp` gains layered search: `VIPER_LIB_PATH` env var → exe-relative `../lib/` → platform standard paths (`/usr/lib/viper`, `/usr/local/viper/lib`, etc.) → existing build-tree fallback.
- Companion library resolvers for `vipergfx`, `viperaud`, `vipergui` threaded through x86_64 and AArch64 `CodegenPipeline` so native linking works identically whether Viper runs from a build tree or an installed prefix.
- `test_linker_support.cpp` unit test covering all four discovery tiers.
- `test_runtime_surface_audit.cpp` validates that every runtime component archive the discovery code looks for is actually produced by the build.
- Exported target set in `ViperTargets.cmake` reconciled with the actual built runtime component libraries, so downstream `find_package(Viper CONFIG REQUIRED)` consumers see the complete runtime.

**AArch64 backend hardening (from `misc/plans/backend-20260409.md`):**
- `FrameBuilder` spill slot lifetimes now carry a block epoch; cross-block reuse is guarded by `(lifetime epoch == current epoch || lifetime still live)`. Prevents an orphaned-slot edge case where a slot allocated in one block could be silently reused in another with a stale offset. `test_aarch64_frame_spill_reuse` regression test added.
- Register allocator `isLiveOut()` wired through the FPR spill path.
- x86_64 `Backend.cpp` debug `fprintf` (left in during a previous session) removed; native compilation no longer writes one stderr line per function.

**IL optimizer:**
- `SCCP` now declares preserved analyses (dominators, loop info, CFG). Previously SCCP conservatively invalidated everything, forcing downstream passes to rebuild. `test_il_pass_manager` validates SCCP preservation.

**Runtime surface policy:**
- New `RuntimeSurfacePolicy.inc` (366 lines) — machine-checked declaration of which runtime components are required for each capability bundle (graphics-enabled, audio-enabled, headless, etc.). Used by `test_runtime_surface_audit` to catch disabled-build link gaps before they ship.
- `rtgen` header parser test (`test_rtgen_header_parse`) validates that runtime metadata generation stays in sync with C header declarations.

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

- **glTF 2.0** — `.gltf` (JSON + external buffers) and `.glb` (single binary container). Mesh extraction with positions, normals, UVs, tangents. PBR metallic-roughness material extraction (baseColorFactor, metallicFactor, roughnessFactor, texture indices). Skeletal animation with joint hierarchy reconstruction, inverse bind matrices, and skin/animation channel extraction. Morph target support
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
- **Collider3D** — New runtime class (`rt_collider3d.c/h`, 822 LOC) with 7 reusable collision shape types: box (`NewBox`), sphere (`NewSphere`), capsule (`NewCapsule`), convex hull (`NewConvexHull` from Mesh3D), triangle mesh (`NewMesh`), heightfield (`NewHeightfield` from Terrain3D), and compound (`NewCompound` + `AddChild` with local transforms). Shapes decouple collision geometry from body instances — `Body3D.SetCollider` attaches a shape, AABB and narrow-phase dispatch use the attached collider. `GetLocalBoundsMin`/`GetLocalBoundsMax` query shape extents. World AABB computation transforms collider bounds through the body's pose (position + orientation).
- **Physics3D rotation dynamics** — Bodies gain quaternion-based `Orientation` (get/set), `AngularVelocity` (get/set), `ApplyTorque`, `ApplyAngularImpulse`, `LinearDamping`/`AngularDamping` properties. Simulation integrates angular velocity, applies damping per-step, and updates orientation via quaternion integration.
- **Physics3D body modes** — Bodies are now classified as dynamic (`PH3D_MODE_DYNAMIC`), static (`PH3D_MODE_STATIC`), or kinematic (`PH3D_MODE_KINEMATIC`). Kinematic bodies participate in collision but are not affected by forces. `Body3D.New(mass)` constructor creates a collider-ready body without legacy shape parameters.
- **Physics3D sleep system** — Bodies track linear and angular velocity magnitude against configurable thresholds (`PH3D_SLEEP_LINEAR_THRESHOLD`, `PH3D_SLEEP_ANGULAR_THRESHOLD`). After `PH3D_SLEEP_DELAY` seconds of sub-threshold motion, bodies enter sleep state and are skipped during simulation. `CanSleep`, `Sleeping`, `Wake()`, `Sleep()` API.
- **Physics3D CCD** — `UseCCD` property enables continuous collision detection via substep sweeps (`PH3D_MAX_CCD_SUBSTEPS = 16`). Fast-moving bodies are advanced in sub-increments to detect tunneling through thin geometry.
- **DistanceJoint3D / SpringJoint3D** — Physics joint constraints with 6-iteration sequential impulse solver
- **Model3D** — New unified 3D asset container (`rt_model3d.c`, 513 LOC). `Model3D.Load(path)` routes by file extension (.vscn, .fbx, .gltf, .glb) and builds an internal resource collection (meshes, materials, skeletons, animations). `Instantiate()` clones the node tree with shared resources — imported geometry is loaded once and reused across instances. `InstantiateScene()` creates a fresh `Scene3D` and attaches cloned top-level nodes below the scene root. `MeshCount`/`MaterialCount`/`AnimationCount` accessors for content inspection.
- **AnimController3D** — High-level animation state controller (`rt_animcontroller3d.c`, 871 LOC). Named states (`AddState`), crossfade transitions (`SetTransitionDuration`), `Play(name)`/`Pause`/`Stop`, speed control, loop modes (once/loop/pingpong), and event frame callbacks (`SetEventFrame`/`EventFired`). `SceneNode3D.BindAnimator` attaches a controller to a node; `Scene3D.SyncBindings(dt)` advances all bound animators and applies skeletal poses.
- **SceneNode3D bindings** — `BindBody(body)`/`ClearBodyBinding()` attaches a `Physics3DBody` to a scene node. `BindAnimator(ctrl)`/`ClearAnimatorBinding()` attaches an `AnimController3D`. `SyncMode` property (0=none, 1=body→node, 2=node→body) controls transform propagation direction. `Scene3D.SyncBindings(dt)` batch-updates all bound nodes from physics bodies and advances bound animators in a single pass.
- **PBR materials** — `Material3D.NewPBR(metallic, roughness, ao)` constructor. Metallic-roughness workflow with `SetAlbedoMap`, `SetMetallicRoughnessMap`, `SetAOMap`, `SetNormalScale`, `SetEmissiveIntensity`. `AlphaMode` (0=opaque, 1=mask, 2=blend), `DoubleSided`, and per-instance `Clone`/`MakeInstance`. Cook-Torrance BRDF with GGX normal distribution and Schlick fresnel implemented in all 4 GPU backends (Metal MSL, D3D11 HLSL, OpenGL GLSL, software rasterizer).
- **NavAgent3D** — Autonomous pathfinding agent on NavMesh3D surfaces (`rt_navagent3d.c`, 548 LOC). A* path query, string-pulling corridor smoothing, steering with configurable speed/acceleration/stopping distance. `SetDestination(x,y,z)` triggers path computation; `Update(dt)` advances along the smoothed corridor. `BindNode(node)` for automatic `SceneNode3D` position sync. Agent states: idle, moving, arrived, stuck.
- **AudioListener3D / AudioSource3D** — 3D positional audio objects (`rt_audio3d_objects.c`, 653 LOC). `AudioListener3D`: position, forward, velocity, with `BindNode`/`BindCamera` for automatic spatial tracking. `AudioSource3D`: positional emitter with inner/outer cone attenuation, min/max distance, inverse-distance rolloff, looping, pitch, gain. `Audio3D.SyncBindings(dt)` batch-updates all bound positions from scene nodes. Distance attenuation and stereo panning integrated into the audio mixer.
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
- **Diagnostic logging** — `NSLog` trace messages added for nil returns from `MTLCreateSystemDefaultDevice`, `vgfx_get_native_view`, `newCommandQueue`, and missing shader entrypoints, replacing silent NULL returns that were difficult to debug on unsupported hardware.

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
- **Typed return metadata** — `runtime.def` signatures annotated with concrete return class names (`obj<ClassName>`, `seq<str>`) for 100+ factory, conversion, and collection methods. New `concreteRuntimeReturnClassQName()` API in `RuntimeClasses` enables both frontends to infer the exact runtime class returned by chained method calls. BASIC frontend gains `inferObjectClassQName()` for recursive expression type tracing through method chains and factory patterns. `lowerAssignment` extracted from `lowerBinary` in Zia lowerer for cleaner assignment handling.

---

### VAPS Packaging System

Comprehensive overhaul with InstallerStub rewrite and expanded platform support:
- **InstallerStub rewrite** — Major rework of the Windows self-extracting installer stub with improved PE generation, IAT wiring, and ZipReader extraction
- **WindowsPackageBuilder** — Rewritten with improved .exe output, shortcut creation, and registry integration
- **ZipWriter enhancements** — Extended API for multi-file archive creation
- **PkgVerify expansion** — Expanded structural checks for ZIP, .deb, and PE output validation
- Shared `PkgUtils.hpp` (readFile, name normalizers, safeDirectoryIterate, new utility functions)
- Warn on missing icons/assets/invalid versions (eliminate silent failures)
- Symlink safety with root-escape detection in all directory traversals
- `package-category` and `package-depends` manifest directives
- ARM64 Windows PE support (machine type 0xAA64)
- `--dry-run` and `--verbose` CLI modes for package content preview
- `.lnk` LinkInfo with VolumeID + LocalBasePath for reliable shortcut resolution
- Fix DEFLATE double-free crash and `.lnk` missing LinkInfo

57+ new packaging tests.

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
- **OGG multi-stream demux** — VideoPlayer detects Theora vs Vorbis streams by packet header signature via `ogg_reader_next_packet_ex()` serial filtering, routing packets to the correct decoder.
- **Audio track integration** — OGV containers with Vorbis audio tracks are handed off to the audio runtime (`vaud_load_music_ogg`) when `VIPER_ENABLE_AUDIO` is active. VideoPlayer manages A/V synchronization.
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
| OGG | Theora | Vorbis | `.ogv` | Infrastructure (headers + YCbCr + audio handoff) |

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

#### Code Editor Enhancements
- **`vg_codeeditor.c` major expansion** — Substantial rework of the GUI code editor widget with improved text handling, selection, scrolling, and rendering

#### Widget Contract Fixes
- Dropdown placeholder strings copied instead of borrowing freed temporaries
- Dismissed notifications compacted immediately instead of accumulating stale entries
- Command palette placeholder and UTF-8 query path completed
- MessageBox prompt/builder flows honor default/cancel button semantics
- Font inheritance applied consistently at construction for all text-bearing widgets

---

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
- Graphics3D texture ownership: CubeMap3D, Material3D, Decal3D, Sprite3D, InstanceBatch3D, and Water3D did not retain their texture/mesh/material references — GC could collect them while still in use. All now use retain/release with finalizer cleanup
- ViperSQL demo: 9 bug fixes (modulo operator, REPLACE parsing, JOIN IS NULL, window PARTITION BY, LIKE case sensitivity, ALTER TABLE locking, CSV validation, persistence NULL crash, distinct hash function)
- AArch64 regalloc: current-instruction source operands could be evicted while allocating the same instruction's def register, corrupting GEP base pointers under register pressure (broke 3D bowling BowlingGame.init)
- AArch64 regalloc: `operandRoles` for immediate-ALU opcodes (AddRI, SubRI, etc.) classified operand 0 as USE+DEF instead of DEF-only, inflating use-position counts and biasing spill decisions
- AArch64 regalloc: FPR loads/stores (`LdrFprFpImm`, `LdrFprBaseImm`, `StrFprFpImm`, etc.) not recognized by `isMemLd`/`isMemSt`, causing incorrect register liveness tracking for floating-point operands
- AArch64 regalloc: clean FPR values loaded from rodata/memory dropped across calls without spilling — caller-saved FP registers were not preserved because the dirty flag was never set for load destinations
- AArch64 regalloc: live-out vregs with no remaining uses in the current block were prematurely released without spilling — successor blocks saw uninitialized registers. `isLiveOut()` check now prevents release when the vreg is live across block boundaries
- AArch64 regalloc: end-of-block spill insertion scanned backward from the last instruction, potentially placing spills after the first terminator. Corrected to scan forward to find the first terminator
- AArch64 CBR terminator: branch arguments on different-target conditional branches were silently dropped — edge blocks only emitted for same-target CBRs, causing miscompilation in branch ladders and multi-level if/else with phi values
- AArch64 loop phi-spill: multi-block loops where the latch (not the split body) carries phi-slot stores were not handled — phi values remained as stack round-trips instead of being converted to register moves
- Zia `main()` entry point: `main()` programs did not receive interface itable initialization or global variable initializer emission — only `start()` was treated as the entry point
- Zia optional struct return: `emitOptionalWrap` returned a raw stack pointer for struct payloads, which dangled after the function returned — now boxes to heap via `emitBoxValue`
- EarlyCSE: dominator-ordered replacement could substitute a temp defined in a textually-later block, creating an illegal use-before-def in the IL
- GVN: same textual-ordering bug as EarlyCSE — redundant load elimination and expression value-numbering could replace with values from textually-later dominating blocks
- Mach-O linker: ObjC metadata sections emitted with generic flags instead of proper Mach-O section types (`S_CSTRING_LITERALS`, `S_LITERAL_POINTERS`, `S_ATTR_NO_DEAD_STRIP`), causing `ld` warnings and potential dead-strip of ObjC metadata
- Mach-O linker: section alignment always emitted as 0 instead of log2 of the actual alignment, causing linker alignment violations for sections requiring >1-byte alignment
- Mach-O linker: symbol resolution failed when object files used different underscore conventions — `findWithMachoFallback` only searched plain and prefixed names, not stripped names
- Mach-O linker: ObjC class symbols (`OBJC_CLASS_$_CAMetalLayer`, etc.) with varying leading underscore counts failed framework rule matching — `normalizeMacFrameworkSymbol` now strips ObjC prefixes for correct matching
- Metal 3D backend: nil returns from `MTLCreateSystemDefaultDevice`, `vgfx_get_native_view`, and `newCommandQueue` silently returned NULL without diagnostics — added `NSLog` trace messages
- Zia `final` keyword: reassignment of `final` variables, for-in loop variables, and match pattern bindings silently succeeded — now produces a compile-time error
- Documentation: error messages in Bible chapters showed stale format (`error:` prefix) instead of actual compiler output (`error[V3000]:` codes); networking examples used non-existent structured response API instead of actual `Http.Get`/`HttpReq`/`HttpRes`; Bible appendix still used old `entity`/`value` terminology instead of `class`/`struct`
- `Pixels.Blur` and `Pixels.Resize` unpacked channels as ARGB (`0xAARRGGBB`) instead of the actual RGBA (`0xRRGGBBAA`) pixel format — R and A channels were swapped in both the horizontal and vertical blur passes and in bilinear resize interpolation, producing visibly incorrect colors on any image with non-opaque alpha
- `Mouse.SetPos` only updated the internal tracking coordinates without warping the OS cursor — platform warp bridge now calls through to the native cursor API on all three platforms
- `Keyboard.CapsLock` relied on key-event toggle counting which could desync from the actual LED state — now queries the OS keyboard state directly on each call
