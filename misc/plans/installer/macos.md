# Installer Hardening — macOS Workstream

**Completable AND verifiable on a Mac now.** Parent plan: `~/.claude/plans/i-need-to-harden-transient-wren.md`.
Sibling files: `windows.md`, `linux.md`.

## Context

Viper hand-builds every installer from scratch in C++ under `src/tools/common/packaging/`
(zero external deps). `viper install-package` (`src/tools/viper/cmd_install_package.cpp`) is the
orchestrator. This file collects every part of the installer-hardening effort that we can both
**implement and fully prove out on macOS**:

- All cross-platform **correctness/hardening bug fixes** — they are byte-output changes provable
  with golden/unit tests on the host (no target OS needed).
- The **config/manifest SSOT** plumbing the whole effort depends on.
- macOS-native formats: the new **`.dmg`** drag-installer and **`.pkg`** branding — both fully
  runtime-testable here via `hdiutil`/`codesign`.

Deb/rpm *metadata* fixes live here (their byte output is golden-tested on Mac); their *live-install*
proof is tracked in `linux.md`. The Windows wizard and Linux AppImage runtime work live in their
sibling files because they need their target OS to verify.

## Scope (in / out)

**In:** X1, L1, L2, L4, M1, M2, M4, M5, X2, WS-C (DMG), X3/X4 (optional). Golden/unit tests for all.
**Out (sibling files):** WS-A Windows wizard, WS-B ARM64 stub, W3–W6 (`windows.md`); WS-D AppImage
and live deb/rpm/desktop verification (`linux.md`).

## Tasks (ordered)

1. **X1 — Config/manifest SSOT.** Add `license`, `maintainer`, `maintainerEmail`, `helpUrl` to
   `PackageConfig.hpp` and `ToolchainInstallManifest.{hpp,cpp}`. Add CLI flags + sane defaults in
   `cmd_install_package.cpp` (`parseArgs`, usage ~81-110, the value-taking-arg list ~554-562).
   Default license from project (GPL-3.0-only) but **sourced**, not hardcoded downstream.
2. **L1/L2/L4 — Linux metadata, consuming config.** Replace `LinuxPackageBuilder.cpp:1531`
   (`License: GPL-3.0-only`) and `:703,:1331` (`noreply@example.invalid`) with config values.
   Narrow `Depends` (~328-349) to the stdlib actually linked in the staged binary (detect, don't
   emit both `libstdc++6 | libc++1`).
3. **M1 — Hardened runtime default-on.** `MacOSPackageBuilder.cpp:956` currently gates
   `--options runtime` on notarization. Make it default for release; add
   `macosDisableHardenedRuntime` opt-out in `PackageConfig`.
4. **M2 — Notarization timeout/retry.** Wrap the `notarytool ... --wait` call (`:970-986`) with a
   configurable `macosNotaryTimeoutSeconds` and a bounded retry on transient submit failures.
5. **M4 — Entitlements error wording.** Confirm `resolvePackageSourcePath` (`:961-964`) names the
   missing entitlements file clearly. (Wording only — not a bug.)
6. **X2 — Up-front asset validation.** Before any build dispatch in `cmd_install_package.cpp`,
   validate that referenced icons/entitlements/license/banner/DMG-background exist with clear errors.
7. **WS-C — macOS `.dmg` drag-installer.** New `MacOSDmgBuilder.{hpp,cpp}` (or `buildMacOSDmg()` in
   `MacOSPackageBuilder.cpp`); new `MacOSDmg` target (enum:36, parse:518, usage:86, filename:699).
   `hdiutil create ... -format UDZO -volname "Viper Toolchain"`, `/Applications` symlink, styled
   background (`misc/images/viperwallpaper*.png`) + volume `.icns` (`IconGenerator`), window layout
   via `.DS_Store` template or `osascript`, then `codesign` (+ optional notarize/staple reusing
   M1/M2). `hdiutil`/`osascript` are Apple system tools — same category as the already-shelled
   `codesign`/`mkbom`/`lsregister`, so dependency policy holds.
8. **M5 — `.pkg` branding.** Add welcome/license panes + background/icon to the Distribution XML
   (`MacOSPackageBuilder.cpp` ~785-859, `PlistGenerator`).
9. **X3/X4 (optional) — Determinism** (fixed archive order/timestamps/gzip params) and demote the
   legacy CPack block (`CMakeLists.txt:749-780`) to one-system-of-record.
10. **Tests.** Golden for RPM spec (license from manifest), deb control (real maintainer), narrowed
    Depends; unit for new config parsing + asset validation; DMG structural check via `PkgVerify`
    plus a mount test. Use the `viper_add_test`/`viper_add_ctest` pattern in `src/tests/CMakeLists.txt`.

## Critical files

- `src/tools/common/packaging/PackageConfig.hpp`, `ToolchainInstallManifest.{hpp,cpp}`
- `src/tools/common/packaging/LinuxPackageBuilder.{hpp,cpp}` (metadata)
- `src/tools/common/packaging/MacOSPackageBuilder.{hpp,cpp}`, new `MacOSDmgBuilder.{hpp,cpp}`,
  `PlistGenerator.*`, `IconGenerator.*`
- `src/tools/viper/cmd_install_package.cpp` (flags, target dispatch, asset validation)
- `src/CMakeLists.txt:279-319` (`viper_packaging` sources), `src/tests/CMakeLists.txt`
- `src/buildmeta/VERSION` (SSOT)

All new/modified files get the full Viper GPL-v3 header.

## Verification

- Full `./scripts/build_viper_mac.sh` (Debug) + `ctest` green, zero warnings; then
  `./scripts/lint_platform_policy.sh` and `./scripts/run_cross_platform_smoke.sh`.
- DMG: build it, `hdiutil attach`, confirm `/Applications` symlink + layout, `codesign --verify`.
- Each P0/P1 fix gets a test that fails before and passes after.
- Structurally verify emitted deb/rpm here (`PkgVerify`); their live install is `linux.md`.

## Status — done this pass

- **X1, L1, L2, L4, M1, M2, X2(DMG assets), WS-C — DONE & verified.** Config/manifest SSOT
  (`license`/`maintainer`/`maintainerEmail`/`homepage` on `ToolchainInstallManifest`; matching
  `PackageConfig` fields + `viper install-package` flags `--license/--maintainer/--maintainer-email/
  --homepage/--macos-notary-timeout`); deb/rpm now source license + maintainer from config and
  narrow the C++ runtime dep (`libstdc++6` vs `libc++1`) by scanning the staged ELF's SONAME with a
  safe fallback; macOS signing enables the hardened runtime by default for Developer ID and bounds
  `notarytool --wait` (both the `.app` path in `MacOSPackageBuilder.cpp` and the toolchain `.pkg`
  path in `cmd_install_package.cpp`) with a configurable timeout + one retry; new
  `buildMacOSToolchainDmg` wraps the `.pkg` in a styled, compressed UDZO `.dmg`
  (`--macos-dmg [--macos-dmg-background PNG] [--macos-dmg-icon ICNS]`), Finder styling best-effort.
- **Tests:** 5 new cases in `src/tests/unit/test_packaging.cpp` (deb maintainer/homepage, default
  placeholder, deb depends narrowing for libstdc++/libc++ via a synthetic ELF, DMG UDIF `koly`
  trailer). All `test_packaging` pass; platform lint clean (baseline bumped 2→3 for the new
  Apple-only DMG guard); cross-platform smoke green.
- **X3 (reproducible archives) + X4 (CPack demotion) — DONE.** `ZipWriter::getDosTime` now stamps a
  fixed epoch in UTC by default (honoring `SOURCE_DATE_EPOCH`), removing wall-clock/timezone
  non-determinism — tar/cpio/ar/gzip already used `mtime=0`; covered by
  `ZipWriter.ProducesByteIdenticalArchivesForIdenticalInput`. The legacy CPack block is gated behind
  `-DVIPER_ENABLE_LEGACY_CPACK=ON` (default OFF), verified by fresh configures both ways.
- **Pre-existing/unrelated:** `test_codegen_arm64_ovf` and `test_linker_reloc_edge_cases` fail at
  HEAD independent of this work (diff touches no codegen/linker).

## Remaining on this workstream

- **M5 — `.pkg` welcome/license panes — DONE.** `generateMacOSToolchain{WelcomeHtml,LicenseText}`
  generate a branded welcome and an SPDX-keyed license pane (license overridable via
  `--macos-pkg-license`, optional `--macos-pkg-background`); resources are added as top-level xar
  members and referenced from the Distribution XML. Verified structurally by decoding the built
  pkg's TOC (`PkgEmbedsWelcomeAndLicensePanes`); final pane *rendering* needs a GUI install to eyeball.
- **Notarization readiness gap (follow-up).** The toolchain `.pkg` flow `productsign`s the wrapper
  but does not `codesign --options runtime` each staged Mach-O *inside* the payload; Apple
  notarization requires every executable to be hardened-signed. A future pass should sign staged
  binaries before building the `.pkg`.
