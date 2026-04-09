# Viper Toolchain Installers — Overview

## Scope

This plan set covers packaging Viper itself, not packaging apps built with Viper.

Target outputs:
- Windows: self-extracting `.exe`
- macOS: flat `.pkg`
- Linux: `.deb`, `.rpm`, and a portable `.tar.gz` fallback

The input to this workflow is a staged `cmake --install` tree. The packager should treat that staged install tree as the source of truth for what ships.

## What Already Exists

Viper already has a substantial packaging stack. The installer work should extend it, not route around it.

| Existing piece | Current file(s) | Reuse in installer work |
|---|---|---|
| Packaging library target | `src/CMakeLists.txt` (`viper_packaging`) | Home for all installer-format code |
| App packaging CLI | `src/tools/viper/cmd_package.cpp` | Reuse command patterns, output naming, verification flow |
| Windows self-extracting installer | `src/tools/common/packaging/WindowsPackageBuilder.*` | Base path for Windows toolchain installer |
| Windows install/uninstall stubs | `src/tools/common/packaging/InstallerStub.*`, `InstallerStubGen.hpp` | Extend for PATH, file associations, and toolchain payload layout |
| Linux `.deb` builder | `src/tools/common/packaging/LinuxPackageBuilder.*` | Refactor/extend for toolchain packaging |
| Linux portable tarball builder | `src/tools/common/packaging/LinuxPackageBuilder.*` | Keep as fallback artifact |
| macOS `.app` bundle builder | `src/tools/common/packaging/MacOSPackageBuilder.*` | Reuse icon/plist/bundle knowledge; not the final `.pkg` format |
| Common metadata types | `src/tools/common/packaging/PackageConfig.hpp` | Reuse `FileAssoc`; do not duplicate app/package metadata primitives |
| Format writers | `ZipWriter`, `ZipReader`, `ArWriter`, `TarWriter`, `PEBuilder`, `PkgDeflate`, `PkgGzip`, `PkgMD5` | Reuse directly |
| Generators | `IconGenerator`, `LnkWriter`, `PlistGenerator`, `DesktopEntryGenerator` | Reuse directly |
| Structural verification | `src/tools/common/packaging/PkgVerify.*` | Extend instead of building separate verifier programs |
| Install metadata from CMake | `cmake --install`, `install_manifest.txt`, generated CMake package files | Primary source for staged packaging |
| Runtime component metadata | `include/viper/runtime/RuntimeComponentManifest.hpp.in` | Validate that installed runtime archives are complete |
| Build capability metadata | `include/viper/platform/Capabilities.hpp.in` | Validate platform-conditional install contents |
| CPack config | root `CMakeLists.txt` | Optional host-side oracle, not the shipping implementation |

## Current Gaps

1. Installed Viper cannot yet discover all native-link inputs outside the build tree. The current gaps are not only runtime archives in `LinkerSupport.cpp`, but also companion graphics/audio libraries in `LinkerSupport.cpp`, `src/codegen/x86_64/CodegenPipeline.cpp`, and `src/codegen/aarch64/CodegenPipeline.cpp`.
2. The root install rules still point man page installation at `man/` instead of `docs/man/`.
3. The staged install tree is not yet validated as a complete ship set. In practice that means the installer plans still need an explicit policy for `LICENSE`, README/release notes, optional editor artifacts such as the VS Code `.vsix`, and generated headers.
4. The exported/installable target set is currently incomplete relative to the runtime components that are actually built. The plan must explicitly fix the missing installed/exported runtime libraries before packaging is treated as complete.
5. There is still a mismatch today between some staged man pages and the binaries that are actually installed, notably `basic-ast-dump` and `basic-lex-dump`.
6. There is no toolchain-specific manifest that maps staged files to platform install locations while preserving the staged install layout.
7. Viper has no native writers yet for flat macOS `.pkg`, `xar`, `cpio`, or `.rpm`.
8. There is no CLI entrypoint dedicated to packaging the Viper toolchain.
9. Signing/notarization/release handling is only implicit and should be a separate phase.

## Guiding Decisions

### 1. Package from the staged install tree

Do not package directly from the build tree unless there is no staged equivalent yet. If a file belongs in the installer, prefer fixing `install()` rules so `cmake --install` stages it correctly.

Corollary:
- do not invent a second "installer layout" when the staged tree already provides the correct relative layout for `bin/`, `lib/`, `include/`, `share/`, and `lib/cmake/Viper/`
- platform builders may re-root the staged tree under a platform-specific prefix, but they should preserve the staged relative paths underneath that root

### 2. Extend existing builders first

Do not create parallel Windows/Linux/macOS packaging stacks if the current builders can be generalized. The default approach is:
- refactor shared code out of an existing builder
- add a toolchain-oriented entrypoint on top of it
- keep app packaging and toolchain packaging on the same lower-level primitives

### 3. Keep one shared manifest

Every platform builder should consume the same toolchain manifest. Platform-specific code should only map that manifest into:
- install paths
- metadata files
- platform post-install behavior

### 4. Separate format generation from signing

Unsigned or locally signed artifacts are the output of the core builder. Authenticode, `productsign`, notarization, and RPM GPG signing are release steps, not format-generation steps.

### 5. Use host tools as verification oracles, not core dependencies

`pkgutil`, `installer`, `dpkg-deb`, `rpm`, `signtool`, and CPack are valuable validation tools on host CI. They should not be required to generate the package bytes in the normal path.

## Recommended Phase Order

| Phase | Deliverable | Notes |
|---|---|---|
| 0 | Install-tree prerequisites | Fix install rules and installed runtime discovery first |
| 1 | Toolchain manifest + path mapping | Shared data model for every platform |
| 2 | Windows toolchain installer | Extend current PE+ZIP installer path |
| 3 | macOS `.pkg` | New format writers, but reuse current metadata/icon helpers |
| 4 | Linux toolchain packages | Extend current `.deb`, add `.rpm`, keep tarball fallback |
| 5 | CLI and scripts | `viper install-package` + staging/release wrappers |
| 6 | Verification matrix | Unit, structural, staging, and install/uninstall tests |
| 7 | Signing and release automation | Post-build only |

## Dependency Graph

```text
Phase 0: install rules + installed runtime discovery
  -> Phase 1: shared toolchain manifest
     -> Phase 2: Windows
     -> Phase 3: macOS
     -> Phase 4: Linux
        -> Phase 5: CLI and scripts
           -> Phase 6: verification matrix
              -> Phase 7: signing and release
```

## Expected End State

After all phases:
- `cmake --install` stages a complete toolchain tree
- the staged tree is internally consistent: installed binaries, man pages, generated headers, exported targets, and optional extras all match the intended ship set
- `viper install-package` can turn that staged tree into native installers
- installed Viper can compile native executables without needing a build tree or source tree
- the installed `ViperConfig.cmake` / `ViperTargets.cmake` export works from the packaged install tree
- Windows/macOS/Linux installers all come from the same manifest and verification flow
- CPack remains a comparison oracle during implementation, not a parallel packaging stack
- release signing is layered on top of already-valid unsigned artifacts
