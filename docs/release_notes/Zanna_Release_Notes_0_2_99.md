---
status: active
audience: public
last-verified: 2026-07-22
---

# Zanna Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Zanna is under active development and not ready for production use.

## Version 0.2.99 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.7 was cut on 2026-06-30. -->

### What this release is about

Viper is now **Zanna**. The rename reaches everything you type: the CLI is `zanna`, programs bind `Zanna.*`, projects save as `.zap`, asset archives as `.zpak`, and the code lives at github.com/zannagames/zanna. ZannaIDE became **Zanna Studio**, and it moves your old settings over for you the first time you open it.

Past the rename, most of the effort went into making Zanna nicer to build on. The runtime's public API now uses one set of naming rules and one way of reporting failure, so a method behaves the way you'd guess and tells you when something goes wrong instead of handing back a mystery value. The 3D side outgrew being a plain renderer: it ships first- and third-person game runtimes, and its importer reads whole scenes — Tiled maps, and FBX with its cameras, lights, and animation — rather than bare geometry.

Zanna Studio and the GUI toolkit were both rebuilt underneath. Packaging picked up real installers on Windows, macOS, and Linux. Four correctness audits, one each for the runtime, the IL layer, Windows, and Linux, went looking for the places the stack breaks under bad input, heavy load, or many threads.

- **Runtime names follow one convention.** Collection sizes read as `Count`, string lengths as `Length`, and cramped abbreviations are spelled out (`LeadZ` became `CountLeadingZeros`). The name you'd guess is usually the right one, and the old names stay as aliases so nothing you've written stops compiling.
- **Recoverable failures return a value, not a crash.** A read past end-of-input, a decrypt that won't authenticate, a lookup that finds nothing: each hands back an `Option` or `Result` you can check, instead of a null or a `-1` you have to remember to test for.
- **Your editor shows the runtime's own docs.** Completion, hover, and the LSP and MCP servers pull documentation, types, and stability notes straight from the runtime. `zanna --dump-runtime-api` emits the same information for your own tools.
- **The 3D renderer picked up modern lighting.** Image-based lighting, clustered lights, screen-space reflections, soft particles, and temporal anti-aliasing work on all four backends. Reversed-Z depth clears up the shimmer you used to see far off in open worlds.
- **First- and third-person game runtimes (new).** First-person games get view-model rendering, point-light shadows, and fog. Character-action games get a fuller kit: spring-arm camera, character movement, combat, cloth, AI behavior trees, quests, and world persistence.
- **The importer reads whole scenes (new).** Tiled maps (`.tmj`/`.tmx`), finite or infinite, load as editable scenes or ready-to-render tilemaps. FBX brings in procedural surfaces, layered animation, constraints, cameras, authored lights, and blend shapes. `zanna asset bake` keeps the whole scene, and your exact texture files, intact through a round-trip.
- **An offline asset pipeline.** Draco and meshopt compression, extended PBR materials, HDR textures, automatic LOD chains, and rigs up to 1,024 bones, all decoded by Zanna's own code. The `zanna asset` tool conditions assets ahead of time, so the work happens at build rather than at load.
- **The combat slowdown is fixed.** A broadphase cache bug made sustained 3D fights lose frame rate over about fifteen seconds. Queries rebuild lazily now, so long fights hold steady.
- **Generated code runs faster.** Dense `switch` statements become jump tables, narrow overflow checks use the CPU's own flag bits, and overflow checks the compiler can prove are unnecessary get dropped.
- **A ChaCha20-Poly1305 fix.** The AEAD path used to produce tags that other implementations rejected for certain keys. Encrypted data now round-trips with any RFC 8439 implementation.
- **The docs and the runtime agree.** Every recorded gap between the documentation and the code — 283 of them — was fixed on whichever side was wrong.
- **Zanna Studio handles multiple roots.** Split panes, crash recovery, a full VT terminal, git that runs in the background with a commit-history view, a debugger that opens objects into named fields, and a command overlay for Go To Line, rename, and workspace-symbol search.
- **Zanna Studio wears the Zanna brand.** Contrast-checked dark and light themes, vector icons that stay sharp at any size, ligature-capable text, project-wide replace with side-by-side diffs, a new-project wizard, rebindable shortcuts, and Windows screen-reader support.
- **The GUI toolkit was rebuilt underneath.** Scalable per-app themes, Unicode grapheme editing with IME on all three platforms, native screen-reader adapters, and virtual list and tree models for big data sets. The public API didn't change, so your GUI code keeps working.
- **Zia gains list combinators.** `map`, `filter`, `reduce`, `firstWhere`, `any`, `all`, and `sum` read their lambda types from the element type and compile down to plain loops — no closures, no allocation.
- **Real installers everywhere.** A native Windows toolchain installer with rollback and repair, and standalone apps that ship as AppImage, RPM, DMG, and Windows installers. Every artifact is checked, checksummed, and manifested before it goes out.
- **A runtime reliability audit.** Sixty-four fixes across memory, threading, archives, and networking, aimed at the rare crashes that show up in long-running and multi-threaded programs.
- **`.zpak` archives are checksummed.** ZPAK v2 adds a per-entry CRC-32, so a corrupt asset archive is caught when it loads rather than used silently. Old v1 archives still read.
- **The IL layer is harder to break.** The IL parser enforces resource limits and rejects malformed text cleanly, float folding matches IEEE-754 exactly, and the analyses handle much larger functions.
- **Native Wayland on Linux (new).** Linux runs on Wayland directly, chosen automatically with an X11 fallback. Keyboard, pointer, touch, clipboard, drag-and-drop, IME, window decorations, and GPU presentation are all there, with no external dependencies.
- **Windows builds use PowerShell.** Every Windows build and test entry point is a PowerShell script now (`build_zanna_win.ps1` and the rest), in place of the old batch files.

### By the Numbers

| Metric | v0.2.7 | v0.2.99 | Delta |
|---|---|---|---|
| Commits | — | 143 | +143 |
| Source files | 3,402 | 3,634 | +232 |
| Production SLOC | 762K | 936K | +174K |
| Test SLOC | 304K | 356K | +52K |
| Zanna Studio SLOC | 28K | 42K | +14K |
| Demo SLOC | 197K | 240K | +43K |

Counts via `scripts/count_sloc.sh` (production 935,635 / test 356,265 / demo 239,751 / zannastudio 42,088 / source files 3,634); commits since the `v0.2.7-dev` tag (2026-06-30). Much of the raw diff is checked-in text-glTF character and model assets, which these SLOC figures leave out.

---

### Runtime public API and contract

- One name per symbol. Collection sizes read as `Count`, semantic lengths as `Length`, boolean checks return true or false, and short abbreviations are written out (`LeadZ` → `CountLeadingZeros`). The old names stay as aliases, so existing code compiles unchanged.
- Recoverable failures return something you can branch on. Terminal reads, collection and channel pops, decryption, HTTP/REST/SMTP sends, parsing, searches, shell commands, and 3D queries all gained `Option`/`Result` forms, so error handling becomes ordinary control flow instead of null checks and sentinel comparisons.
- The dangerous parts are labeled as such. Manual memory, trap-state changes, and legacy ciphers moved into clearly named `Runtime.Unsafe` and `Crypto.Legacy` homes, so nothing risky sits behind a harmless-looking name.
- The runtime's documentation reaches you where you work: editor completion and hover, the LSP and MCP servers, and `zanna --dump-runtime-api`, which also records each 3D binding's ownership, nullability, and whether it can fail.

### 3D rendering

- Image-based lighting, a clustered forward+ light path, screen-space reflections, soft particles, and temporal anti-aliasing all land on Metal, OpenGL, D3D11, and the software backend, so a scene looks the same wherever it renders.
- The GPU backends use reversed-Z float depth, which puts precision in the distance and stops the z-fighting and shimmer you'd get across a wide open-world clip range. The software backend keeps standard depth as the deterministic reference.
- Occlusion culling rasterizes real triangles instead of bounding boxes, so a near wall or a rotated object actually hides what's behind it. That means fewer wasted draws in crowded scenes.
- Frame pacing steadied over long sessions. GPU backends sync to the display instead of a redundant CPU limiter, on-screen text reuses its rasterized glyphs rather than re-uploading them each frame, and Metal no longer stalls the CPU while streaming textures in, so the periodic hitching is gone.
- On Windows, the D3D11 backend clamps every shader constant buffer before upload and supports the full BC compressed-texture family, which closes a batch of driver-specific glitches.
- New things you can query: why a backend fell back, how many draws were dropped, mesh memory budgets. `TrySet…` setters and accurate `BackendSupports` probes let you ask whether a capability exists instead of trapping when it doesn't.

### 3D assets, animation, and the asset pipeline

- The importer decodes Draco, meshopt, and Basis Universal compressed glTF, quantized attributes, and KTX2 textures, so assets exported from standard tools load directly. A malformed stream fails with a named error instead of crashing.
- FBX import is thorough now. A file authored in a DCC tool arrives with its meshes, skeletons and skinning, NURBS and patch surfaces, layered and constraint-driven animation, node-coupled cameras, lights, and blend shapes — binary or ASCII, through one evaluator. You get the scene, not just the geometry.
- Tiled maps load directly. `SceneDocument` and `TiledMapLoader` read finite or infinite Tiled JSON (`.tmj`) and TMX (`.tmx`) into an editable scene or a render-ready tilemap in orthogonal, isometric, staggered, hexagonal, or oblique projection. Tilesets, transforms, parallax, tint, collision, typed metadata, and animation all survive a save and reload.
- `zanna asset bake` keeps the whole scene, not just its geometry: every scene, camera and its animation, light, skeletal and morph clip, and material variant, plus the exact source bytes of your KTX2/PNG/JPEG/GIF/BMP textures. A per-slot report tells you which textures were kept untouched and which had to be re-encoded.
- Materials cover the full PBR set — clearcoat, sheen, anisotropy, transmission, volume — with signed and unsigned BC6H HDR textures, on all four backends.
- Animation scales up: skeletons to 1,024 bones, up to eight influences per vertex with GPU skinning, cubic-spline tangents, and tolerance-based clip compression. `GenerateLODs` builds LOD chains for you, and `AssetDiagnostics3D.GetImportReport` reports in JSON what an import skipped or truncated.
- A hardening pass makes asset parsing strict and all-or-nothing, and keeps rendering, physics, navigation, terrain, and particles bounded and recoverable. A bad asset or an overloaded scene degrades in a defined way rather than crashing, and mesh and ray data that never changes is cached instead of rebuilt each frame.

### First- and third-person game runtimes

- The first-person work turns the renderer into an FPS-capable stack: view-model rendering, point-light shadows, height fog, auto-exposure and color-grading post-effects, raw and relative mouse input, and Doppler-shifted spatial audio with occlusion and ducking.
- The third-person suite adds what a character-action game needs: a spring-arm camera with target lock, character movement over moving platforms, combat volumes with health and ragdoll, cloth, cinematics with dialogue, AI perception and behavior trees, minimaps, quests, world persistence, and a baked-GI, reflection-probe, and procedural-sky lighting pass. Each subsystem ships with a probe that checks it behaves the same interpreted and compiled.
- A raycast-suspension `Vehicle3D` with an engine, brake, and steering model joins the stack, and `Game.UI` widgets now draw against either a 2D `Canvas` or a `Canvas3D`.

### Physics and runtime correctness

- The combat slowdown is gone. The spatial-query broadphase no longer rebuilds its cache on every pose change, so a fight that used to degrade over about fifteen seconds holds steady. `World3D.QueryBroadphaseRebuildCount` lets you watch the rebuild rate.
- 3D physics got a stability pass: joints solve against a true inertia tensor, resting boxes settle flat instead of micro-rocking, and a friction cone plus swept CCD keep fast objects from tunneling through walls.
- The 2D graphics and physics runtime got the same treatment the 3D layer already had: fail-closed, light on allocation, deterministic camera math, correct joints, and cached transforms instead of a per-frame rebuild.
- Several documentation-review findings landed as behavior fixes. VM callbacks run through the interpreter (`Lazy`, the `Option`/`Result` combinators, `Parallel`, and `Async` all work on both VMs), the regex engine lost its 32-group cap, locale parsing follows RFC 5646, collection membership uses value equality, and the time and calendar functions no longer depend on the host time zone.
- ChaCha20-Poly1305 clamps its Poly1305 key per RFC 8439 — some keys used to produce tags other implementations rejected — and every `Zanna.Crypto` entry point validates its handles before use.

### Runtime reliability audits

- A sixty-four-finding audit shored up the runtime's foundations. In practice: garbage collection, weak references, and shutdown finalization run deterministically; `Map` and `IntMap` growth is all-or-nothing; and long-running multi-threaded programs no longer risk the rare crash in monitors, channels, promises, or thread-pool teardown.
- Networking objects — HTTP clients and servers, TLS, WebSocket, SSE, SMTP — got stable identities and clean lifecycles, with strict protocol framing and predictable cancel, close, redirect, and keep-alive behavior.
- ZIP handling bounds metadata, expansion ratios, and extraction resources, so a hostile archive can't exhaust memory or disk, and a failed write cleans up after itself.
- ZPAK v2 adds a per-entry CRC-32 and compatibility flags to `.zpak` archives. Corruption is caught at load, and v1 archives still read.

### Codegen, IL, and the native linker

- Dense `switch` statements compile to a bounds check plus a jump table, and 16- and 32-bit overflow-checked math uses the CPU's flag bits instead of widening everything to 64 bits.
- A whole-function range analysis works out when a checked add, subtract, multiply, or divide can't trap and rewrites it to the plain form, so a safety check you don't need costs nothing. Loop strength reduction gets the same care: it swaps a loop's repeated multiply for an add only when it can prove every address stays in range.
- The IL specification moved to 0.3.0, covering the new jump tables, the branchless `select`, and the checked and narrow arithmetic.
- An IL-layer audit hardened the text and analysis pipeline: the parser enforces configurable resource limits and rejects malformed input cleanly, float constant folding follows IEEE-754 exactly, and the optimizer's analyses use bounded worklists instead of recursion, so very large functions compile.
- The native linker resolves cleanly on Linux and Windows/MSVC, finishes the CRT-less PE path on x86-64 and AArch64, and rejects dynamic imports that only exist on another OS, so a cross-platform mistake shows up at link time rather than on first run.

### Zanna Studio

- ZannaIDE is now Zanna Studio. The binary is `zannastudio`, the installers and docs follow, and your settings migrate on their own the first time you launch it. The workbench wears the new brand, with dark and light palettes whose every text-on-background pairing is contrast-checked so it stays readable either way.
- The workbench handles multiple documents and multiple roots: per-document undo, cursor, and scroll state; split panes that come back with your session; crash-recovery swap files; a welcome and recents screen; and inline SCM diff gutters.
- Source control runs in the background, so status, staging, and push/pull never freeze the UI, and a History mode browses commits side by side. Push and pull go through a real terminal session that spots credential prompts and masks what you type. Open documents notice external edits, deletes, and renames and recover sensibly.
- The integrated terminal is a full VT terminal: alternate screens, 24-bit color, xterm keys, scroll regions, and bracketed paste. Full-screen console programs run correctly, your scrollback survives when they quit, and runaway output truncates with a marker instead of hanging the workbench.
- The debugger expands values in place in the Variables view — List, Seq, Map, and now class instances into named fields with nested previews — so you can read live object state while stopped instead of scattering print statements.
- A command overlay replaces the external prompts for Go To Line, Add Watch, Rename Symbol, Extract Local/Function, and workspace-symbol search. BASIC editing picks up go-to-definition, references, rename, and call hierarchy.
- Editing goes deeper: project-wide replace with a per-match preview, a side-by-side diff for working-tree changes, drag-to-reorder tabs, and breadcrumb symbol navigation. The shell is yours to set up, with rebindable shortcuts, searchable settings, and a new-project wizard that scaffolds a runnable project from templates.
- On Windows, a native UI Automation provider exposes the workbench to assistive technology, file dialogs use the modern native pickers, and the pointer shows a full, context-appropriate cursor set.

### GUI toolkit

- A large rework sits behind the same public surface, so your GUI code keeps working while the toolkit under it gets better.
- Themes are scalable and per-app now, with custom palettes, contrast policies, and reduced-motion support.
- Text editing handles Unicode 17 graphemes correctly and supports IME composition on Linux, macOS, and Windows, so non-Latin input works everywhere. Glyph rendering keeps up: gamma-correct blending, OpenType ligatures, per-glyph font fallback, and TrueType-collection (`.ttc`) loading, so coding fonts and mixed-script text render cleanly at any size.
- A shared semantic tree feeds each platform's native accessibility layer, so screen readers see real widget semantics.
- Virtual list and tree models render only what's on screen, which keeps large data sets responsive. Interactive data grids and full flex/grid/dock layout round out the widgets.
- A built-in vector icon library lets toolbars, trees, tabs, and status bars ask for an icon by name and get crisp strokes at any scale, with no bitmaps to ship.

### Languages

- Zia gains target-typed list combinators — `map`, `filter`, `reduce`, `firstWhere`, `any`, `all`, `sum` — that infer their lambda parameter types from the element type and lower to plain loops.
- A correctness pass adds `for`-in over a String, range patterns in `match`, and `final`/`let` fields, and rejects chained comparisons with a clear message.
- Runtime object types carry across the language boundary now: member chaining type-checks in Zia, and BASIC reports a proper `B2001` on a mismatched object assignment instead of failing later during IL verification.

### Packaging and installers

- The Windows toolchain setup is a native installer: a statically linked, high-DPI host with signed payloads, journaled rollback, and a full install, upgrade, modify, repair, and uninstall lifecycle, including per-user and all-users scope and an Apps & Features entry. Unicode install paths work throughout.
- That installer now ships Zanna Studio, and standalone apps package as AppImage, RPM, DMG, and Windows installers with desktop launchers and icons. `zanna package --dry-run --json` is there for scripting.
- Every artifact passes a structural check and emits a SHA-256 checksum and a machine-readable manifest before it ships. `zanna install-package` refuses a package that doesn't match the target OS or architecture.

### Windows and Linux

- Every Windows build, test, and demo entry point is a PowerShell script now — `build_zanna_win.ps1`, `build_demos_win.ps1`, and the rest — working on both PowerShell 5.1 and 7, in place of the old batch files.
- A Windows adapter audit made the failure paths deterministic across sockets, entropy, locale, TLS, process launch, timed waits, and file watching, and the D3D11 backend builds cleanly under MSVC.
- The matching Linux audit hardened the X11/GLX, ALSA, inotify, and PTY paths and added a dependency-free headless graphics backend, so graphics code runs with no display server attached.
- Linux also gains a complete, dependency-free Wayland backend: `AUTO` prefers Wayland and falls back to X11, covering xdg-shell window management, fractional scaling, client-side decorations, full input with relative-pointer and IME protocols, EGL-accelerated `Canvas3D` presentation, and AT-SPI screen-reader export.
- Native builds are stable on both: static archives no longer pull in unresolved libgcc/libstdc++ helpers, and the generated codegen tables are checked in for clean from-source builds.

### Tests

Test code grew by about 52K SLOC alongside the work above:

- Each third-person subsystem ships a VM-versus-native probe, and the codegen work adds jump-table, narrow-arithmetic, and range-demotion coverage.
- Contract-fingerprint and name-uniqueness guards lock the runtime API and registry so the surface can't drift quietly. The asset pipeline adds end-to-end bake-fidelity checks for whole scenes, exact source textures, and decoded-only textures.
- The reliability audits land regression and stress coverage across GC, collections, archives, networking, IL parsing, and optimizer scalability.
- The Studio work adds terminal-semantics, theme-contrast, vector-icon, font-shaping, and Windows-accessibility coverage, plus a probe per workbench feature.

---

Demos and docs tracked the work. The demo set — the ASHFALL FPS campaign, Ridgebound, Neon Lanes, Zia chess, Xenoscape, and the `game3d` showcase — exercises the engine surface above, and the documentation was reorganized into a topical hierarchy and reconciled against the live runtime registry.

<!-- END DRAFT -->
