---
status: active
audience: public
last-verified: 2026-07-19
---

# Zanna Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Zanna is under active development and not ready for production use.

## Version 0.2.99 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.7 was cut on 2026-06-30. -->

### What this release is about

The platform has a new name: **Zanna** (formerly Viper). The CLI is `zanna`, programs bind `Zanna.*`, project files use `.zap`, asset archives use `.zpak`, and the project now lives at github.com/zannagames/zanna (ADR 0110). The IDE is renamed too — ZannaIDE is now **Zanna Studio**, built as the `zannastudio` binary, and it migrates your existing settings automatically the first time you open it (ADR 0118).

Beyond the rename, this release is about making Zanna easier to build on. The runtime's public API settles on one predictable naming and error-handling scheme, so you spend less time guessing what a method is called or how it behaves when something goes wrong. The 3D stack grows from a renderer into a real-time game engine: new first-person and third-person runtimes give you enough to stand up a character-action game.

Zanna Studio and the GUI toolkit each take a top-to-bottom modernization pass, and packaging gains real installers on every platform. Underneath, four deep correctness audits — runtime, IL, Windows, and Linux — make the whole stack more predictable under bad input, heavy load, and concurrency.

### Highlights

- **One canonical name for everything.** Every public API symbol now has a single name — sizes are `Count`, semantic lengths are `Length`, abbreviations are spelled out — so the name you guess is usually the one that works. Old names stay as aliases, so existing code keeps compiling.
- **Errors you can handle instead of crash on.** Recoverable failures — a read past end-of-input, a decrypt that doesn't authenticate, a lookup that finds nothing — now return `Option`/`Result` values instead of nulls or `-1` sentinels.
- **Your editor shows the real docs.** Completion and hover, plus the LSP and MCP servers, now surface the runtime's own documentation, types, and stability for every entry; `zanna --dump-runtime-api` emits the same contract for your own tooling.
- **The 3D renderer looks like a modern engine.** Image-based lighting, clustered lights, screen-space reflections, soft particles, and temporal anti-aliasing land on all four backends; reversed-Z depth ends far-distance shimmer in open worlds.
- **First- and third-person game runtimes (new).** View-model rendering, point-light shadows, and fog for first-person games; a full third-person suite — spring-arm camera, character dynamics, combat, cloth, AI behavior trees, quests, world persistence — for character-action games.
- **A real 3D asset pipeline.** Draco and meshopt compression, extended PBR materials, HDR textures, automatic LOD chains, and 1,024-bone rigs, decoded entirely by in-tree code — plus a `zanna asset` tool that conditions assets offline so load-time work moves to build time.
- **The combat lag ramp is fixed.** A broadphase cache bug made sustained 3D combat slow down over ~15 seconds; queries now rebuild lazily, so extended fights hold their frame rate.
- **Faster generated code.** Dense `switch` statements compile to jump tables, narrow overflow checks use native flag-setting instructions, and provably-safe overflow checks are dropped entirely — including a hardened loop strength-reduction pass that fires only when it can prove the arithmetic never overflows.
- **A ChaCha20-Poly1305 interoperability fix.** The AEAD path previously produced non-interoperable tags for some keys; encrypted data now round-trips correctly with other RFC 8439 implementations.
- **The docs match the runtime.** Every logged mismatch between the documentation and the implementation — 283 in all — was fixed on whichever side was wrong, so what the docs promise is what the runtime does.
- **Zanna Studio becomes a multi-root workbench.** Split panes, crash recovery, a full VT terminal, async git, a grouped-Variables debugger, and an in-editor command overlay for Go To Line, rename, and workspace-symbol lookup.
- **Zanna Studio gets the Zanna brand and a depth pass.** Contrast-checked dark and light themes, scalable vector icons, ligature-capable text, project-wide replace with side-by-side diffs, a new-project wizard, and rebindable shortcuts. The debugger now expands objects into named fields, the terminal runs full-screen console programs, and source control browses commit history — with Windows screen-reader support throughout.
- **The GUI toolkit modernizes end to end.** Scalable per-app themes, Unicode grapheme editing with IME support on all three platforms, native screen-reader adapters, and virtual list/tree models for large data — all behind the existing public API, so your GUI code keeps working.
- **Zia gains list combinators.** `map`, `filter`, `reduce`, `firstWhere`, `any`, `all`, and `sum` infer their lambda types from the element type and compile to inline loops — no closures, no overhead.
- **Real installers on every platform.** The Windows toolchain gets a native, transactional installer with rollback and repair; standalone apps package as AppImage, RPM, DMG, and Windows installers; every artifact is verified, checksummed, and manifested before it ships.
- **A deep runtime reliability audit.** Sixty-four fixes across memory management, concurrency, archives, and networking keep long-running and multi-threaded programs predictable under load and teardown.
- **`.zpak` archives get integrity checks.** ZPAK v2 adds per-entry CRC-32 checksums, so a corrupt asset archive is caught at load instead of silently used; v1 archives still read.
- **The IL layer hardens against hostile input.** The IL parser enforces resource limits and fails cleanly on malformed text, floating-point folding follows IEEE-754 exactly, and analyses scale to much larger functions.
- **Windows builds run on PowerShell.** Every Windows build and test entry point is now a PowerShell script (`build_zanna_win.ps1` and friends), replacing the old batch files.

### By the Numbers

| Metric | v0.2.7 | v0.2.99 | Delta |
|---|---|---|---|
| Commits | — | 128 | +128 |
| Source files | 3,402 | 3,579 | +177 |
| Production SLOC | 762K | 897K | +135K |
| Test SLOC | 304K | 344K | +40K |
| Zanna Studio SLOC | 28K | 40K | +12K |
| Demo SLOC | 197K | 239K | +42K |

Counts via `scripts/count_sloc.sh` (production 897,243 / test 344,098 / demo 238,941 / zannastudio 39,733 / source files 3,579); commits since the `v0.2.7-dev` tag (2026-06-30). Much of the raw diff is checked-in text-glTF character and model assets, which the SLOC figures exclude.

---

### Runtime public API and contract

- Every public symbol settles on one canonical name: collection sizes read as `Count`, semantic lengths as `Length`, boolean probes return true/false, and `Entity3D` positions are plain properties. Terse abbreviations spell themselves out (`LeadZ` → `CountLeadingZeros`). Old names remain as aliases, so existing code keeps compiling.
- Recoverable failures return values you can branch on: terminal reads, collection and channel pops, decryption, HTTP/REST/SMTP sends, parsing, searches, shell commands, and 3D queries all gain `Option`/`Result` forms. Error handling becomes ordinary control flow instead of null checks and sentinel comparisons.
- The sharp edges are labelled: manual memory, trap-state mutation, and legacy ciphers move into plainly named `Runtime.Unsafe` and `Crypto.Legacy` homes, so nothing dangerous hides behind an innocent name.
- The 17,000-line runtime registry splits into documented, domain-oriented fragments, each entry carrying authored documentation (ADR 0101). That documentation now reaches you directly — in editor completion and hover, in the LSP and MCP servers, and in `zanna --dump-runtime-api`, which also records each 3D binding's ownership, nullability, and fallibility (ADR 0102).

### 3D rendering

- Image-based lighting, a clustered forward+ light path, screen-space reflections, soft particles, and temporal anti-aliasing land across Metal, OpenGL, D3D11, and the software backend — so a scene lit and post-processed on one platform looks the same on the others.
- GPU backends render reversed-Z float depth: precision concentrates in the distance, so large open-world clip ranges stop z-fighting and shimmering. The software backend stays standard-depth as the deterministic reference.
- The occlusion system rasterizes real triangles instead of bounding boxes, so near walls and rotated geometry actually cull what hides behind them — fewer wasted draws in dense scenes.
- A flicker pass fixes two structural races in Metal frame pacing and transient buffers, corrects `Quat.FromEuler` to the documented axis order, and stops instanced forests and particles from popping as a block.
- On Windows, the D3D11 backend clamps every shader-facing constant buffer to safe values before upload and now supports the full BC compressed-texture family, closing a class of driver-dependent rendering glitches.
- New observability surfaces — backend fallback reasons, dropped-draw counts, mesh memory budgets — plus `TrySet…` setters and accurate `BackendSupports` probes let your code query a capability instead of trapping on a backend that lacks it (ADRs 0069–0071).

### 3D assets, animation, and the asset pipeline

- The in-tree importer now decodes Draco, meshopt, and Basis Universal compressed glTF, quantized attributes, and KTX2 textures — assets exported from standard DCC tools load directly, and malformed streams fail with named diagnostics instead of crashing.
- A 48-item Graphics3D hardening pass makes glTF/GLB, KTX2, and FBX parsing strict and transactional; gives ASCII FBX the same typed multi-object/rig/animation pipeline as binary; retains exact bone names and animation ticks; caches immutable mesh/tangent/ray data; and makes rendering, physics, navigation, terrain, vegetation, and particle work bounded, failure-atomic, and observable (ADR 0139).
- Materials extend to the full PBR set — clearcoat, sheen, anisotropy, transmission, volume — plus signed/unsigned BC6H HDR textures, across all four backends.
- Animation scales up: skeletons to 1,024 bones, up to eight influences per vertex with GPU skinning, cubic-spline tangents, and tolerance-based clip compression.
- `GenerateLODs` builds bounded LOD chains automatically, and `AssetDiagnostics3D.GetImportReport` tells you exactly what an import skipped or truncated, as structured JSON.
- A new `zanna asset bake` / `zanna asset validate` tool conditions assets offline, moving load-time work to build time.

### First- and third-person game runtimes

- The first-person pass turns the renderer into an FPS-capable stack: view-model rendering, point-light shadows, height fog, auto-exposure and color-grading post-FX, raw/relative mouse input, and Doppler-fed spatial audio with occlusion and ducking.
- The third-person suite adds the pieces a character-action game needs: a spring-arm camera with target-lock, character dynamics over moving platforms, combat volumes with health and ragdoll, cloth, cinematics with dialogue, AI perception and behavior trees, minimaps, quests, world persistence, and a baked-GI/reflection-probe/procedural-sky lighting round. Every subsystem ships with a VM-versus-native equivalence probe, so it behaves the same interpreted and compiled (ADRs 0074–0100).
- A raycast-suspension `Vehicle3D` with an engine/brake/steering model joins the stack, and `Game.UI` widgets now draw against either a 2D `Canvas` or a `Canvas3D` (ADR 0065).

### Physics and runtime correctness

- The combat lag ramp is gone: the spatial-query broadphase no longer rebuilds its cache on every pose change, so a fight that used to degrade over ~15 seconds now holds steady. `World3D.QueryBroadphaseRebuildCount` lets you watch the rebuild rate.
- 3D physics gets a stability round: joints solve against a true inertia tensor, resting boxes settle on their faces instead of micro-rocking, and a friction cone plus swept CCD keep fast objects from tunneling.
- The 2D graphics and physics runtime gets the same fail-closed, allocation-light treatment the 3D layer already had — deterministic camera math, correct joint behavior, and cached transforms instead of per-frame reallocation.
- The documentation-review sweep lands as behavior fixes: VM callbacks execute through the interpreter (`Lazy`, `Option`/`Result` combinators, `Parallel`, and `Async` all work on both VMs), the regex engine loses its 32-group cap, locale parsing implements RFC 5646, collection membership uses value equality, and the time/calendar surface no longer depends on the host time zone.
- ChaCha20-Poly1305 now clamps its Poly1305 key per RFC 8439 — previously some keys produced tags other implementations would reject — and every `Zanna.Crypto` entry point validates handles before use.
- These stay correct because standing gates catch regressions: each GPU backend diffs against the software-raster golden (which caught a vignette bug shared by all three), software rendering verifies byte-identical run to run, and physics replays checksum for VM/native and parallel/serial identity.

### Runtime reliability audits (new)

- A sixty-four-finding audit hardens the runtime's foundations (ADRs 0115–0136). In practice: garbage collection, weak references, and shutdown finalization are deterministic; `Map`/`IntMap` growth is transactional; and long-running multi-threaded programs no longer risk rare crashes in monitors, channels, promises, or thread-pool teardown.
- Networking objects — HTTP clients and servers, TLS, WebSocket, SSE, SMTP — gain stable identity and transactional lifecycles, with strict protocol framing and deterministic cancellation, close, redirect, and keep-alive behavior.
- ZIP handling bounds metadata, expansion ratios, and extraction resources, so a hostile archive can't exhaust memory or disk; a failed write cleans up its staged output.
- ZPAK v2 (ADR 0134) adds per-entry CRC-32 checksums and compatibility flags to `.zpak` archives: corruption is detected at load, and v1 archives still read.

### Codegen, IL, and the native linker

- Dense `switch` statements compile to a bounds check plus a jump table, and 16-/32-bit overflow-checked arithmetic uses native flag-setting instructions instead of widening to 64 bits.
- A whole-function range analysis (ADR 0026) proves when a checked add, subtract, multiply, or divide can never trap and rewrites it to the plain form, so safety checks you don't need cost you nothing. Loop strength reduction gets the same treatment: it replaces a loop's repeated multiply with an add only when it can prove every address it computes stays free of overflow.
- The IL spec advances to 0.3.0 (ADR 0064), covering the new jump tables, branchless `select`, and checked/narrow arithmetic.
- An IL-layer audit hardens the whole textual and analysis pipeline: the parser enforces configurable resource limits and rejects malformed input cleanly (ADR 0111), floating-point constant folding follows IEEE-754 exactly (ADR 0114), and the optimizer's analyses replace recursion with bounded worklists so very large functions compile.
- The native linker resolves cleanly on Linux and Windows/MSVC, completes the CRT-less PE path on x86-64 and AArch64, and rejects dynamic imports exclusive to another platform — so a cross-platform build error surfaces at link time, not on first run on the other OS.

### Zanna Studio

- ZannaIDE is renamed Zanna Studio (ADR 0118): the binary is `zannastudio`, installers and docs follow, and your existing settings migrate automatically the first time you launch it. The workbench also wears the new brand — rebuilt dark and light palettes with every text/background pairing contrast-checked, so the UI stays readable in both themes.
- The workbench goes multi-document and multi-root: per-document undo/cursor/scroll state, split panes that restore with your session, crash-recovery swap files, a welcome/recents surface, and inline SCM diff gutters.
- Source control runs asynchronously, so status, staging, and push/pull never freeze the UI, and a new History mode browses commits and shows each change side by side. Push and pull run through a real terminal session that detects credential prompts and masks what you type. Open documents track external change, delete, and rename with sensible recovery.
- The integrated terminal is a full VT terminal (ADR 0066): alternate screens, 24-bit color, xterm keys, scroll regions, line and character editing, cursor visibility, and bracketed paste with platform-native paste chords. Full-screen console programs run correctly, your scrollback survives when they exit, and runaway child output truncates with a marker instead of hanging the workbench.
- The debugger manages watches from the command palette and expands values in the Variables view in place — List, Seq, and Map, and now class instances into named fields with nested previews (ADR 0138), so you can inspect live object state while stopped instead of adding print statements.
- An in-editor command overlay (ADR 0067) replaces external prompts for Go To Line, Add Watch, Rename Symbol, Extract Local/Function, and workspace-symbol lookup; BASIC editing gains go-to-definition, references, rename, and call hierarchy.
- Editing deepens: project-wide replace with per-match preview, a side-by-side diff for working-tree changes, drag-to-reorder tabs, and breadcrumb symbol navigation. The shell is yours to configure — rebindable keyboard shortcuts, searchable settings, and a new-project wizard that scaffolds a runnable project from templates.
- Startup brings the window to the native foreground (ADR 0068), and an adaptive poll cadence keeps the UI live while builds, debugging, or indexing run — without burning CPU when idle.
- On Windows, a native UI Automation provider exposes the workbench to assistive technology, file dialogs use the modern native pickers, and the pointer shows a full context-appropriate cursor set (ADR 0137).

### GUI toolkit

- A large modernization program lands behind the existing public surface (ADRs 0106–0109), so your GUI code keeps working while the toolkit underneath gets substantially better.
- Themes become scalable and per-app, with custom palettes, contrast policies, and reduced-motion support.
- Text editing handles Unicode 17 graphemes correctly and supports IME composition on Linux, macOS, and Windows, so non-Latin input works everywhere. Glyph rendering steps up to match: gamma-correct blending, OpenType ligatures, a per-glyph font-fallback chain, and TrueType-collection (`.ttc`) loading, so coding fonts and mixed-script text render cleanly at any size.
- A shared semantic tree projects through native accessibility adapters on each platform, so screen readers see real widget semantics.
- Virtual list/tree models render only what's visible, keeping large data sets responsive; interactive data grids and full flex/grid/dock layout round out the widget set.
- A built-in scalable vector icon library (ADR 0137) lets toolbars, trees, tabs, and status bars request icons by name and get crisp strokes at any scale — no bitmap assets to ship — and scrolling animates smoothly, paced against the platform compositor so motion doesn't tear.
- A deterministic test harness drives real input and captures framebuffers and accessibility snapshots, so GUI behavior is testable without a display server.

### Languages

- Zia gains target-typed list combinators — `map`, `filter`, `reduce`, `firstWhere`, `any`, `all`, `sum` — that infer lambda parameter types from the element type and lower to inline loops.
- A language-correctness pass adds String `for`-in, range match patterns, and `final`/`let` fields, and rejects chained comparisons with a clear diagnostic.
- Runtime object types now propagate across the registry seam in both languages: member chaining type-checks in Zia, and BASIC raises a proper `B2001` on mismatched object assignment instead of failing later in IL verification.

### Packaging and installers

- The Windows toolchain setup becomes a native installer (ADR 0103): a statically linked, high-DPI setup host with signed payloads, journaled rollback, and a full install/upgrade/modify/repair/uninstall lifecycle, including per-user and all-users scope and Apps & Features entries. Unicode install paths work end to end.
- The toolchain installer now ships Zanna Studio (ADR 0025), and standalone applications package as AppImage, RPM, DMG, and Windows installers with desktop launchers and icons; `zanna package --dry-run --json` supports scripting.
- Every artifact passes structural verification (`PkgVerify`, ADR 0073) and emits a SHA-256 checksum and machine-readable manifest before it ships; `zanna install-package` rejects a package that doesn't match the target OS or architecture.

### Windows and Linux

- Every Windows build, test, and demo entry point is now a PowerShell script (ADR 0113) — `build_zanna_win.ps1`, `build_demos_win.ps1`, and friends — compatible with PowerShell 5.1 and 7, replacing the old batch files.
- A Windows adapter audit makes failure paths deterministic across sockets, entropy, locale, TLS, process launch, timed waits, and file watching; the D3D11 backend builds cleanly under MSVC.
- The matching Linux audit hardens X11/GLX, ALSA, inotify, and PTY paths, and adds a dependency-free headless graphics backend, so graphics code runs without a display server.
- Native builds are stable on both platforms: static archives no longer pull unresolved libgcc/libstdc++ helpers, and the generated codegen tables are checked in for clean source builds.

### Tests

Test code grows by ~39K SLOC, tracking the work above:

- The third-person suite's subsystems each ship a VM-versus-native probe, and the codegen round adds jump-table, narrow-arithmetic, and range-demotion coverage.
- The runtime API and registry work is locked by contract-fingerprint and name-uniqueness guards, so the surface can't drift silently.
- The reliability audits land regression and stress coverage across GC, collections, archives, networking, IL parsing, and optimizer scalability.
- The Studio depth pass adds terminal-semantics, theme-contrast, vector-icon, font-shaping, and Windows-accessibility coverage plus per-feature workbench probes.

---

Demos and docs tracked the work: the demo set — the ASHFALL FPS campaign, Ridgebound, Neon Lanes, Zia chess, Xenoscape, and the `game3d` showcase — exercises the engine surface above, and the documentation tree reorganizes into a topical hierarchy reconciled against the live runtime registry.

<!-- END DRAFT -->
