# Phase 3: macOS `.pkg` Toolchain Installer

## Goal

Produce a native flat macOS `.pkg` for the Viper toolchain that installs into `/usr/local/viper` and creates command symlinks in `/usr/local/bin`.

This phase is new format work, but it should still reuse the packaging pieces Viper already has wherever possible.

## Reuse First

Use existing infrastructure for:
- icon generation: `IconGenerator.*`
- plist/XML string assembly patterns: `PlistGenerator.*`
- compression primitive: `PkgDeflate.*`
- gzip primitive where applicable: `PkgGzip.*`
- verification home: `PkgVerify.*`
- staging/source-of-truth logic from Phase 1

Use existing host tooling only as an oracle:
- CPack `productbuild` config in the root `CMakeLists.txt`
- `pkgutil`
- `installer`

Important correction to the draft:
- do not treat `MacOSPackageBuilder` as throwaway code
- do not invent separate icon/plist/file-association models for installer work

`MacOSPackageBuilder` still owns app packaging. The `.pkg` work should reuse its metadata helpers and file-association knowledge where relevant, not duplicate them.

## What Is Actually New

Viper does not currently have native writers for:
- `cpio` newc
- `xar`
- zlib framing for xar TOC/data
- SHA-1 needed by xar

That is the genuinely new work in this phase.

Precondition from Phase 0:
- the staged install tree must already contain the exact ship set, including generated headers, license/readme artifacts, and any optional extras that are intentionally shipped
- the macOS builder should package that staged tree under `/usr/local/viper` without inventing a second file layout

## Recommended New Shared Utilities

Instead of phase-local one-off hash files, add one shared packaging-side hash utility:

- `PkgHash.hpp/cpp`
  - `sha1()`
  - `sha1Hex()`
  - `sha256()`
  - `sha256Hex()`

Implementation source:
- factor or port the existing SHA implementations from `src/runtime/text/rt_hash.c`
- keep the packaging-side API simple and allocation-free

This avoids creating `PkgSHA1` in one phase and `PkgSHA256` in another with duplicated scaffolding.

## Recommended New Files

- `src/tools/common/packaging/CpioWriter.hpp/cpp`
- `src/tools/common/packaging/XarWriter.hpp/cpp`
- `src/tools/common/packaging/PkgZlib.hpp/cpp`
- `src/tools/common/packaging/PkgHash.hpp/cpp`
- `src/tools/common/packaging/MacOSToolchainPkgBuilder.hpp/cpp`

## Builder responsibilities

### `MacOSToolchainPkgBuilder`

Input:
- `ToolchainInstallManifest`

Output:
- unsigned flat `.pkg`

Responsibilities:
1. map manifest entries to `/usr/local/viper/<stagedRelativePath>`
2. build `Payload` as `gzip(cpio(newc))`
3. build `Scripts` archive with:
   - `postinstall` for `/usr/local/bin` symlinks
   - optional `preinstall` cleanup for stale symlinks
4. build `PackageInfo`
5. build `Distribution`
6. add resources such as license text
7. assemble the final xar archive

## Installer policy

Recommended layout:
- main install root: `/usr/local/viper`
- command symlinks:
  - `/usr/local/bin/viper`
  - `/usr/local/bin/zia`
  - `/usr/local/bin/vbasic`
  - `/usr/local/bin/ilrun`
  - `/usr/local/bin/il-verify`
  - `/usr/local/bin/il-dis`
  - `/usr/local/bin/zia-server`
  - `/usr/local/bin/vbasic-server`

Postinstall responsibilities:
- create or refresh symlinks
- optionally refresh man page caches if present

Important detail:
- symlink creation should resolve to the real toolchain binaries under `/usr/local/viper/bin`
- installed runtime discovery must therefore use a canonicalized executable path when a tool is launched through `/usr/local/bin`

The package itself should stay unsigned in the core builder. Signing and notarization live in the release phase.

## Verification strategy

Viper-native verification should be the primary gate:
- extend `PkgVerify` with:
  - `verifyCpio()`
  - `verifyXar()`
  - `verifyMacOSPkg()`

Host-side oracle checks on macOS CI:
- `pkgutil --expand-full`
- `pkgutil --payload-files`
- optional privileged install smoke

Existing CPack `productbuild` support should be used as a comparison oracle during development, not as the production implementation path.

## Modified Existing Files

- `src/CMakeLists.txt`
  - add new packaging sources to `viper_packaging`
- `src/tools/common/packaging/PkgVerify.hpp`
- `src/tools/common/packaging/PkgVerify.cpp`

## Test Plan

### Pure unit tests
- `CpioWriter` entry encoding, alignment, trailer, symlink handling
- `PkgZlib` round-trip and Adler-32 vectors
- `PkgHash` SHA-1/SHA-256 known-answer tests
- `XarWriter` header, TOC compression, checksum validation, heap offsets

### Builder tests
- minimal `.pkg` build from a mock manifest
- verify `Distribution`, `PackageInfo`, `Payload`, and `Scripts` exist
- verify payload expands to expected install paths
- verify `postinstall` contains symlink creation logic
- verify the builder preserves staged-relative layout under `/usr/local/viper`

### macOS host integration
- generate `.pkg`
- expand with `pkgutil`
- optionally install in CI or a privileged nightly lane
- run installed `viper --version`
- compile a tiny native executable using the installed toolchain

## Exit Criteria

This phase is done when:
- Viper can generate a structurally valid flat `.pkg` without `pkgbuild` or `productbuild`
- verification is native first, with `pkgutil` only as an oracle
- installed Viper works from the installed prefix without a build tree
