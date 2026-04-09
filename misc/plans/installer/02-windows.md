# Phase 2: Windows Toolchain Installer

## Goal

Ship a Windows-native Viper toolchain installer without replacing the packaging stack that already exists.

Primary deliverable:
- a self-extracting `.exe` that installs the staged Viper toolchain into `Program Files`, writes uninstall metadata, supports silent install/uninstall, and can optionally register PATH and file associations

Secondary deliverable:
- an optional GUI shell later, only if the existing stub-based flow proves insufficient

## Reuse First

The core implementation should extend these existing pieces:
- `WindowsPackageBuilder.*`
- `InstallerStub.*`
- `InstallerStubGen.hpp`
- `PEBuilder.*`
- `ZipWriter.*`
- `LnkWriter.*`
- `IconGenerator.*`
- `PkgVerify::verifyPEZipOverlay()`

Important correction to the draft:
- do not start by creating a parallel `ViperWindowsPackageBuilder` plus standalone `ViperInstaller.cpp` and `ViperUninstaller.cpp`
- the current Windows installer path already knows how to:
  - build a PE+ZIP overlay
  - generate install and uninstall stubs
  - create shortcuts
  - register uninstall metadata

The right first move is to extend that path for a toolchain payload.

## Recommended Design

### 2A. Generalize the current Windows package builder

Refactor `WindowsPackageBuilder` into:
- shared overlay/layout assembly
- app-packaging entrypoint
- toolchain-packaging entrypoint

Suggested addition:

```cpp
struct WindowsToolchainBuildParams {
    ToolchainInstallManifest manifest;
    std::string outputPath;
    bool addToUserPath{true};
    bool registerFileAssociations{false};
    bool createStartMenuShortcut{true};
    bool createDesktopShortcut{false};
};

void buildWindowsToolchainInstaller(const WindowsToolchainBuildParams& params);
```

The toolchain entrypoint should translate manifest entries into the existing `WindowsPackageLayout` model instead of inventing a second runtime install model.

### 2B. Extend the installer stub rather than replacing it

The current stub path already owns:
- extraction
- directory creation
- uninstall metadata
- shortcut install/remove
- uninstall cleanup

Additive work for toolchain support:
- append/remove `bin` directory from user PATH
- broadcast `WM_SETTINGCHANGE`
- write/remove file association keys when enabled
- optionally add a "Viper Command Prompt" shortcut that launches a shell with the Viper `bin` directory prepended

Important policy clarification:
- the current packaging metadata model can reuse `FileAssoc` for extension/description/MIME data
- the actual "open with" command is still installer policy for the toolchain package, because the toolchain does not currently ship a GUI document handler
- default behavior should therefore be conservative: PATH on, file associations off unless the product decision is explicit

This keeps the runtime behavior inside one installer engine rather than splitting behavior across:
- generated machine-code stubs
- a separate GUI EXE
- a second uninstaller implementation

### 2C. Defer GUI wizard work

If a GUI wizard is still desired, make it a follow-on layer that reuses the same:
- ZIP overlay format
- `WindowsPackageLayout`
- silent install implementation
- uninstall behavior

It should not invent a second installer payload structure.

That follow-on layer can be:
- `win32/ViperInstallerGui.cpp`
- Windows-host only
- optional for phase completion

## Install policy

Recommended defaults:
- install root: `C:\Program Files\Viper`
- elevation: keep the current elevated installer path for the machine-wide installation root
- PATH: write to `HKCU\Environment\Path` by default
- file associations: write under per-user association paths if possible; avoid forcing machine-wide association registration unless the installer is explicitly configured that way

Rationale:
- machine-wide files in `Program Files`
- user-scoped shell experience updates
- lower blast radius during uninstall

## Overlay layout

Reuse the existing `app/` + `meta/` ZIP overlay convention where it makes sense.

Recommended toolchain payload:
- `app/<stagedRelativePath>`
- `meta/license.txt`
- `meta/readme.txt` or similar optional doc
- `meta/icon.ico`
- `meta/start_menu.lnk`
- `meta/desktop.lnk`

The Windows toolchain builder should preserve the staged relative tree beneath the install root instead of re-deriving directory choices from file kind.

Do not make the runtime installer depend on parsing a text manifest from the overlay if the install layout is already embedded in `WindowsPackageLayout`.

Text metadata files in `meta/` are fine for diagnostics and offline inspection, but the installer should continue to use precomputed layout metadata.

## Modified Existing Files

- `src/tools/common/packaging/WindowsPackageBuilder.hpp`
- `src/tools/common/packaging/WindowsPackageBuilder.cpp`
- `src/tools/common/packaging/InstallerStub.hpp`
- `src/tools/common/packaging/InstallerStub.cpp`
- `src/tools/common/packaging/InstallerStubGen.hpp` if new helper opcodes or string helpers are needed
- `src/tools/common/packaging/PEBuilder.cpp` only if manifest resource generation needs small extensions

Possible new file:
- `src/tools/common/packaging/WindowsToolchainInstaller.cpp`
  - only if the adapter logic becomes too large to keep in `WindowsPackageBuilder.cpp`

## Test Plan

### Unit / structural
- build minimal toolchain installer from a mock manifest
- verify PE structure and ZIP overlay
- verify expected overlay paths are present
- verify uninstall layout entries are generated for files and directories
- verify PATH and file-association metadata are encoded in the stub layout
- verify staged-relative install paths are preserved under the chosen install root

### Windows host integration
- silent install into a temp directory
- verify binaries, libraries, headers, and CMake files land in the expected locations
- verify `viper.exe --version`
- verify user PATH update
- verify uninstall registry values
- verify file associations if enabled
- silent uninstall
- verify files, PATH, and registry entries are removed

### Cross-host validation
- on non-Windows hosts, still generate the `.exe`
- verify with `verifyPEZipOverlay()`
- inspect overlay entries with `ZipReader`

## Exit Criteria

This phase is done when:
- Viper can build a Windows toolchain installer from a staged install tree
- the current stub path, not a parallel installer stack, owns install/uninstall behavior
- Windows CI can install, run, and uninstall the toolchain silently
