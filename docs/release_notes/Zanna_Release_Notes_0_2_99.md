---
status: active
audience: public
last-verified: 2026-07-18
---

# Zanna Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Zanna is under active development and not ready for production use.

## Version 0.2.99 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.7 was cut on 2026-06-30. -->

### What this release is about

The platform has a new name: **Zanna** (formerly Viper). The CLI is `zanna`, programs bind `Zanna.*`, project files use `.zap`, asset archives use `.zpak`, and the project lives at github.com/zannagames/zanna (ADR 0110). The IDE is renamed too — ZannaIDE is now **Zanna Studio**, with the `zannastudio` binary and a one-time automatic migration of your existing settings (ADR 0118).

Beyond the rename, this release is about making the platform easier to build on. The runtime's public API settles on one predictable naming and error-handling scheme, so you spend less time guessing what a method is called or what it does on failure. The 3D stack grows from a renderer into a real-time game engine, with first-person and third-person game runtimes you can build a character-action game on. Zanna Studio and the GUI toolkit take major modernization passes — from theming, typography, and iconography through debugging and the terminal — packaging gains real installers on every platform, and a series of deep correctness audits — runtime, IL, Windows, and Linux — make the whole stack more predictable under bad input, heavy load, and concurrency.

### Highlights

- **One canonical name for everything.** Every public API symbol now has exactly one name — sizes are `Count`, semantic lengths are `Length`, abbreviations are spelled out — so the name you guess is usually the name that works. Old names remain as compatibility aliases.
- **Errors are values you can inspect.** Recoverable failures — a read past end-of-input, a decrypt that doesn't authenticate, a lookup that finds nothing — now return `Option`/`Result` values instead of nulls or `-1` sentinels, so you can handle them without crashing or guessing.
- **A machine-readable API contract.** `zanna --dump-runtime-api` now carries types, ownership, stability, and documentation for every entry, and the same docs surface in completion, hover, LSP, and MCP (ADRs 0027, 0101).
- **The 3D renderer looks like a modern engine.** Image-based lighting, clustered lights, screen-space reflections, soft particles, and temporal anti-aliasing land on all four backends; reversed-Z depth ends far-distance shimmer in open worlds.
- **First- and third-person game runtimes (new).** View-model rendering, point-light shadows, and fog for FPS games; a twenty-seven-subsystem third-person suite — spring-arm camera, character dynamics, combat, cloth, AI behavior trees, quests, world persistence — for character-action games (ADRs 0064–0100).
- **A real 3D asset pipeline.** Draco and meshopt compression, extended PBR materials, HDR textures, automatic LOD chains, and 1,024-bone rigs — all decoded by zero-dependency in-tree code, with a new `zanna asset` tool for offline conditioning.
- **The combat lag ramp is fixed.** A broadphase cache bug made sustained 3D combat slow down over ~15 seconds; queries now rebuild lazily, so extended fights hold their frame rate.
- **Faster generated code.** Dense `switch` statements compile to jump tables, narrow overflow checks use native flag-setting instructions, and provably-safe overflow checks are removed entirely (ADR 0026).
- **A ChaCha20-Poly1305 interop fix.** The AEAD path previously produced non-interoperable tags for some keys; encrypted data now round-trips correctly with other RFC 8439 implementations.
- **283 documentation-review findings resolved.** Every logged mismatch between the docs and the implementation was fixed on whichever side was wrong — so what the docs promise is what the runtime does.
- **Zanna Studio becomes a multi-root workbench.** Split panes, crash recovery, a full VT terminal, async git integration, a grouped-Variables debugger, and an in-editor command overlay (ADRs 0066–0068).
- **Zanna Studio takes a depth pass end to end.** Contrast-checked dark and light brand themes, scalable vector icons, ligature-capable text rendering, project-wide replace with side-by-side diffs, a new-project wizard, rebindable keyboard shortcuts, and Windows screen-reader support (ADR 0137).
- **The debugger opens up your objects.** Class instances in the Variables view now expand into named fields with nested previews — inspection no longer stops at an opaque object handle (ADR 0138).
- **Terminal and git go deeper.** The integrated terminal adds the control sequences full-screen console programs depend on, and source control gains commit-history browsing with per-commit diffs plus push/pull with masked credential prompts.
- **The GUI toolkit modernizes end to end.** Scalable themes, Unicode grapheme editing with IME support on all three platforms, native accessibility adapters, virtual list/tree models for large data, and a deterministic test harness (ADRs 0106–0109).
- **Zia gains list combinators.** `map`, `filter`, `reduce`, `firstWhere`, `any`, `all`, and `sum` infer their lambda types from the element type and compile to inline loops — no closures, no overhead.
- **Real installers on every platform.** The Windows toolchain gets a native, transactional installer with rollback and repair; standalone apps package as AppImage, RPM, DMG, and Windows installers; every artifact is verified, checksummed, and manifested before it ships (ADRs 0025, 0073, 0103).
- **A deep runtime reliability audit.** Sixty-four findings across memory management, concurrency, archives, and networking are fixed, so long-running and multi-threaded programs behave predictably under load and teardown (ADRs 0115–0136).
- **`.zpak` archives get integrity checks.** ZPAK v2 adds per-entry CRC-32 checksums and compatibility flags, so a corrupt asset archive is detected instead of silently loaded; v1 archives still read (ADR 0134).
- **The IL layer hardens against hostile input.** The IL parser enforces resource limits and fails cleanly on malformed text, the optimizer's floating-point folding follows IEEE-754 exactly, and analyses scale to much larger functions (ADRs 0111, 0114).
- **Windows builds run on PowerShell.** Every Windows build and test entry point is now a PowerShell script (`build_zanna_win.ps1` and friends), replacing the old batch files (ADR 0113).

### By the Numbers

| Metric | v0.2.7 | v0.2.99 | Delta |
|---|---|---|---|
| Commits | — | 127 | +127 |
| Source files | 3,402 | 3,577 | +175 |
| Production SLOC | 762K | 895K | +133K |
| Test SLOC | 304K | 343K | +39K |
| Zanna Studio SLOC | 28K | 40K | +12K |
| Demo SLOC | 197K | 239K | +42K |

Counts via `scripts/count_sloc.sh` (production 894,609 / test 342,731 / demo 238,941 / zannastudio 39,733 / source files 3,577); commits since the `v0.2.7-dev` tag (2026-06-30). Much of the raw diff is checked-in text-glTF character and model assets, which the SLOC figures exclude.

---

### Runtime public API and contract

- Every public symbol settles on one canonical name: collection sizes read as `Count`, semantic lengths as `Length`, boolean probes return true/false, and `Entity3D` positions are plain properties. Terse abbreviations spell themselves out (`LeadZ` → `CountLeadingZeros`), and the generator refuses to mint new aliases, so the surface stays consistent going forward.
- Recoverable failures return values: terminal reads, collection and channel pops, decryption, HTTP/REST/SMTP sends, parsing, searches, shell commands, and 3D queries all gain `Option`/`Result` forms. Your error handling becomes ordinary control flow instead of null checks and sentinel comparisons.
- The sharp edges are labelled: manual memory, trap-state mutation, and legacy ciphers move into plainly named `Runtime.Unsafe` and `Crypto.Legacy` homes, so nothing dangerous hides behind an innocent name.
- The 17,000-line runtime registry splits into documented, domain-oriented fragments, each entry requiring authored documentation (ADR 0101). That documentation now reaches you directly — in editor completion and hover, LSP, MCP, and `zanna --dump-runtime-api`.
- A runtime-boundary audit (ADR 0102) fixes the registry as the supported surface: the API dump now records each 3D binding's ownership, nullability, and fallibility, with a deterministic fingerprint that tests lock against drift.

### 3D rendering

- Image-based lighting, a clustered forward+ light path, screen-space reflections, soft particles, and temporal anti-aliasing land across Metal, OpenGL, D3D11, and the software backend — so a scene lit and post-processed on one platform looks the same on the others.
- GPU backends render reversed-Z float depth: precision concentrates in the distance, so large open-world clip ranges stop z-fighting and shimmering. The software backend stays standard-depth as the deterministic reference.
- The occlusion system rasterizes real triangles instead of bounding boxes, so near walls and rotated geometry actually cull what hides behind them — fewer wasted draws in dense scenes.
- A flicker pass fixes two structural races in Metal frame pacing and transient buffers, corrects `Quat.FromEuler` to the documented axis order, and stops instanced forests and particles from popping as a block.
- The D3D11 backend clamps every shader-facing constant buffer to safe values before upload and expands compressed-texture support to the full BC family, closing a class of driver-dependent rendering glitches on Windows.
- New observability surfaces (ADRs 0069–0071) — backend fallback reasons, dropped-draw counts, mesh memory budgets — plus `TrySet…` and accurate `BackendSupports` probes let your code query a capability instead of trapping on a backend that lacks it.

### 3D assets, animation, and the asset pipeline

- The zero-dependency importer now decodes Draco, meshopt, and Basis Universal compressed glTF assets, quantized attributes, and KTX2 textures — assets exported from standard DCC tools load directly, and malformed streams fail with named diagnostics instead of crashing.
- Materials extend to the full PBR set — clearcoat, sheen, anisotropy, transmission, volume — plus signed/unsigned BC6H HDR textures, across all four backends.
- Animation scales up: skeletons to 1,024 bones, up to eight influences per vertex with GPU skinning, cubic-spline tangents, and tolerance-based clip compression.
- `GenerateLODs` builds bounded LOD chains automatically, and `AssetDiagnostics3D.GetImportReport` tells you exactly what an import skipped or truncated, as structured JSON.
- A new `zanna asset bake` / `zanna asset validate` tool conditions assets offline, so load-time work moves to build time.

### First- and third-person game runtimes

- A first-person pass turns the renderer into an FPS-capable stack: view-model rendering, point-light shadows, height fog, auto-exposure and color-grading post-FX, raw/relative mouse input, and Doppler-fed spatial audio with occlusion and ducking.
- A third-person suite (ADRs 0074–0100) adds twenty-seven subsystems for character-action games: spring-arm camera with target-lock, character dynamics over moving platforms, combat volumes with health and ragdoll, cloth, cinematics with dialogue, AI perception and behavior trees, minimaps, quests, world persistence, and a baked-GI/reflection-probe/procedural-sky lighting round. Each ships with a VM-versus-native equivalence probe.
- A raycast-suspension `Vehicle3D` with an engine/brake/steering model joins the stack, and `Game.UI` widgets now draw against either a 2D `Canvas` or a `Canvas3D` (ADR 0065).

### Physics and runtime correctness

- The combat lag ramp is gone: the spatial-query broadphase no longer rebuilds its cache on every pose change, so a fight that used to degrade over ~15 seconds now holds steady. `World3D.QueryBroadphaseRebuildCount` lets you watch the rebuild rate.
- 3D physics gets a stability round: joints solve against a true inertia tensor, resting boxes settle on their faces instead of micro-rocking, and a friction cone plus swept CCD keep fast objects from tunneling.
- The 2D graphics and physics runtime receives the same fail-closed, allocation-light treatment the 3D layer already had — deterministic camera math, correct joint behavior, and cached transforms instead of per-frame reallocation.
- The 283-finding documentation-review sweep lands as code fixes: VM callbacks execute through the interpreter (`Lazy`, `Option`/`Result` combinators, `Parallel`, `Async` all work on both VMs), the regex engine loses its 32-group cap, locale parsing implements RFC 5646, collection membership uses value equality, and the time/calendar surface no longer depends on the host time zone.
- ChaCha20-Poly1305 now clamps its Poly1305 key per RFC 8439 — previously some keys produced tags other implementations would reject — and every `Zanna.Crypto` entry point validates handles before use.
- Standing conformance gates catch regressions early: each GPU backend diffs against the software-raster golden (this caught a vignette bug shared by all three), software rendering verifies byte-identical run to run, and physics replays checksum for VM/native and parallel/serial identity.

### Runtime reliability audits (new)

- A sixty-four-finding audit hardens the runtime's foundations (ADRs 0115–0136). For your programs, that means: garbage collection, weak references, and shutdown finalization are deterministic; `Map`/`IntMap` growth is transactional; and long-running multi-threaded programs no longer risk rare crashes in monitors, channels, promises, or thread-pool teardown.
- Networking objects — HTTP clients and servers, TLS, WebSocket, SSE, SMTP — gain stable identity and transactional lifecycles, with strict protocol framing and deterministic cancellation, close, redirect, and keep-alive behavior.
- ZIP handling bounds metadata, expansion ratios, and extraction resources, so a hostile archive can't exhaust memory or disk; failed writes clean up their staged output.
- ZPAK v2 (ADR 0134) adds per-entry CRC-32 checksums and compatibility flags to `.zpak` archives: corruption is detected at load, and v1 archives still read.
- A cppcheck static-analysis pass with reviewed suppressions cleaned the runtime during the audit and remains available as an optional manual target (ADR 0135).

### Codegen, IL, and the native linker

- Dense `switch` statements compile to a bounds check plus a jump table, and 16-/32-bit overflow-checked arithmetic uses native flag-setting instructions — no more widening to 64 bits.
- A whole-function range analysis (ADR 0026) proves when a checked add, subtract, multiply, or divide can never trap and rewrites it to the plain form, so safety checks you don't need cost you nothing.
- The IL spec advances to 0.3.0 (ADR 0064), covering the new jump tables, branchless `select`, and checked/narrow arithmetic.
- An IL-layer audit hardens the whole textual and analysis pipeline: the parser enforces configurable resource limits and rejects malformed input cleanly (ADR 0111), floating-point constant folding follows IEEE-754 exactly (ADR 0114), the optimizer's analyses replace recursion with bounded worklists so very large functions compile, and the linker renames collisions safely with stable output ordering.
- The native linker resolves cleanly on Linux and Windows/MSVC, completes the CRT-less PE path on x86-64 and AArch64, and rejects dynamic imports exclusive to another platform — so a cross-platform build error surfaces at link time, not at first run on the other OS.

### Zanna Studio

- ZannaIDE is renamed Zanna Studio (ADR 0118): the binary is `zannastudio`, installers and docs follow, and your existing settings migrate automatically the first time you launch it.
- The workbench goes multi-document and multi-root: per-document undo/cursor/scroll state, split panes, crash-recovery swap files, a welcome/recents surface, and inline SCM diff gutters.
- Git integration runs asynchronously, so status, staging, and push/pull never freeze the UI; open documents track external change, delete, and rename with sensible recovery.
- The integrated terminal becomes a full VT terminal (ADR 0066) — alternate screens, 24-bit color, xterm keys — and runaway child output truncates with a marker instead of hanging the workbench.
- The debugger adds watch management from the command palette and a grouped Variables view that expands List, Seq, and Map values in place.
- An in-editor command overlay (ADR 0067) replaces external prompts for Go To Line, Add Watch, Rename Symbol, Extract Local/Function, and workspace-symbol lookup; BASIC editing gains go-to-definition, references, rename, and call hierarchy.
- Startup brings the window to the native foreground (ADR 0068), and an adaptive poll cadence keeps the UI live while builds, debugging, or indexing run — without burning CPU when idle.
- The workbench wears the Zanna brand: rebuilt dark and light palettes around a charcoal-green field and green accent, with every text/background pairing contrast-checked so the UI stays readable in both themes.
- Editing deepens: project-wide replace with per-match preview, a side-by-side diff view for working-tree changes, drag-to-reorder tabs, breadcrumb symbol navigation, and split layouts that restore with your session.
- The shell becomes yours to configure: keyboard shortcuts rebind per user, a new-project wizard scaffolds a runnable project from templates, and settings are searchable.
- The debugger's Variables view expands class instances into named fields with nested previews (ADR 0138), alongside the existing List/Seq/Map expansion — inspect live object state while stopped instead of adding print statements.
- The terminal fills in the control sequences full-screen console programs rely on — scroll regions, line and character editing, tab stops, cursor visibility, and bracketed paste with platform-native paste chords — and your scrollback is intact when they exit.
- Source control adds a History mode — browse commits, open a commit's files, and view each change side by side — and push/pull run through a real terminal session that detects credential prompts and masks what you type.
- On Windows, a native UI Automation provider exposes the workbench to assistive technology, file dialogs move to the modern native pickers, and the pointer shows a full context-appropriate cursor set.

### GUI toolkit

- A forty-item modernization program lands behind the existing public surface (ADRs 0106–0109), so your GUI code keeps working while the toolkit underneath gets substantially better.
- Themes become scalable and per-app, with custom palettes, contrast policies, and reduced-motion support.
- Text editing handles Unicode 17 graphemes correctly and supports IME composition on Linux, macOS, and Windows — non-Latin input works everywhere.
- A shared semantic tree projects through native accessibility adapters on each platform, so screen readers see real widget semantics.
- Virtual list/tree models render only what's visible, keeping large data sets responsive; interactive data grids and complete flex/grid/dock layout round out the widget set.
- A deterministic TestHarness drives real input and captures framebuffers and accessibility snapshots, so GUI behavior is testable in CI-free local runs.
- Text rendering steps up: gamma-correct glyph blending, OpenType ligatures, a per-glyph font-fallback chain, and TrueType-collection (`.ttc`) loading — coding fonts and mixed-script text render cleanly at any size.
- A built-in scalable vector icon library (ADR 0137) lets toolbars, trees, tabs, and status bars request icons by name and get crisp strokes at any scale — no bitmap assets to ship.
- Scrolling animates smoothly, with frame presentation paced against the platform compositor so motion doesn't tear or stutter.

### Languages

- Zia gains target-typed list combinators — `map`, `filter`, `reduce`, `firstWhere`, `any`, `all`, `sum` — that infer lambda parameter types from the element type and lower to inline loops.
- A language-correctness pass adds String `for`-in, range match patterns, and `final`/`let` fields, and rejects chained comparisons with a clear diagnostic.
- Runtime object types now propagate across the registry seam in both languages: member chaining type-checks in Zia, and BASIC raises a proper `B2001` on mismatched object assignment instead of failing later in IL verification.

### Packaging and installers

- The Windows toolchain setup becomes a native installer (ADR 0103): a statically linked, high-DPI setup host with signed payloads, journaled rollback, and a full install/upgrade/modify/repair/uninstall lifecycle — including per-user and all-users scope and Apps & Features entries. Unicode install paths work end to end.
- The toolchain installer now ships Zanna Studio (ADR 0025), and standalone applications package as AppImage, RPM, DMG, and Windows installers with desktop launchers and icons; `zanna package --dry-run --json` supports scripting.
- Every artifact passes structural verification (`PkgVerify`, ADR 0073) and emits a SHA-256 checksum and machine-readable manifest before it ships; `zanna install-package` rejects a package that doesn't match the target OS or architecture.
- The repository consolidates under `src/`, and the Linux self-extracting format is renamed `linux-bundle` (`.run`).

### Windows and Linux

- Every Windows build, test, and demo entry point is now a PowerShell script (ADR 0113) — `build_zanna_win.ps1`, `build_demos_win.ps1`, and friends — compatible with PowerShell 5.1 and 7, replacing the old batch files.
- A Windows adapter audit makes failure paths deterministic across sockets, entropy, locale, TLS, process launch, timed waits, and file watching; the D3D11 backend builds cleanly under MSVC.
- The matching Linux audit hardens X11/GLX, ALSA, inotify, and PTY paths, and adds a dependency-free headless graphics backend — graphics code now runs without a display server.
- Native builds are stable on both platforms: static archives no longer pull unresolved libgcc/libstdc++ helpers, and the generated codegen tables are checked in for clean source builds.

### Tests

Test code grows by ~39K SLOC. The third-person suite's twenty-seven subsystems each ship a VM-versus-native probe; the codegen round adds jump-table, narrow-arithmetic, and range-demotion coverage; the runtime API and registry work are locked by contract-fingerprint and name-uniqueness guards; the reliability audits land regression and stress coverage across GC, collections, archives, networking, IL parsing, and optimizer scalability; and the Studio depth pass adds terminal-semantics, theme-contrast, vector-icon, font-shaping, and Windows-accessibility coverage plus per-feature workbench probes.

---

Demos and docs tracked the work: the demo set — the ASHFALL FPS campaign, Ridgebound, Neon Lanes, Zia chess, Xenoscape, and the `game3d` showcase — exercises the engine surface above, and the documentation tree reorganizes into a topical hierarchy reconciled against the live runtime registry.

<!-- END DRAFT -->
