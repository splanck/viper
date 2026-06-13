# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A hardening cycle continuing v0.2.6. The headline new work ends per-frame Graphics3D rendering flicker, closes the open-world ("3D Next Level") streaming gaps, adds node-animation playback over a substantially upgraded glTF/FBX importer, gives ViperIDE a real VM-backed debugger, and opens a machine-readable agent-facing CLI. A late codegen performance round unlocks the full AArch64 argument-register pool and tightens x86-64 and AArch64 allocator overheads, spot light shadow maps join the directional-light cascade budget, and shared platform adapters consolidate all host-specific socket, entropy, and file-dialog logic. Around it, a repo-wide sweep makes recoverable runtime traps fail closed, promotes IL and codegen invariants from debug asserts to release-mode validation, unifies scalar instruction semantics across the tree-walking VM and both bytecode engines, re-skins the GUI toolkit through a shared anti-aliased drawing core, and modularizes the runtime into focused translation units — while the Linux and Windows/MSVC builds both return to green.

- **Graphics3D flicker stabilized.** Queue-order-independent occlusion history with covered-streak gating, camera-depth-fitted shadow cascades, per-camera LOD/impostor hysteresis, and conservative terrain-horizon culling; every backend validates draw-command index ranges and shares one depth-bias scale.
- **glTF/FBX import & node animation (new).** `NodeAnimation3D`/`NodeAnimator3D` play node, object, camera, and morph-weight clips, and `Model3D`/`Assets3D` can pull a single clip from an external file; the FBX importer gains full transforms, cubic curves, PBR routing, and async loading.
- **Canvas3D authoring helpers (new).** `DrawMeshWind` foliage sway, `DrawImage2D` HUD blits, and native `SetFullscreen`/`ToggleFullscreen` (`Game3D.Keys.F11`); `DrawMeshSkinned` takes an `AnimController3D` pose and `Material3D` gains depth-bias and `ShadowMode`.
- **Open-world streaming & navmesh tooling (new).** `WorldStream3D` cells load binary sidecars into a resident-byte budget, `NavMesh3D.Export`/`Import` round-trip versioned `VNAVMSH2`, compressed textures drain large mips across frames, and stream manifests resolve `asset://` sidecars.
- **3D runtime & Game3D fail closed.** Non-finite pose/playback inputs clamp or revert to the bind pose, scene/transform/raycast/physics/navigation entry points reject bad handles, stored references are class-checked, and screenshot/render-target capture finalizes with no present side effect.
- **Runtime fails closed (repo-wide).** A cppcheck-driven audit makes every recoverable `rt_trap` return a safe sentinel and free its locals; unrecoverable entropy/DRBG failures route through a new non-returning `rt_abort`, so no path proceeds with predictable key material when a trap hook returns.
- **IL, codegen & linker hardening.** The IL builder promotes its debug-only invariants to release-mode validation and shares one checked-range implementation with CheckOpt (which now demotes proven-safe overflow-checked arithmetic), the verifier makes the EH `resumetok` a linear handler-provenance capability that only exception dispatch can mint (ADR 0005), and codegen and the native linker surface diagnostics instead of asserting, atop the frame-layout fix that closes the O1 miscompiles.
- **Unified scalar semantics & stable IL storage (new).** A shared, VM-neutral kernel makes the tree-walking VM and both bytecode engines yield identical values and trap kinds for one IL module, and block/instruction storage moves to stable-address containers with interned identifiers so references survive insertion and erasure.
- **Bytecode VM hardening.** The VM validates memory ranges and indirect callees before dereference, traps instead of asserting on bad branch/switch targets, and bounds runaway programs via `BytecodeVM.setMaxInstructions` and a signal-safe interrupt.
- **GUI refined-depth visual pass.** A shared anti-aliased `vg_draw` core and new radius/elevation/gradient/focus/motion theme tokens route the whole widget set through one rounded, elevated style; ViperIDE draws vector toolbar icons and a `GUI.GroupBox` card rebuilds the settings panel.
- **ViperIDE debugging, build feedback & zoom (new).** A VM-backed debug adapter (`viper run --debug-adapter`) replaces v0.2.6's non-executing placeholder with pause, continue, step, source breakpoints, call stacks, and locals over JSON; the editor adds persistent UI zoom, build-duration and error/warning status, and absolute `viper` path resolution.
- **Agent-facing CLI & structured diagnostics (new).** `viper check` (type-check/verify gate with differentiated exit codes), `viper eval` (one-shot snippet evaluation with JSON results and a distinct trap exit code), and `viper explain`/`--print-error-codes` over a central diagnostic-code catalog, plus `--dump-runtime-api`/`--dump-opcodes` registry dumps; the Zia/BASIC LSP and MCP servers now carry the same code/stage/range/help/notes/fix-it diagnostics, so did-you-mean corrections reach editor clients.
- **Frontend parsing & semantics.** BASIC adopts overflow-aware numeric, line-label, and `SELECT CASE` parsing and accepts numbered `DIM` fields inside `CLASS` bodies; Zia restores circular and self binds by treating an in-progress bind as a known dependency rather than a fatal `V1000`.
- **Cross-platform utilities, language servers & CLI.** The Text/Time/IO surface closes documented edge cases, the BASIC/Zia LSP/MCP servers compute UTF-16 ranges and enforce a strict initialize-before-use lifecycle, and the CLI rejects mismatched run/build/ABI combinations behind atomic, descriptor-safe tool outputs.
- **Project loading & packaging.** Convention detection scans real BASIC/Zia tokens and rejects symlink escapes; package writers stage through same-directory temp files and hash assets with SHA-256; staged toolchain binaries detect their own OS and architecture from the object header rather than the host.
- **Windows and Linux builds back to green.** Windows restores the BASIC-VM, installer, ViperIDE, and x86-64 suites (NOMINMAX, extended MSVC atomics, an 8-job parallelism cap) and closes its D3D11 sign-off gaps; Linux returns to a full green suite — 1,670 passing.
- **Spot light shadow maps.** Shadow-casting spot lights join the shared directional shadow budget, built from a single perspective light view-projection; all four backends (software/OpenGL/Metal/D3D11) sample with a perspective divide and cone-angle suppression while the directional cascade path stays orthographic.
- **Codegen performance round.** The AArch64 allocator gains clobber-aware argument-register eviction (sixteen previously excluded registers re-enter the pool), spilled-operand reload caching (N reloads per block collapse to one resident-register home), shared pre-RA copy-forwarding between backends, and an FPCMP→CSET one-instruction materialization. The x86-64 allocator eliminates duplicated end-of-block spill stores and per-instruction CQO re-scans and deep copies; the scheduler models memory dependences precisely instead of treating LEA as a memory barrier. Six additional correctness fixes land: allocator carries visible to post-RA peepholes, disjoint GPR/XMM spill-placeholder index ranges, deterministic AArch64 phi/cold-block handling, shared per-function AArch64 trap blocks, complete conditional-branch successor liveness, and valid Mach-O arm64 compact-unwind encodings.
- **Shared platform adapters.** Socket setup/teardown/flags, CSPRNG selection, and file-dialog directory enumeration each move into dedicated `rt_*_platform_{win,posix}.c` adapters behind `rt_*_platform.h` headers; a ratcheting lint baseline ensures the remaining raw-macro count in shared TUs can only decrease.

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 101 | +101 |
| Source files | 3,096 | 3,337 | +241 |
| Production SLOC | 669K | 719K | +50K |
| Test SLOC | 278K | 296K | +18K |
| Demo SLOC | 192K | 193K | +1K |

Counts via `scripts/count_sloc.sh` (production 719,038 / test 295,880 / demo 193,428 / source files 3,337); commits since the v0.2.6 release (2026-06-01).

---

### Graphics3D rendering

- A sustained pass ends per-frame visible-triangle flicker: queue-order-independent occlusion history with covered-streak gating, camera-depth-fitted shadow cascades, per-camera LOD/impostor hysteresis, and conservative terrain-horizon culling.
- Every backend (software/OpenGL/Metal/D3D11) validates draw-command index ranges and shares one depth-bias scale; D3D11 closes its remaining sign-off gaps and hardens compressed-texture lifetime.
- Shadow-casting spot lights join the shared directional shadow budget: directional lights consume slots first by cascade count, then spot lights fill remaining slots ranked by intensity and camera distance. Spot lights build a single perspective view-projection from position, direction, outer cone angle, and range; all four shader languages (GLSL/HLSL/MSL/software) perspective-divide spot shadow coordinates and suppress contributions outside the cone angle while keeping directional paths orthographic and cascaded.

### 3D assets, animation, and Canvas3D (new)

- New `NodeAnimation3D`/`NodeAnimator3D` types expose node, object, camera, and morph-weight clips, and `Model3D`/`Assets3D` can pull a single clip from an external file (`LoadAnimation`/`LoadNodeAnimation`).
- The FBX importer gains full model transforms (rotation order, pivots, Z-up), cubic curves, PBR texture-slot routing, and async loading; glTF accessor and skin-weight validation tightens.
- `DrawMeshWind` sways foliage with height-weighted vertex deformation on every backend, `DrawImage2D` blits a `Pixels` image into the 2D overlay (HUD minimaps via `RenderTarget3D.AsPixels`), and `SetFullscreen`/`ToggleFullscreen`/`IsFullscreen` plus `Game3D.Keys.F11` drive native fullscreen; `DrawMeshSkinned` accepts an `AnimController3D` pose, and `Material3D` gains depth-bias and `ShadowMode` (auto/none/cast).
- Material3D/Light3D scalar setters (`SetRoughness`/`SetMetallic`/`SetAO`/`SetEmissiveIntensity`/`SetNormalScale`/`SetReflectivity`, `Light3D.SetEnabled`/`SetCastsShadows`) bind as methods alongside their property forms, and `embed`-ed assets now link into native binaries — fixing a duplicate `viper_asset_blob` definition that had silently dropped them.

### Open-world streaming, navigation, and 3D safety

- `WorldStream3D` cells load a binary sidecar into a resident-byte budget, and `NavMesh3D.Export`/`Import` round-trip versioned `VNAVMSH2` assets — geometry, traversal costs, areas, off-mesh links, obstacles, and agent params — while still reading legacy `VNAVMSH1`. Compressed `TextureAsset3D` uploads drain large mips across frames with KTX2 length validation, and stream manifests resolve `asset://` sidecars through the asset manager.
- Non-finite interpolation, playback, and bone inputs across Skeleton3D/AnimController3D/BlendTree3D/IKSolver3D and CPU skinning clamp or revert to the bind pose, and scene, transform, raycast, navigation, physics, and terrain entry points reject bad handles; stored camera/node/character/navmesh references are class-checked, so a reused slot is nulled rather than dereferenced.
- Game3D async loads read worker-safe request snapshots, rebuild the world's body/name indices on detected corruption, keep tree spawn/despawn roll-back-exact, and finalize `ScreenshotFinal`/render-target capture with no present side effect, leaked post-FX state, or stale-identity cache hit. The Scene3D spatial index records a ~1,800× indexed-vs-flat cull speedup on a 10k-node fixture.

### Runtime hardening (fails closed)

- A cppcheck-driven audit makes every recoverable `rt_trap` return a safe sentinel and release its locals across core, text, crypto, network (HTTP/WebSocket/SMTP/TLS/URL), OOP, process, media decoders, and input helpers — and routes the few unrecoverable cases (entropy exhaustion, DRBG failure) through a new non-returning `rt_abort`, so no path can proceed with predictable key material when a trap hook returns.
- BigInt gains checked size arithmetic and full arbitrary-width two's-complement bitwise/shift semantics; heap and pool allocation validate payload headers under the registry lock and freed blocks against their owning slab.
- RSA signature verification goes constant-time; HTTP/2 framing, `204`/`304` empty-body suppression, the WSS accept-vs-`Stop` race, per-policy retry jitter, and SSE encoding negotiation tighten. Temp-file, archive, and savedata names fail closed when secure entropy is unavailable instead of dropping to predictable pid/time values.

### IL, codegen, and the native linker

- Debug-only IL-builder invariants — name uniqueness, operand and block-parameter bounds, branch-argument counts, terminated-block appends — promote to release-mode validation, the parser rolls back cleanly on a partial instruction parse, and a new verifier dataflow catches double-release and use-after-release across the CFG (loops included) while preserving the entry-stack-address dominance contract that frontend cleanup blocks rely on.
- The EH `resumetok` becomes a linear handler-provenance capability (ADR 0005): exception dispatch is its sole producer, forwarding is validated edge by edge, and `resume.*` may consume only the active token that reached its block — new `resume_token_mismatch`, `handler_invalid_entry`, and `resume_token_escape` diagnostics reject forged or stale tokens at verify time instead of leaving them for a backend or VM check. Native EH lowering seeds each pushed handler with its post-dispatch stack so a trap inside a typed-catch, finally, or rethrow helper resolves to a concrete outer native frame, and `resume.label` validates its site token before branching, diverting an invalid edge to a synthetic failure block.
- A VM-neutral `ScalarOps` kernel — checked add/sub/mul, signed/unsigned div-rem, masked shifts, two's-complement wrapping, checked narrowing, half-open `idx.chk` normalization, and round-to-even f64→int casts — now backs the tree-walking VM and both bytecode dispatch engines, so one IL module yields identical values and trap kinds across every execution mode (bytecode format bumped to v3 to carry explicit result widths). `Function::blocks` and `BasicBlock::instructions` move to a stable-address `StableList`, and a module-owned `StringInterner` carries interned `Symbol` handles on functions, blocks, instructions, branch targets, and direct callees so analyses compare compact handles instead of rehashing strings — and the BASIC lowerer drops its now-undefined `block - &blocks[0]` pointer arithmetic for the shared stable-address index helpers.
- Shared `AllocaRoots` helpers make BasicAA conservative for raw and unknown-indirect calls, MemorySSA and DSE fix same-block-overwrite handling, ConstFold/SCCP/Peephole normalize integer folds to i16/i32/i64 result widths, and CheckOpt shares one checked-range implementation with the builder — now demoting proven-safe overflow-checked i64 arithmetic to plain ops. Pass invalidation became state-based and Mem2Reg transactional.
- x86-64/AArch64 selection, encoding, Win64 unwind slots, register-allocation operand protection, and AArch64 relocations surface diagnostics instead of asserting — atop the shared frame-layout fix that closes the O1 miscompiles — the object writers bound their fixed-width name fields, and the native linker hardens relocation ordering, symbol resolution, and PE-image writing.
- **Codegen performance.** A focused pass reduces allocator and scheduler overheads across both backends. AArch64: clobber-aware argument-register eviction unlocks all sixteen previously excluded argument registers (x0-x7/v0-v7) for allocation; spilled-operand reload caching assigns a resident physical register to the first use and reuses it within the block (N frame reloads → 1); a shared pre-RA copy-forwarding pass runs before register allocation on both pipelines. x86-64: duplicated end-of-block spill stores eliminated (the def's suffix store already keeps the slot current), redundant CQO re-scans and per-instruction instruction deep-copies removed. Scheduler: LEA no longer treated as a memory barrier in MemoryOpt (unblocks dependent instruction scheduling), and per-instruction memory-dependence edges modeled precisely. AArch64: FPCMP + branch sequences collapse to a single CSET where the flag consumer is a conditional branch. Six correctness fixes accompany the perf work: allocator-register carries are now visible to post-RA peepholes, GPR and XMM spill-placeholder index ranges are disjoint, the AArch64 allocator is made deterministic across phi-node and cold-block orderings, AArch64 trap blocks are shared per function (bounding block growth), every conditional-branch successor is included in RA liveness, and Mach-O arm64 compact-unwind encodings are valid.

### Bytecode VM and support libraries

- The bytecode VM validates memory ranges and indirect callees before dereference, traps instead of asserting on bad branch/switch targets, and bounds runaway programs via `BytecodeVM.setMaxInstructions` and a signal-safe interrupt; the arena and SmallVector guard size and pointer math.
- Sound attach-state synchronizes under the context lock, ALSA recovers from short writes, and FIFO event-queue overflow becomes a priority policy that drops transient motion events before close/key-up/focus-lost. Standalone context menus register as active app overlays so right-click menus paint and take input, editor monospace fonts survive later app-wide font propagation, printable-punctuation shortcuts (zoom in/out) reach the GUI layer on all three platforms, and glyph caches, file dialogs, and the code editor overflow-guard.

### GUI refined-depth visual pass

- A shared anti-aliased `vg_draw` core — rounded rectangles, discs, lines, soft drop-shadows, gradients, and deterministic fixed-point coverage — plus new radius/elevation/gradient/focus/motion theme tokens route buttons, inputs, dropdowns, menus, dialogs, tooltips, scrollbars, tabs, lists, and the file tree through one rounded, elevated style.
- ViperIDE's toolbar draws vector icons in place of Unicode glyphs, a titled `GUI.GroupBox` card and `GUI.Label.SetWordWrap` rebuild its settings panel, and gated hover/press/focus motion plus wheel-scrollable dialog content finish the pass.

### ViperIDE — debugging, build feedback, and zoom (new)

- A debug adapter replaces v0.2.6's non-executing placeholder: `viper run --debug-adapter` drives pause, continue, step in/over/out, source breakpoints, call stacks, and locals over newline-delimited JSON and stops on unhandled traps before they unwind, while ViperIDE spawns and steps real programs (a source-stepping preview) and feeds the child commands through `Process.Handle.WriteStdin`.
- ViperIDE adds persistent UI zoom (`GUI.App.SetUiScale`/`GetUiScale`), millisecond build-duration and error/warning status that auto-opens Problems on failure, and absolute-path resolution of the `viper` compiler before each build.

### Frontends, language servers, and CLI

- BASIC replaces unchecked numeric, line-label, and `SELECT CASE` conversions with overflow-aware token parsing and accepts numbered `DIM` fields inside `CLASS` bodies; Zia restores circular and self binds by treating an in-progress bind as a known dependency rather than a fatal `V1000`; both frontends harden defaults around lowerer state, runtime-registry lookups, import cycles, and completion-cursor bounds.
- Process, name-mangling, and integer helpers guard against overflow and untrusted input; the Text/Time/IO surface closes documented edge cases (`TextWrap` no-wrap at width ≤ 0, locale-independent `Json.Format`, embedded-NUL rejection in `DateOnly`/`DateTime`, file-only `File.Move`).
- A new agent-facing CLI: `viper check <target> --diagnostic-format=json` is a fast type-check/verify gate (exit 0 clean / 1 usage / 2 compile errors), `viper eval` runs a snippet in one shot with JSON results and a distinct trap exit code, `viper explain <CODE>` and `--print-error-codes` read a central `diag_catalog` of diagnostic codes, and `--dump-runtime-api`/`--dump-opcodes` emit machine-readable registry inventories generated from the live binary.
- The BASIC/Zia LSP/MCP servers compute UTF-16 ranges and reject malformed URIs and duplicate JSON keys — now also handling case-insensitive URI schemes, UNC authorities, and a strict MCP initialize-before-use lifecycle — and route structured diagnostics (code, stage, range, help text, notes, and applicable fix-its) so did-you-mean corrections surface in editor clients; the CLI rejects mismatched run/build and ABI/platform combinations, `il-dis` becomes a bytecode disassembler, and a shared `ScopedProcess` with atomic IL/asset-blob outputs stops the CLI tools from leaking redirected descriptors or environment or leaving partial files on a failed write.

### Project loading and packaging

- Convention detection scans real BASIC/Zia tokens instead of text, rejects sources that escape the project root through symlinks, and gates install hooks and home-Desktop shortcuts behind explicit manifest opt-ins.
- The PNG, ZIP, tar, PE, and DEB readers and writers tighten chunk ordering, header bounds, and path validation; package and archive writers stage through same-directory temp files so a failed write leaves no partial artifact, hash asset content with SHA-256 so a same-size/mtime edit still invalidates the build cache, and detect a staged toolchain binary's OS and architecture from its PE/ELF/Mach-O header rather than the host.

### Windows and Linux builds

- On Windows, NOMINMAX, extended MSVC atomic shims, a simplified CreateProcess stdio path, a standard-VM bridge that no longer double-releases borrowed strings, and an 8-job MSVC parallelism cap (avoiding C1060 heap exhaustion) restore the BASIC-VM, installer, ViperIDE, and x86-64 codegen suites, while the D3D11 backend closes its sign-off gaps.
- On Linux, restored OpenGL symbol loading, X11 backing-store sizing, ELF zero-fill/`PT_LOAD` grouping, and an expanded dynamic-import policy bring the full suite to 1,670 passing.

### Tests

~18K new test SLOC.

- **3D rendering, assets, and animation** — spot-shadow perspective/orthographic coordinate and budget-ordering coverage, vegetation sway, billboard batching, versioned navmesh round-trip, reference repair, texture-atlas, FBX/node-animation import, and D3D11 mip-validation coverage.
- **IL, codegen, and cross-engine** — IL release-lifetime and alias-analysis precision, VM-vs-bytecode scalar-semantics equivalence, AArch64 register-allocation regressions, spilled-operand reload-caching pin test (zero inter-use reloads), and duplicate-spill-store elimination verification.
- **Agent CLI and diagnostics** — the `agent_cli` suite, the diagnostic-code catalog, and structured bridge/MCP/LSP diagnostic and hover coverage.
- **Runtime, frontends, and GUI** — media/pixel decode, BASIC parsing, Text/Time/IO edge cases, and GUI overlay/font-propagation regressions.

---

Demos and docs tracked the work: the `game3d-showcase` gained an F11 fullscreen toggle, wind-swayed foliage, a render-to-image minimap, physics props, wandering sentinels, and spot-shadow coverage; the Graphics3D guides picked up spot-shadow, KTX2, glTF-extension, streaming, navmesh-export, spatial-index, and window/image/foliage/skinned-draw entries; the man pages and codemap documented the agent-facing CLI, diagnostics-code schema, and Zia grammar; and `TextWrap`, JSON depth, file-vs-directory moves, and date parsing were clarified. Structurally: the runtime's largest translation units — input/action, JSON, directory, audio decode, 2D pixels, GameUI, GUI, network, TLS, and regex — split into focused per-feature files behind internal headers; the Zia editor/IntelliSense services moved out of `fe_zia` into `zia_editor_services`; the Zia lowerer's runtime-call coercions and canonical names moved behind a generated, lint-gated facade; a new `viper_text_core` static library and `fe_common` `CollectionMethodCatalog`/`RuntimeMethodResolver` extract the GUI/TUI piece-table text engine and the collection/overload-scoring code shared by both frontends; platform-specific socket, entropy, and file-dialog logic consolidated into `rt_*_platform_{win,posix}.c` adapters with a ratcheting lint baseline; and a codegen backend-consolidation design record and shared utilities archive lay groundwork for merging the x86-64 and AArch64 backend scaffolding. Native linking now tolerates audio-disabled hosts by falling back to the runtime audio stubs.

<!-- END DRAFT -->
