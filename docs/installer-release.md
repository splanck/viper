# Installer and Package Release Guide

Viper produces native toolchain installers for Windows, macOS, and Linux from
one validated `cmake --install` stage. This guide defines the supported
developer and release paths, the trust gates, generated metadata, lifecycle
guarantees, and native-host validation commands. ADR 0073 records the release
pipeline decision.

## Build from one staged tree

Use the platform build script for the full build. Do not invoke raw CMake as a
replacement for the canonical build:

```text
scripts\build_viper_win.cmd
./scripts/build_viper_mac.sh
./scripts/build_viper_linux.sh
```

The installer wrappers run the same build path and then call
`viper install-package`:

```text
scripts\build_installer.cmd --target windows --output-dir artifacts
./scripts/build_installer.sh --target macos --macos-dmg --output-dir artifacts
./scripts/build_installer.sh --target all --output-dir artifacts
```

For repeatable package work, stage once and package that immutable tree:

```bash
cmake --install build --prefix "$PWD/build/release-stage"
build/src/tools/viper/viper install-package \
  --stage-dir build/release-stage --target all --output-dir artifacts
```

`install-package` inspects the staged `viper` executable and rejects a target or
architecture that conflicts with its PE, Mach-O, or ELF header. A macOS
universal stage must contain valid, in-bounds Mach-O slices; a single-slice
binary is never relabeled as universal.

## Output contract

Use `--output-file` when exactly one artifact is expected and `--output-dir`
when a target emits more than one. The legacy `-o` option is a file for one
target unless it names an existing directory; it is a directory for multiple
targets. This removes the former ambiguity where an extensionless file name was
silently treated as a directory.

Every successful invocation writes:

- one `<artifact>.sha256` sidecar per artifact;
- `SHA256SUMS` for a directory or multi-artifact output; and
- a schema-versioned JSON inventory, normally `viper-artifacts.json`, containing
  file name, format, platform, architecture, version, byte size, SHA-256,
  verification state, trust state, and `SOURCE_DATE_EPOCH`.

Override the inventory location with `--artifact-manifest`. Metadata and
sidecars are written atomically after all selected artifacts pass. Release
generation takes a directory lock, refuses to overwrite an existing artifact
set, and removes partial outputs on failure.

Verify an artifact and its sidecar together:

```bash
viper install-package --verify-only artifacts/viper-toolchain.run --require-checksum
```

The Linux self-extracting toolchain is a `.run` bundle selected by
`--target linux-bundle`. Toolchain packaging does not accept an `appimage`
target or infer `.AppImage` during verification: the bundle is not an AppImage
filesystem image and has one unambiguous public name.

## Release mode

Set `SOURCE_DATE_EPOCH` to a non-negative integer, normally the release commit
timestamp, then add `--release`:

```bash
export SOURCE_DATE_EPOCH="$(git log -1 --format=%ct)"
```

Release mode rejects `--no-verify`, `--windows-sign-no-verify`, debug Windows
toolchains, missing trust credentials, and an invalid or absent
`SOURCE_DATE_EPOCH`. Its trust requirements are platform-specific:

### Windows

Windows release output requires Authenticode signing. Prefer a certificate
already imported into the certificate store:

```powershell
$env:VIPER_WINDOWS_SIGN_THUMBPRINT = "<SHA-1 thumbprint>"
$env:VIPER_WINDOWS_TIMESTAMP_URL = "https://timestamp.digicert.com"
viper install-package --stage-dir build\release-stage --target windows `
  --output-file artifacts\viper-toolchain-windows-x64.exe `
  --windows-sign --release
```

A PFX can instead be supplied with `VIPER_WINDOWS_SIGN_PFX` and
`VIPER_WINDOWS_SIGN_PASSWORD`. That path passes the password to `signtool` and
therefore also requires `VIPER_WINDOWS_SIGN_PASSWORD_ARGV_OK=1`; importing the
PFX into the ephemeral user certificate store avoids that exposure. Timestamp
URLs must use HTTPS. Signing is followed by `signtool verify /pa /all /tw /v`.

### macOS

macOS release output requires all four gates:

- `VIPER_MACOS_APP_SIGN_IDENTITY` for every nested Mach-O executable and helper
  app;
- `VIPER_MACOS_SIGN_IDENTITY` for the product package;
- `VIPER_MACOS_NOTARY_PROFILE` for `notarytool`; and
- `--macos-staple` for both the package and optional disk image.

```bash
viper install-package --stage-dir build/release-stage --target macos \
  --macos-dmg --output-dir artifacts --macos-staple --release
```

The builder verifies nested code signatures, the signed product package,
notarization results, stapled tickets, Gatekeeper package assessment, the DMG
UDIF structure, the read-only mounted image, and Gatekeeper's `open` assessment.
Use `--macos-min-version` to override the architecture-based deployment floor.

### Linux

Linux release output requires an OpenPGP key for `.deb` and `.rpm` artifacts:

```bash
export VIPER_LINUX_SIGN_KEY="<GPG key id>"
viper install-package --stage-dir build/release-stage --target all \
  --output-dir artifacts --release
```

Debian output is signed with `dpkg-sig` and must report `GOODSIG`; RPM output is
signed with `rpmsign` and must pass `rpmkeys --checksig` (or the `rpm` fallback).
The `.run` and portable tarball use the generated SHA-256 and inventory trust
records rather than package-manager signatures.

## Installer behavior and user experience

### Windows setup

The Windows installer uses a compact native wizard with an explicit license
acceptance control, the actual current-user or all-users scope, the resolved
destination, quiet automation, accurate cancellation code 1602, DPI awareness,
modern common controls, long-path awareness, Viper branding, and honest success
and error messages. x64 and ARM64 use one transactional PowerShell file backend;
the script is gzip-packed so the command stays below Windows' real 32,767
character process limit.

The backend reads stored overlay ranges directly from the signed executable,
streams SHA-256 verification, validates every manifest path, rejects extra or
missing payload files, rejects reparse-point traversal, and refuses to replace
unowned files. It stages the new payload beside the destination, snapshots PATH
and affected registry trees, journals backups, removes stale owned files only
after preflight, and retains rollback state until native metadata succeeds.
Either uninstaller recovers an interrupted transaction before removal. Shortcut
ownership is persisted separately so an upgrade cannot claim an unrelated
Desktop or Start Menu shortcut. PATH uninstall removes only the token Viper
actually added.

### macOS Installer and disk image

The product package supplies welcome, license, read-me, destination, and
conclusion panes with generated light/dark artwork. Distribution metadata
declares the allowed host architectures, root-volume restriction, install
domains, and minimum OS. The installed handler app also carries its minimum OS
in `Info.plist`.

The optional DMG has a generated background, volume icon, positioned package
icon, and Applications link. Finder styling is bounded so a headless builder
cannot hang indefinitely; the resulting image is always converted to and
remounted as read-only before it is accepted. Volume names and AppleScript
strings are validated and escaped, and input/output image collisions are
rejected.

### Linux packages and bundle

Debian and RPM packages use conservative runtime dependencies. Developer tools
such as CMake, make, compilers, desktop cache helpers, and manpage cache helpers
are recommendations rather than hard runtime dependencies. Packages include
copyright/README/license metadata, desktop and MIME registrations, hicolor
icons, and post-signature verification.

The `.run` bundle uses an XDG content-addressed cache keyed by the complete
payload hash. It rejects unsafe ownership, permissions, and symlink components;
serializes concurrent first extraction with a recoverable lock; extracts into
an atomic staging directory; verifies the payload before reuse; and supports
quiet and colored terminal output. It does not require FUSE.

Portable Linux tarball `install.sh` and `uninstall.sh` support `PREFIX`,
`DESTDIR`, `--dry-run`, `--force`, and `--quiet`. Installation preflights
unowned conflicts, journals same-filesystem backups, rolls back failures,
removes stale owned files on upgrade, and records the actual prefix. Uninstall
is also transactional and preserves unrelated files.

## Native release workflows

The manually dispatched workflows are:

- `.github/workflows/windows-release-installer.yml`
- `.github/workflows/macos-release-installer.yml`
- `.github/workflows/linux-release-installer.yml`

Each uses the canonical build script, packages a staged tree, verifies every
artifact and checksum, applies native trust when `release=true`, runs lifecycle
checks on a disposable host, and uploads only the resulting artifact directory.
Every native job builds a baseline containing an owned stale file, upgrades to
the final package, proves stale cleanup, preserves an unrelated sentinel,
exercises installed compilation, and uninstalls.

Workflow signing secrets are documented by their references in each YAML file.
Use automation-only signing material and ephemeral runner keychains/keyrings.
No workflow writes private keys into an artifact.

## Native validation handoff

### Windows clean VM

Build and package in a Developer Command Prompt:

```bat
set VIPER_BUILD_TYPE=Release
set VIPER_SKIP_INSTALL=1
set VIPER_EXTRA_CMAKE_ARGS=-DVIPER_INSTALL_VIPERIDE=ON
scripts\build_viper_win.cmd
cmake --install build --prefix "%CD%\build\release-stage" --config Release
build\src\tools\viper\Release\viper.exe install-package --stage-dir build\release-stage --target windows --output-file build\viper-toolchain.exe
build\src\tools\viper\Release\viper.exe install-package --verify-only build\viper-toolchain.exe --require-checksum
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\validate-windows-toolchain-installer.ps1 -Installer build\viper-toolchain.exe
```

To exercise the upgrade path, package a baseline stage containing
`share\viper\installer-upgrade-stale.txt`, remove that file before generating
the final installer, then add
`-BaselineInstaller build\viper-toolchain-baseline.exe` to the validation
script. Add `-RequireSignature` for a signed release candidate.

### Linux disposable host or VM

```bash
export VIPER_BUILD_TYPE=Release VIPER_SKIP_INSTALL=1
export VIPER_EXTRA_CMAKE_ARGS=-DVIPER_INSTALL_VIPERIDE=ON
./scripts/build_viper_linux.sh
cmake --install build --prefix "$PWD/build/release-stage"
build/src/tools/viper/viper install-package --stage-dir build/release-stage \
  --target all --output-dir build/installers
for artifact in build/installers/*.deb build/installers/*.rpm \
                build/installers/*.run build/installers/*.tar.gz; do
  build/src/tools/viper/viper install-package --verify-only "$artifact" --require-checksum
done
ctest --test-dir build --output-on-failure -R '^linux_toolchain_bundle_smoke$'
sudo env VIPER_RUN_LINUX_INSTALLER_SMOKE=1 \
  ctest --test-dir "$PWD/build" --output-on-failure \
  -R '^linux_toolchain_(deb|rpm)_installer_smoke$'
```

The privileged package-manager tests refuse to run when a Viper package is
already installed. Use only a disposable machine: they intentionally install
under `/usr`, perform a same-version baseline-to-current upgrade, build and run
an installed CMake/native-codegen consumer, remove the package, and check that
unrelated content survives.

### macOS disposable host

```bash
export VIPER_BUILD_TYPE=Release VIPER_SKIP_INSTALL=1
./scripts/build_viper_mac.sh
ctest --test-dir build --output-on-failure -R '^macos_toolchain_installer_smoke$'
sudo env VIPER_RUN_MACOS_INSTALLER_SMOKE=1 \
  ctest --test-dir "$PWD/build" --output-on-failure \
  -R '^macos_toolchain_installer_smoke$'
```

The privileged macOS test refuses a host with an existing installation, asks
Installer.app to evaluate the Distribution choices, installs into `/usr/local`,
upgrades from a baseline package when `VIPER_BASELINE_PACKAGE` is supplied,
proves stale-owned cleanup and unrelated-file preservation, checks all commands,
CMake discovery and native codegen, runs the installed uninstaller, and verifies
package receipt removal.
