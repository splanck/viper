//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PlistGenerator.hpp
// Purpose: Generate macOS Info.plist XML for .app bundles.
//
// Key invariants:
//   - Output is valid XML conforming to Apple's PropertyList-1.0 DTD.
//   - CFBundleExecutable must match the actual binary filename.
//   - NSHighResolutionCapable is always set to true.
//
// Ownership/Lifetime:
//   - Pure function, no state.
//
// Links: PackageConfig.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "PackageConfig.hpp"

#include <string>
#include <vector>

namespace viper::pkg {

/// @brief Parameters for Info.plist generation.
struct PlistParams {
    std::string executableName;              ///< Binary filename in Contents/MacOS/
    std::string bundleId;                    ///< CFBundleIdentifier (reverse DNS)
    std::string bundleName;                  ///< CFBundleName (display name)
    std::string version;                     ///< CFBundleVersion
    std::string iconFile;                    ///< CFBundleIconFile (without .icns extension)
    std::string minOsVersion;                ///< LSMinimumSystemVersion (default "10.13")
    std::vector<FileAssoc> fileAssociations; ///< CFBundleDocumentTypes
};

/// @brief Generate an Info.plist XML string.
/// @param params Plist parameters.
/// @return Complete Info.plist XML content.
std::string generatePlist(const PlistParams &params);

/// @brief Generate a PkgInfo file content (always "APPL????").
/// @return 8-byte PkgInfo content.
std::string generatePkgInfo();

} // namespace viper::pkg
