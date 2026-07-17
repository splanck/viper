# Installer Hardening — Linux Workstream

**Requires a Linux env (or QEMU) to complete and verify.** Parent plan:
`~/.claude/plans/i-need-to-harden-transient-wren.md`. Sibling files: `macos.md`, `windows.md`.

## Context

Linux packages are hand-built in `src/tools/common/packaging/LinuxPackageBuilder.{hpp,cpp}`
(`.deb` via `ArWriter`, `.rpm` via `rpmbuild`+spec, `.tar.gz` via `TarWriter`). The chosen new
target is a portable **AppImage** single-file. The deb/rpm **metadata bug fixes** (L1 license,
L2 maintainer, L4 narrowed Depends) are implemented in the **macOS session** because their byte
output is golden-testable on the host — this file covers what genuinely needs a running Linux
kernel/desktop to finish or prove.

## Scope (in / out)

**In:** WS-D (AppImage), L5 (path-traversal hardening of generated scripts), and the **live-install
verification** of the deb/rpm fixes done in `macos.md`. **Out:** everything in `macos.md`/`windows.md`.
Depends on **X1 config plumbing** (macOS session).

## Tasks

### WS-D — AppImage (recommended v1: dependency-free extract-and-exec) — DONE
- Added `LinuxRuntimeStubGen.{hpp,cpp}` with a FUSE-less self-extracting runtime stub. The v1
  runtime is a POSIX shell stub rather than the originally proposed raw ELF/syscall stub; it
  extracts the appended tar.gz payload to `$XDG_CACHE_HOME`/`$TMPDIR` and `exec`s `AppRun`.
- Payload = `TarWriter` + `PkgGzip`. It embeds `AppRun`, `.desktop` (`DesktopEntryGenerator`),
  `.png` AppIcon (`PkgPNG`/`IconGenerator`), file-association metadata, and the staged toolchain.
- Output `Zanna-<ver>-<arch>.AppImage` (+ `chmod +x` bit). Wired a new `appimage` target into the
  enum/parse/usage/filename/build/verify sites in `cmd_install_package.cpp` and
  `LinuxPackageBuilder`.
- **Depth upgrade (flagged, larger, out of v1):** true type-2 **squashfs** payload + FUSE mount —
  needs a new hand-rolled `SquashFsWriter` (can reuse our gzip). Deferred deliberately to stay
  dependency-free without a multi-week writer; this is a conscious scope cap, not a silent one.
- Add an aarch64 ELF stub variant for ARM64 Linux once x86-64 is proven.

### L5 — Generated-script hardening — DONE
- Centralized the generated `install.sh`/`uninstall.sh` path validation helpers in
  `LinuxPackageBuilder.cpp`, and made uninstall check symlink path components before removing
  manifest-listed files.

### Live verification of macOS-session deb/rpm fixes — DONE
- Tightened the existing Linux deb/rpm smoke tests to assert maintainer/license/dependency metadata.
- Added `scripts/validate-linux-toolchain-installer.sh` for Linux validation, including AppImage
  build/verify/execute and installer-labeled CTest coverage. Privileged deb/rpm installation remains
  gated by `ZANNA_RUN_LINUX_INSTALLER_SMOKE=1` and root, matching the existing smoke tests.

## Critical files

New `LinuxRuntimeStubGen.{hpp,cpp}`; `src/tools/common/packaging/LinuxPackageBuilder.{hpp,cpp}`,
`TarWriter.*`, `PkgGzip.*`, `DesktopEntryGenerator.*`, `PkgPNG.*`, `IconGenerator.*`;
`src/tools/zanna/cmd_install_package.cpp`; new `scripts/validate-linux-toolchain-installer.sh`.

## Verification

- Host (Mac) pre-checks: `LinuxRuntimeStubGen` encoder unit tests; `PkgVerify`/`readelf`-style
  structural check on the emitted ELF + AppImage layout.
- Linux E2E (required): `chmod +x Zanna-*.AppImage && ./Zanna-*.AppImage` on a clean distro (or
  QEMU) — confirm extract-and-exec, icon/.desktop integration; `dpkg -i`/`rpm -i` install + removal;
  `scripts/validate-linux-toolchain-installer.sh`.
