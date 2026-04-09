# Phase 6: Verification Matrix

## Goal

Treat installer work like any other binary-format subsystem in Viper:
- fast structural tests in CTest
- builder tests that produce small real artifacts
- host-oracle checks where available
- install/uninstall smoke on the relevant OS

The verification stack should extend existing packaging tests and verifiers, not create a separate testing island.

## Reuse First

Use existing infrastructure:
- `src/tests/unit/test_packaging.cpp`
- `PkgVerify.*`
- current packaging builders for Windows/Linux/macOS app packaging
- existing smoke-test conventions and host capability gating in the test suite

Important correction to the draft:
- do not hardcode expectations around a fixed test count in the plan
- define verification by coverage layers and acceptance criteria instead

## Verification Layers

### 1. Pure format tests

These are byte-level tests with no filesystem install step.

Add coverage for:
- `CpioWriter`
- `PkgZlib`
- `PkgHash` SHA-1/SHA-256
- `XarWriter`
- `RpmWriter`
- toolchain manifest/path mapping
- installed runtime archive discovery
- installed companion-library discovery for `vipergfx`, `vipergui`, and `viperaud`
- staged-export consistency checks for `ViperConfig.cmake` / `ViperTargets.cmake`

Recommended home:
- extend `src/tests/unit/test_packaging.cpp`
- add focused companion tests only if that file becomes too large

### 2. Builder tests

Build tiny real artifacts from mock manifests and verify them structurally.

Examples:
- minimal Windows toolchain installer
- minimal macOS `.pkg`
- minimal Linux `.deb`
- minimal Linux `.rpm`
- portable tarball

These tests should verify:
- expected payload members exist
- file modes and install paths are correct
- builder metadata is internally consistent

### 3. Native verifier extensions

Extend `PkgVerify` rather than adding separate verification executables.

Recommended new entry points:
- `verifyCpio()`
- `verifyXar()`
- `verifyMacOSPkg()`
- `verifyRpm()`
- toolchain-specific helpers if needed for PE+ZIP overlays

Every builder should call native verification before reporting success.

The verification layer should also grow a small staged-install validator that can run before format generation:
- inspect staged contents
- validate runtime/support-library completeness
- validate exported-package completeness
- fail before any platform builder starts if the staged tree is already inconsistent

### 4. Host-oracle checks

Run platform tools when available, but keep them as second-line validation.

### macOS
- `pkgutil --expand-full`
- `pkgutil --payload-files`
- optional `installer -pkg ...` smoke lane

### Linux Debian hosts
- `dpkg-deb -I`
- `dpkg-deb -c`
- optional `dpkg -i` / `dpkg -r` smoke lane

### Linux RPM hosts
- `rpm -qip`
- `rpm -qlp`
- `rpm -K`
- optional `rpm -i` / `rpm -e` smoke lane

### Windows
- silent install/uninstall
- registry checks
- PATH checks
- file association checks

### 5. Installed-toolchain smoke

This is the most important end-to-end test.

For every supported host lane:
1. install the produced toolchain artifact
2. run `viper --version`
3. configure and build a tiny external CMake consumer with `find_package(Viper CONFIG REQUIRED)`
4. compile a tiny native executable with the installed CLI toolchain
5. run that executable
6. uninstall the toolchain
7. verify cleanup

That directly validates the prerequisites from Phase 0:
- staged install completeness
- installed runtime archive discovery
- installed companion-library discovery
- installed exported-package correctness

## Recommended CTest Layout

### Unit labels
- `packaging`
- `installer`
- `native_toolchain_install`

### Host-gated labels
- `requires_windows`
- `requires_macos`
- `requires_linux_deb`
- `requires_linux_rpm`
- `requires_privileged_install`

This should follow the existing capability/skip conventions already used elsewhere in the repo.

## Suggested Coverage by Phase

### After Phase 1
- manifest gathering
- path mapping
- installed runtime discovery
- exported-package smoke from the staged install tree

### After Phase 2
- PE+ZIP overlay verification
- Windows install/uninstall smoke

### After Phase 3
- cpio/xar/pkg verification
- macOS `.pkg` expansion and optional install smoke

### After Phase 4
- `.deb` and `.rpm` verification
- Linux package install/uninstall smoke

### After Phase 5
- CLI and wrapper-script smoke

## CI Recommendations

### Pull request lanes
- structural packaging tests
- mock builder tests
- staged-install validation
- host-oracle expansion/listing tests where available

### Nightly or release lanes
- privileged install/uninstall smoke on each platform
- compile-and-run smoke using the installed toolchain
- signing validation once Phase 7 exists

## Exit Criteria

This phase is done when:
- every new format writer has byte-level tests
- every installer builder has structural verification
- every supported host has an install/run/uninstall smoke path
- every staged install can be validated before packaging begins
- failures point to the right layer: format, builder, staging, or host install
