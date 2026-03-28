//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/WindowsPackageBuilder.hpp
// Purpose: Assemble a Windows self-extracting installer .exe from a compiled
//          native binary and package assets.
//
// Key invariants:
//   - Output is a valid PE32+ executable with ZIP payload as overlay.
//   - ZIP payload contains: application .exe, assets, uninstall metadata,
//     .lnk shortcuts, .ico icon file.
//   - PE .text section contains a minimal installer stub.
//   - Resource section embeds RT_MANIFEST for UAC elevation.
//   - Overlay data (ZIP) is appended after the last PE section.
//
// Ownership/Lifetime:
//   - Single-use builder functions.
//
// Links: PEBuilder.hpp, ZipWriter.hpp, LnkWriter.hpp, IconGenerator.hpp,
//        PackageConfig.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "PackageConfig.hpp"

#include <string>

namespace viper::pkg {

/// @brief Parameters for building a Windows self-extracting installer.
struct WindowsBuildParams {
    std::string projectName;    ///< Project name (used for display name fallback).
    std::string version;        ///< Version string (e.g. "1.2.0").
    std::string executablePath; ///< Path to compiled native .exe binary.
    std::string projectRoot;    ///< Project root directory (for resolving assets).
    PackageConfig pkgConfig;    ///< Package manifest configuration.
    std::string outputPath;     ///< Output .exe path.
    std::string archStr;        ///< Architecture string ("x64" or "arm64").
};

/// @brief Build a Windows self-extracting installer .exe.
///
/// Creates a PE32+ executable containing:
/// 1. PE headers with RT_MANIFEST resource (UAC elevation).
/// 2. Minimal .text stub (placeholder for installer logic).
/// 3. ZIP overlay containing the application binary, assets, shortcuts, icons.
///
/// The ZIP payload is structured as:
///   app/<name>.exe          — The application binary
///   app/uninstall.exe       — Uninstaller PE (reads meta/install.ini)
///   app/<assets>/           — Asset files
///   meta/install.ini        — Installation metadata (paths, registry keys)
///   meta/icon.ico           — Application icon (if source PNG exists)
///   meta/shortcut.lnk       — Start Menu shortcut
///   meta/desktop.lnk        — Desktop shortcut (if enabled)
///
/// @param params Build parameters.
/// @throws std::runtime_error on failure.
void buildWindowsPackage(const WindowsBuildParams &params);

} // namespace viper::pkg
