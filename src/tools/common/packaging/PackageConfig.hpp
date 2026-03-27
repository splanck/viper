//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PackageConfig.hpp
// Purpose: Data structures for package manifest configuration, parsed from
//          package-* directives in viper.project files.
//
// Key invariants:
//   - All paths in AssetEntry are relative to the project root directory.
//   - displayName defaults to the project name if package-name is absent.
//   - targetArchitectures defaults to host architecture if empty.
//
// Ownership/Lifetime:
//   - Value type, fully copyable/movable.
//
// Links: project_loader.hpp (embedded in ProjectConfig)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <vector>

namespace viper::pkg
{

/// @brief A single asset entry: source file/dir -> target relative dir.
struct AssetEntry
{
    std::string sourcePath; ///< Relative to project root.
    std::string targetPath; ///< Relative to install dir.
};

/// @brief A file association declaration.
struct FileAssoc
{
    std::string extension;   ///< e.g. ".zia"
    std::string description; ///< e.g. "Zia Source File"
    std::string mimeType;    ///< e.g. "text/x-zia"
};

/// @brief All package-related configuration from viper.project.
struct PackageConfig
{
    std::string displayName; ///< package-name (defaults to project name)
    std::string author;      ///< package-author
    std::string description; ///< package-description
    std::string homepage;    ///< package-homepage
    std::string license;     ///< package-license (SPDX)
    std::string identifier;  ///< package-identifier (reverse DNS)
    std::string iconPath;    ///< package-icon (relative path to PNG)

    std::vector<AssetEntry> assets;
    std::vector<FileAssoc> fileAssociations;

    bool shortcutDesktop{false};
    bool shortcutMenu{true};

    std::string minOsWindows; ///< "10.0"
    std::string minOsMacos;   ///< "11.0"

    std::vector<std::string> targetArchitectures; ///< "x64", "arm64"

    std::string category;             ///< package-category (e.g. "Game", "Development", "Utility")
    std::vector<std::string> depends; ///< package-depends (e.g. "libc6", "libx11-6")

    std::string postInstallScript;  ///< Custom post-install script content
    std::string preUninstallScript; ///< Custom pre-uninstall script content

    /// @brief Check if any package-* directives were specified.
    bool hasPackageConfig() const
    {
        return !displayName.empty() || !author.empty() || !description.empty() ||
               !identifier.empty() || !iconPath.empty() || !assets.empty();
    }
};

} // namespace viper::pkg
