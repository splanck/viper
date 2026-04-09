# Phase 0-1: Install Prerequisites, Installed Runtime Discovery, and Toolchain Manifest

## Why This Comes First

Every later phase depends on two things being true:
- the staged install tree is complete
- an installed `viper` can find its runtime archives and support files without a build tree

If either of those is missing, the installers may build successfully but ship a broken toolchain.

## Reuse First

Before adding any new code, this phase should reuse:
- existing `install()` rules in the root `CMakeLists.txt` and `src/CMakeLists.txt`
- CMake's generated `install_manifest.txt`
- generated runtime archive metadata in `viper/runtime/RuntimeComponentManifest.hpp`
- generated platform capability metadata in `viper/platform/Capabilities.hpp`
- generated package config/export files in `ViperConfig.cmake` and `ViperTargets.cmake`
- existing `PackageConfig::FileAssoc` rather than inventing a second file-association struct

## Phase 0: Install-Tree Prerequisites

### 0A. Fix staged install contents

The installer packager should not reach back into the source tree for normal ship artifacts. If the installer needs a file, the staging step should install it first.

Required fixes:
- fix man page install paths in the root `CMakeLists.txt` from `man/man1` and `man/man7` to `docs/man/man1` and `docs/man/man7`
- resolve the current man-page/binary mismatch for `basic-ast-dump` and `basic-lex-dump`
  - either install those tools as part of the toolchain
  - or stop staging their man pages
- ensure the staged tree includes:
  - binaries
  - static libraries
  - public headers
  - generated public headers such as `version.hpp`, `Capabilities.hpp`, and `RuntimeComponentManifest.hpp`
  - CMake package files
  - man pages
  - `LICENSE`
  - `README.md` or a dedicated release note/readme file if we want it shipped
  - any editor-extension artifact we explicitly choose to ship
- ensure the installed/exported library set is complete enough for the staged `ViperTargets.cmake` to describe the actual shipped toolchain
  - the current root export list misses runtime component libraries that are built in `src/runtime/CMakeLists.txt`
  - Phase 0 should explicitly reconcile that before installer work relies on the installed export set

Recommendation:
- add toolchain docs under `${CMAKE_INSTALL_DOCDIR}/viper/`
- keep the shipped-file list conservative; do not install development-only docs by default
- if the VS Code extension is shipped, stage the `.vsix` artifact only; do not stage the unpacked extension source tree or its `node_modules`

### 0B. Installed runtime discovery

Current installed-link support is still build-tree-centric in several places:
- `src/codegen/common/LinkerSupport.cpp`
- `src/codegen/x86_64/CodegenPipeline.cpp`
- `src/codegen/aarch64/CodegenPipeline.cpp`

This phase is not done until an installed `viper` can discover all of its native-link inputs from the installed layout, not just the runtime component archives.

#### Required search order

1. `VIPER_LIB_PATH`
2. executable-relative install layout, using the real/canonical executable path when launchers are symlinked
3. configured standard install locations
4. current build-tree fallback

Recommended search logic:
- executable-relative:
  - `<exe_dir>/../lib`
- standard locations:
  - Linux: `/usr/lib`, `/usr/local/lib`, plus distro-specific `lib64` variants if the staged install/export layout uses them
  - macOS: `/usr/local/viper/lib` or another compiled-in install prefix only when it matches the staged/exported layout policy
  - Windows: prefer executable-relative and explicit env override; avoid hard-coded `Program Files` guessing

#### Implementation guidance

Do not create a second archive naming table for installed layouts. Reuse:
- `archiveNameForComponent()`
- `RuntimeComponentManifest.hpp`

Add a small installed-layout helper instead:
- build-layout path resolver
- installed-layout path resolver
- top-level function that tries installed layout first, then build layout

The same resolver family must also cover companion libraries such as `vipergfx`, `viperaud`, and `vipergui`.

Important detail:
- those companion libraries are currently looked up directly in the backend pipelines, not only through `runtimeArchivePath()`
- Phase 0 should therefore update the common resolver and then thread it through the x86_64 and AArch64 codegen pipelines so native-link behavior is consistent between build-tree and installed-tree execution

### 0C. Validation rules

Installed discovery must verify:
- required runtime component archives exist
- required companion libraries exist when capability-enabled features need them
- capability-gated libraries match the installed platform config
- diagnostics clearly report which archive or support library was missing
- the installed CMake package files can still resolve the installed targets they refer to

## Phase 1: Shared Toolchain Manifest

### Goal

Define one manifest for "everything that ships in a Viper toolchain install" and make every platform builder consume it.

This manifest is not a replacement for `PackageConfig`. `PackageConfig` remains the app-packaging config parsed from `viper.project`. The new manifest is the output of staging and validation for packaging Viper itself.

## Recommended New Files

### `src/tools/common/packaging/ToolchainInstallManifest.hpp/cpp`

Suggested model:

```cpp
namespace viper::pkg {

enum class ToolchainFileKind {
    Binary,
    RuntimeArchive,
    SupportLibrary,
    Library,
    Header,
    CMakeConfig,
    ManPage,
    Doc,
    Extra,
};

struct ToolchainFileEntry {
    ToolchainFileKind kind;
    std::filesystem::path stagedAbsolutePath;
    std::string stagedRelativePath;
    uint64_t sizeBytes;
    uint32_t unixMode;
    bool executable;
};

struct ToolchainInstallManifest {
    std::string version;
    std::string arch;
    std::vector<ToolchainFileEntry> files;
    std::vector<FileAssoc> fileAssociations;

    uint64_t totalSizeBytes() const;
};

enum class InstallPathPolicy {
    WindowsProgramFilesRoot,
    MacOSUsrLocalViperRoot,
    LinuxUsrRoot,
    PortableArchive,
};

ToolchainInstallManifest gatherToolchainInstallManifest(
    const std::filesystem::path& stagePrefix,
    std::optional<std::filesystem::path> installManifestPath = std::nullopt);

std::string mapInstallPath(const ToolchainFileEntry& file, InstallPathPolicy policy);

} // namespace viper::pkg
```

## Manifest gathering rules

Preferred source:
- `install_manifest.txt` from the build tree, if available

Fallback:
- recursive walk of the staged prefix

Why prefer `install_manifest.txt`:
- it reflects exactly what CMake installed
- it avoids drift between install rules and ad-hoc directory scans
- it preserves symlink-aware intent if we later add staged symlinks

The gatherer should normalize and categorize files into the manifest, not blindly mirror directory names.

## Install path mapping

Centralize path mapping in one place. Do not duplicate Windows/macOS/Linux path decisions across builders.

Recommended rule:
- preserve `stagedRelativePath` underneath the chosen install root
- use `ToolchainFileKind` for validation, metadata policy, and filtering, not for rebuilding a second hand-maintained directory map

Examples:

### Windows
- install root: `C:\Program Files\Viper`
- `bin/viper.exe` -> `C:\Program Files\Viper\bin\viper.exe`
- `lib/cmake/Viper/ViperConfig.cmake` -> `C:\Program Files\Viper\lib\cmake\Viper\ViperConfig.cmake`

### macOS
- install root: `/usr/local/viper`
- `bin/viper` -> `/usr/local/viper/bin/viper`
- `lib/cmake/Viper/ViperConfig.cmake` -> `/usr/local/viper/lib/cmake/Viper/ViperConfig.cmake`
- command symlinks are a platform-specific post-install policy and should not require duplicating the staged tree itself

### Linux
- install root: `/usr`
- `bin/viper` -> `/usr/bin/viper`
- `lib/libviper_runtime.a` -> `/usr/lib/libviper_runtime.a` or the staged/install-configured libdir equivalent
- `lib/cmake/Viper/ViperConfig.cmake` -> `/usr/lib/cmake/Viper/ViperConfig.cmake` or the staged/install-configured libdir equivalent

Important correction:
- do not invent a Linux-only `lib/viper` subtree if the staged install and exported CMake package use the normal `lib/` install layout
- if distro-specific `lib64` or multiarch directories are needed, let the staged install layout drive that outcome

## Validation rules

The gatherer should fail fast on:
- missing `viper` binary
- empty runtime archive set
- missing generated CMake package files
- missing runtime component archives that are required by the generated runtime manifest
- missing support libraries such as `vipergfx`, `vipergui`, or `viperaud` when the generated capability/config state says they should ship
- incomplete installed/exported target coverage relative to the staged runtime component set

The gatherer should warn, not fail, on:
- missing optional extras such as editor extensions
- capability-disabled libraries that were intentionally not installed
- omitted file associations for the toolchain package if the product decision is to keep them off by default

## Modified Existing Files

- `CMakeLists.txt`
  - fix man page source paths
  - install docs/license artifacts that should ship
  - reconcile `VIPER_PUBLIC_LIB_TARGETS` with the actual shipped runtime component libraries
- `src/codegen/common/LinkerSupport.cpp`
  - add installed-layout discovery
- `src/codegen/common/LinkerSupport.hpp`
  - expose installed-layout helpers as needed
- `src/codegen/x86_64/CodegenPipeline.cpp`
  - replace build-tree-only companion-library lookups with shared installed/build-layout resolution
- `src/codegen/aarch64/CodegenPipeline.cpp`
  - replace build-tree-only companion-library lookups with shared installed/build-layout resolution
- `src/CMakeLists.txt`
  - add `ToolchainInstallManifest.cpp` to `viper_packaging`

## Test Plan

### Unit tests
- installed-runtime discovery via `VIPER_LIB_PATH`
- installed-runtime discovery via executable-relative layout
- build-layout fallback preserved
- manifest gather from mock staged prefix
- manifest gather from mock `install_manifest.txt`
- path mapping for Windows, macOS, Linux, portable archive
- validation of runtime component completeness using generated manifest data
- validation that the installed export set covers the staged/public runtime libraries we expect to ship

### Integration tests
- `cmake --install` to a temp prefix
- gather toolchain manifest from that prefix
- run installed `viper --version`
- compile a tiny native executable with the installed toolchain
- configure and build a tiny external CMake consumer with `find_package(Viper CONFIG REQUIRED)` against the staged install tree

## Exit Criteria

This phase is done when:
- staged installs contain every file later installer phases need
- installed `viper` can find runtime archives without a build tree
- installed `viper` can find required companion graphics/audio libraries without a build tree
- the staged export/config package is self-consistent and usable by an external CMake consumer
- platform builders can consume a single validated toolchain manifest
