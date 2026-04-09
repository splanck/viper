# Phase 4: Linux Toolchain Packages (`.deb`, `.rpm`, `.tar.gz`)

## Goal

Package the staged Viper toolchain for Linux using one shared manifest and one shared Linux file-set mapping.

Deliverables:
- `.deb` built by extending the current Linux packaging stack
- `.rpm` built with a new native writer
- `.tar.gz` preserved as a portable fallback artifact

## Reuse First

The Linux plan should build on:
- `LinuxPackageBuilder.*`
- `DesktopEntryGenerator.*`
- `IconGenerator.*`
- `PkgGzip.*`
- `PkgMD5.*`
- `ArWriter.*`
- `TarWriter.*`
- `PkgVerify::verifyDeb()`
- shared `CpioWriter` and `PkgHash` from the macOS phase

Important correction to the draft:
- do not create a second, unrelated Linux package stack if the current `LinuxPackageBuilder` can be split into reusable lower-level helpers

The current builder already knows how to emit:
- `.deb`
- `.tar.gz`
- desktop entries
- MIME XML
- icon trees

The toolchain work should refactor and extend that path.

Important correction:
- toolchain packaging should preserve the staged install layout underneath the Linux package root
- do not move libraries into a new `lib/viper` subtree if the staged install and exported CMake package use the normal `lib/` layout

## Recommended Refactor

Before adding RPM support, extract the current Linux builder into two layers:

### Layer 1: shared Linux file-set packer

Input:
- normalized file list with install paths and metadata

Responsibilities:
- emit `data.tar.gz`
- emit control/data file lists
- emit icon, desktop, and MIME resources
- compute `md5sums`
- emit portable tarball from the same file set

This layer should be used by:
- existing app packaging
- new toolchain packaging

### Layer 2: format-specific wrappers

- `.deb` wrapper around the shared Linux file set
- `.rpm` wrapper around the same shared Linux file set

That avoids having three separate places that each reinvent:
- FHS mapping
- directory creation
- metadata scripts
- MIME and desktop resource generation

## `.deb` plan

Extend the existing `.deb` builder to accept a staged toolchain manifest.

Recommended toolchain metadata:
- package name: `viper`
- section: `devel`
- priority: `optional`
- installed size from manifest total
- homepage/license/description from builder metadata or release metadata

Recommended maintainer scripts:
- `postinst`
  - `mandb` if available
  - `update-mime-database` if needed
  - `update-desktop-database` if needed
- `postrm`
  - refresh `mandb`
  - refresh MIME/desktop caches if we installed those assets

Toolchain associations need a product decision before they are enabled by default:
- `.zia`
- `.bas`
- `.il`

Recommendation:
- keep file associations and desktop integration off by default for the raw toolchain package unless Viper is also shipping a user-facing editor/GUI entrypoint with sensible open behavior
- still keep the builder capable of emitting them later from the shared metadata path if the product decision changes

## `.rpm` plan

RPM is the truly new format work in this phase.

Recommended new file:
- `src/tools/common/packaging/RpmWriter.hpp/cpp`

Reused support:
- `CpioWriter`
- `PkgGzip`
- `PkgMD5`
- shared `PkgHash` for SHA-256

Design guidance:
- keep `RpmWriter` low-level and format-focused
- do not mix Linux FHS mapping or toolchain manifest scanning into `RpmWriter`
- feed it an already-normalized Linux file set from the shared Linux packer

That keeps the `.rpm` writer reusable and keeps Linux policy in one place.

## Portable archive

Do not drop the current `.tar.gz` path.

It remains useful for:
- unsupported Linux distributions
- non-root installs
- CI smoke validation
- local development/testing before `.deb` or `.rpm` install

The tarball should come from the same normalized Linux file set as `.deb` and `.rpm`.

It is also the simplest early oracle for Phase 1 correctness because it exercises the staged file set without adding distro-specific metadata.

## Recommended New / Modified Files

New:
- `src/tools/common/packaging/RpmWriter.hpp/cpp`

Modified:
- `src/tools/common/packaging/LinuxPackageBuilder.hpp`
- `src/tools/common/packaging/LinuxPackageBuilder.cpp`
- `src/tools/common/packaging/PkgVerify.hpp`
- `src/tools/common/packaging/PkgVerify.cpp`

Possible internal helper split if needed:
- `src/tools/common/packaging/LinuxPackageLayout.hpp/cpp`

## Test Plan

### Pure unit tests
- `.deb` control/data generation from normalized Linux file set
- RPM header/tag/alignment tests
- RPM payload checksum tests
- path reconstruction from RPM basename/dirname arrays

### Builder tests
- toolchain `.deb` from mock manifest
- toolchain `.rpm` from mock manifest
- portable tarball from same mock manifest
- verify MIME XML, `.desktop`, and icon assets only when that integration is enabled
- verify man pages and CMake config files are included where expected
- verify staged-relative paths are preserved under the Linux package root
- verify the installed/exported CMake package paths remain valid after packaging

### Host oracle tests

On Debian/Ubuntu CI:
- `dpkg-deb -I`
- `dpkg-deb -c`
- optional install/uninstall smoke

On RPM host CI:
- `rpm -qip`
- `rpm -qlp`
- `rpm -K`
- optional install/uninstall smoke

On generic Linux CI:
- always build `.tar.gz`
- optionally build `.deb` and verify structurally even if install is not performed

## Exit Criteria

This phase is done when:
- toolchain `.deb` and `.tar.gz` come from a refactored shared Linux file-set path
- `.rpm` uses the same normalized Linux file set instead of a parallel manifest path
- Linux host smoke can install Viper and compile a tiny native executable with the installed toolchain
