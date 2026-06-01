# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.5 was cut on 2026-05-07. -->

### What this release is about

A hardening cycle pushing toward alpha quality that also delivered substantial new capability. The headline new work is a code-first `Viper.Game3D` engine layered over the 3D runtime and a real ViperIDE editor with build/run and language services; the hardening backbone swept memory, threads, crypto, IO, graphics, the bytecode VM, the native toolchain, and packaging, while the Zia frontend closed its major language gaps and raw pointers left both source languages.

- **Zia frontend stability.** `defer`; structured `try`/`catch`/`finally`, multi-catch, bare rethrow; `Result[T]` with `?` propagation; weak fields, function references, constrained generics, default interface methods; declaration-order independence.
- **Pointer-safety gate.** Zia and BASIC reject raw `Ptr` types and pointer-signature runtime APIs; the typed surface is now the only surface. (Biggest user-visible change.)
- **Memory, GC & threads ownership.** Validated retain/release wrappers, weak-ref CAS retain inside the GC lock, trap-safe finalizers, class-ID validation on every public threads / MessageBus entry, and saturated wait deadlines.
- **Crypto, TLS & IO security.** Canonical `Viper.Crypto.*` (scrypt, AES-GCM+AAD, approved-mode module, fixed-schedule ECDSA P-256); TLS Key-Usage / Basic-Constraints / EKU enforcement; hardened temp-file, archive, and ZIP64 paths.
- **Network protocol correctness.** Independent HPACK tables, strict RFC 7230 `Transfer-Encoding` parsing (closes a request-smuggling avenue), and WebSocket frame/close-code validation.
- **Native toolchain becomes real.** Bounds, alignment, and reloc-correctness rounds across all four object readers and three writers; `fe_zia` is now native-linked into `zia`, carrying ViperIDE's IntelliSense against the real semantic engine.
- **Toolchain installer completion.** Native-emitted Windows `.msi`/`.exe`, macOS `.pkg`, and Linux `.deb`/`.rpm`/tarball packages reach feature parity with signing, file associations, dependency advertisement, and deep post-build verification.
- **Standard-library namespace de-clutter (breaking).** Seven root modules re-home under their documented taxonomy with no back-compat aliases; `Math`, `String`, `Terminal`, and intrinsic `Option`/`Result`/`Error` stay at root.
- **Backends, bytecode VM & Windows HiDPI.** x86-64 fold-liveness and AT&T operand validation; AArch64 sub-word transfers and CFG fixes; two's-complement bytecode arithmetic; Win32 physical-pixel sizing and waitable-timer frame pacing.
- **GUI correctness audit.** Five rounds closing handle-validation, dialog-lifetime, focus-routing, and menubar/toolbar/statusbar gaps; every public `Viper.GUI.*` entry routes through `rt_gui_widget_handle_checked`.
- **3D runtime & graphics hardening.** A multi-backend Graphics3D correctness round (software/OpenGL/Metal/D3D11 skinning, shadows, readback), bone-topology-frozen skeletal animation, shape-accurate Physics3D queries with a warm-started contact solver that settles resting stacks, and saturating 2D draw / scaled-tilemap math.
- **Code-first Game3D engine (new).** A `Viper.Game3D.*` layer over the 3D runtime: a `World3D` fixed-step update/render loop with internal worker, floating-origin, streaming, and perf/editor telemetry controls backed by `Scene3D.RebaseOrigin`, an `Entity3D` hierarchy with cycle-safe parenting, first-person/free-fly/orbit/follow cameras and a grounded character controller, entity-aware physics/collision events with indexed contact access and solver-iteration tuning, `Animator3D`, spatial `Sound3D`, and `Effects3D` VFX presets.
- **Game-engine surface (plan 24).** New `Viper.Game.UI` widgets, `AnimTimeline` + multi-event `AnimStateMachine`, `Projectile2D`, rotated-texture draws, named audio mixer groups, and a `Viper.System.Clipboard`.
- **ViperIDE editor wiring.** A streaming `Viper.System.Process`, a structured `Viper.Zia.Toolchain`, a semantic `Viper.Zia.ProjectIndex`, and an editable `Viper.Game.Scene` back the in-development editor's build/run, project tree, completion, diagnostics, and symbols; a codegen and dead-strip rewrite cut the ViperIDE native x64 build from ~340s to ~35s.

### By the Numbers

| Metric | v0.2.5 | v0.2.6 | Delta |
|---|---|---|---|
| Commits | — | 274 | +274 |
| Source files | 2,996 | 3,088 | +92 |
| Production SLOC | 552K | 664K | +112K |
| Test SLOC | 228K | 277K | +49K |
| Demo SLOC | 188K | 192K | +4K |

Counts via `scripts/count_sloc.sh` (production 663,819 / test 276,864 / demo 192,140 / source files 3,088); commits since the `v0.2.5-dev` tag.

---

### Memory, GC, and object identity

- Public `Memory.*` routes through validated wrappers that authenticate live handles; array-element releases cover str/obj/box slots; `rt_obj_new_i64` traps on overflowing sizes and `rt_obj_free` rejects non-zero refcounts.
- GC drops the lock during traversal, restores via iterative worklist BFS, and wraps finalizers in `setjmp` recovery; `rt_gc_run_all_finalizers` snapshots `{obj, retained}` so cleanup releases only what it retained.
- `rt_weakref_get` retains its target through a CAS loop inside the GC lock, closing a TOCTOU window; a sticky immortal refcount is distinct from the `SIZE_MAX` corruption marker.
- `rt_obj_get_hash_code` mixes pointers through splitmix64 so Map/Set buckets no longer cluster; `Box` rejects mismatched-tag unboxes and rolls back `ValueType.AddField` under `setjmp` if GC tracking traps.

### Runtime objects and MessageBus

- Boxed values carry a class ID so `Object.Equals`/`GetHashCode` dispatch through `Set[Box]` and `Map[Box, ...]`; `Box.Try*` option accessors added.
- MessageBus validates class IDs on every entry, hashes topics by full byte length (preserving embedded NULs), retains the bus across calls, and orders unsubscribe so unref cannot re-enter a half-freed node.
- `Diagnostics.Trap` routes through a validating `rt_trap_string` that always escapes control bytes, quotes, backslashes, and NULs; `Parse.*Option` accessors give graceful failure on a stable ABI.

### Crypto, TLS, and IO security

- `Viper.Crypto.*` canonicalized: new `KeyDerive.ScryptSHA256` (RFC 7914), `Password.Hash` defaulting to scrypt with PBKDF2 legacy-verify, `Cipher.EncryptAAD`/`DecryptAAD` for AES-GCM, an approved-mode `Crypto.Module` with self-tests, and `Hash.ConstantTimeEquals`.
- RSA modulus floor raised to 1024 bits with secure-zeroed key buffers; ECDSA P-256 uses a fixed-schedule scalar multiply and validates every public point at ingress.
- TLS chain validation enforces Key Usage, Basic Constraints, and EKU, scans every SAN DNS name, and fails closed on malformed tails or more than 16 intermediates.
- Temp files use a 64-bit entropy nonce; assets decode into a private `mkdtemp` directory; `asset_name_is_safe()` rejects absolute/drive/dot/NUL paths; recursive removal uses `openat`/`unlinkat` with no-follow; ZIP64 sentinels and unsupported encryption flags are rejected.

### Threads

- Joins are repeatable; every `Viper.Threads` built-in carries a class ID surfaced through `TypeName`/`ToString`; retain-during-call discipline applies to every public entry.
- Monitor finalize-while-waiting wakes parked waiters with an error instead of hanging; capacity-0 `Channel.TrySend` hands off only when a receiver is waiting; `Parallel.*Pool` runs nested same-pool work inline.
- `Async` and `Parallel` switched to retained results; future listeners run under `setjmp` with per-listener cancel; Win32/POSIX deadline math saturates, closing the ~49-day Win32 hang.

### Core runtime and numeric round-trip

- `rt_format_f64_roundtrip` emits the shortest `%.*g` whose `strtod` recovers the original IEEE-754 bits; `Convert.ToString_Double` routes through it, with the 15-digit BASIC display form preserved as a separate entry point so goldens are unchanged.
- Time, text, threads, and `SafeI64.Add` adopt overflow-checked / `memcpy`-based arithmetic so signed overflow never relies on implementation-defined behaviour; Perlin 2D/3D guards NaN/inf inputs and clamps octave counts.

### Collections runtime

- All 26 collection types carry stable class IDs, register typed GC traversal, and follow a uniform retain-on-return contract declared via `returnsOwned` so the optimizer stops emitting defensive retains.
- `Queue` and `Stack` gain opt-in `owns_elements`; `Map.Values`/`IntMap.Values`/`FrozenMap.Values`/`MultiMap.Get` return owning `Seq`s.
- `Bytes.ReadI16/I32` sign-extend correctly (were zero-extending); new range-validated `BinaryBuffer` U16/U32 read/write; map/set constructors trap on size overflow instead of returning partial objects.

### Audio and 2D graphics

- 2D graphics: saturating int64 clip math and class-ID validation across AutoTile2D / Path2D / ShapeRenderer2D / TextRenderer2D / RenderPass2D; premultiplied-alpha edges; alpha-preserving `Canvas.BlitAlpha`; pooled per-frame tile lights. SpriteFont gains its own class ID and BDF/PSF entry points (BitmapFont measurement preserved via aliases), VideoPlayer gains a class ID, and renderer rotation / scaled-tilemap hit-tests are NaN- and INT64-guarded.
- `Pixels.Get`/`Set`/`Fill` keep their raw `0xRRGGBBAA` contract but now unpack a tagged `Color.RGBA(...)` argument instead of bit-reinterpreting it (fixes the Xenoscape cyan-bevel artifact); new `*Color` accessors are canonical.
- Image IO hardening across PNG/BMP/JPEG/GIF: chunk validation, pixel-offset checks, no partial saves on failure, and normalized per-frame delays.

### Graphics3D

- **Asset pipeline.** A `GLTF` class plus `Scene3D.Load` and new skinned/morphed/blended mesh draws; importers reject collinear or malformed triangles, ear-clip concave FBX n-gons, accept glTF `MAT2`/`MAT3` accessors and percent-encoded/data-URI buffers, walk deep trees iteratively, and close STL geometry batches cleanly on failure.
- **Normal-matrix correctness.** Skinning and `Mesh3D.Transform` route normals through a corrected inverse-transpose `(M⁻¹)ᵀ` — fixing counter-rotated lighting under rotation and shear — with an identity fallback reserved for genuinely singular matrices so very small-scaled objects keep their rotation.
- **Skeletal animation and morphs.** Quaternion-slerp interpolation with finite-matrix validation; `Skeleton3D` freezes bone topology once pose buffers bind so a late `AddBone` traps rather than desyncing the palette; finite-masked keyframes fall back to the bind-pose TRS; `MorphTarget3D` cloning fails closed on a short or missing delta; blend and transition times clamp to finite, non-negative values.
- **Backend correctness (software/OpenGL/Metal/D3D11).** Oversized D3D11 bone palettes stay active for clamped uploads; completed shadow-map slots track as a contiguous prefix with clip-`w`/depth validation; OpenGL RTT and HDR-readback math are bounds-checked; D3D11 swapchains recreate after a failed resize; GPU post-FX color and gamma are unified across all four backends.
- **Compositing and HiDPI.** GPU post-FX frames present through the backend compositor even when final overlays are recorded — split backends draw overlays after tonemapping before present — and screen-space overlays composite through a depth-bypass sub-pass sized in logical coordinates so HUD layout stays stable on HiDPI displays.
- **Physics3D and queries.** A warm-started sequential-impulse contact solver with sleep islands and island-batched contact scheduling lands stable, deterministic resting box stacks; oriented-box SAT with clipped multi-point OBB face manifolds, GJK/EPA convex-hull contacts against hulls, spheres, capsules, and boxes with separated-overlapping-AABB coverage, BVH-pruned triangle-mesh contacts against spheres, capsules, boxes, and convex hulls, shape-accurate mesh/convex raycasts and collisions, and adaptive sphere/capsule sweeps for thin geometry; heightfield contacts honour signed, non-uniform vertical scale; correct static/kinematic body semantics and collision-layer masks; enter/stay/exit matching moved from an O(n²) scan to a hash table; cached query broadphases and collision-event objects, mesh-raycast BVH reuse, contact purging on body removal, and a fixed quaternion vector-rotation aliasing bug.
- **Robustness at scale.** World-space setters clamp extreme inputs; a row-major `Mat4` inverse fix repairs parented world-to-local; scene-transform invalidation is allocation-free via parent-revision tracking; capacity growth and instanced submission counts across Model3D/Canvas3D/Game3D are integer-overflow-safe; deep hierarchies traverse iteratively; look-at and basis construction reject degenerate vectors; Metal skybox zero-vector fallback follows the engine's `-Z` camera convention; and mirrored (negative-scale) transforms reverse their winding order.
- **Performance — geometry & shading.** CPU skinning precomputes each bone's normal matrix once instead of per vertex per influence; software terrain-splat setup and per-pixel UV interpolation are hoisted out of the inner loop; environment reflections reuse the perturbed shading normal; and frustum planes validate once per extraction.
- **Performance — draws & queries.** GPU render-target CPU mirrors allocate lazily; `Particles3D` batches alpha and additive billboards as one sorted mesh and reuses per-frame draw buffers; camera controllers update through raw-component math rather than allocating a `Vec3` per frame; Canvas3D motion-history and temp-object tracking move from linear scans to O(1) hash lookups with per-draw blend classification cached once; Scene3D queries and draw culling use an internal spatial index with flat-path parity and generated 10k-node candidate-reduction coverage; and a morphed-draw use-after-free is fixed by retaining the source mesh for the deferred draw.
- **Visibility, lighting, and upload telemetry APIs.** `SceneNode3D.SetAutoLOD`/`SetImpostor`; `Canvas3D.SetOcclusionCulling` with `DrawCount`/`OccludedDrawCount`, `SetTextureUploadBudget`, `TextureUploadBytes`, and `TextureUploadPendingBytes`; `SetClusteredLighting`/`MaxActiveLights` (software >16-light baseline, 16-light fallback on unsupported backends); `SetShadowCascades` for capability-gated primary-directional CSM; `Light3D.CastsShadows` plus now-movable `SetPosition`/`SetDirection`; `Material3D.Has*` slot inspection; and `Camera3D.ScreenToRayOrigin` with tunable `NearPlane`/`FarPlane`.
- **Physics joints & forces.** `HingeJoint3D` gains a motor, signed angle readout, and limits; `SixDofJoint3D` adds a linear motor for sliders/pistons plus joint-frame pose-angle limits; plus `RopeJoint3D` and off-center `Physics3DBody.ApplyImpulseAtPoint`/`ApplyForceAtPoint`, all iterated by `Physics3DWorld.SolverIterations`.
- **Animation & IK APIs.** `IKSolver3D.TwoBone`/`LookAt`/`FABRIK` with pole-vector `SetPole`, ground-aligned terrain-foot IK via `SetGroundNormal`, and `AnimController3D.SetIKSolver`; `BlendTree3D` 1D/2D blendspaces bound through `SetBlendTree`; additive `PlayLayerAdditive`/`CrossfadeLayerAdditive`, whole-skeleton `SetAnimationLOD`, and distal-bone-freezing `SetBoneLOD`; and `Animation3D.Retarget`, now a cross-skeleton humanoid remap that infers canonical joints from mixamo/Unreal/Blender bone names and scales translations by bone-length ratio onto a differently-proportioned rig.
- **Navigation APIs.** `NavMesh3D.Bake`/`BakeTiled` run a from-scratch voxel pipeline — voxelize geometry, keep the slope-walkable surface, erode by `agentRadius`, emit a grid mesh whose triangle count is decoupled from input density — while `RebuildTile` and tiled `AddObstacle`/`RemoveObstacle`/`UpdateObstacle` re-carve a single tile in place from updated source geometry and an XZ query grid gives O(cell) point location; per-triangle `SetArea`/`GetArea` flags feed `GetTraversalCost` and off-mesh-link cost path weighting. `NavAgent3D` RVO velocity-obstacle avoidance (`AvoidanceEnabled`/`AvoidanceRadius`) runs over an O(N) spatial hash with head-on deadlock tie-breaking, with `SetTarget`/`ClearTarget` crowd targets, plus `AddOffMeshLink` and `World3D.bakeNavMesh` editor hooks over the owned scene.
- **Assets, streaming & misc.** `TextureAsset3D` KTX2 metadata, mip-residency telemetry, retained native compressed mip blocks, BC3/BC7 modes 0-7 software decode, and representative ETC2/ASTC RGBA8 fallbacks behind `BackendSupports("bc7"/"astc"/"etc2")` gates, with already-bound `Material3D` slots following later resident-mip changes; scene-indexed `Model3D` import (`SceneCount`/`GetCamera`/`InstantiateSceneAt`) with glTF scene-local cameras; deferred/cancellable `Game3D.AssetHandle3D` async loads with texture-aware template-cache residency controls, `Assets3D.GetResidentBytes`, and a streaming hitch probe for zero-upload-budget gating; `World3D` entity/body/draw counters and `WorldStream3D` manifest-driven cell/terrain load-unload telemetry; plus `Canvas3D.Resize`, a queued `PollEvent()`/`Poll()` contract that advances `DeltaTime`, window-vs-output sizing, an `Entity3D` `OrbitController`, PBR `SetShadingModel(2)`, and binary-STL streaming.

### Game3D runtime (new)

- A code-first `Viper.Game3D.*` engine layered over Graphics3D. `World3D` drives the scene, physics, camera, input, audio, and effects from one fixed-step update/render loop; `Entity3D` gives a transform/mesh/body hierarchy with cycle-safe, ownership-checked parenting and auto-spawned subtrees, and a physics body can belong to only one spawned entity, so `spawn`/`attachBody` reject sharing it.
- Internal 3D concurrency groundwork now reuses `Viper.Threads.Pool` / `Viper.Threads.Parallel`, with a Phase-0 surface probe for ordered worker results and `World3D` worker-count replay parity plus an internal main-thread commit queue for future async upload handoff.
- Carryover hardening adds fixed-loop accumulator and spiral-guard CTest coverage, a no-`Mat4.` guard for common Game3D samples, direct Zia coverage for `Entity3D.FromNode`, method-qualified destroyed-handle and double-despawn diagnostics, missing package-asset error-handle coverage, imported-group spawn/despawn churn, cache-clear reload coverage, and packaged glTF hierarchy loading through `Assets3D.LoadModelAsset`.
- Built-in first-person, free-fly, orbit, and follow camera controllers plus a grounded `CharacterController`; per-frame `Input3D` snapshots make `runFrames` deterministic, and native update callbacks are validated as executable on all three platforms.
- `Animator3D` (crossfade, speed, state-time, event queries, additive layers, blend-tree base poses, IK solver binding, and worker-backed batch updates through `World3D.workerCount`) over the animation controller; enter/stay/exit collision events; spatial `Sound3D` (listener-follow, positional/attached playback, distance attenuation) and `Effects3D` presets (explosion, sparks, dust, smoke, impact decals); plus environment, model-template, asset-handle, lazy `World3D.stream`, `WorldStream3D`, and debug helpers.
- `examples/3d/openworld_slice/` adds a tested streaming vertical slice: cell and heightmapped-terrain manifests with per-tile heightfield colliders and nav-bake sources, all-quadrant bounded-residency traversal with deterministic replay, KTX2 textures, a skinned glTF agent with crossfade plus LookAt and terrain-sampled foot IK, async asset-handle completion, character/physics/nav stepping, a committed software baseline, a capability-gated GPU smoke with a degenerate-basis robustness pass, and a named local macOS Release software/Metal perf baseline.
- The spatial-audio classes are renamed `Audio3D`/`AudioListener3D`/`AudioSource3D` → `Sound3D`/`SoundListener3D`/`SoundSource3D` (breaking), matching the canonical `Viper.Sound` family.

### Game runtime

- **Plan-24 additions.** New `Viper.Game.UI` widgets (TextInput, Table, Modal, Slider, Dropdown, Tooltip), `AnimTimeline` plus multi-event `AnimStateMachine`, `Projectile2D`, `Renderer2D.DrawTextureRotated[At]`, named audio mixer groups, and a `Viper.System.Clipboard` text surface.
- Behavior, Entity, Lighting2D, Game-UI, and ScreenFX math saturates at int64 limits; tilemap raycast DDA checks both side-touched tiles so corner-crossing rays cannot skip solids.
- `Config`/`LevelData` release input text and parsed JSON on every path; Quadtree, Pathfinder, ButtonGroup, AchievementTracker, and SpriteAnimation gain class IDs and destroy-time release; Typewriter reveals by UTF-8 codepoint.
- Edge hardening across `Viper.Game`/`Game.UI`: TextInput preserves its NUL-terminated invariant; Dropdown/Tooltip/TextInput hit-test with saturating math; `AnimStateMachine` rejects out-of-clip frame events and catches wrapped forward/reverse crossings; and Grid2D self-copy, Tween endpoint overflow, and non-Canvas handle rejection are fixed.

### Network and GUI

- HTTP/2 gets independent encode/decode HPACK tables and length-aware decode preserving embedded NULs; HTTP/1.1 enforces strict RFC 7230 `Transfer-Encoding` parsing (closing the smuggling avenue); the URL parser validates IPv6 brackets, empty hosts, and out-of-range ports.
- WebSocket rejects non-minimal frames and validates close codes; SSE rejects control bytes and non-leading-`/` targets; UDP detects truncation via `MSG_TRUNC`/`WSAEMSGSIZE`; TLS `key_share` is exact-length validated.
- Every public `Viper.GUI.*` entry routes through `rt_gui_widget_handle_checked` (NULL/destroyed/wrong-type rejected) across five audit rounds; event/layout fixes add a non-shifting event sentinel, preserve focus/modifier state across backends, suppress synthetic clicks after a handled mouse-up, and route shortcuts through focus.

### Compiler, IL, codegen

- AArch64: the protection set covers both use and def operands (fixing register-reuse clobbers); `i1`/`i16`/`i32` loads/stores use byte/halfword/word transfer opcodes; `cbz`/`cbnz` are real terminators; X29 is no longer allocator-managed.
- x86-64: cross-block fold-liveness guards on SIB and IMUL→LEA stop strength reductions from erasing live virtual registers; block-DCE preserves physical registers at exits; the AT&T emitter rejects invalid `CALL`/`JMP`/`JCC`/`LEA`/`SETcc`/`MOVZX` operand classes and non-`RCX` shift counts before printing.
- x86-64 throughput for large targets: a single-pass liveness DCE, dataflow store-forwarding, suffix-liveness move folding, and direct per-function text sections (reusing the encoder's offset/size estimates) replace double measurement — the rewrite behind the ViperIDE link win.
- Bytecode VM: explicit two's-complement wrapping arithmetic, consistent checked float→int traps, and validated locals/pointers/alloca sizes before host state is touched.
- Structural: the largest backend and frontend hot functions decompose into per-family helpers; three dominator implementations unify into one shared pass; AArch64 encoder dispatch becomes table-driven; one `Bytecode.def` X-macro generates the enum, names, dispatch table, and `isKnownOpcode`, and the `run()` switch drops `default:` so `-Wswitch` makes every opcode handler mandatory.

### Native toolchain (linker, readers, writers)

- All four object readers (ELF/COFF/Mach-O/Archive) and three writers received bounds-checking and alignment-UB fixes with per-file caps; a shared `ObjFileWriterUtil` keeps COFF/ELF/Mach-O hardening in sync; typed `InputSectionKey` replaces ad-hoc bit-packing.
- The COFF reloc addend convention now agrees across reader, applier, and writer; weak externals honor `IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY`; deterministic identity hashing keeps duplicate-symbol and reloc-target detection stable across re-runs.
- New AArch64 load/store reloc kinds and a corrected `BRANCH19`; `RelocApplier` validates instruction class per reloc kind; section identity is preserved across reader→writer copies; targeted P1 fixes land Mach-O TLV validation, ADDR32NB RVAs, trampoline bounds checks, and namespaced GOT stubs.
- ELF emits `PT_TLS` with logical (non-serialized) zero-fill sizes; Mach-O `MH_SUBSECTIONS_VIA_SYMBOLS` splits text per atom; the PE writer narrows through overflow-checked helpers; dead-strip now scales with the relocation graph — indexing COMDAT children and reverse unwind maps once per object instead of rescanning every sibling section — which is what made the ViperIDE x64 link unbounded.

### Standard-library namespace de-clutter (breaking)

- Seven root modules re-home under their documented `docs/viperlib/` taxonomy: `Lazy`/`LazySeq` → `Viper.Functional`, `Machine`/`Environment`/`Exec` → `Viper.System`, `Log` → `Viper.Diagnostics`, `Fmt` → `Viper.Text`. Hard rename, no compatibility aliases; `Math`, `String`, `Terminal`, and intrinsic `Option`/`Result`/`Error` stay at root.
- `runtime.def` paths, `obj<…>` type tokens, relocated `RT_ALIAS` names, the codegen native-vs-runtime classification predicates, and ~410 consumer files across `src/`, `tests/`, `examples/`, `docs/`, and `misc/` were rewritten atomically.

### Zia language stability

- **Declaration-order independence.** Types, type aliases, inheritance, and interface-implementation relationships pre-register before body analysis, so declarations can refer to ones that appear later; base/interface layouts register before derived ones.
- **Type checks and casts.** `is`/`as` targets are resolved and validated — optional unwrapping, runtime class/interface checks, primitive exact checks; match arms agree on a result type instead of falling back to `Unknown`.
- **Interfaces, generics, `Result[T]`, `Unit`.** Interface bodies lower as default vtable implementations; generic constraints carry through and validate at instantiation; postfix `?` propagates `Err`; `Unit` lowers as a pointer-sized singleton; `Void`/`Never`/`Module` are rejected in value positions.
- **Names and visibility.** Qualified names are accepted in constraints, `extends`/`implements`, and struct-literal positions; module-scoped collisions get module-qualified names with short-name compatibility preserved; private fields cannot be bound externally.

### ViperIDE and IntelliSense

- `zia` force-loads `fe_zia` so IntelliSense, hover, diagnostics, and symbols hit the real semantic engine instead of the weak `rt_zia_completion_stub.c` stubs; a new highlight bridge feeds the GUI tokenizer the live keyword set from `Lexer::lookupKeyword`.
- New `Viper.System.Process` / `Process.Handle` streaming surface: argument-vector startup with cwd/env, non-blocking stdout/stderr reads, poll, exit-code, kill, wait, and GC finalization.
- New `Viper.Zia.Toolchain` (`Check`/`Compile` plus `*ForFile`) returns structured `Seq`/`Map` diagnostic and result records; ViperIDE live diagnostics moved off tab-delimited parsing onto it, with weak-stub parity when `fe_zia` is absent. `Viper.Zia.SemanticJob` adds pollable background completion, signature, hover, symbols, and diagnostics jobs for active editor language services.
- New `Viper.Zia.ProjectIndex`: an explicit-lifetime project index with dirty-buffer import resolution, structured definition/reference results, and rename workspace-edit generation with visible-collision detection.
- `Viper.Game.Scene` graduated to a full editable JSON scene document: non-trapping load returning structured diagnostics under enforced resource/overflow limits, typed scene/object properties, a deterministic canonical-v1 round-trip, an isolated `BuildTilemap` render copy, and atomic same-directory save.
- A prerequisite runtime slice exposes structured primitives for project trees, automation, palettes, and debugger integration: `Workspace.FileIndex`/`Watcher`, `Assets.Resolver`, `Project.Manifest`, `Workspace.Edit`, GUI `TestHarness`/`VirtualList`/`VirtualTree`/`CommandState`/`Accessibility`, `Debug.Protocol`, and `Text.FuzzyMatch`.
- **The editor gains real build/run and language services.** Now a standalone top-level project, ViperIDE has an argument-vector build/run loop with streamed cancellable output, clickable diagnostics, gutter breakpoints, Quick Open, workspace symbols, and file-tree operations that preview Zia bind rewrites on rename. Completion, diagnostics, hover, signature, outline, call hierarchy, and fold regions are content-revision-gated off the keystroke path, and code actions cover Organize Binds, Apply Fix-It, Create Missing Bind, Suppress Warning, and Format.
- A native large-file performance pass moves completion, diagnostics, indexing, and search off the typing path via O(1) no-wrap layout, dirty-line syntax caching, lazy oversized-source-guarded indexing, cached `.gitignore` walks, and revision-keyed snapshots — each backed by wall-clock probes (`VIPERIDE_PERF_LOG`). New `Viper.GUI.CodeEditor.Revision` and `TreeView.GetNodeAt(x, y)` back the hot path and context menus.
- Cooperative project search (literal/regex, case/whole-word, include/exclude), a categorized command palette that flags unsupported commands with a reason, and list-backed Problems/Output/Search/References tool panels with live build/run state. The wired debug protocol remains a non-executing placeholder — not real debugging yet.

### Windows, MSVC, and HiDPI

- Top-level CMake opts into CMP0141 with embedded MSVC debug info; the Windows import policy expands for path/DPI/timer/CRT symbols, and the CRT import flavour threads through both backends so packaged payloads force release-runtime imports even from a Debug tool build.
- Win32 HiDPI uses physical pixels throughout, sizing the native client area from the already-scaled framebuffer via `AdjustWindowRectExForDpi` and pacing frames with a high-resolution waitable timer; public `Canvas` sizing stays behind `coord_scale` to match macOS Retina.
- D3D11 bone-palette cbuffer sized from the shared 256-bone constant (the old 128-bone buffer overran an 8 KiB mapping under the debug UCRT); portable helpers add UTF-8 file-stat aliases, MSVC bit-scan, and a platform TLS invalid-socket sentinel.
- New `Viper.GUI.App.GetScale` (real implementation plus graphics-disabled stub) lets layout convert physical to logical pixels: `layoutSettingsPanel` applies its width breakpoints in logical pixels, fixing the ViperIDE Preferences dock whose adaptive compact width never engaged on 2x displays (1x behaviour byte-identical).

### Packaging

- Windows VAPS installer: PE32+ payload validation, adjacent-DLL discovery with redistributable classification, Add/Remove metadata, install-scope and sign-thumbprint parity across `package`/`install-package`, and a `meta/manifest.sha256` integrity check; gated by a user-scope smoke and a headless package-smoke ctest.
- Windows toolchain installers default to per-user scope and reject MSVC Debug-CRT payloads without `--allow-debug-toolchain`; new switches cover scope, PATH mutation, file associations, and shortcuts, with a post-build pass validating the emitted `.msi`/`.exe` against the staged manifest.
- macOS toolchain generation no longer shells out to `pkgbuild`/`productbuild` — Viper writes the XAR/CPIO archives, CMake discovery wrappers, and symlinks itself — and `install-package` adds native `.pkg` verification plus Developer ID signing, notarization, and stapling.
- Linux toolchain packages advertise runtime/developer dependencies and ship install/uninstall scripts; `.deb` control fields, `.rpm` headers, and `.zia`/`.il` MIME registration are validated; VAPS asset packaging iterates a root-validated safe directory and hardens `ar`/USTAR/ZIP/TAR writers and `SOURCE_DATE_EPOCH`.

### Tests

~26K new test SLOC.

- **Memory / GC / MessageBus / Box / Parse** — contract suites for the validated `Memory.*` surface, weak-ref CAS retain and resurrected-cycle finalizers, `Box.ValueType` alignment/tag validation, trap-string escaping, and typed `Parse.*Option` round-trips.
- **Collections / codegen / bytecode VM** — class-ID distinctness and retain-on-return suites; cross-block SIB and IMUL→LEA fold-liveness cases, `AsmEmitter` operand-class diagnostics, move-folding/store-forwarding equivalence, and direct-bytecode wrapping/conversion/bounds regressions.
- **Native linker** — `parseSize`, archive ordering, `CodeSection` identity, ELF symbol-size preservation, the COFF reloc addend convention, AArch64 reloc validators, weak-external `SEARCH_NOLIBRARY` paths, PE overflow guards, and large-fanout dead-strip.
- **Process / Zia tooling / ViperIDE primitives** — boxed-arg streaming exec; ProjectIndex definition/reference/rename; focused CTests for workspace index/watcher, asset resolution, manifest parsing, transactional edits, fuzzy match, editable scene flows, scaled tilemap hit-testing, GUI automation, headless debug protocol, and ViperIDE editor hot-path/tool-panel probes.
- **Zia language hardening** — interpreted, optimized-`viper run`, and native coverage for structured catch/finally, multi-typed catches, bare rethrow, namespace globals, constrained generics, `Result[T]`, weak fields, and function references.
- **Graphics / GUI / crypto / packaging** — pixel raw-vs-Color APIs, stale-handle audits, SAN matching beyond the extraction cap, ZIP manifest duplicate/uncovered-entry rejection, and the non-elevated Windows user-installer smoke.

---

Demos and docs tracked the work: the 3D Bowling demo became a playable arcade lane and, with a new code-first Game3D sample set (a sub-20-line hello scene, a copyable starter, and a full-stack showcase with a balanced lighting rig and magenta/overlay/exposure smoke checks), migrated onto the Game3D layer alongside chess and Crackman polish; the flat `src/runtime/graphics` tree split into domain subdirectories with its largest 3D translation units (Game3D, Scene3D, Canvas3D) decomposed into focused per-feature files, and ViperIDE moved into its own top-level project; and `docs/viperlib/` gained `system.md`, `zia.md`, `game/scene.md`, and `graphics/game3d.md` plus a Doxygen pass over the 3D runtime and the native-toolchain design docs.

<!-- END DRAFT -->
