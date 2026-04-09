# Phase 2: Windows GUI Installer (.exe)

## Goal

A self-extracting .exe with a multi-page wizard: welcome → license → install path → options → progress → complete. Adds Viper to PATH, registers in Add/Remove Programs, creates Start Menu shortcuts, generates an uninstaller. Handles upgrade detection.

## Architecture

The installer is a **compiled C++ Win32 program** (`ViperInstaller.cpp`) using raw Win32 API — no MFC, no ATL. It's compiled during `cmake --build` on Windows as a standalone executable, then embedded as the PE stub in the self-extracting package (same pattern as the existing `WindowsPackageBuilder`).

For cross-platform generation (building on macOS/Linux), the existing `InstallerStub` machine-code approach serves as a silent-install fallback. The GUI wizard is Windows-build-only.

**Package format**: PE32+ executable + ZIP overlay (same as existing VAPS pattern).

## New Files

### `src/tools/common/packaging/win32/ViperInstaller.cpp` (~900 LOC)

Standalone Win32 GUI program. Compiled as `viper_platform_installer.exe`.

**Entry point**: `WinMain` — locates ZIP overlay in own PE image via backward EOCD scan (same as existing InstallerStub), parses `meta/manifest.ini` from the overlay using `ZipReader`.

**DPI awareness**: The PE manifest (embedded via PEBuilder RT_MANIFEST) must include:
```xml
<asmv3:application xmlns:asmv3="urn:schemas-microsoft-com:asm.v3">
  <asmv3:windowsSettings xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">
    <dpiAware>true/pm</dpiAware>
    <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">
      PerMonitorV2,PerMonitor
    </dpiAwareness>
  </asmv3:windowsSettings>
</asmv3:application>
```
Additionally, call `InitCommonControlsEx()` with `ICC_PROGRESS_CLASS` in WinMain before creating windows. This requires linking comctl32.lib and embedding a `dependentAssembly` for comctl32 v6 in the manifest.

**Upgrade detection**: On startup, check registry `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Viper` for `DisplayVersion`. If found:
- Compare with current installer version
- If same version: offer Repair/Uninstall/Cancel
- If older: offer Upgrade (default) / Cancel
- If newer: warn "A newer version is already installed" / Downgrade / Cancel

**Wizard pages** (implemented as child panels swapped in a single frame window):

1. **Welcome** — "Welcome to Viper vX.Y.Z Setup", Viper logo (optional BMP loaded from ZIP), Next/Cancel buttons. If upgrading: "Viper vOLD is currently installed. This will upgrade to vNEW."
2. **License** — Read-only multiline edit (`ES_MULTILINE | ES_READONLY | WS_VSCROLL`) showing LICENSE text extracted from ZIP. "I accept the terms" checkbox gates the Next button.
3. **Install Path** — Edit box defaulting to `C:\Program Files\Viper` (or existing install path if upgrading). Browse button using `SHBrowseForFolderW`. Displays:
   - Required space: `XX MB`
   - Available space: `YY MB` (via `GetDiskFreeSpaceExW`)
   - Warning if insufficient space
4. **Options** — Checkboxes:
   - [x] Add Viper to user PATH
   - [x] Create Start Menu shortcuts
   - [ ] Create Desktop shortcut
   - [x] Register .zia/.bas file associations
5. **Progress** — `PROGRESS_CLASS` control (requires comctl32 v6 manifest) + status label ("Installing viper.exe..."). Extraction loop reads ZIP entries via `ZipReader`, creates directory tree, writes files. Updates progress bar per file.
6. **Complete** — "Viper vX.Y.Z has been installed successfully." Checkboxes:
   - [ ] Open Viper Command Prompt
   - [ ] View README
   Close button.

**Silent mode**: If `/S` is passed on command line, skip all UI and install to default (or `/D=path`) directory with all options enabled. This enables CI testing.

**Key Win32 functions used**:
- `CreateWindowExW`, `RegisterClassExW` — window/control creation
- `SetWindowTextW`, `SendMessageW` — control state
- `SHBrowseForFolderW` — folder picker dialog
- `CreateDirectoryW`, `CreateFileW`, `WriteFile` — file extraction
- `GetDiskFreeSpaceExW` — available disk space query
- `RegOpenKeyExW`, `RegSetValueExW`, `RegDeleteTreeW` — PATH, uninstall, file associations
- `SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE)` — PATH notification
- `SHChangeNotify(SHCNE_ASSOCCHANGED)` — file association refresh
- `InitCommonControlsEx` — common controls init
- `PBM_SETRANGE32`, `PBM_SETPOS` — progress bar
- All standard: kernel32.dll, user32.dll, shell32.dll, advapi32.dll, ole32.dll, comctl32.dll

**ZIP overlay reading**: Use existing `ZipReader` class to parse the overlay. The overlay start offset is found by scanning backward from EOF for the EOCD signature (0x06054B50).

**PATH modification** (new — the existing InstallerStub does NOT touch PATH at all):
1. Read current user PATH: `RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment")`, `RegQueryValueExW(L"Path")`
2. Parse PATH as semicolon-delimited, check if bin dir already present (case-insensitive `_wcsicmp` per segment)
3. If not present: append `;C:\Program Files\Viper\bin` and `RegSetValueExW` with `REG_EXPAND_SZ` type (preserves `%USERPROFILE%` references in existing PATH)
4. Broadcast `SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, NULL)` so running Explorer/shells pick up the change
5. **Important**: Use HKCU (user PATH), not HKLM (system PATH). User PATH doesn't require admin elevation and is the standard for per-user dev tool installations. The existing InstallerStub uses HKLM for the uninstall registry key — keep that for Add/Remove Programs, but PATH goes in HKCU.

**File association registration** (when opted in):
```
HKCR\.zia → (Default) = "ViperZiaFile"
HKCR\ViperZiaFile → (Default) = "Zia Source File"
HKCR\ViperZiaFile\DefaultIcon → (Default) = "C:\Program Files\Viper\bin\zia.exe,0"
HKCR\ViperZiaFile\shell\open\command → (Default) = "\"C:\Program Files\Viper\bin\zia.exe\" \"%1\""
```
Same pattern for .bas and .il. Call `SHChangeNotify(SHCNE_ASSOCCHANGED)` after.

### `src/tools/common/packaging/win32/ViperUninstaller.cpp` (~350 LOC)

Standalone Win32 program compiled as `viper_platform_uninstaller.exe`.

1. Parse command line for `/S` (silent mode)
2. If not silent: `MessageBoxW` confirmation: "Remove Viper vX.Y.Z and all its components?"
3. Read `meta/manifest.ini` from install directory for file list
4. Delete all installed files (iterate manifest, `DeleteFileW`)
5. Remove empty directories bottom-up (`RemoveDirectoryW`)
6. Remove Viper bin path from `HKCU\Environment\Path` (read PATH string, find and remove the Viper bin segment, write back, broadcast `WM_SETTINGCHANGE`)
7. Remove file association registry keys (`HKCR\.zia`, `HKCR\ViperZiaFile`, etc.) + `SHChangeNotify(SHCNE_ASSOCCHANGED)`
8. Remove `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Viper` key (this is in HKLM, matching the existing InstallerStub pattern)
9. Remove Start Menu shortcuts (delete .lnk files from `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Viper\`)
10. Remove Desktop shortcut if present
11. Broadcast `WM_SETTINGCHANGE` and `SHChangeNotify(SHCNE_ASSOCCHANGED)`
12. `MoveFileExW(self, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)` to schedule self-deletion
13. If not silent: `MessageBoxW` "Viper has been removed. Some files may be deleted after restart."

### `src/tools/common/packaging/ViperWindowsPackageBuilder.hpp/cpp` (~300 LOC)

Orchestrates the Windows installer build:

```cpp
struct ViperWindowsBuildParams {
    ViperInstallManifest manifest;
    std::string outputPath;           // e.g. "viper-0.2.4-win-x64.exe"
    std::string installerExePath;     // pre-compiled ViperInstaller.exe (or empty for InstallerStub fallback)
    std::string uninstallerExePath;   // pre-compiled ViperUninstaller.exe (or empty)
    std::string iconPngPath;          // optional Viper logo
};

void buildViperWindowsInstaller(const ViperWindowsBuildParams& params);
```

Implementation:
1. Build ZIP payload using `ZipWriter`:
   - Map all manifest files to Windows install paths under `app/`
   - Add uninstaller.exe → `app/uninstall.exe` (if provided)
   - Generate `meta/manifest.ini`: version, arch, file count, total size, file list with relative paths
   - Generate `meta/icon.ico` from PNG using `IconGenerator` (if icon provided)
   - Generate `meta/license.txt` from manifest.licenseText
   - Generate `meta/start_menu.lnk` using `LnkWriter` (Viper Command Prompt → cmd.exe /k "set PATH=...;%PATH%")
   - Generate `meta/desktop.lnk` if applicable
2. If `installerExePath` is provided (Windows build):
   - Read the pre-compiled ViperInstaller.exe
   - Append ZIP as PE overlay
3. Else (cross-platform fallback):
   - Use existing `InstallerStub::buildInstallerStub()` for a silent installer
   - Build PE via `PEBuilder` with the stub .text + import table
   - Append ZIP overlay
4. Update PE manifest with DPI-aware + comctl32 v6 + UAC requireAdministrator
5. Verify output with `PkgVerify::verifyPEZipOverlay()`

### `generateViperInstallerManifest()` helper

New function in PEBuilder.cpp (or separate file) that produces the combined XML manifest:
```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security><requestedPrivileges>
      <requestedExecutionLevel level="requireAdministrator" uiAccess="false"/>
    </requestedPrivileges></security>
  </trustInfo>
  <dependency>
    <dependentAssembly>
      <assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls"
        version="6.0.0.0" processorArchitecture="*" publicKeyToken="6595b64144ccf1df"/>
    </dependentAssembly>
  </dependency>
  <asmv3:application xmlns:asmv3="urn:schemas-microsoft-com:asm.v3">
    <asmv3:windowsSettings xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">
      <dpiAware>true/pm</dpiAware>
    </asmv3:windowsSettings>
  </asmv3:application>
</assembly>
```

## Modified Files

- `src/CMakeLists.txt`:
  - On Windows: `add_executable(viper_platform_installer WIN32 ...)` with `/SUBSYSTEM:WINDOWS`, linked to kernel32, user32, shell32, advapi32, ole32, comctl32
  - On Windows: `add_executable(viper_platform_uninstaller WIN32 ...)` same deps
  - All platforms: add `ViperWindowsPackageBuilder.cpp` to `viper_packaging`
- `src/tools/common/packaging/PEBuilder.cpp` — add `generateViperInstallerManifest()` with DPI + comctl32 v6 + UAC

## Testing

- Structural: Build installer PE, verify ZIP overlay with `PkgVerify::verifyPEZipOverlay()`
- Manifest: Verify `meta/manifest.ini` present in ZIP with expected entries, verify `meta/license.txt`
- DPI manifest: Verify RT_MANIFEST resource contains `dpiAware` element
- Windows CI: Run installer with `/S /D=C:\Temp\ViperTest`, verify:
  - Files exist in install dir
  - PATH contains Viper bin
  - Uninstall registry key present with correct DisplayVersion
  - File associations registered (`.zia` → ViperZiaFile)
  - Start Menu shortcuts exist
- Windows CI: Run uninstaller with `/S`, verify:
  - Files removed
  - PATH cleaned
  - Registry keys removed
  - File associations removed
- Cross-platform: Build Windows installer on macOS, verify PE+ZIP structural validity
