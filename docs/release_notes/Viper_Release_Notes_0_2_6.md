# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.5 was cut on 2026-05-07. -->

### What this release is about

A hardening cycle pushing toward alpha quality that also delivered substantial new capability. The hardening backbone is broad: the Zia frontend closed its major stability and language gaps, raw pointers were removed from both source languages, the native linker became real enough to consume optimized C++ object input and ship ViperIDE's IntelliSense end-to-end, and a correctness/ownership pass swept memory, threads, crypto, IO, graphics, the bytecode VM, packaging, and installers. The headline new work is ViperIDE — the in-development Viper/Zia code editor — moving off runtime stubs onto real build/run, completion, diagnostics, project tree, and symbols — joined by an expanded game-engine surface (plan-24 widgets, an editable `Viper.Game.Scene` document) and a streaming process / toolchain / project-index tooling layer; a new code-first `Viper.Game3D` engine then layered a full 3D gameplay surface over the Graphics3D runtime.

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
- **3D runtime & graphics hardening.** A multi-backend Graphics3D correctness round (software/OpenGL/Metal/D3D11 skinning, shadows, readback), bone-topology-frozen skeletal animation, shape-accurate Physics3D queries, and saturating 2D draw / scaled-tilemap math.
- **Code-first Game3D engine (new).** A `Viper.Game3D.*` layer over the 3D runtime: a `World3D` fixed-step update/render loop, an `Entity3D` hierarchy with cycle-safe parenting, first-person/free-fly/orbit/follow cameras and a grounded character controller, physics/collision events, `Animator3D`, spatial `Sound3D`, and `Effects3D` VFX presets.
- **Game-engine surface (plan 24).** New `Viper.Game.UI` widgets, `AnimTimeline` + multi-event `AnimStateMachine`, `Projectile2D`, rotated-texture draws, named audio mixer groups, and a `Viper.System.Clipboard`.
- **ViperIDE editor wiring.** A streaming `Viper.System.Process`, a structured `Viper.Zia.Toolchain`, a semantic `Viper.Zia.ProjectIndex`, and an editable `Viper.Game.Scene` back the in-development editor's build/run, project tree, completion, diagnostics, and symbols; a codegen and dead-strip rewrite cut the ViperIDE native x64 build from ~340s to ~35s.

### By the Numbers

| Metric | v0.2.5 | v0.2.6 | Delta |
|---|---|---|---|
| Commits | — | 213 | +213 |
| Source files | 2,996 | 3,058 | +62 |
| Production SLOC | 552K | 627K | +75K |
| Test SLOC | 228K | 262K | +34K |
| Demo SLOC | 188K | 189K | +1K |

Counts via `scripts/count_sloc.sh` (production 627,234 / test 262,309 / demo 189,256 / source files 3,058).

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

- New skinned/morphed/blended mesh draws, a `GLTF` class plus `Scene3D.Load`, quaternion-slerp skeletal interpolation with finite-matrix validation, and a row-major `Mat4` inverse fix that repairs parented world-to-local; world-space setters clamp extreme inputs, deep hierarchies traverse via heap stacks, and importers reject collinear/malformed triangles.
- Backend correctness across software/OpenGL/Metal/D3D11: skinning normals route through inverse-transpose matrices (identity fallback when singular), oversized D3D11 bone palettes stay active for clamped uploads, completed shadow-map slots are tracked as a contiguous prefix with clip-`w`/depth validation, and OpenGL RTT extents / HDR-readback math are bounds-checked with D3D11 swapchain targets recreated after a failed resize.
- Skeletal animation and morphs: `Skeleton3D` freezes bone topology once pose buffers bind so a late `AddBone` traps instead of desyncing the palette; per-component finite-masked keyframes fall back to the bind-pose TRS rather than collapsing to zero/identity; `MorphTarget3D` cloning fails closed on a short or missing delta buffer; AnimationController blend/transition times clamp to finite non-negative values.
- Physics3D and scene queries: oriented-box SAT, shape-accurate mesh/convex raycasts and collisions, adaptive sphere/capsule sweeps for thin geometry, heightfield contacts honouring signed/non-uniform vertical scale, large-static-surface contact-point reconstruction, and a fixed quaternion vector-rotation aliasing bug; rays self-normalize, look-at/basis construction rejects degenerate or singular vectors, and the VSCN loader walks deep hierarchies iteratively with finite-or-fallback clamped transforms.
- Canvas3D/Game3D fixes align shading-model constants, promote `SetShadingModel(2)` to PBR, add `Camera3D.ScreenToRayOrigin`, keep final overlays after GPU post-FX finalization, snapshot Game3D input per frame, reverse winding for mirrored mesh transforms, stream exact binary STL files, clamp synthetic mouse/delta-time extremes, and tighten backend-light/material/draw-queue validation while preserving opaque front-to-back sorting.
- A further hardening round: Model3D node walks (count/clone/scene-ref) go iterative for deep imported hierarchies, capacity growth across Model3D/Game3D/Canvas3D is integer-overflow-safe, Physics3D collision enter/stay/exit matching moves from an O(n^2) scan to a hash table, Particles3D draws alpha and additive billboards as one batched sorted mesh, and Canvas3D motion-blur keys and deferred static-mesh draws are corrected.
- The 3D runtime also adds a public `Canvas3D.Resize`, normalizes `Input3D.moveAxis`, lets `OrbitController` target an `Entity3D`, indexes spawned Game3D entities by body/name for faster lookups, ear-clips concave FBX n-gons, accepts glTF `MAT2`/`MAT3` accessor metadata, caches normal-map tangent revisions, and splits new helper-heavy regions into private `.inc` implementation slices for readability.

### Game3D runtime (new)

- A code-first `Viper.Game3D.*` engine layered over Graphics3D. `World3D` drives the scene, physics, camera, input, audio, and effects from one fixed-step update/render loop; `Entity3D` gives a transform/mesh/body hierarchy with cycle-safe, ownership-checked parenting and auto-spawned subtrees.
- Built-in first-person, free-fly, orbit, and follow camera controllers plus a grounded `CharacterController`; per-frame `Input3D` snapshots make `runFrames` deterministic, and native update callbacks are validated as executable on all three platforms.
- `Animator3D` (crossfade, speed, state-time, event queries) over the animation controller; enter/stay/exit collision events; spatial `Sound3D` (listener-follow, positional/attached playback, distance attenuation) and `Effects3D` presets (explosion, sparks, dust, smoke, impact decals); plus environment, model-template, and debug helpers.
- The spatial-audio classes are renamed `Audio3D`/`AudioListener3D`/`AudioSource3D` → `Sound3D`/`SoundListener3D`/`SoundSource3D` (breaking), matching the canonical `Viper.Sound` family.

### Game runtime

- **Plan-24 additions.** New `Viper.Game.UI` widgets (TextInput, Table, Modal, Slider, Dropdown, Tooltip), `AnimTimeline` plus multi-event `AnimStateMachine`, `Projectile2D`, `Renderer2D.DrawTextureRotated[At]`, named audio mixer groups, and a `Viper.System.Clipboard` text surface.
- Behavior, Entity, Lighting2D, Game-UI, and ScreenFX math saturates at int64 limits; tilemap raycast DDA checks both side-touched tiles so corner-crossing rays cannot skip solids.
- `Config`/`LevelData` release input text and parsed JSON on every path; Quadtree, Pathfinder, ButtonGroup, AchievementTracker, and SpriteAnimation gain class IDs and destroy-time release; Typewriter reveals by UTF-8 codepoint.
- Edge hardening across `Viper.Game`/`Game.UI`: TextInput keeps its NUL-terminated invariant (truncate-at-NUL, ignore typed NULs, memmove truncation); Dropdown, Tooltip, and TextInput map coordinates with saturating math; `AnimStateMachine` rejects out-of-clip frame events and detects wrapped crossings on forward/reverse loops; `AnimTimeline` clears stale markers before no-op advances; Grid2D self-copy is a memmove-backed no-op; Tween avoids opposite-sign endpoint overflow; Dialogue and Achievement reject non-Canvas handles.

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
- **The editor gains real build/run and language services.** Now a standalone top-level project, ViperIDE gained an argument-vector build/run loop with streamed, cancellable output and clickable diagnostics, persisted gutter breakpoints, file-tree project operations with previewed Zia bind rewrites on rename, Quick Open, and workspace symbols. Language services — completion, diagnostics, hover, signature, outline, incoming/outgoing calls, semantic fold regions, and IntelliSense docs fed by Zia declaration comments — are content-revision-gated to stay off the keystroke path; code actions cover Organize Binds, Suppress Warning, Apply Fix-It, Create Missing Bind, Trim Trailing Whitespace, and Format Document/Selection.
- A native large-file performance pass keeps the editor responsive: O(1) no-wrap layout, dirty-line syntax caching, lazy project-index startup with oversized-source guards, cached `.gitignore` workspace walks, capped semantic workers, and revision-keyed snapshots move completion/diagnostics/indexing/search off typing — each guarded by typing/paint, scroll, selection-drag, fold-generation, and minimap wall-clock probes (`VIPERIDE_PERF_LOG`). New `Viper.GUI.CodeEditor.Revision` (a cursor/scroll-ignoring change probe) and `Viper.GUI.TreeView.GetNodeAt(x, y)` back the hot path and context menus.
- Cooperative project search (literal/regex, case/whole-word, include/exclude filters), a categorized command palette that marks unsupported language-service commands unavailable with a reason, and tool panels with live build/run state, severity-coloured Problems rows, and surfaced suppression/fix-it actions. The Problems/Output/Search/References/Debug tabs remain lightweight list-backed surfaces, and the wired debug protocol is still a non-executing placeholder — not real debugging yet.

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

Demos and docs tracked the runtime work: the 3D Bowling demo was overhauled into a playable arcade lane (stable roll/aim cameras, PBR lane and ball materials) and migrated onto the new Game3D layer, joined by a code-first Game3D sample set (a sub-20-line hello scene, a copyable starter project, and a full-stack showcase) alongside chess and Crackman polish; stale Windows debug/O0 build pins were dropped once optimized x86-64 builds were restored; the flat `src/runtime/graphics` tree was split into domain subdirectories (rename-only); ViperIDE moved out of the demo gallery into its own top-level project; and `docs/viperlib/` (including new `system.md`, `zia.md`, `game/scene.md`, and `graphics/game3d.md`) plus a Doxygen pass over the Game3D and Graphics3D runtime and the native-linker/native-assembler design docs were refreshed.

### Commits

See `git log v0.2.5-dev..HEAD -- .` for the full 213-commit history since v0.2.5.

<!-- END DRAFT -->
