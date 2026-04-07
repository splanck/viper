# Phase 5: CLI Orchestration and Build Scripts

## Goal

Wire everything together with a `viper install-package` CLI command and convenience build scripts so generating installers is a single command.

## New Files

### `src/tools/viper/cmd_install_package.cpp` (~300 LOC)

```
Usage: viper install-package [options]

  Build a platform-native installer for the Viper compiler toolchain.

Options:
  --target <platform>    windows, macos, linux-deb, linux-rpm, all
  --arch <arch>          x64, arm64 (default: host architecture)
  --install-prefix <dir> Path to cmake --install staging dir (required)
  --source-root <dir>    Path to Viper source tree (default: auto-detect)
  -o <path>              Output file or directory
  --verbose, -v          Verbose output
  --version <ver>        Override version string
```

Implementation:
1. Parse args, determine target platform(s)
2. Call `gatherViperInstallTree(installPrefix, sourceRoot)` to build manifest
3. Dispatch to the appropriate builder:
   - `windows` → `buildViperWindowsInstaller()`
   - `macos` → `buildMacOSPkg()`
   - `linux-deb` → `buildViperDeb()`
   - `linux-rpm` → `buildViperRpm()`
   - `all` → all applicable for current host
4. Run `PkgVerify` on output
5. Print summary (output path, size, file count)

### `scripts/build_installer.sh` (~80 LOC)

Convenience wrapper for Unix/macOS:
```bash
#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-build}"
TARGET="${2:-auto}"  # auto, macos, linux-deb, linux-rpm

# Stage install tree
STAGING="$BUILD_DIR/install_staging"
cmake --install "$BUILD_DIR" --prefix "$STAGING"

# Detect target if auto
if [ "$TARGET" = "auto" ]; then
    case "$(uname -s)" in
        Darwin) TARGET="macos" ;;
        Linux)  TARGET="linux-deb" ;;
        *)      echo "Unknown platform"; exit 1 ;;
    esac
fi

# Build installer
"$BUILD_DIR/src/tools/viper/viper" install-package \
    --target "$TARGET" \
    --install-prefix "$STAGING" \
    --source-root "$SOURCE_ROOT" \
    -o "$BUILD_DIR/installers/" \
    --verbose

echo "Installer written to $BUILD_DIR/installers/"
```

### `scripts/build_installer.cmd` (~60 LOC)

Windows equivalent:
```cmd
@echo off
set BUILD_DIR=%~1
if "%BUILD_DIR%"=="" set BUILD_DIR=build

cmake --install %BUILD_DIR% --prefix %BUILD_DIR%\install_staging
%BUILD_DIR%\src\tools\viper\Release\viper.exe install-package ^
    --target windows ^
    --install-prefix %BUILD_DIR%\install_staging ^
    --source-root %~dp0.. ^
    -o %BUILD_DIR%\installers\ ^
    --verbose
```

## Modified Files

- `src/tools/viper/main.cpp` — add `install-package` to command dispatch (alongside existing `package`, `build`, `run`, etc.)
- `src/CMakeLists.txt` — add `cmd_install_package.cpp` to viper CLI sources

## Testing

- Integration: Run `build_installer.sh` in CI, verify output file exists and has expected format
- Verify `--target all` produces all applicable formats for the host
- Verify `--verbose` prints per-file progress
- Verify error handling: missing install prefix, empty manifest
