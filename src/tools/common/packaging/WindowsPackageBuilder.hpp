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
//   - Output is a valid PE32+ executable with a stored ZIP bootstrap overlay.
//   - The main application/toolchain payload is a DEFLATE-compressed inner ZIP,
//     while small bootstrap files are stored for direct extraction by the stub.
//   - Overlay data contains the compressed payload, shortcuts, and a packaged
//     uninstaller PE.
//   - PE .text section contains a real x64 installer stub that extracts files,
//     writes uninstall metadata, and installs shortcuts. ARM64 payload packages
//     reuse the same bootstrap so the installer can run under Windows-on-ARM
//     x64 emulation while deploying ARM64 binaries.
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
#include "ToolchainInstallManifest.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace viper::pkg {

/// @brief Parameters for building a Windows self-extracting installer.
struct WindowsBuildParams {
    std::string projectName;    ///< Project name (used for display name fallback).
    std::string version;        ///< Version string (e.g. "1.2.0").
    std::string executablePath; ///< Path to compiled native .exe binary.
    std::string projectRoot;    ///< Project root directory (for resolving assets).
    PackageConfig pkgConfig;    ///< Package manifest configuration.
    std::string outputPath;     ///< Output .exe path.
    std::string archStr;        ///< Payload architecture string ("x64" or "arm64").
};

/// @brief Build a Windows self-extracting installer .exe.
///
/// Creates a PE32+ executable containing:
/// 1. PE headers with RT_MANIFEST resource (UAC elevation).
/// 2. x64 .text stub implementing installation logic.
/// 3. ZIP overlay containing a compressed inner payload, shortcuts, and the
///    generated uninstaller.
///
/// The outer ZIP bootstrap is structured as:
///   meta/payload.zip           - compressed install-root payload
///   meta/install_manifest.next - next installed-file manifest for upgrades
///   meta/start_menu.lnk        - Start Menu shortcut (if enabled)
///   meta/desktop.lnk           - Desktop shortcut (if enabled)
///
/// @param params Build parameters.
/// @throws std::runtime_error on failure.
void buildWindowsPackage(const WindowsBuildParams &params);

/// @brief Return imported DLL names from a PE32+ import table.
///
/// Returns an empty vector when the input is not a supported PE32+ image or the
/// import directory cannot be parsed safely.
std::vector<std::string> importedDllNamesFromPe(const std::vector<uint8_t> &data);

/// @brief Parameters for building a Windows toolchain installer from a staged manifest.
struct WindowsToolchainBuildParams {
    ToolchainInstallManifest manifest;          ///< Staged file list produced by gatherToolchainInstallManifest.
    std::string outputPath;                     ///< Output .exe path for the installer.
    std::string archStr{"x64"};                 ///< Payload architecture ("x64" or "arm64").
    std::string displayName{"Viper"};           ///< Human-readable product name shown in Add/Remove Programs.
    std::string publisher{"Viper Project"};     ///< Publisher string written to the uninstall registry key.
    std::string identifier{"org.viper.toolchain"}; ///< Unique product identifier used as the registry key name.
    std::string installDirName{"Viper"};        ///< Directory name under the selected install root.
    std::string homepage{"https://github.com/splanck/viper"}; ///< Support/update URL.
    std::string installScope{"user"};           ///< "user" for LocalAppData/HKCU, "machine" for ProgramFiles/HKLM.
    bool addToPath{true};                       ///< Add bin/ to the selected PATH registry value.
    bool registerFileAssociations{false};       ///< Register .zia/.bas/.il file associations.
    bool createStartMenuShortcuts{true};        ///< Create Viper developer shortcuts in the Start Menu.
};

/// @brief Build a Windows toolchain installer .exe from a staged install manifest.
///
/// Packages every staged file into a compressed inner ZIP carried by a PE32+
/// self-extracting stub. The stub installs files under the selected Viper install
/// root, writes an uninstall registry key, updates PATH, and creates Start Menu
/// shortcuts when requested. All required toolchain components (viper binary,
/// CMake config, runtime archives) must already be validated in the manifest by
/// validateToolchainInstallManifest before calling this function.
///
/// @param params Build parameters.
/// @throws std::runtime_error on any I/O, validation, or PE assembly failure.
void buildWindowsToolchainInstaller(const WindowsToolchainBuildParams &params);

} // namespace viper::pkg
