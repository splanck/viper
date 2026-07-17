//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/MacOSPackageBuilder.hpp
// Purpose: Build macOS .app bundles packaged inside a ZIP archive.
//
// Key invariants:
//   - Produces a ZIP containing a valid .app bundle structure.
//   - Executable has Unix mode 0755 in ZIP external attributes.
//   - All other files have 0644, directories have 040755.
//   - ICNS icon is generated from source PNG if package-icon is specified.
//
// Ownership/Lifetime:
//   - Builder is single-use: call build() once.
//
// Links: ZipWriter.hpp, PlistGenerator.hpp, PkgPNG.hpp, PackageConfig.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "PackageConfig.hpp"
#include "ToolchainInstallManifest.hpp"

#include <string>

namespace zanna::pkg {

/// @brief Parameters for building a macOS .app-in-.zip package.
struct MacOSBuildParams {
    std::string projectName;    ///< Project name (used for .app name)
    std::string version;        ///< Version string
    std::string executablePath; ///< Path to the compiled native binary
    std::string projectRoot;    ///< Absolute path to project root directory
    PackageConfig pkgConfig;    ///< Package configuration from manifest
    std::string outputPath;     ///< Output .zip file path
};

/// @brief Build a macOS .app bundle inside a ZIP archive.
/// @param params Build parameters.
/// @throws std::runtime_error on failure.
void buildMacOSPackage(const MacOSBuildParams &params);

/// @brief Build a macOS .app bundle wrapped in a drag-to-install .dmg (macOS-only, hdiutil).
/// @details Stages and signs the bundle exactly like buildMacOSPackage, then wraps it (with an
///          /Applications symlink) into a compressed .dmg. `params.outputPath` is the .dmg path.
/// @param params Build parameters.
/// @throws std::runtime_error on failure or when run off macOS.
void buildMacOSAppDmg(const MacOSBuildParams &params);

/// @brief Parameters for building a macOS toolchain installer package.
struct MacOSToolchainBuildParams {
    ToolchainInstallManifest manifest;             ///< Staged files and metadata to package.
    std::string outputPath;                        ///< Output `.pkg` file path.
    std::string identifier{"org.zanna.toolchain"}; ///< CFBundleIdentifier / pkg id.
    std::string displayName{"Zanna Toolchain"};    ///< Human-readable package name.
    std::string packageVersion;      ///< Optional dotted numeric package version override.
    std::string minimumMacOSVersion; ///< Optional minimum OS (default: 10.15/11.0 by arch).
    std::string licenseFilePath; ///< Optional license file shown in the installer (else generated).
    std::string backgroundImagePath;     ///< Optional installer background image (PNG).
    std::string applicationSignIdentity; ///< Developer ID Application identity for nested code.
};

/// @brief Build a macOS `.pkg` installer for the staged toolchain.
/// @param params Manifest, output path, identifier, and display metadata.
/// @throws std::runtime_error on failure.
void buildMacOSToolchainPackage(const MacOSToolchainBuildParams &params);

/// @brief Parameters for wrapping a built toolchain `.pkg` in a styled `.dmg` disk image.
struct MacOSToolchainDmgParams {
    std::string pkgPath;                               ///< Path to the already-built `.pkg`.
    std::string outputPath;                            ///< Output `.dmg` path.
    std::string volumeName{"Zanna Toolchain"};         ///< Mounted volume / window title.
    std::string pkgDisplayName{"Zanna Toolchain.pkg"}; ///< Filename shown inside the image.
    std::string backgroundPng; ///< Optional window background image (absolute path).
    std::string volumeIcns;    ///< Optional volume icon `.icns` (absolute path).
};

/// @brief Wrap a built toolchain `.pkg` in a compressed, styled `.dmg` ("double-click to install").
/// @details macOS-only: shells to `hdiutil`, with best-effort `osascript`/`SetFile` styling so a
///          headless run still yields a valid image.
/// @throws std::runtime_error on failure or when run off macOS.
void buildMacOSToolchainDmg(const MacOSToolchainDmgParams &params);

} // namespace zanna::pkg
