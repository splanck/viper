# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A hardening cycle continuing v0.2.6, with focused new 3D and tooling capability. The new work ends per-frame Graphics3D flicker, adds spot-light shadow maps and a deterministic multi-threaded software rasterizer, plays node animation over a rebuilt glTF/FBX importer, gives ViperIDE a VM-backed debugger, and opens a machine-readable agent-facing CLI. The hardening backbone makes recoverable traps and asset loaders fail closed, promotes IL and codegen invariants to release-mode validation, unifies scalar semantics across the tree-walking VM and both bytecode engines, re-skins the GUI through a shared anti-aliased core, and lands a codegen performance round — while Linux and Windows/MSVC both return to green.

- **Graphics3D flicker stabilized.** Occlusion history with covered-streak gating, depth-fitted shadow cascades, LOD/impostor hysteresis, and terrain-horizon culling end per-frame triangle flicker; every backend validates draw-command index ranges.
- **Spot-light shadow maps (new).** Shadow-casting spot lights share the directional shadow budget across all four backends, sampling with a perspective divide and cone-angle suppression.
- **glTF/FBX import & node animation (new).** `NodeAnimation3D`/`NodeAnimator3D` play node, camera, and morph-weight clips; the FBX importer gains full transforms, cubic curves, PBR routing, and async loading.
- **Deterministic software rasterizer.** A worker-pool rasterizer reproduces the single-threaded image bit-for-bit, so headless and CI renders stay reproducible while throughput scales with cores.
- **Open-world streaming & navmesh tooling (new).** `WorldStream3D` cells load into a resident-byte budget, `NavMesh3D.Export`/`Import` round-trip versioned `VNAVMSH2`, and stream manifests resolve `asset://` sidecars.
- **Runtime fails closed (repo-wide).** A cppcheck-driven audit makes every recoverable `rt_trap` return a safe sentinel; unrecoverable entropy/DRBG failures route through a non-returning `rt_abort` so no path proceeds with predictable key material.
- **Asset loaders fail closed (new).** Corrupt, truncated, or oversized 2D images and 3D models/scenes return `null` with queryable diagnostics; a shared count guard rejects any element count larger than its backing bytes, and libFuzzer harnesses keep the parsers crash-free.
- **Game3D degradation diagnostics (new).** A process-wide `Game3D.Diagnostics` surface counts the rare correct-but-degraded fallbacks (brute-force broadphase, clamped CCD, evicted audio voices, navmesh-grid fallback) so smoke probes can assert a clean run.
- **Backend & audio telemetry (new).** Opt-in `Canvas3D` backend counters (draws, dropped draws, mesh-cache hits/misses, stream uploads, fallback binds, active presentation path) and an audio-mixer diagnostics surface (renders, partial writes, xruns, recoveries, write failures) make a degraded render or audio path measurable rather than silent.
- **IL & codegen invariants validate in release.** The IL builder promotes debug-only invariants to release-mode validation, the verifier makes the EH `resumetok` a linear handler-provenance capability (ADR 0005), and codegen and the linker surface diagnostics instead of asserting.
- **Codegen performance round.** The AArch64 allocator unlocks all sixteen argument registers and caches spilled-operand reloads, and shares pre-RA copy-forwarding with x86-64; six correctness fixes accompany the perf work.
- **Unified scalar semantics (new).** A VM-neutral kernel makes the tree-walking VM and both bytecode engines yield identical values and trap kinds for one IL module; block/instruction storage moves to stable-address containers with interned identifiers.
- **GUI refined-depth visual pass.** A shared anti-aliased `vg_draw` core and new theme tokens route the whole widget set through one rounded, elevated style; ViperIDE gains vector toolbar icons and a `GUI.GroupBox` settings card.
- **ViperIDE debugging & build feedback (new).** A VM-backed debug adapter (`viper run --debug-adapter`) replaces v0.2.6's non-executing placeholder with pause/continue/step, breakpoints, call stacks, and locals; the editor adds persistent UI zoom and build-duration/error status.
- **Agent-facing CLI (new).** `viper check`, `viper eval`, and `viper explain`/`--print-error-codes` over a central diagnostic-code catalog, plus `--dump-runtime-api`/`--dump-opcodes` registry dumps; the LSP/MCP servers now carry the same structured diagnostics.
- **Windows and Linux builds back to green.** Windows restores the BASIC-VM, installer, ViperIDE, and x86-64 suites and closes its D3D11 sign-off gaps; Linux returns to a full green suite — 1,670 passing.

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 143 | +143 |
| Source files | 3,096 | 3,361 | +265 |
| Production SLOC | 669K | 734K | +65K |
| Test SLOC | 278K | 300K | +22K |
| Demo SLOC | 192K | 194K | +2K |

Counts via `scripts/count_sloc.sh` (production 733,549 / test 299,899 / demo 194,156 / source files 3,361); commits since the `v0.2.6-dev` tag (2026-06-01).

---

### Graphics3D rendering

- A sustained pass ends per-frame visible-triangle flicker: queue-order-independent occlusion history with covered-streak gating, camera-depth-fitted shadow cascades, per-camera LOD/impostor hysteresis, and conservative terrain-horizon culling.
- Every backend (software/OpenGL/Metal/D3D11) validates draw-command index ranges and shares one depth-bias scale. D3D11 closes its sign-off gaps and routes uploads and screenshot readback through shared helpers that reject oversized, unaligned, or stale GPU state, and clamps post-FX scalar/enum inputs and effect-chain capacity before indexed passes; OpenGL drains GPU errors after every upload and bounds its texture/mesh caches by age and LRU.
- The software backend rasterizes across a deterministic worker pool — parallel scanline fills that reproduce the single-threaded image bit-for-bit — so headless and CI renders stay reproducible while throughput scales with cores.
- Shadow-casting spot lights join the shared directional shadow budget: a single perspective light view-projection drives all four shader languages, which perspective-divide spot coordinates and suppress contributions outside the cone while directional paths stay orthographic and cascaded.
- Anisotropic filtering arrives as `Material3D.Anisotropy` (clamped 1–16, advertised via `BackendSupports("anisotropy")`), and `Canvas3D` exposes per-frame submission telemetry (`DrawsSubmitted`, `SortPasses`, `BackendStateChanges`) with stable radix/bucket sorts over reusable scratch. Opt-in backend counters add draw/dropped-draw, mesh-cache hit/miss, stream-upload, and fallback-bind totals plus the active presentation path, and a `VIPER_OPENGL_PRESENT` override compares the Linux GL direct, probe, and offscreen paths during triage.

### 3D assets, animation, and Canvas3D (new)

- `NodeAnimation3D`/`NodeAnimator3D` play node, object, camera, and morph-weight clips, and `Model3D`/`Assets3D` can pull a single clip from an external file.
- The FBX importer gains full model transforms (rotation order, pivots, Z-up), cubic curves, PBR texture-slot routing, and async loading; glTF accessor and skin-weight validation tightens.
- New authoring helpers: `DrawMeshWind` foliage sway, `DrawImage2D` HUD blits, `DrawMeshSkinned` from an `AnimController3D` pose, native `SetFullscreen`/`ToggleFullscreen` (`Game3D.Keys.F11`), and `Material3D` depth-bias/`ShadowMode`.
- `Canvas3D` adds `BackendName` and `BackendFallback` (true when init fell back to software), and a recoverable texture-fallback diagnostic records when a failed upload substitutes a placeholder, so a degraded frame is observable rather than silent.
- Content loaders stop trapping on bad content: every 2D image and 3D model/scene loader returns `null` and records a thread-local last-error code/message (`Assets3D.LastLoadError`), reserving traps for null or invalid handles.
- A shared `rt_untrusted_count` guard validates every element count read from a 3D asset against its backing bytes across the glTF, FBX, OBJ/STL, Scene3D, and Game3D loaders, and new libFuzzer harnesses keep these parsers crash-free.

### Open-world streaming, navigation, and 3D safety

- `WorldStream3D` cells load a binary sidecar into a resident-byte budget; `NavMesh3D.Export`/`Import` round-trip versioned `VNAVMSH2` assets while still reading legacy `VNAVMSH1`; compressed `TextureAsset3D` uploads drain large mips across frames.
- Non-finite interpolation, playback, and bone inputs across the animation stack clamp or revert to the bind pose; scene, transform, raycast, navigation, and physics entry points reject bad handles; stored references are class-checked so a reused slot is nulled rather than dereferenced.
- Game3D async loads read worker-safe snapshots, drop stale publishes so a cancelled load can't overwrite current state, and finalize screenshot/render-target capture with no present side effect. The Scene3D spatial index records a ~1,800× indexed-vs-flat cull speedup on a 10k-node fixture.
- A retained `Entity3D` whose entity was despawned degrades predictably — neutral reads, no-op writes, a counted `StaleEntityCalls` touch — instead of trapping; genuinely invalid handles still trap.
- Fallbacks that stay correct but shed fidelity are now observable: a process-wide `Viper.Game3D.Diagnostics` static exposes saturating counters (brute-force broadphase, clamped CCD, dropped animation events, evicted audio voices, navmesh-grid fallback, stale-entity touches) with a zero-omitting `Summary()`, and `Physics3DWorld` surfaces the same physics counts per world.
- `Physics3DWorld` adds fixed-step controls (explicit timestep and substep budget) so the simulation advances deterministically and independently of render frame rate.

### Runtime hardening (fails closed)

- A cppcheck-driven audit makes every recoverable `rt_trap` return a safe sentinel and release its locals across core, text, crypto, network, OOP, process, media decoders, and input helpers — and routes unrecoverable entropy/DRBG failures through a non-returning `rt_abort`, so no path proceeds with predictable key material.
- BigInt gains checked size arithmetic and full arbitrary-width two's-complement bitwise/shift semantics; heap and pool allocation validate payload headers under the registry lock, and the pool freelist takes a short spinlock so a concurrent pop never reads a stale next pointer.
- Text-format parsers fail closed and reach further: CSV/JSON preserve parse-failure state through trap recovery, HTML/XML grow traversal stacks under overflow checks, Markdown normalizes entity-obfuscated URL schemes, and TOML extends to escapes, inline tables, and arrays of tables.
- RSA verification goes constant-time; HTTP/2 framing, empty-body suppression, the WSS accept-vs-`Stop` race, and retry jitter tighten. Temp-file, archive, and savedata names fail closed when secure entropy is unavailable, and the GIF and Theora/OGV decoders roll back per-frame so a corrupt packet leaves decoder state intact.

### IL, codegen, and the native linker

- Debug-only IL-builder invariants — name uniqueness, operand and block-parameter bounds, branch-argument counts — promote to release-mode validation, and a new verifier dataflow catches double-release and use-after-release across the CFG.
- The EH `resumetok` becomes a linear handler-provenance capability (ADR 0005): exception dispatch is its sole producer, and new `resume_token_mismatch`/`handler_invalid_entry`/`resume_token_escape` diagnostics reject forged or stale tokens at verify time. Native EH lowering seeds each handler with its post-dispatch stack so a trap inside a catch/finally resolves to a concrete outer frame.
- A VM-neutral `ScalarOps` kernel now backs the tree-walking VM and both bytecode engines, so one IL module yields identical values and trap kinds across every execution mode (bytecode format bumped to v3). `Function::blocks`/`BasicBlock::instructions` move to a stable-address `StableList` with interned `Symbol` handles so references survive insertion and erasure.
- Shared `AllocaRoots` helpers make alias analysis conservative for unknown calls, ConstFold/SCCP/Peephole normalize integer folds to fixed result widths, and CheckOpt shares one checked-range implementation with the builder — now demoting proven-safe overflow-checked i64 arithmetic to plain ops.
- Selection, encoding, Win64 unwind, and AArch64 relocations surface diagnostics instead of asserting — atop the shared frame-layout fix that closes the O1 miscompiles — and the native linker hardens relocation ordering, symbol resolution, and PE-image writing.
- **Codegen performance.** AArch64 unlocks all sixteen argument registers via clobber-aware eviction and collapses N block reloads to one resident home; x86-64 eliminates duplicated end-of-block spill stores, models memory dependences precisely, and stops treating LEA as a barrier. Six correctness fixes accompany the work.

### Bytecode VM and support libraries

- The bytecode VM validates memory ranges and indirect callees before dereference, traps instead of asserting on bad branch/switch targets, and bounds runaway programs via `BytecodeVM.setMaxInstructions` and a signal-safe interrupt.
- Game3D loop helpers (`run`/`runFixed`/overlay variants) now accept script function references from both the tree-walking and bytecode VMs, not only raw native pointers — the bytecode VM invokes these callbacks re-entrantly while a native call is suspended, so script-defined loops drive the engine under either VM.
- Sound gains an audio-mixer diagnostics surface (render calls, partial writes, xruns, recoveries, unrecovered failures) and a reworked ALSA backend with explicit hw/sw PCM configuration and transient wait/recovery; attach-state synchronizes under the context lock, and event-queue overflow drops transient motion events before close/key-up/focus-lost; glyph caches, file dialogs, and the code editor gain overflow guards.

### GUI refined-depth visual pass

- A shared anti-aliased `vg_draw` core — rounded rectangles, discs, lines, soft shadows, gradients, deterministic fixed-point coverage — plus new radius/elevation/gradient/focus/motion theme tokens route buttons, inputs, dropdowns, menus, dialogs, tooltips, scrollbars, tabs, lists, and the file tree through one rounded, elevated style.
- ViperIDE draws vector toolbar icons in place of Unicode glyphs, a titled `GUI.GroupBox` card and `GUI.Label.SetWordWrap` rebuild its settings panel, and gated hover/press/focus motion plus wheel-scrollable dialog content finish the pass.

### ViperIDE — debugging, build feedback, and zoom (new)

- A debug adapter replaces v0.2.6's non-executing placeholder: `viper run --debug-adapter` drives pause, continue, step in/over/out, source breakpoints, call stacks, and locals over newline-delimited JSON and stops on unhandled traps before they unwind.
- ViperIDE adds persistent UI zoom (`GUI.App.SetUiScale`), millisecond build-duration and error/warning status that auto-opens Problems on failure, and absolute-path resolution of the `viper` compiler before each build.

### Frontends, language servers, and CLI

- BASIC adopts overflow-aware numeric, line-label, and `SELECT CASE` parsing and accepts numbered `DIM` fields inside `CLASS` bodies; Zia restores circular and self binds by treating an in-progress bind as a known dependency rather than a fatal `V1000`.
- The Text/Time/IO surface closes documented edge cases (`TextWrap` at width ≤ 0, locale-independent `Json.Format`, embedded-NUL rejection in `DateOnly`/`DateTime`).
- A new agent-facing CLI: `viper check` is a fast type-check/verify gate (exit 0/1/2), `viper eval` runs a snippet in one shot with JSON results, `viper explain`/`--print-error-codes` read a central `diag_catalog`, and `--dump-runtime-api`/`--dump-opcodes` emit registry inventories from the live binary.
- The BASIC/Zia LSP/MCP servers compute UTF-16 ranges, enforce a strict initialize-before-use lifecycle, and route structured diagnostics (code, stage, range, help, fix-its) so did-you-mean corrections reach editor clients; the CLI rejects mismatched run/build/ABI combinations behind atomic, descriptor-safe outputs.

### Project loading and packaging

- Convention detection scans real BASIC/Zia tokens, rejects sources that escape the project root through symlinks, and gates install hooks behind explicit manifest opt-ins.
- Package and archive writers stage through same-directory temp files so a failed write leaves no partial artifact, hash assets with SHA-256, and detect a staged binary's OS/architecture from its object header rather than the host; ZIP archives stamp a fixed date by default for byte-identical output.
- The Windows toolchain installer becomes a native dialog wizard (license display, user-vs-machine scope) with a native ARM64 bootstrap; Linux gains a self-extracting, FUSE-less AppImage toolchain package; and macOS packaging adds a styled `.dmg` wrapping the `.pkg`.

### Windows and Linux builds

- On Windows, NOMINMAX, extended MSVC atomic shims, a standard-VM bridge that no longer double-releases borrowed strings, and an 8-job MSVC parallelism cap restore the BASIC-VM, installer, ViperIDE, and x86-64 suites, while the D3D11 backend closes its sign-off gaps.
- On Linux, restored OpenGL symbol loading, X11 backing-store sizing, and ELF zero-fill/`PT_LOAD` grouping bring the full suite to 1,670 passing; Linux now defaults to the deterministic software backend for reproducible headless and CI renders, with the hardened OpenGL backend explicitly selectable.

### Tests

~21K new test SLOC. CTest resource locks now serialize tests that share generated codegen or VM-trace artifacts, keeping the suite parallel-safe.

- **3D rendering, assets, and animation** — spot-shadow coordinate and budget-ordering coverage, vegetation sway, versioned navmesh round-trip, FBX/node-animation import, D3D11 upload/readback-helper validation, fail-closed content-loader diagnostics, untrusted-count guard and parser fuzz harnesses, and `Game3D.Diagnostics` fallback coverage.
- **IL, codegen, and cross-engine** — IL release-lifetime and alias-analysis precision, VM-vs-bytecode scalar-semantics equivalence, AArch64 register-allocation regressions, duplicate-spill-store elimination, and Game3D script-callback parity across interpreted and native runs.
- **Agent CLI and diagnostics** — the `agent_cli` suite, the diagnostic-code catalog, and structured bridge/MCP/LSP diagnostic and hover coverage.
- **Runtime, frontends, and GUI** — media/pixel decode, BASIC parsing, Text/Time/IO edge cases, GUI overlay/font-propagation regressions, and Linux AppImage / Windows installer-wizard packaging coverage.

---

Demos and docs tracked the work: `game3d-showcase` gained F11 fullscreen, wind-swayed foliage, a render-to-image minimap, spot shadows, a renderer/audio counter HUD, and a procedural fallback forest for its missing optional tree asset, while the Graphics3D guides, man pages, and codemap documented spot shadows, backend-fallback observability, streaming, navmesh export, and the agent-facing CLI. Structurally, the runtime's largest translation units split into focused per-feature files, the Zia editor/IntelliSense services moved into `zia_editor_services` with runtime-extern registration collapsed into a metadata table, and socket/entropy/file-dialog logic consolidated into `rt_*_platform_{win,posix}.c` adapters.

<!-- END DRAFT -->
