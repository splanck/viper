# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.99 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.7 was cut on 2026-06-30. -->

### What this release is about

A cleanup-and-packaging follow-up to the v0.2.7 hardening cycle, with a codegen optimization round and a broadened public API.

The runtime's public surface finishes settling into its final shape — one canonical name for everything, terse abbreviations spelled out, and, most visible to your code, every recoverable failure now handed back as a value you can inspect. Reading past the end of input, a decrypt that doesn't authenticate, a lookup that finds nothing, a parse that fails: each returns an `Option` or a `Result` instead of a null, a `-1`, or a silent side channel, so your programs handle the unhappy path on purpose. The older shapes stay as compatibility aliases, and `viper --dump-runtime-api` now emits a full machine-readable contract — types, ownership, stability, and security notes — so editors and tools can consume the surface without guessing.

Underneath, codegen gets faster: dense switches compile to jump tables, small-integer overflow checks lower to native flag-setting instructions, and a new range analysis proves when a checked add or divide can safely become a plain one. And packaging grows up — the installer now ships ViperIDE, and standalone applications package as AppImage, RPM, DMG, and Windows installers — while a Linux native-build fix and a round of Game3D runtime additions round out the release.

- **The public API settles on one name for everything.** Aliases and duplicates are gone and the generator refuses to mint new ones; collection sizes read as `Count` and semantic lengths as `Length`, boolean probes return true/false, and `Entity3D` positions are plain properties — with both languages, ViperIDE, the demos, and the docs rebound to the smaller surface.
- **Failures come back as values you can inspect.** Terminal reads, diagnostics, collection and channel pops, decryption, HTTP/REST/SMTP sends, data-format parsing, searches, shell commands, and game and 3D queries all gain `Option`- and `Result`-returning forms, so absence and error are values instead of nulls, sentinels, or side channels. The older forms remain for compatibility.
- **Terse names spell themselves out, and the sharp edges are clearly labelled.** `LeadZ` becomes `CountLeadingZeros`, `NumSci` becomes `Scientific`, and factories drop redundant `New` prefixes; manual memory, trap-state mutation, legacy ciphers, and the TLS-verification bypass move into plainly named `Runtime.Unsafe`, `Crypto.Legacy`, and testing-only homes so nothing dangerous hides behind an innocent name.
- **A machine-readable API contract (ADR 0027).** `viper --dump-runtime-api` keeps its shape and adds parsed types, ownership, stability, capability, and security metadata for every entry, while a new internal-only marker keeps implementation helpers out of the public dump.
- **Faster switches and overflow checks.** Dense `switch` statements compile to a single bounds check plus a jump table on both backends, and 32- and 16-bit overflow-checked arithmetic now lowers to native flag-setting instructions instead of widening to 64 bits.
- **Checked arithmetic is demoted when it's provably safe (ADR 0026).** A new whole-function range analysis proves when an overflow-checked add, subtract, multiply, or divide can never trap and rewrites it to the plain, faster form — sharing one range implementation with the verifier so the proof and the rejection always agree.
- **The IL spec version is now 0.3.0 (ADR 0064).** The `il` banner catches up to the surface these codegen rounds added — `switch.i32` jump tables, branchless `select`, and checked/narrow arithmetic — and snapshot builds are now date-stamped (`0.2.99.20260702`).
- **Packaging becomes a real release story (ADR 0025).** The toolchain installer now builds and ships ViperIDE, and standalone applications package as first-class AppImage, RPM, DMG, and Windows installers with desktop launchers, branded icons, and post-build verification, plus a `viper package --dry-run --json` for scripting.
- **Linux native builds are stable again.** Static archives stop pulling in libgcc helpers the native linker can't resolve, a few libstdc++ runtime imports are recognized, and non-interactive installs fall back to a local prefix when none is set.
- **Game3D runtime and test knobs.** A standalone terrain can now join physics collision, water planes recenter over off-origin terrain, GPU post-FX composites the HUD into captured frames, and new environment switches let display-requiring tests render without stealing window focus.

Demos and docs tracked the work. Ridgebound was rebuilt around the new runtime — a character-controller player crossing a spawned terrain with first-person and free-look, sprint stamina, scanner-gated beacons, boulder physics, nav-mesh critters, and weather — the ViperSQL and baseball apps moved onto the new terminal APIs, fresh IL benchmark kernels exercise the codegen round, and the packaging, Game3D, CLI, and library docs picked up the new installer payloads, canonical names, and API contract.

### By the Numbers

| Metric | v0.2.7 | v0.2.99 | Delta |
|---|---|---|---|
| Commits | — | 16 | +16 |
| Source files | 3,402 | 3,421 | +19 |
| Production SLOC | 762K | 771K | +9K |
| Test SLOC | 304K | 307K | +3K |
| ViperIDE SLOC | 28K | 28K | flat |
| Demo SLOC | 197K | 198K | +1K |

Counts via `scripts/count_sloc.sh` (production 771,478 / test 307,433 / demo 198,287 / viperide 28,238 / source files 3,421); commits since the `v0.2.7-dev` tag (2026-06-30). The range touched 1,177 files (+39,844 / −12,546) — the runtime API contract expansion, a codegen optimization round, and the Ridgebound rebuild, plus internal planning docs and regenerated golden and API-audit fixtures.

<!-- END DRAFT -->
