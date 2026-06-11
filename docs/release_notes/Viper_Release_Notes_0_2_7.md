# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A short hardening cycle continuing v0.2.6. The headline new work ends per-frame Graphics3D rendering flicker, closes the open-world ("3D Next Level") streaming gaps, adds node-animation playback over a substantially upgraded glTF/FBX importer, and gives ViperIDE a real VM-backed debugger. Around it, a repo-wide sweep makes recoverable runtime traps fail closed, promotes IL and codegen invariants from debug asserts to release-mode validation, re-skins the GUI toolkit through a shared anti-aliased drawing core, and modularizes the runtime into focused translation units — while the Linux and Windows/MSVC builds both return to green.

- **Graphics3D flicker stabilized.** Queue-order-independent occlusion history with covered-streak gating, camera-depth-fitted shadow cascades, per-camera LOD/impostor hysteresis, and conservative terrain-horizon culling; every backend validates draw-command index ranges and shares one depth-bias scale.
- **glTF/FBX import & node animation (new).** `NodeAnimation3D`/`NodeAnimator3D` play node, object, camera, and morph-weight clips, and `Model3D`/`Assets3D` can pull a single clip from an external file; the FBX importer gains full transforms, cubic curves, PBR routing, and async loading.
- **Canvas3D authoring helpers (new).** `DrawMeshWind` foliage sway, `DrawImage2D` HUD blits, and native `SetFullscreen`/`ToggleFullscreen` (`Game3D.Keys.F11`); `DrawMeshSkinned` takes an `AnimController3D` pose and `Material3D` gains depth-bias and `ShadowMode`.
- **Open-world streaming & navmesh tooling (new).** `WorldStream3D` cells load binary sidecars into a resident-byte budget, `NavMesh3D.Export`/`Import` round-trip versioned `VNAVMSH2`, compressed textures drain large mips across frames, and stream manifests resolve `asset://` sidecars.
- **3D runtime & Game3D fail closed.** Non-finite pose/playback inputs clamp or revert to the bind pose, scene/transform/raycast/physics/navigation entry points reject bad handles, stored references are class-checked, and screenshot/render-target capture finalizes with no present side effect.
- **Runtime fails closed (repo-wide).** A cppcheck-driven audit makes every recoverable `rt_trap` return a safe sentinel and free its locals; unrecoverable entropy/DRBG failures route through a new non-returning `rt_abort`, so no path proceeds with predictable key material when a trap hook returns.
- **IL, codegen & linker hardening.** The IL builder promotes its debug-only invariants to release-mode validation and shares one checked-range implementation with CheckOpt (which now demotes proven-safe overflow-checked arithmetic); codegen and the native linker surface diagnostics instead of asserting, atop the frame-layout fix that closes the O1 miscompiles.
- **Bytecode VM hardening.** The VM validates memory ranges and indirect callees before dereference, traps instead of asserting on bad branch/switch targets, and bounds runaway programs via `BytecodeVM.setMaxInstructions` and a signal-safe interrupt.
- **GUI refined-depth visual pass.** A shared anti-aliased `vg_draw` core and new radius/elevation/gradient/focus/motion theme tokens route the whole widget set through one rounded, elevated style; ViperIDE draws vector toolbar icons and a `GUI.GroupBox` card rebuilds the settings panel.
- **ViperIDE debugging, build feedback & zoom (new).** A VM-backed debug adapter (`viper run --debug-adapter`) replaces v0.2.6's non-executing placeholder with pause, continue, step, source breakpoints, call stacks, and locals over JSON; the editor adds persistent UI zoom, build-duration and error/warning status, and absolute `viper` path resolution.
- **Frontend parsing & semantics.** BASIC adopts overflow-aware numeric, line-label, and `SELECT CASE` parsing and accepts numbered `DIM` fields inside `CLASS` bodies; Zia restores circular and self binds by treating an in-progress bind as a known dependency rather than a fatal `V1000`.
- **Cross-platform utilities, language servers & CLI.** The Text/Time/IO surface closes documented edge cases, the BASIC/Zia LSP/MCP servers compute UTF-16 ranges and enforce a strict initialize-before-use lifecycle, and the CLI rejects mismatched run/build/ABI combinations behind atomic, descriptor-safe tool outputs.
- **Project loading & packaging.** Convention detection scans real BASIC/Zia tokens and rejects symlink escapes; package writers stage through same-directory temp files and hash assets with SHA-256; staged toolchain binaries detect their own OS and architecture from the object header rather than the host.
- **Windows and Linux builds back to green.** Windows restores the BASIC-VM, installer, ViperIDE, and x86-64 suites (NOMINMAX, extended MSVC atomics, an 8-job parallelism cap) and closes its D3D11 sign-off gaps; Linux returns to a full green suite — 1,670 passing.

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 62 | +62 |
| Source files | 3,096 | 3,282 | +186 |
| Production SLOC | 669K | 715K | +46K |
| Test SLOC | 278K | 293K | +15K |
| Demo SLOC | 192K | 193K | +1K |

Counts via `scripts/count_sloc.sh` (production 715,258 / test 293,389 / demo 193,428 / source files 3,282); commits since the v0.2.6 release (2026-06-01).

---

### Graphics3D rendering

- **Flicker stabilization.** A sustained pass ends per-frame visible-triangle flicker: queue-order-independent occlusion history with covered-streak gating, camera-depth-fitted shadow cascades, per-camera LOD/impostor hysteresis, and conservative terrain-horizon culling.
- **Backend correctness.** Every backend (software/OpenGL/Metal/D3D11) validates draw-command index ranges and shares one depth-bias scale; D3D11 closes its remaining sign-off gaps and hardens compressed-texture lifetime.

### 3D assets, animation, and Canvas3D (new)

- **Node animation.** New `NodeAnimation3D`/`NodeAnimator3D` types expose node, object, camera, and morph-weight clips, and `Model3D`/`Assets3D` can pull a single clip from an external file (`LoadAnimation`/`LoadNodeAnimation`).
- **Importers.** The FBX importer gains full model transforms (rotation order, pivots, Z-up), cubic curves, PBR texture-slot routing, and async loading; glTF accessor and skin-weight validation tightens.
- **Canvas3D helpers.** `DrawMeshWind` sways foliage with height-weighted vertex deformation on every backend, `DrawImage2D` blits a `Pixels` image into the 2D overlay (HUD minimaps via `RenderTarget3D.AsPixels`), and `SetFullscreen`/`ToggleFullscreen`/`IsFullscreen` plus `Game3D.Keys.F11` drive native fullscreen. `DrawMeshSkinned` accepts an `AnimController3D` pose; `Material3D` gains depth-bias and `ShadowMode` (auto/none/cast).
- **API completeness.** Material3D/Light3D scalar setters (`SetRoughness`/`SetMetallic`/`SetAO`/`SetEmissiveIntensity`/`SetNormalScale`/`SetReflectivity`, `Light3D.SetEnabled`/`SetCastsShadows`) bind as methods alongside their property forms, and `embed`-ed assets now link into native binaries — fixing a duplicate `viper_asset_blob` definition that had silently dropped them.

### Open-world streaming, navigation, and 3D safety

- **Streaming & navmesh.** `WorldStream3D` cells load a binary sidecar into a resident-byte budget, and `NavMesh3D.Export`/`Import` round-trip versioned `VNAVMSH2` assets — geometry, traversal costs, areas, off-mesh links, obstacles, and agent params — while still reading legacy `VNAVMSH1`. Compressed `TextureAsset3D` uploads drain large mips across frames with KTX2 length validation, and stream manifests resolve `asset://` sidecars through the asset manager.
- **Fail closed.** Non-finite interpolation, playback, and bone inputs across Skeleton3D/AnimController3D/BlendTree3D/IKSolver3D and CPU skinning clamp or revert to the bind pose, and scene, transform, raycast, navigation, physics, and terrain entry points reject bad handles; stored camera/node/character/navmesh references are class-checked, so a reused slot is nulled rather than dereferenced.
- **Game3D robustness.** Game3D async loads read worker-safe request snapshots, rebuild the world's body/name indices on detected corruption, keep tree spawn/despawn roll-back-exact, and finalize `ScreenshotFinal`/render-target capture with no present side effect, leaked post-FX state, or stale-identity cache hit. The Scene3D spatial index records a ~1,800× indexed-vs-flat cull speedup on a 10k-node fixture.

### Runtime hardening (fails closed)

- **Recoverable traps.** A cppcheck-driven audit makes every recoverable `rt_trap` return a safe sentinel and release its locals across core, text, crypto, network (HTTP/WebSocket/SMTP/TLS/URL), OOP, process, media decoders, and input helpers — and routes the few unrecoverable cases (entropy exhaustion, DRBG failure) through a new non-returning `rt_abort`, so no path can proceed with predictable key material when a trap hook returns.
- **Numeric & allocation.** BigInt gains checked size arithmetic and full arbitrary-width two's-complement bitwise/shift semantics; heap and pool allocation validate payload headers under the registry lock and freed blocks against their owning slab.
- **Crypto & network.** RSA signature verification goes constant-time; HTTP/2 framing, `204`/`304` empty-body suppression, the WSS accept-vs-`Stop` race, per-policy retry jitter, and SSE encoding negotiation tighten. Temp-file, archive, and savedata names fail closed when secure entropy is unavailable instead of dropping to predictable pid/time values.

### IL, codegen, and the native linker

- **IL builder & verifier.** Debug-only invariants — name uniqueness, operand and block-parameter bounds, branch-argument counts, terminated-block appends — promote to release-mode validation, the parser rolls back cleanly on a partial instruction parse, and a new verifier dataflow catches double-release and use-after-release across the CFG (loops included) while preserving the entry-stack-address dominance contract that frontend cleanup blocks rely on.
- **Optimizer & analysis.** Shared `AllocaRoots` helpers make BasicAA conservative for raw and unknown-indirect calls, MemorySSA and DSE fix same-block-overwrite handling, ConstFold/SCCP/Peephole normalize integer folds to i16/i32/i64 result widths, and CheckOpt shares one checked-range implementation with the builder — now demoting proven-safe overflow-checked i64 arithmetic to plain ops. Pass invalidation became state-based and Mem2Reg transactional.
- **Codegen & linker.** x86-64/AArch64 selection, encoding, Win64 unwind slots, register-allocation operand protection, and AArch64 relocations surface diagnostics instead of asserting — atop the shared frame-layout fix that closes the O1 miscompiles — the object writers bound their fixed-width name fields, and the native linker hardens relocation ordering, symbol resolution, and PE-image writing.

### Bytecode VM and support libraries

- **Bytecode VM.** The bytecode VM validates memory ranges and indirect callees before dereference, traps instead of asserting on bad branch/switch targets, and bounds runaway programs via `BytecodeVM.setMaxInstructions` and a signal-safe interrupt; the arena and SmallVector guard size and pointer math.
- **Audio/graphics/GUI audit.** Sound attach-state synchronizes under the context lock, ALSA recovers from short writes, and FIFO event-queue overflow becomes a priority policy that drops transient motion events before close/key-up/focus-lost. Standalone context menus register as active app overlays so right-click menus paint and take input, editor monospace fonts survive later app-wide font propagation, printable-punctuation shortcuts (zoom in/out) reach the GUI layer on all three platforms, and glyph caches, file dialogs, and the code editor overflow-guard.

### GUI refined-depth visual pass

- **Shared AA core.** A shared anti-aliased `vg_draw` core — rounded rectangles, discs, lines, soft drop-shadows, gradients, and deterministic fixed-point coverage — plus new radius/elevation/gradient/focus/motion theme tokens route buttons, inputs, dropdowns, menus, dialogs, tooltips, scrollbars, tabs, lists, and the file tree through one rounded, elevated style.
- **ViperIDE chrome.** The toolbar draws vector icons in place of Unicode glyphs, a titled `GUI.GroupBox` card and `GUI.Label.SetWordWrap` rebuild the settings panel, and gated hover/press/focus motion plus wheel-scrollable dialog content finish the pass.

### ViperIDE — debugging, build feedback, and zoom (new)

- **VM-backed debugger.** A debug adapter replaces v0.2.6's non-executing placeholder: `viper run --debug-adapter` drives pause, continue, step in/over/out, source breakpoints, call stacks, and locals over newline-delimited JSON and stops on unhandled traps before they unwind, while ViperIDE spawns and steps real programs (a source-stepping preview) and feeds the child commands through `Process.Handle.WriteStdin`.
- **Editor feedback.** Persistent UI zoom (`GUI.App.SetUiScale`/`GetUiScale`), millisecond build-duration and error/warning status that auto-opens Problems on failure, and absolute-path resolution of the `viper` compiler before each build.

### Frontends, language servers, and CLI

- **Frontend parsing.** BASIC replaces unchecked numeric, line-label, and `SELECT CASE` conversions with overflow-aware token parsing and accepts numbered `DIM` fields inside `CLASS` bodies; Zia restores circular and self binds by treating an in-progress bind as a known dependency rather than a fatal `V1000`; both frontends harden defaults around lowerer state, runtime-registry lookups, import cycles, and completion-cursor bounds.
- **Utilities & Text/Time/IO.** Process, name-mangling, and integer helpers guard against overflow and untrusted input; the Text/Time/IO surface closes documented edge cases (`TextWrap` no-wrap at width ≤ 0, locale-independent `Json.Format`, embedded-NUL rejection in `DateOnly`/`DateTime`, file-only `File.Move`).
- **Language servers & CLI.** The BASIC/Zia LSP/MCP servers compute UTF-16 ranges and reject malformed URIs and duplicate JSON keys — now also handling case-insensitive URI schemes, UNC authorities, and a strict MCP initialize-before-use lifecycle; the CLI rejects mismatched run/build and ABI/platform combinations, `il-dis` becomes a bytecode disassembler, and a shared `ScopedProcess` with atomic IL/asset-blob outputs stops the CLI tools from leaking redirected descriptors or environment or leaving partial files on a failed write.

### Project loading and packaging

- **Project loading.** Convention detection scans real BASIC/Zia tokens instead of text, rejects sources that escape the project root through symlinks, and gates install hooks and home-Desktop shortcuts behind explicit manifest opt-ins.
- **Packaging.** The PNG, ZIP, tar, PE, and DEB readers and writers tighten chunk ordering, header bounds, and path validation; package and archive writers stage through same-directory temp files so a failed write leaves no partial artifact, hash asset content with SHA-256 so a same-size/mtime edit still invalidates the build cache, and detect a staged toolchain binary's OS and architecture from its PE/ELF/Mach-O header rather than the host.

### Windows and Linux builds

- **Windows.** NOMINMAX, extended MSVC atomic shims, a simplified CreateProcess stdio path, a standard-VM bridge that no longer double-releases borrowed strings, and an 8-job MSVC parallelism cap (avoiding C1060 heap exhaustion) restore the BASIC-VM, installer, ViperIDE, and x86-64 codegen suites, while the D3D11 backend closes its sign-off gaps.
- **Linux.** Restored OpenGL symbol loading, X11 backing-store sizing, ELF zero-fill/`PT_LOAD` grouping, and an expanded dynamic-import policy bring the full suite to 1,670 passing.

### Tests

- New tests close the vegetation, billboard, navmesh round-trip, reference-repair, texture-atlas, FBX/node-animation import, D3D11 mip-validation, media/pixel-decode, BASIC parsing, and Text/Time/IO gaps, plus IL release-lifetime and alias-analysis precision, AArch64 register-allocation, and GUI overlay/font-propagation regressions.

---

Demos and docs tracked the work: the `game3d-showcase` gained an F11 fullscreen toggle, wind-swayed foliage, a render-to-image minimap, physics props, and wandering sentinels, while the Graphics3D guides and Viper library docs picked up KTX2, glTF-extension, streaming, navmesh-export, spatial-index, and window/image/foliage/skinned-draw entries and clarified `TextWrap`, JSON depth, file-vs-directory moves, and date parsing. Structurally, the runtime's largest translation units — input/action, JSON, directory, audio decode, 2D pixels, GameUI, GUI, network, TLS, and regex among them — split into focused per-feature files behind internal headers.

<!-- END DRAFT -->
