# Phase 5: CLI Orchestration and Build Scripts

## Goal

Wire everything together with a `viper install-package` CLI command and convenience build scripts so generating installers is a single command.

## New Files

### `src/tools/viper/cmd_install_package.cpp` (~350 LOC)

```
Usage: viper install-package [options]

  Build a platform-native installer for the Viper compiler toolchain.

Options:
  --target <platform>    windows, macos, linux-deb, linux-rpm, all
  --arch <arch>          x64, arm64 (default: host architecture)
  --install-prefix <dir> Path to cmake --install staging dir (required)
  --source-root <dir>    Path to Viper source tree (default: auto-detect via findBuildDir)
  -o <path>              Output file or directory (default: ./installers/)
  --verbose, -v          Verbose output
  --version <ver>        Override version string (default: read from buildmeta/VERSION)
  --identifier <id>      macOS bundle identifier (default: com.viper-lang.viper)
  --icon <path>          PNG icon for installer branding (optional)
  --installer-exe <path> Pre-compiled Windows GUI installer (Windows-build-only)
  --uninstaller-exe <path> Pre-compiled Windows uninstaller (Windows-build-only)
```

Implementation:
1. Parse args, determine target platform(s)
2. Validate --install-prefix exists and contains expected files (bin/viper at minimum)
3. Auto-detect --source-root by walking up from current dir looking for CMakeLists.txt + src/buildmeta/VERSION
4. Call `gatherViperInstallTree(installPrefix, sourceRoot, arch)` to build manifest
5. Validate manifest: at least 1 binary, version non-empty
6. Dispatch to the appropriate builder:
   - `windows` → `buildViperWindowsInstaller()`
   - `macos` → `buildMacOSPkg()`
   - `linux-deb` → `buildViperDeb()`
   - `linux-rpm` → `buildViperRpm()`
   - `all` → all applicable for current host:
     - macOS host: macos + linux-deb + linux-rpm + windows (silent fallback)
     - Linux host: linux-deb + linux-rpm + windows (silent fallback)
     - Windows host: windows (GUI) + linux-deb + linux-rpm
7. Run `PkgVerify` on each output artifact
8. Print summary per artifact:
   ```
   Built 3 installer packages:
     viper-0.2.4-macos-arm64.pkg     (42.3 MB) ✓ verified
     viper_0.2.4_amd64.deb           (38.1 MB) ✓ verified
     viper-0.2.4-1.x86_64.rpm        (38.4 MB) ✓ verified
   ```

**Error handling**:
- Missing install prefix → clear error with suggested cmake --install command
- Missing binaries in install prefix → error listing which binaries were expected
- Missing source root → error with suggested --source-root flag
- Builder failure → error with builder-specific diagnostic

**Output naming conventions**:
- Windows: `viper-{version}-win-{arch}.exe`
- macOS: `viper-{version}-macos-{arch}.pkg`
- Linux .deb: `viper_{version}_{arch}.deb` (Debian naming: underscores, arch is amd64/arm64)
- Linux .rpm: `viper-{version}-1.{arch}.rpm` (RPM naming: dashes, arch is x86_64/aarch64)

### `scripts/build_installer.sh` (~100 LOC)

Convenience wrapper for Unix/macOS:
```bash
#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-build}"
TARGET="${2:-auto}"  # auto, macos, linux-deb, linux-rpm, all, windows

# Stage install tree
STAGING="$BUILD_DIR/install_staging"
echo "Staging Viper installation to $STAGING..."
cmake --install "$BUILD_DIR" --prefix "$STAGING"

# Detect target if auto
if [ "$TARGET" = "auto" ]; then
    case "$(uname -s)" in
        Darwin) TARGET="macos" ;;
        Linux)  TARGET="linux-deb" ;;
        *)      echo "Unknown platform"; exit 1 ;;
    esac
fi

# Detect architecture
ARCH="x64"
case "$(uname -m)" in
    arm64|aarch64) ARCH="arm64" ;;
esac

# Create output directory
OUTPUT_DIR="$BUILD_DIR/installers"
mkdir -p "$OUTPUT_DIR"

# Build installer
echo "Building $TARGET installer for $ARCH..."
"$BUILD_DIR/src/tools/viper/viper" install-package \
    --target "$TARGET" \
    --arch "$ARCH" \
    --install-prefix "$STAGING" \
    --source-root "$SOURCE_ROOT" \
    -o "$OUTPUT_DIR" \
    --verbose

echo ""
echo "Installer(s) written to $OUTPUT_DIR/"
ls -lh "$OUTPUT_DIR"/viper-* "$OUTPUT_DIR"/viper_* 2>/dev/null || true
```

### `scripts/build_installer.cmd` (~70 LOC)

Windows equivalent:
```cmd
@echo off
setlocal

set BUILD_DIR=%~1
if "%BUILD_DIR%"=="" set BUILD_DIR=build

set TARGET=%~2
if "%TARGET%"=="" set TARGET=windows

echo Staging Viper installation...
cmake --install %BUILD_DIR% --prefix %BUILD_DIR%\install_staging
if errorlevel 1 (echo Staging failed & exit /b 1)

set OUTPUT_DIR=%BUILD_DIR%\installers
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo Building %TARGET% installer...
%BUILD_DIR%\src\tools\viper\Release\viper.exe install-package ^
    --target %TARGET% ^
    --arch x64 ^
    --install-prefix %BUILD_DIR%\install_staging ^
    --source-root %~dp0.. ^
    --installer-exe %BUILD_DIR%\src\tools\common\packaging\win32\Release\viper_platform_installer.exe ^
    --uninstaller-exe %BUILD_DIR%\src\tools\common\packaging\win32\Release\viper_platform_uninstaller.exe ^
    -o %OUTPUT_DIR% ^
    --verbose

echo.
echo Installer(s) written to %OUTPUT_DIR%\
dir /b "%OUTPUT_DIR%\viper-*" 2>nul
```

## Relationship with Existing `viper package`

`src/tools/viper/cmd_package.cpp` (223 lines) implements `viper package` which compiles a Viper PROJECT into a platform-specific app installer. It takes a `viper.project` file with app metadata and produces .app/.deb/.exe/.tar.gz packages for distributing apps built WITH Viper.

`viper install-package` is different: it packages VIPER ITSELF from a cmake install tree. Different input (cmake install prefix), different output (platform SDK/toolchain installer), different metadata (version from buildmeta/VERSION, not viper.project). Minimal code overlap — they share only the low-level format writers (ZipWriter, ArWriter, etc.) through the `viper_packaging` library.

## Modified Files

- `src/tools/viper/main.cpp` — add `install-package` to command dispatch table (alongside existing `package`, `build`, `run`, etc.)
- `src/CMakeLists.txt` — add `cmd_install_package.cpp` to viper CLI sources

## Testing

- **Argument parsing**: Verify all flags parse correctly, defaults applied
- **Error cases**: Missing install prefix, empty manifest, invalid target
- **Auto-detection**: Verify host platform detection, arch detection
- **Output naming**: Verify correct filename conventions per platform
- **Verbose output**: Verify per-file progress logging
- **Integration**: Run `build_installer.sh` in CI:
  - macOS CI: Produces .pkg, verify with pkgutil
  - Linux CI: Produces .deb, verify with dpkg-deb
  - Windows CI: Produces .exe, verify with PkgVerify
- **Cross-platform**: On macOS, `--target linux-deb` produces valid .deb (verify structure)
- **`--target all`**: Verify all applicable formats generated for host
