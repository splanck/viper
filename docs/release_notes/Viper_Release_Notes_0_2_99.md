# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.99 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.7 was cut on 2026-06-30. -->

### What this release is about

A cleanup-and-packaging follow-up to the v0.2.7 hardening cycle. Three efforts dominate: the runtime's public API settles into its final shape, the 3D stack takes a large step from a renderer into a modern real-time engine, and packaging grows into a real installer-and-release story.

Most visible to your code, the public surface finishes settling. Every symbol has one canonical name, terse abbreviations are spelled out, and every recoverable failure now comes back as a value you can inspect — a read past end-of-input, a decrypt that doesn't authenticate, a lookup that finds nothing each return an `Option` or `Result` instead of a null, a `-1`, or a silent side channel. The older shapes stay as compatibility aliases, `viper --dump-runtime-api` emits a full machine-readable contract, and the 17,000-line registry behind it splits into documented, domain-oriented fragments (ADRs 0027, 0101–0102).

The 3D renderer gains image-based lighting, a clustered forward+ path, screen-space reflections, soft particles, and temporal anti-aliasing across all four backends, then a reversed-Z depth model, triangle-accurate occlusion culling, a production glTF/Draco/PBR asset pipeline, and — on top — first-person and third-person game runtimes (the latter alone twenty-seven subsystems, each with a VM==native probe) (ADRs 0064–0102). Underneath, codegen compiles dense switches to jump tables and demotes provably-safe overflow checks, the native linker resolves cleanly on Linux and Windows/MSVC while rejecting cross-platform imports, and a signed release pipeline structurally verifies, checksums, and manifests every artifact before it ships (ADRs 0025–0026, 0073).

- **The public API settles on one canonical name.** Aliases and duplicates are gone and the generator refuses to mint new ones — `Count` for sizes, `Length` for semantic lengths, boolean probes returning true/false, `Entity3D` positions as plain properties — with both languages, ViperIDE, the demos, and the docs rebound.
- **Recoverable failures come back as values.** Terminal reads, collection and channel pops, decryption, HTTP/REST/SMTP sends, data-format parsing, searches, shell commands, and 3D queries all gain `Option`/`Result` forms, so absence and error are values instead of nulls or sentinels; the older forms remain for compatibility.
- **A machine-readable API contract (ADR 0027).** `viper --dump-runtime-api` keeps its shape and adds parsed types, ownership, stability, capability, and security metadata for every entry, with an internal-only marker keeping helpers out of the public dump.
- **The runtime registry modularizes and publishes its docs (ADR 0101).** The 17,000-line `runtime.def` splits into an ordered manifest and domain fragments requiring authored `@summary`/`@details`, and the dump rises to schema 3 with Markdown docs surfaced through completion, hover, LSP, and MCP.
- **Modern 3D lighting arrives (new).** Image-based lighting, a clustered forward+ path binning lights into camera-space froxels, screen-space reflections, soft particles, and temporal anti-aliasing land across Metal, OpenGL, D3D11, and software, fed by a from-scratch Zstandard decoder for compressed KTX2 textures.
- **Reversed-Z depth and triangle-accurate occluders (new).** GPU scene passes render reversed-Z float depth on every capable backend so precision concentrates in the distance, and the Hi-Z occluder grid rasterizes real triangles instead of leaning on an AABB rectangle.
- **A first-person engine pass (new).** View-model rendering, point-light shadows, height fog, an auto-exposure/color-LUT/sun-shaft post-FX stack, raw/relative mouse input, and Doppler-fed spatial audio turn the renderer into an FPS-capable stack.
- **A third-person action-game runtime suite (new; ADRs 0074–0100).** Twenty-seven subsystems — spring-arm camera with target-lock, Character3D dynamics, combat and ragdoll, cloth, cinematics, AI behavior trees, minimaps, world persistence, quests, and a baked-GI/reflection-probe/procedural-sky lighting round — extend the stack into a character-action toolkit.
- **The production 3D asset pipeline (new).** Full sequential and edgebreaker Draco decode, an extended PBR set, BC6H HDR textures, automatic LOD chains, and 1,024-bone skeletal rigs close the zero-dependency glTF/KTX2 path, conditioned offline through a new `viper asset` tool.
- **Canvas3D observability and non-trapping probes (new; ADRs 0069–0071).** Read-only accessors surface backend fallbacks, dropped draws, and mesh-memory budgets, while `TrySetClusteredLighting` and runtime-accurate `BackendSupports` let code query a capability instead of trapping on a backend that lacks it.
- **The D3D11 backend gets a robustness pass.** Every shader-facing constant buffer is clamped to finite in-range values before upload, compressed-texture support expands to first-class BC1/BC3/BC4/BC5, and the oversized HLSL sources move to chunked lazy storage so MSVC builds cleanly.
- **Faster switches and overflow checks.** Dense `switch` statements compile to a bounds check plus a jump table on both backends, and 32-/16-bit overflow-checked arithmetic lowers to native flag-setting instructions instead of widening to 64 bits.
- **Checked arithmetic is demoted when provably safe (ADR 0026).** A whole-function range analysis proves when a checked add/sub/mul/div can never trap and rewrites it to the plain form — sharing one range implementation with the verifier — and recovers power-of-two modulo bounds after peephole lowering.
- **The IL spec is now 0.3.0 (ADR 0064).** The `il` banner catches up to `switch.i32` jump tables, branchless `select`, and checked/narrow arithmetic, and snapshot builds are date-stamped (`0.2.99.20260704`).
- **Crypto validates up front, and a ChaCha20-Poly1305 interop fix.** The AEAD path clamps its Poly1305 key with the RFC 8439 limb masks — it previously produced non-interoperable tags — and every `Viper.Crypto` entry point validates handles before use and halts after a validation trap.
- **Zia gains target-typed list combinators (new).** `map`, `filter`, `reduce`, `firstWhere`, `any`, `all`, and `sum` target their lambdas from the receiver's element type and lower to inline loops, alongside a shared precedence-climbing parser and a language-correctness pass.
- **ViperIDE becomes a multi-root workbench (new; ADRs 0066–0068).** Async git source control, a real VT terminal, a grouped-Variables debugger, an in-editor command overlay, named vector toolbar icons, split panes with crash recovery, and OS foreground activation land atop a shell split into focused modules.
- **Packaging becomes a real release story (ADRs 0025, 0073).** The toolchain installer ships ViperIDE, standalone apps package as AppImage/RPM/DMG/Windows installers, and a signed release pipeline structurally verifies, checksums, and manifests every artifact — while the repository consolidates under `src/`.
- **Native builds are stable on Linux and Windows.** Static archives stop pulling unresolved libgcc/libstdc++ helpers, the CRT-less PE path is completed on x86-64 and AArch64, the linker rejects imports exclusive to another platform, and the generated codegen tables are checked in for clean source builds.

### By the Numbers

| Metric | v0.2.7 | v0.2.99 | Delta |
|---|---|---|---|
| Commits | — | 75 | +75 |
| Source files | 3,402 | 3,505 | +103 |
| Production SLOC | 762K | 833K | +71K |
| Test SLOC | 304K | 321K | +17K |
| ViperIDE SLOC | 28K | 37K | +9K |
| Demo SLOC | 197K | 236K | +39K |

Counts via `scripts/count_sloc.sh` (production 832,853 / test 321,026 / demo 235,789 / viperide 36,969 / source files 3,505); commits since the `v0.2.7-dev` tag (2026-06-30). The range touched 2,914 files (+479,431 / −53,918); most of the raw insertions are the checked-in text-glTF character and model assets, which the SLOC figures above exclude.

---

### Runtime public API and contract

- Every public symbol settles on one canonical name — collection sizes read as `Count`, semantic lengths as `Length`, boolean probes return true/false, and `Entity3D` positions are plain properties (ADR 0026, position accessors) — with aliases and duplicates removed and the generator refusing to mint new ones. Terse abbreviations spell themselves out (`LeadZ`→`CountLeadingZeros`, `NumSci`→`Scientific`), and factories drop redundant `New` prefixes.
- Recoverable failures return values, not sentinels: terminal reads, diagnostics, collection and channel pops, decryption, HTTP/REST/SMTP sends, data-format parsing, searches, shell commands, and game/3D queries gain `Option`- and `Result`-returning forms, with the older forms kept for compatibility.
- The sharp edges are labelled — manual memory, trap-state mutation, legacy ciphers, and the TLS-verification bypass move into plainly named `Runtime.Unsafe`, `Crypto.Legacy`, and testing-only homes so nothing dangerous hides behind an innocent name.
- `viper --dump-runtime-api` (ADR 0027) keeps its shape and adds parsed types, ownership, stability, capability, and security metadata per entry, with an internal-only marker excluding implementation helpers.
- The 17,000-line `runtime.def` modularizes (ADR 0101) into an ordered root manifest and domain fragments — declaration order and the X-macro entry point preserved — while `rtgen` resolves quoted includes (rejecting missing, duplicate, cyclic, or class-scoped ones) and requires authored `@summary`/`@details` on every row. The dump rises to schema 3 with Markdown documentation and a checked-in domain reference, surfaced through Zia/BASIC completion and hover, LSP items, and MCP results.

### 3D rendering

- Image-based lighting adds spherical-harmonic ambient and prefiltered specular cubemaps; a clustered forward+ path bins point and spot lights into camera-space froxels (`Canvas3D.ClusteredLighting`, a 64-light GPU budget, a flat 16-light software path, and a `VIPER_3D_CLUSTERS=0` escape hatch); and screen-space reflections (`Material3D.SsrEnabled`), soft particles, and temporal anti-aliasing layer on top — all across the Metal, OpenGL, D3D11, and software backends over a from-scratch Zstandard/KTX2 decoder.
- GPU scene passes render reversed-Z float depth on every backend that advertises it (Metal, D3D11, OpenGL): the projection's z row is negated at the canvas seam, depth clears to `0` and compares with the `Greater` family, and the skybox sits at the far plane, so open-world clip ranges stop shimmering. The software backend stays standard as the deterministic golden reference, and `Camera3D`/`Mat4.Perspective` keep the standard convention.
- The Hi-Z occluder grid rasterizes each eligible opaque draw's real triangles into a 256×256 per-texel depth buffer (perspective-correct, capped at 1,024 triangles per draw and 8,192 per frame), so near walls and rotated geometry cull what hides behind them instead of leaning on a conservative AABB rectangle.
- Directional shadows default to two cascades on capable backends with bounded coverage, logarithmic splits, cross-cascade blending, distance fades, and hardware comparison sampling; new `Canvas3D` surfaces expose shadow distance, vsync present pacing, and a software render scale, `World3D.RenderInterpolation` blends pre-step poses for smooth fixed-step rendering, and async scene-depth probes drive stall-free lens-flare occlusion.
- A flicker-and-correctness pass fixes two structural races — a Metal frames-in-flight throttle and per-draw transient-buffer ring end stale-transform instancing and mid-read geometry rewrites — corrects `Quat.FromEuler`/`SetRotationEuler` to the documented pitch→X/yaw→Y/roll→Z axes, and stops instanced forests, transparent draws, and end-of-life particles from popping as a block. Hot paths get cheaper via TRS-space skeleton blending, once-per-draw radix keys, batched `SceneNode.SetTransform`, and allocation-free component getters.
- The D3D11 backend absorbs a robustness pass: every shader-facing constant buffer (camera, skybox, scene, post-FX, TAA, SSR, IBL, clustered-light) is clamped to finite in-range values before upload, compressed-texture support expands to first-class BC1/BC3/BC4/BC5 alongside BC7/ASTC/ETC2 with per-format block validation, IBL cubemaps and bloom mip chains are validated, and the oversized HLSL sources move to chunked lazy storage so MSVC builds cleanly.
- Canvas3D observability (ADRs 0069–0071) exposes `IsAvailable`, a `BackendFallbackReason` string, per-frame event- and instanced-draw drop counts, and mesh snapshot/retained-byte budgets (`Mesh3D.RetainedBytes`), while `TrySetClusteredLighting` and runtime-accurate `BackendSupports("hdr-scene"/"taa")` let code query a capability rather than trap.

### 3D assets, animation, and the production pipeline

- The from-scratch importer decodes `EXT_meshopt_compression` streams and their octahedral/quaternion/exponential filters, `KHR_mesh_quantization` attributes and morph deltas, full sequential and edgebreaker `KHR_draco_mesh_compression` geometry, and `KHR_texture_basisu` textures over in-tree BasisLZ/ETC1S and UASTC KTX2 decoders — rejecting malformed streams with named recoverable diagnostics.
- A shared QEM `GenerateLODs` decimates each unique mesh once into bounded LOD chains that survive instantiation, `SceneAsset` applies and reverses `KHR_materials_variants`, and `AssetDiagnostics3D.GetImportReport` returns structured JSON for skipped primitives, truncated skin influences, and ignored extensions.
- The zero-dependency path reaches production: signed/unsigned BC6H HDR blocks and the extended PBR set (clearcoat, IOR, sheen, anisotropy, transmission, volume across all four backends), plus animation import scaled to production rigs — skeletons up to 1,024 bones partitioned into 256-slot draw palettes, optional influence-5–8 side streams, CUBICSPLINE tangents, and tolerance-based clip compression.
- New runtime surfaces — `SceneAsset.LoadWithOptionsEx`, `Mesh3D.CompactStreams`, `Canvas3D.DrawInstancedSkinned`, texture-residency streaming with telemetry, 48-byte compact vertex streams, and Radiance HDR panorama cubemaps — sit behind new offline `viper asset bake` and `viper asset validate` conditioning.
- `Vegetation3D.SetSeed` pins scatter to a reproducible layout (ADR 0072), `Canvas3D`/`RenderTarget3D` grow to 16,384 px with HDR emissive preserved through upload, and `NavMesh3D` bake and pathfinding scale up with struct-derived voxel budgets, cached off-mesh adjacency, and full funnel corridors.

### First- and third-person game runtimes

- A first-person pass adds view-model rendering, point-light shadows with slot budgeting and telemetry, height fog, and an auto-exposure/color-LUT/sun-shaft post-FX stack, plus nine-slice HUD draws and anti-aliased 2D text; input gains raw/relative mouse and gamepad-merged look; spatial audio gains pitch, low-pass occlusion, sidechain ducking, and Doppler; and authoring adds deterministic mesh simplification/LOD, Quickhull convex hulls, CCD, and tiled navmesh bake/rebuild.
- A third-person action-game suite (ADRs 0074–0100) extends the stack across twenty-seven subsystems: a spring-arm controller with target-lock, Character3D dynamics over moving platforms, combat volumes with health and ragdoll blend-out, slow-motion time control, rail-camera/timeline cinematics with dialogue and facial/voice metering; Verlet cloth, physics-material footstep surface events, AI perception with behavior trees, interaction prompts, minimap markers, world persistence, and quests; plus a world-and-lighting round — async streaming with HLOD proxies, terrain holes and splat auto-blend, baked-GI lightmaps, reflection probes, and procedural sky with time-of-day. Each subsystem is wired through the IL registries with a matching ADR and a VM==native probe.
- Game3D integration knobs land alongside: a standalone terrain can join physics collision, water planes recenter over off-origin terrain, GPU post-FX composites the HUD into captured frames, and `Game.UI` widgets draw against either a 2D `Canvas` or a `Canvas3D` (ADR 0065).

### Codegen, IL, and the native linker

- Dense `switch` statements compile to a single bounds check plus a jump table on both backends, and 32-/16-bit overflow-checked arithmetic lowers to native flag-setting instructions instead of widening to 64 bits; branchless `select` and if-conversion land alongside.
- A whole-function range analysis (ADR 0026) proves when an overflow-checked add, subtract, multiply, or divide can never trap and rewrites it to the plain form, sharing one range implementation with the verifier and recovering power-of-two modulo bounds after peephole lowering so `expr % 2^k` keeps verifying at `-O2`.
- The IL spec advances to 0.3.0 (ADR 0064) so the banner covers the `switch.i32` jump tables, branchless `select`, and checked/narrow arithmetic these rounds added; snapshot builds are date-stamped (`0.2.99.20260704`).
- The native linker resolves cleanly on both toolchains: static archives stop pulling libgcc/libstdc++ helpers it can't resolve, the MSVC C++ runtime archives resolve from `VCToolsInstallDir`, and the CRT-less PE path initializes from `main` and exits through `ExitProcess` (mirrored on AArch64). It now rejects dynamic imports exclusive to another platform, completes Windows AArch64 PE output, signs Mach-O ad-hoc with a dependency-free SHA-256, preserves ELF `p_filesz` zero-fill, and broadens its known libc/Windows import set for the new GUI and input paths.
- The generated AArch64 and x86-64 codegen tables are checked into the tree, so a clean source build consumes them directly instead of regenerating, and the OpenGL backend build is restored.

### Crypto and runtime hardening

- The ChaCha20-Poly1305 AEAD path clamps its Poly1305 key with the RFC 8439 26-bit limb masks — it previously produced non-interoperable tags for some one-time keys — and every `Viper.Crypto` entry point validates its handles before use and halts after a validation trap instead of continuing with sentinel buffers.
- The Canvas3D and Game3D robustness round makes the physics broadphase deterministic across VM and native, prepares image-based lighting lazily on the first PBR draw, sanitizes light and shadow-bias inputs, and scales the software rasterizer to sixteen workers.
- A Graphics3D/Game3D runtime-boundary audit (ADR 0102) fixes the canonical registry as the supported surface and `rt_*` as an internal embedding ABI, raising `--dump-runtime-api` to schema 4 with each 3D binding's backing C symbol, ownership, nullability, and fallibility plus a deterministic manifest fingerprint. The pass repairs managed-string lifetimes across the glTF/FBX/Canvas3D/Game3D/VSCN paths, raises FBX import to the 1,024-bone limit, replaces recursive scene serialization with bounded iteration, and makes BC6H math and lazy initialization thread-safe.

### ViperIDE and GUI

- An asynchronous `Process`-backed git layer keeps porcelain-v2 status, staged actions, and push/pull off the UI thread; BASIC editing gains go-to-definition, references, rename, call hierarchy, and workspace symbols over the runtime `FileIndex`; and open documents track external change, delete, and rename with state-specific save-and-reload recovery.
- The integrated terminal becomes a real terminal (ADR 0066) — CSI addressing, alternate-screen buffers, 24-bit and 256-color SGR, xterm key sequences, and a bounded replay buffer — while new non-trapping process and PTY read-result APIs surface runaway child output with a truncation marker instead of trapping the workbench. The debugger adds command-palette watch management and a grouped Variables view with a VM-backed expander for List, Seq, and Map values.
- A reusable in-editor command overlay (ADR 0067) replaces external prompts for Go To Line, Add Watch, Rename Symbol, Extract Local/Function, and workspace-symbol lookup, and toolbar and activity-bar buttons trade glyph labels for semantic named icons drawn through the vector renderer.
- ViperIDE becomes a multi-document, multi-root workbench: per-document buffers preserve undo/cursor/scroll across tab switches, split panes, crash-recovery swap files, a welcome/recents surface, and inline SCM diff gutters fill out the editing surface, and workspaces advertise multiple roots. The monolithic `AppShell` splits into focused modules, and a performance pass adds damage-region partial repaint plus an incremental `Viper.Zia.Document` mirror that syncs edit deltas.
- `Viper.GUI.App.Activate` (ADR 0068) requests native foreground and menu ownership across macOS, Windows, Linux, and the mock backend; ViperIDE uses it at startup and adds an adaptive `PollWait` cadence that stays live while builds, debugging, terminals, overlays, or indexing are active.

### Frontends and language tooling

- Zia gains target-typed list combinators — `map`, `filter`, `reduce`, `firstWhere`, `any`, `all`, and `sum` target their lambda arguments from the receiver's element type and lower to inline loops over the list runtime.
- A shared precedence-climbing parser and a language-correctness pass land alongside: String `for`-in, range match patterns, `final`/`let` fields, chained-comparison rejection, and MCP `structuredContent` for diagnostics, completions, symbols, and runtime search.

### Packaging, release pipeline, and repo layout

- The toolchain installer now builds and ships ViperIDE (ADR 0025), and standalone applications package as first-class AppImage, RPM, DMG, and Windows installers with desktop launchers, branded icons, and post-build verification, plus a `viper package --dry-run --json` for scripting.
- Package builders route through a shared `PkgVerify` (ADR 0073) that structurally validates PE, Mach-O, and ELF artifacts; `viper install-package` rejects a target or architecture that conflicts with the staged binary's object header; the Linux self-extracting format is renamed to `linux-bundle` (`.run`); and every artifact emits a SHA-256 checksum and a machine-readable manifest gated on structural and lifecycle verification.
- The repository consolidates under `src/` — the top-level `tests/` fixture tree and `viperide/` tree move to `src/tests/fixtures/` and `src/viperide/`, with CMake registration, test helpers, and e2e scripts repointed.

### Windows and Linux builds, and tests

- Windows and Linux native builds return to green, with the D3D11 backend building cleanly under MSVC after the chunked-HLSL and constant-buffer alignment work, and a build-acceleration pass shortens demo codegen and linking on both platforms.
- The third-person suite's twenty-seven subsystems each ship a VM==native probe; the codegen round adds jump-table, narrow-arithmetic, and range-demotion coverage; and the runtime API, registry-modularization, and boundary-audit work are locked by contract-fingerprint and leaf-name-uniqueness guards.

---

Demos and docs tracked the work. ASHFALL — a new nine-level first-person campaign written entirely in Zia over the engine surface above, with hybrid CC0-and-procedural art — joined the set as the flagship for the first-person pass, Ridgebound became a small survival game atop its character-controller and boulder physics, 3D Bowling grew into Neon Lanes Championship 2.0 (two-to-four-player local matches, a physics-driven championship tour, and a broadcast-presentation pass), Zia chess became the canonical release-style demo and retired its BASIC predecessor, and Xenoscape shipped its 1.0 ten-region Metroidvania; a fresh `game3d` showcase drives the clustered-lighting and reflection paths. The 3D, packaging, CLI, and library docs picked up the new lighting surface, installer payloads, canonical names, the machine-readable API contract, the reversed-Z depth model and triangle-accurate occluders, the compressed-glTF and production asset pipeline, the third-person action-game runtime suite, the signed release pipeline and consolidated `src/` layout, and the modularized registry's domain reference (ADRs 0025–0102).

<!-- END DRAFT -->
