//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/DesktopEntryGenerator.hpp
// Purpose: Generate freedesktop.org .desktop files and MIME type XML.
//
// Key invariants:
//   - .desktop files follow freedesktop.org Desktop Entry Specification.
//   - MIME XML follows freedesktop.org shared-mime-info format.
//
// Ownership/Lifetime:
//   - Output returned as std::string (caller-owned).
//
// Links: DesktopEntryGenerator.cpp, LinuxPackageBuilder.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "PackageConfig.hpp"

#include <string>
#include <vector>

namespace viper::pkg {

/// @brief Parameters for .desktop file generation.
struct DesktopEntryParams {
    std::string name;        ///< Display name (e.g. "ViperIDE")
    std::string comment;     ///< Short description
    std::string execPath;    ///< Path to executable (e.g. "/usr/bin/viperide")
    std::string iconName;    ///< Icon name (e.g. "viperide")
    std::string categories;  ///< freedesktop.org categories (e.g. "Development;TextEditor;")
    bool terminal{false};    ///< Whether to run in a terminal
    std::vector<FileAssoc> fileAssociations; ///< For MimeType= field
};

/// @brief Generate a .desktop file.
/// @return .desktop file content as a string.
std::string generateDesktopEntry(const DesktopEntryParams &params);

/// @brief Generate a MIME type XML file for file associations.
/// @param packageName The package name (used as namespace in the XML).
/// @param assocs The file associations to register.
/// @return MIME XML content as a string.
std::string generateMimeTypeXml(const std::string &packageName,
                                const std::vector<FileAssoc> &assocs);

} // namespace viper::pkg
