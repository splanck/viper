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

### WS-D — AppImage (recommended v1: dependency-free extract-and-exec)
- New `LinuxRuntimeStubGen.{hpp,cpp}` — a minimal **x86-64 ELF runtime stub** (raw SysV/syscall
  analog of `InstallerStubGen`; reuse ELF header knowledge from the codegen/native-compiler layer).
  On run it extracts the appended payload to `$XDG_CACHE_HOME`/`$TMPDIR` and `execve`s the entry
  point — a legitimate FUSE-less AppImage fallback mode.
- Payload = `TarWriter` + `PkgGzip` (both already exist). Embed the `.desktop`
  (`DesktopEntryGenerator`) and `.png` AppIcon (`PkgPNG`/`IconGenerator`) per the AppImage spec.
- Output `Viper-<ver>-<arch>.AppImage` (+ `chmod +x` bit). Wire a new `AppImage` target into the
  enum/parse/usage/filename sites in `cmd_install_package.cpp` (36/518/86/699) and `LinuxPackageBuilder`.
- **Depth upgrade (flagged, larger, out of v1):** true type-2 **squashfs** payload + FUSE mount —
  needs a new hand-rolled `SquashFsWriter` (can reuse our gzip). Deferred deliberately to stay
  dependency-free without a multi-week writer; this is a conscious scope cap, not a silent one.
- Add an aarch64 ELF stub variant for ARM64 Linux once x86-64 is proven.

### L5 — Generated-script hardening
- Centralize the scattered `..` / `/../` path-traversal guards in the emitted `install.sh`/
  `uninstall.sh` (currently around the install-script generation in `LinuxPackageBuilder.cpp`).

### Live verification of macOS-session deb/rpm fixes
- Confirm the fixed `.deb`/`.rpm` actually install: `dpkg -i` / `rpm -i`, correct Maintainer +
  License metadata, narrowed `Depends`/`Requires`, and that `postinst`/`postun` hooks
  (`mandb`, `update-mime-database`, `update-desktop-database`) run cleanly.

## Critical files

New `LinuxRuntimeStubGen.{hpp,cpp}`; `src/tools/common/packaging/LinuxPackageBuilder.{hpp,cpp}`,
`TarWriter.*`, `PkgGzip.*`, `DesktopEntryGenerator.*`, `PkgPNG.*`, `IconGenerator.*`;
`src/tools/viper/cmd_install_package.cpp`; new `scripts/validate-linux-toolchain-installer.sh`.

## Verification

- Host (Mac) pre-checks: `LinuxRuntimeStubGen` encoder unit tests; `PkgVerify`/`readelf`-style
  structural check on the emitted ELF + AppImage layout.
- Linux E2E (required): `chmod +x Viper-*.AppImage && ./Viper-*.AppImage` on a clean distro (or
  QEMU) — confirm extract-and-exec, icon/.desktop integration; `dpkg -i`/`rpm -i` install + removal;
  `scripts/validate-linux-toolchain-installer.sh`.
