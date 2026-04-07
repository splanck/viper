# Phase 2: Windows GUI Installer (.exe)

## Goal

A self-extracting .exe with a multi-page wizard: welcome → license → install path → options → progress → complete. Adds Viper to PATH, registers in Add/Remove Programs, creates Start Menu shortcuts, generates an uninstaller.

## Architecture

The installer is a **compiled C++ Win32 program** (`ViperInstaller.cpp`) using raw Win32 API — no MFC, no ATL. It's compiled during `cmake --build` on Windows as a standalone executable, then embedded as the PE stub in the self-extracting package (same pattern as the existing `WindowsPackageBuilder`).

For cross-platform generation (building on macOS/Linux), the existing `InstallerStub` machine-code approach serves as a silent-install fallback. The GUI wizard is Windows-build-only.

**Package format**: PE32+ executable + ZIP overlay (same as existing VAPS pattern).

## New Files

### `src/tools/common/packaging/win32/ViperInstaller.cpp` (~800 LOC)

Standalone Win32 GUI program. Compiled as `viper_platform_installer.exe`.

**Entry point**: `WinMain` — locates ZIP overlay in own PE image, parses `meta/manifest.ini` from the overlay.

**Wizard pages** (implemented as child panels swapped in a single frame window):

1. **Welcome** — "Welcome to Viper vX.Y.Z Setup", Viper logo (optional), Next/Cancel buttons
2. **License** — Read-only multiline edit (`ES_MULTILINE | ES_READONLY | WS_VSCROLL`) showing LICENSE text. "I accept" checkbox gates the Next button.
3. **Install Path** — Edit box defaulting to `C:\Program Files\Viper`, Browse button using `SHBrowseForFolderW`. Shows required/available disk space.
4. **Options** — Checkboxes:
   - [x] Add Viper to user PATH
   - [x] Create Start Menu shortcuts
   - [ ] Create Desktop shortcut
5. **Progress** — `PROGRESS_CLASS` control + status label. Extraction loop reads ZIP entries, creates directories, writes files. Updates bar per file.
6. **Complete** — "Viper has been installed successfully." + Open Command Prompt / Close buttons.

**Key Win32 functions used**:
- `CreateWindowExW`, `RegisterClassExW` — window/control creation
- `SetWindowTextW`, `SendMessageW` — control state
- `SHBrowseForFolderW` — folder picker dialog
- `CreateDirectoryW`, `CreateFileW`, `WriteFile` — file extraction
- `RegOpenKeyExW`, `RegSetValueExW` — PATH and uninstall registry
- `SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE)` — PATH notification
- `PBM_SETRANGE32`, `PBM_SETPOS` — progress bar
- All standard: kernel32.dll, user32.dll, shell32.dll, advapi32.dll, ole32.dll

**ZIP overlay reading**: Scan backward from EOF for ZIP end-of-central-directory signature (0x06054B50), read central directory, extract entries. Identical approach to existing `InstallerStub`.

### `src/tools/common/packaging/win32/ViperUninstaller.cpp` (~300 LOC)

Standalone Win32 program compiled as `viper_platform_uninstaller.exe`.

1. `MessageBoxW` confirmation: "Remove Viper vX.Y.Z?"
2. Read `meta/manifest.ini` from install directory
3. Delete all installed files + directories (bottom-up)
4. Remove Viper bin path from `HKCU\Environment\Path`
5. Remove `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Viper` key
6. Remove Start Menu/Desktop shortcuts
7. `MoveFileExW(self, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)` to self-delete

### `src/tools/common/packaging/ViperWindowsPackageBuilder.hpp/cpp` (~250 LOC)

Orchestrates the Windows installer build:

```cpp
struct ViperWindowsBuildParams {
    ViperInstallManifest manifest;
    std::string outputPath;           // e.g. "viper-0.2.4-win-x64.exe"
    std::string installerExePath;     // pre-compiled ViperInstaller.exe
    std::string uninstallerExePath;   // pre-compiled ViperUninstaller.exe
    std::string iconPngPath;          // optional Viper logo
};

void buildViperWindowsInstaller(const ViperWindowsBuildParams& params);
```

Implementation:
1. Build ZIP payload using `ZipWriter`:
   - Map all manifest files to Windows paths under `app/`
   - Add `uninstall.exe` → `app/uninstall.exe`
   - Generate `meta/manifest.ini` with version, file list, install dir default
   - Generate `meta/icon.ico` from PNG using `IconGenerator`
   - Generate `meta/start_menu.lnk` using `LnkWriter`
2. Read the pre-compiled `ViperInstaller.exe` as the PE base
3. Append the ZIP as overlay (same technique as existing `WindowsPackageBuilder`)
4. Patch PE checksum

## Modified Files

- `src/CMakeLists.txt`:
  - On Windows: `add_executable(viper_platform_installer WIN32 src/tools/common/packaging/win32/ViperInstaller.cpp)` with `/SUBSYSTEM:WINDOWS` and linking to kernel32, user32, shell32, advapi32, ole32, comctl32
  - On Windows: `add_executable(viper_platform_uninstaller WIN32 src/tools/common/packaging/win32/ViperUninstaller.cpp)` same link deps
  - All platforms: add `ViperWindowsPackageBuilder.cpp` to `viper_packaging`

## Testing

- Structural: Build installer PE, verify ZIP overlay with `PkgVerify::verifyPEZipOverlay()`
- Manifest: Verify `meta/manifest.ini` present in ZIP with expected entries
- Windows CI: Run installer with `/S` (silent), verify files in Program Files, verify PATH contains Viper bin, verify uninstall registry key
- Windows CI: Run uninstaller, verify files removed, PATH cleaned
