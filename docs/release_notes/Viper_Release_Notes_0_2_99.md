# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.99 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.7 was cut on 2026-06-30. -->

### What this release is about

A cleanup-and-packaging follow-up to the v0.2.7 hardening cycle, pairing a settled public API and a codegen optimization round with a major 3D rendering push and a real installer story.

The runtime's public surface finishes settling into its final shape — one canonical name for everything, terse abbreviations spelled out, and, most visible to your code, every recoverable failure now handed back as a value you can inspect. Reading past the end of input, a decrypt that doesn't authenticate, a lookup that finds nothing, a parse that fails: each returns an `Option` or a `Result` instead of a null, a `-1`, or a silent side channel, so your programs handle the unhappy path on purpose. The older shapes stay as compatibility aliases, and `viper --dump-runtime-api` now emits a full machine-readable contract — types, ownership, stability, and security notes — so editors and tools can consume the surface without guessing.

The 3D renderer takes a large step toward modern real-time lighting. Image-based lighting brings spherical-harmonic ambient and prefiltered specular cubemaps, a clustered forward+ path bins point and spot lights into camera-space froxels so a scene can carry dozens at once, and screen-space reflections, soft particles, and temporal anti-aliasing land on top — every one carried across the Metal, OpenGL, D3D11, and software backends, fed by a from-scratch Zstandard decoder for compressed KTX2 textures.

Underneath, codegen gets faster: dense switches compile to jump tables, small-integer overflow checks lower to native flag-setting instructions, and a new range analysis proves when a checked add or divide can safely become a plain one. Packaging grows up — the installer now ships ViperIDE, and standalone applications package as AppImage, RPM, DMG, and Windows installers — and the native linker resolves cleanly on both Linux and Windows/MSVC toolchains.

- **The public API settles on one name for everything.** Aliases and duplicates are gone and the generator refuses to mint new ones; collection sizes read as `Count` and semantic lengths as `Length`, boolean probes return true/false, and `Entity3D` positions are plain properties — with both languages, ViperIDE, the demos, and the docs rebound to the smaller surface.
- **Failures come back as values you can inspect.** Terminal reads, diagnostics, collection and channel pops, decryption, HTTP/REST/SMTP sends, data-format parsing, searches, shell commands, and game and 3D queries all gain `Option`- and `Result`-returning forms, so absence and error are values instead of nulls, sentinels, or side channels. The older forms remain for compatibility.
- **Terse names spell themselves out, and the sharp edges are clearly labelled.** `LeadZ` becomes `CountLeadingZeros`, `NumSci` becomes `Scientific`, and factories drop redundant `New` prefixes; manual memory, trap-state mutation, legacy ciphers, and the TLS-verification bypass move into plainly named `Runtime.Unsafe`, `Crypto.Legacy`, and testing-only homes so nothing dangerous hides behind an innocent name.
- **A machine-readable API contract (ADR 0027).** `viper --dump-runtime-api` keeps its shape and adds parsed types, ownership, stability, capability, and security metadata for every entry, while a new internal-only marker keeps implementation helpers out of the public dump.
- **Modern 3D lighting arrives (new).** Image-based lighting adds spherical-harmonic ambient and prefiltered specular cubemaps; a clustered forward+ path bins lights into camera-space froxels (`Canvas3D.ClusteredLighting`, a 64-light GPU budget, with the software backend keeping its flat 16-light path and a `VIPER_3D_CLUSTERS=0` escape hatch); and screen-space reflections (`Material3D.SsrEnabled`), soft particles, and temporal anti-aliasing layer on top — all carried across the Metal, OpenGL, D3D11, and software backends and fed by a from-scratch Zstandard decoder for compressed KTX2 textures.
- **Game3D physics, water, and test knobs.** A standalone terrain can now join physics collision, water planes recenter over off-origin terrain, GPU post-FX composites the HUD into captured frames, `Game.UI` widgets draw against either a 2D `Canvas` or a `Canvas3D` (ADR 0065), and new environment switches let display-requiring tests render without stealing window focus.
- **Faster switches and overflow checks.** Dense `switch` statements compile to a single bounds check plus a jump table on both backends, and 32- and 16-bit overflow-checked arithmetic now lowers to native flag-setting instructions instead of widening to 64 bits.
- **Checked arithmetic is demoted when it's provably safe (ADR 0026).** A new whole-function range analysis proves when an overflow-checked add, subtract, multiply, or divide can never trap and rewrites it to the plain, faster form — sharing one range implementation with the verifier so the proof and the rejection always agree; it also recovers power-of-two modulo bounds after peephole lowering, so `expr % 2^k`-shaped code keeps verifying at `-O2`.
- **The IL spec version is now 0.3.0 (ADR 0064).** The `il` banner catches up to the surface these codegen rounds added — `switch.i32` jump tables, branchless `select`, and checked/narrow arithmetic — and snapshot builds are now date-stamped (`0.2.99.20260704`).
- **Packaging becomes a real release story (ADR 0025).** The toolchain installer now builds and ships ViperIDE, and standalone applications package as first-class AppImage, RPM, DMG, and Windows installers with desktop launchers, branded icons, and post-build verification, plus a `viper package --dry-run --json` for scripting.
- **Native builds are stable on Linux and Windows.** On Linux, static archives stop pulling in libgcc helpers the native linker can't resolve, a few libstdc++ runtime imports are recognized, and non-interactive installs fall back to a local prefix when none is set. On Windows, the native linker now discovers the MSVC C++ runtime archives from `VCToolsInstallDir` and the CMake cache, accepts machine-unknown COFF objects, and keeps static-only `__std_*` helpers out of the dynamic import path so static STL helpers resolve before import thunks.
- **ViperIDE source control, navigation, and polish (new).** An asynchronous, `Process`-backed git layer — porcelain v2 status, staged async actions, `PATH`/`VIPER_GIT_BINARY` resolution, cancellation, and non-trapping stdout/stderr result reads — keeps source-control operations off the UI thread; BASIC editing gains go-to-definition, references, rename, call hierarchy, and workspace symbols; and the editor adds debugger watch expressions, async-safe debug restart, explicit external delete/move conflict handling, runtime-paged project search/completion discovery, bounded stable-row tool panels, a grouped watch/local Variables view, and command routing organized around a named `CommandContext`.

Demos and docs tracked the work. Ridgebound grew from a terrain-crossing tech demo into a small survival game — its character-controller player, scanner-gated beacons, and boulder physics now joined by campfires, forage, warmth-driven stamina, and weather-aware lighting under the new image-based sky — while 3D Bowling added multi-player hot-seat scoring and a fresh `game3d` overhaul showcase drives the clustered-lighting and reflection paths. The ViperSQL and baseball apps moved onto the new terminal APIs, fresh IL benchmark kernels exercise the codegen round, and the 3D, packaging, CLI, and library docs picked up the new lighting surface, installer payloads, canonical names, and API contract.

### By the Numbers

| Metric | v0.2.7 | v0.2.99 | Delta |
|---|---|---|---|
| Commits | — | 23 | +23 |
| Source files | 3,402 | 3,435 | +33 |
| Production SLOC | 762K | 781K | +19K |
| Test SLOC | 304K | 310K | +6K |
| ViperIDE SLOC | 28K | 31K | +3K |
| Demo SLOC | 197K | 200K | +3K |

Counts via `scripts/count_sloc.sh` (production 781,444 / test 309,900 / demo 200,372 / viperide 31,379 / source files 3,435); commits since the `v0.2.7-dev` tag (2026-06-30). The range touched 1,689 files (+71,045 / −18,816) — the runtime API-contract expansion, the 3D rendering overhaul, a codegen optimization round, and the Ridgebound rebuild, plus internal planning docs and regenerated golden and API-audit fixtures.

<!-- END DRAFT -->
