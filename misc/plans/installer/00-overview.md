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

## What Gets Installed

| Category | Files | Count |
|----------|-------|-------|
| CLI tools | viper, zia, vbasic, ilrun, il-verify, il-dis, zia-server, vbasic-server | 8 |
| Runtime libs | libviper_runtime.a + component archives | ~15 |
| Graphics/audio | libvipergfx.a, libviperaud.a | 2 |
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

## Phases

| Phase | Deliverable | New Files | ~LOC |
|-------|-------------|-----------|------|
| 1 | Install manifest + path mapping | 2 | 300 |
| 2 | Windows GUI installer (.exe) | 4 | 1200 |
| 3 | macOS .pkg installer | 8 | 1300 |
| 4 | Linux .rpm package | 4 | 900 |
| 5 | CLI command + build scripts | 3 | 400 |
| 6 | Verification + tests | 2 | 500 |

Total: ~23 new files, ~4600 LOC

## Dependency Graph

```
Phase 1 (Manifest)
  ├── Phase 2 (Windows .exe)
  ├── Phase 3 (macOS .pkg)
  │     └── Phase 4 (Linux .rpm)  ← shares CpioWriter from Phase 3
  └── Phase 5 (CLI orchestration) ← depends on 2-4
        └── Phase 6 (Verification)
```

Phases 2, 3, and 4 can proceed in parallel after Phase 1.
