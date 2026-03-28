//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

#include <string>

namespace viper::pkg {

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

} // namespace viper::pkg
