# Viper Platform Installer — Overview

## Problem

Viper requires building from source on every machine. End users need native installers that feel like standard platform software installs: a wizard EXE on Windows, a .pkg on macOS, and .deb/.rpm packages on Linux.

## Constraint

Zero external dependencies. All format writers hand-written in C++. No NSIS, no WiX, no dpkg-deb, no rpmbuild, no productbuild, no pkgbuild.

## Existing Infrastructure (src/tools/common/packaging/)

VAPS already packages apps built WITH Viper. These format writers are fully reusable:

| Component | File | Reusable For |
|-----------|------|-------------|
| ZipWriter | ZipWriter.cpp | Windows overlay, macOS payload |
| ZipReader | ZipReader.cpp | Windows installer extraction |
| PEBuilder | PEBuilder.cpp | Windows installer PE |
| InstallerStub | InstallerStub.cpp | Windows silent install (fallback) |
| ArWriter | ArWriter.cpp | Linux .deb |
| TarWriter | TarWriter.cpp | Linux .deb, .rpm payload base |
| PkgDeflate | PkgDeflate.cpp | All compression |
| PkgGzip | PkgGzip.cpp | Linux .deb/.rpm, macOS .pkg payload |
| PkgMD5 | PkgMD5.cpp | Linux .deb checksums |
| IconGenerator | IconGenerator.cpp | All platforms |
| LnkWriter | LnkWriter.cpp | Windows shortcuts |
| PlistGenerator | PlistGenerator.cpp | macOS metadata |
| DesktopEntryGenerator | DesktopEntryGenerator.cpp | Linux .desktop |
| LinuxPackageBuilder | LinuxPackageBuilder.cpp | .deb pattern |
| WindowsPackageBuilder | WindowsPackageBuilder.cpp | .exe pattern |
| MacOSPackageBuilder | MacOSPackageBuilder.cpp | .app pattern |
| PkgVerify | PkgVerify.cpp | Structural validation |
| PackageConfig | PackageConfig.hpp | File association infrastructure |

Existing test coverage: 71 tests in `src/tests/unit/test_packaging.cpp` covering ZIP, TAR, AR, PE, LNK, ICNS, ICO, Plist, Desktop entries, and verification.

## What Gets Installed

| Category | Files | Count |
|----------|-------|-------|
| CLI tools | viper, zia, vbasic, ilrun, il-verify, il-dis, zia-server, vbasic-server | 8 |
| Runtime libs | libviper_runtime.a/.lib + component archives | ~15 |
| Graphics/audio | libvipergfx, libviperaud, libvipergui | 3 |
| Headers | include/viper/**/*.hpp | ~20+ |
| Man pages | man1/*.1, man7/*.7 | 9 |
| CMake config | ViperConfig.cmake, ViperTargets.cmake | 2 |
| VS Code ext | zia-language-0.1.0.vsix | 1 |
| License | LICENSE | 1 |

**Note on library extensions**: macOS/Linux use `.a` (e.g. `libviper_rt_base.a`). Windows with clang-cl uses `.lib` (e.g. `viper_rt_base.lib`). The manifest gatherer must handle both.

## Install Locations

| Category | Windows | macOS | Linux |
|----------|---------|-------|-------|
| Binaries | `C:\Program Files\Viper\bin\` | `/usr/local/viper/bin/` | `/usr/bin/` |
| Libraries | `C:\Program Files\Viper\lib\` | `/usr/local/viper/lib/` | `/usr/lib/viper/` |
| Headers | `C:\Program Files\Viper\include\viper\` | `/usr/local/viper/include/viper/` | `/usr/include/viper/` |
| Man pages | N/A | `/usr/local/share/man/` | `/usr/share/man/` |
| CMake | `C:\Program Files\Viper\lib\cmake\Viper\` | `/usr/local/viper/lib/cmake/Viper/` | `/usr/lib/cmake/Viper/` |
| Docs | `C:\Program Files\Viper\` | `/usr/local/viper/` | `/usr/share/doc/viper/` |
| PATH symlinks | N/A | `/usr/local/bin/` (symlinks) | N/A (installed directly) |

## Critical Cross-Cutting Concern: Runtime Library Discovery

Currently `findBuildDir()` in `src/codegen/common/LinkerSupport.cpp:59` only searches for `CMakeCache.txt` by walking up directories. After installation there is no build directory. **No installed-path search logic exists today.** A new layered search strategy is needed:

1. `VIPER_LIB_PATH` environment variable (explicit override)
2. Relative to viper executable: `<exe_dir>/../lib/` (standard FHS-like layout)
3. Platform standard paths: `/usr/lib/viper/`, `/usr/local/viper/lib/`, `C:\Program Files\Viper\lib\`
4. Existing `findBuildDir()` fallback (for development)

The installed layout is flat (`lib/libviper_rt_base.a`) while the build layout is nested (`build/src/runtime/libviper_rt_base.a`). The search must handle both.

This must be implemented as Phase 0 — without it, an installed Viper cannot compile native executables.

## Prerequisite Fix: Man Page Install Path

The root `CMakeLists.txt` install commands (lines 588-594) reference `${CMAKE_SOURCE_DIR}/man/` but the actual man pages are at `${CMAKE_SOURCE_DIR}/docs/man/`. This path mismatch means `cmake --install` silently skips man pages. Fix this before the installer can include them.

## Existing `viper package` Command

`src/tools/viper/cmd_package.cpp` implements `viper package` which compiles Viper projects into platform-specific app installers (.app, .deb, .exe, .tar.gz). This is for packaging apps built WITH Viper — different scope from the planned `viper install-package` which packages Viper ITSELF. Minimal code overlap but can share the `PlatformInstallConfig` manifest struct.

## Upgrade Behavior

| Platform | Mechanism | Notes |
|----------|-----------|-------|
| Windows | Installer detects existing installation via registry, offers overwrite/cancel | Check `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Viper` for `DisplayVersion` |
| macOS | Installer.app overwrites files at same paths automatically | postinstall script is idempotent (ln -sf) |
| Linux .deb | dpkg handles upgrades natively via version comparison | postinst must be idempotent |
| Linux .rpm | rpm -U handles upgrades | Same FHS paths ensure clean replacement |

## File Associations

Infrastructure exists in `PackageConfig.hpp` (FileAssoc struct). The installer should register:
- `.zia` → "Zia Source File" (`text/x-zia`) → open with `zia`
- `.bas` → "BASIC Source File" (`text/x-basic`) → open with `vbasic`
- `.il` → "Viper IL Module" (`text/x-viper-il`) → open with `ilrun`

Platform-specific registration:
- Windows: Registry keys under `HKCR\.zia`, `HKCR\ViperZiaFile`, `DefaultIcon`, `shell\open\command`
- macOS: UTType declarations via `lsregister` in postinstall
- Linux: Already handled by MIME XML + .desktop file in existing infrastructure

## Phases

| Phase | Deliverable | New Files | ~LOC |
|-------|-------------|-----------|------|
| 0 | Man page path fix + runtime library discovery for installed Viper | 2 modified | 120 |
| 1 | Install manifest + path mapping | 2 new | 350 |
| 2 | Windows GUI installer (.exe) | 4 new | 1400 |
| 3 | macOS .pkg installer | 8 new | 1300 |
| 4 | Linux .rpm package + .deb enhancements | 4 new | 900 |
| 5 | CLI command + build scripts | 3 new | 400 |
| 6 | Verification + tests | ~70 new test cases | 600 |

Total: ~25 new files, ~5070 LOC

## Dependency Graph

```
Phase 0 (Library discovery + man page fix)
  └── Phase 1 (Manifest)
        ├── Phase 2 (Windows .exe)
        ├── Phase 3 (macOS .pkg)
        │     └── Phase 4 (Linux .rpm)  ← shares CpioWriter from Phase 3
        └── Phase 5 (CLI orchestration) ← depends on 2-4
              └── Phase 6 (Verification)
```

Phases 2, 3, and 4 can proceed in parallel after Phase 1.
