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
| Runtime libs | libviper_runtime.a + component archives | ~15 |
| Graphics/audio | libvipergfx.a, libviperaud.a, libvipergui.a | 3 |
| Headers | include/viper/*.hpp | ~20 |
| Man pages | man1/*.1, man7/*.7 | 8 |
| CMake config | ViperConfig.cmake, ViperTargets.cmake | 2 |
| VS Code ext | zia-language.vsix | 1 |
| License | LICENSE | 1 |

## Install Locations

| Category | Windows | macOS | Linux |
|----------|---------|-------|-------|
| Binaries | `C:\Program Files\Viper\bin\` | `/usr/local/viper/bin/` | `/usr/bin/` |
| Libraries | `C:\Program Files\Viper\lib\` | `/usr/local/viper/lib/` | `/usr/lib/viper/` |
| Headers | `C:\Program Files\Viper\include\` | `/usr/local/viper/include/` | `/usr/include/viper/` |
| Man pages | N/A | `/usr/local/share/man/` | `/usr/share/man/` |
| CMake | `C:\Program Files\Viper\lib\cmake\` | `/usr/local/viper/lib/cmake/` | `/usr/lib/cmake/Viper/` |
| Docs | `C:\Program Files\Viper\` | `/usr/local/viper/` | `/usr/share/doc/viper/` |
| PATH symlinks | N/A | `/usr/local/bin/` (symlinks) | N/A (installed directly) |

## Critical Cross-Cutting Concern: Runtime Library Discovery

Currently `findBuildDir()` in `src/codegen/common/LinkerSupport.cpp` (line 57) only searches for `CMakeCache.txt` by walking up directories. After installation there is no build directory. A new search strategy is needed:

1. `VIPER_LIB_PATH` environment variable (explicit override)
2. Relative to viper executable: `<exe_dir>/../lib/` (standard FHS-like layout)
3. Platform standard paths: `/usr/lib/viper/`, `/usr/local/viper/lib/`, `C:\Program Files\Viper\lib\`
4. Existing `findBuildDir()` fallback (for development)

This must be implemented as Phase 0 or early Phase 1 — without it, an installed Viper cannot compile native executables.

## Upgrade Behavior

| Platform | Mechanism | Notes |
|----------|-----------|-------|
| Windows | Installer detects existing installation via registry, offers overwrite/cancel | Registry key: `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Viper` |
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
- macOS: UTType declarations in .pkg Distribution or via `lsregister` in postinstall
- Linux: Already handled by MIME XML + .desktop file in existing infrastructure

## Phases

| Phase | Deliverable | New Files | ~LOC |
|-------|-------------|-----------|------|
| 0 | Runtime library discovery for installed Viper | 1 modified | 100 |
| 1 | Install manifest + path mapping | 2 | 350 |
| 2 | Windows GUI installer (.exe) | 4 | 1400 |
| 3 | macOS .pkg installer | 8 | 1300 |
| 4 | Linux .rpm package | 4 | 900 |
| 5 | CLI command + build scripts | 3 | 400 |
| 6 | Verification + tests | 2 | 600 |

Total: ~25 new files, ~5050 LOC

## Dependency Graph

```
Phase 0 (Library discovery)
  └── Phase 1 (Manifest)
        ├── Phase 2 (Windows .exe)
        ├── Phase 3 (macOS .pkg)
        │     └── Phase 4 (Linux .rpm)  ← shares CpioWriter from Phase 3
        └── Phase 5 (CLI orchestration) ← depends on 2-4
              └── Phase 6 (Verification)
```

Phases 2, 3, and 4 can proceed in parallel after Phase 1.
