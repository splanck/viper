# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.99 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.7 was cut on 2026-06-30. -->

### What this release is about

A short cleanup-and-packaging follow-up to the v0.2.7 hardening cycle. The pre-alpha runtime surface finishes settling on its canonical public names — aliases and duplicates gone, the generator refusing to mint new ones, `Count`/`Length` and boolean shapes normalized — while packaging graduates from a toolchain-only path into a full release story that ships ViperIDE and packages standalone applications as AppImage, RPM, DMG, and Windows installers. A Linux native-build stability fix and a round of Game3D runtime surface round it out; line counts move only slightly because the API work is a rename-and-prune.

- **Runtime API surface canonicalized.** The public runtime API settles on one canonical name set: `RT_ALIAS` becomes a generator error, duplicate and alias entries are removed, cardinality splits into `Count` (collections/containers, plus `BitSet` population count) versus semantic `Length`, boolean probes and toggle setters use `i1`, acronyms move to PascalCase (`UseCcd`/`PostFx`/`Pbr`/`EntityA`/`EntityB`), and simple `Entity3D` setters give way to property assignment — with `--dump-runtime-api` now printing public signature text and the whole tree (both frontends, ViperIDE, demos, docs, golden IL) rebound to the smaller surface.
- **Implementation targets stay private.** A new `RT_INTERNAL_FUNC` / `publicSurface` descriptor bit keeps targets like `Random.inst_*` available to class-method lowering while dropping them out of generated externs and the API dump; concrete GUI widget handles inherit `Viper.GUI.Widget` methods without copying them into every class catalog.
- **`Entity3D` position accessors become properties (ADR 0026).** `Position`/`WorldPosition` reclassify from zero-argument methods to read-only properties, and Zia sema now resolves runtime-class member access up front — emitting `V-ZIA-METHOD-CALL` with an add-`()` fix-it when a zero-arg method is read as a field, and an internal compiler error instead of a mistyped fallback for a genuinely unknown member.
- **Toolchain installers now ship ViperIDE.** The installable toolchain builds and stages ViperIDE by default (`VIPER_INSTALL_VIPERIDE`, generated `viperide.buildinfo`), installer manifests require every user-facing binary, and Linux/macOS/Windows packages gain desktop launchers, command symlinks, branded dependency-free Viper icons, and payload-membership verification.
- **Applications package on all three platforms (ADR 0025).** Standalone apps now ship as first-class AppImage, RPM, DMG, and Windows-installer targets with AppStream metadata, RPM `Requires`, LaunchServices roles, and publisher/wizard metadata; a manual Windows Release Installer workflow builds through the canonical script, restores optional PFX signing, verifies, and uploads the installer; and the package CLI gains license/readme/welcome and platform-specific options, inline `--option=value` forms, a `macos-dmg` alias, and `viper package --dry-run --json`.
- **Linux native build stability restored.** Outlined AArch64 atomics are disabled so static archives stop importing libgcc helpers the native linker can't resolve, `__once_proxy` is recognized as a libstdc++ runtime import, signature-whitelist registration drops `std::call_once` for TLS-relocation-free function-local statics, and non-interactive installs fall back from `/usr/local` to `build/install` when no prefix is set.
- **Game3D runtime surface and graphics test knobs.** `World3D.SpawnHeightfieldCollider` lets a standalone `Terrain3D` join physics and character-controller collision, `Water3D.SetPosition` centers planes over off-origin terrain, `Viper.Game3D.Keys` mirrors the full shared keyboard table, `Canvas3D.BackendSupports` gains CPU/GPU post-FX aliases, and the split GPU post-FX path replays the final overlay so HUD composites into presented and captured frames; `VIPER_GFX_NO_ACTIVATE`/`VIPER_GFX_HIDE_WINDOWS` (macOS and Linux/X11) let display-requiring CTest cases render without stealing focus.

Demos and docs tracked the work: the `game3d-showcase` was rebuilt around the new runtime — a `CharacterController3D` player on a spawned terrain heightfield with first-person/free-look, sprint stamina, scanner-gated beacons, boulder physics, nav-mesh critters, and weather, over split-out movement/camera/math modules — while the packaging, Game3D, CLI, and tools docs and man pages absorbed the new installer payloads, options, and canonical API names.

### By the Numbers

| Metric | v0.2.7 | v0.2.99 | Delta |
|---|---|---|---|
| Commits | — | 7 | +7 |
| Source files | 3,402 | 3,402 | 0 |
| Production SLOC | 762K | 764K | +2K |
| Test SLOC | 304K | 305K | +1K |
| ViperIDE SLOC | 28K | 28K | flat |
| Demo SLOC | 197K | 198K | +1K |

Counts via `scripts/count_sloc.sh` (production 763,977 / test 305,017 / demo 197,752 / viperide 28,238 / source files 3,402); commits since the `v0.2.7-dev` tag (2026-06-30). The range touched 758 files (+15,083 / −10,449) — mostly the API rename-and-prune and the game3d-showcase rebuild — so net SLOC moves only a couple thousand.

<!-- END DRAFT -->
