//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/WindowsInstallerMetadata.hpp
// Purpose: Define the deterministic metadata exchanged by the Windows package
//          builder and the native installer/maintenance host.
//
// Key invariants:
//   - Schema 3 is line-oriented, UTF-8, deterministic, and strictly parsed.
//   - Every variable field is percent-escaped before tab-delimited encoding.
//   - Payload records carry the install-relative path, SHA-256, byte size, and
//     owning component needed for preflight, repair, and transactional removal.
//   - The schema contains no host paths or build timestamps.
//
// Ownership/Lifetime:
//   - Metadata values own all strings and vectors; parsers do not retain views.
//
// Links: WindowsPackageBuilder.cpp, WindowsInstallerHost.cpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zanna::pkg {

/// @brief Current native Windows installer metadata schema.
inline constexpr uint32_t kWindowsInstallerMetadataSchema = 3;

/// @brief One selectable payload component shown by the native wizard.
struct WindowsInstallerComponentMetadata {
    std::string id;             ///< Stable lowercase identifier.
    std::string label;          ///< Accessible user-visible label.
    std::string description;    ///< Concise component purpose.
    bool required{false};       ///< Required components cannot be deselected.
    bool defaultSelected{true}; ///< Selection for a clean Typical install.
    uint64_t sizeBytes{0};      ///< Uncompressed payload size owned by the component.
};

/// @brief One file inside meta/payload.zip.
struct WindowsInstallerPayloadMetadata {
    std::string path;        ///< Validated install-relative slash path.
    std::string sha256;      ///< Lowercase SHA-256 of the uncompressed bytes.
    uint64_t sizeBytes{0};   ///< Uncompressed file size.
    std::string componentId; ///< Empty means the required core component.
};

/// @brief One shortcut generated at install time for the selected destination.
struct WindowsInstallerShortcutMetadata {
    std::string root;           ///< "desktop" or "start-menu".
    std::string relativePath;   ///< Root-relative destination path.
    std::string targetRoot;     ///< "install" or "windows".
    std::string targetPath;     ///< Root-relative target executable path.
    std::string workingRoot;    ///< "install", "profile", or "windows".
    std::string workingPath;    ///< Optional root-relative working directory.
    std::string argumentPrefix; ///< Optional fixed argument such as /k or /c.
    std::string argumentPath;   ///< Optional install-relative path passed as one quoted argument.
    std::string description;    ///< Accessible Shell Link description.
    std::string iconRoot;       ///< Empty, "install", or "windows".
    std::string iconPath;       ///< Optional root-relative icon path.
    int32_t iconIndex{0};       ///< Icon resource index.
    std::string componentId;    ///< Empty or the component controlling the shortcut.
};

/// @brief One ordinary installed file carried by a stored outer-ZIP entry.
/// @details This is used for the signed maintenance executable, which cannot be
///          recursively embedded in the repair payload it carries itself.
struct WindowsInstallerOuterFileMetadata {
    std::string overlayPath; ///< Entry in the outer ZIP containing the bytes.
    std::string path;        ///< Install-relative destination path.
    std::string sha256;
    uint64_t sizeBytes{0};
    std::string componentId;
};

/// @brief One safe Open-With registration owned by the package.
struct WindowsInstallerAssociationMetadata {
    std::string extension; ///< Extension including the leading dot.
    std::string description;
    std::string mimeType;
    std::string progId;
    std::string arguments; ///< Optional arguments placed before the quoted file path.
};

/// @brief Complete package contract consumed by the native installer host.
struct WindowsInstallerMetadata {
    uint32_t schemaVersion{kWindowsInstallerMetadataSchema};
    std::string packageMode{"setup"};       ///< "setup" or "maintenance".
    std::string productKind{"application"}; ///< "application" or "toolchain".
    std::string identifier;
    std::string displayName;
    std::string version;
    std::string publisher;
    std::string description;
    std::string contact;
    std::string homepage;
    std::string documentationUrl;
    std::string updateManifestUrl;
    std::string updateRsaModulus;
    std::string updateRsaExponent;
    std::string architecture;
    std::string channel{"stable"};
    std::string commit;
    std::string defaultScope{"user"}; ///< "user" or "machine".
    std::string defaultInstallDir;
    std::string executableName;
    std::string associationExecutable;
    std::string pathRelativePath;
    std::string displayIconRelativePath;
    std::string payloadEntry{"meta/payload.zip"};
    std::string cleanupEntry{"meta/cleanup.exe"};
    std::string cleanupSha256; ///< SHA-256 of the signed detached cleanup helper.
    std::string licenseEntry{"meta/license.txt"};
    std::string readmeEntry{"meta/readme.txt"};
    std::string installedManifestRelativePath{".zanna-install-manifest.txt"};
    std::string stateRelativePath{".zanna-install-state.v2"};
    std::string uninstallerRelativePath{"uninstall.exe"};
    std::string minimumWindowsVersion{"10.0.17763"};
    bool addToPath{false};
    bool registerFileAssociations{false};
    bool createShortcuts{false};
    uint64_t installedSizeBytes{0};
    std::vector<WindowsInstallerComponentMetadata> components;
    std::vector<WindowsInstallerPayloadMetadata> payloadFiles;
    std::vector<WindowsInstallerOuterFileMetadata> outerFiles;
    std::vector<WindowsInstallerShortcutMetadata> shortcuts;
    std::vector<WindowsInstallerAssociationMetadata> associations;
};

/// @brief Serialize metadata into the canonical schema-3 UTF-8 representation.
/// @throws std::runtime_error when required values or records are invalid.
std::string serializeWindowsInstallerMetadata(const WindowsInstallerMetadata &metadata);

/// @brief Parse and strictly validate canonical schema-3 metadata.
/// @throws std::runtime_error on malformed, duplicate, unknown, or unsafe data.
WindowsInstallerMetadata parseWindowsInstallerMetadata(std::string_view text);

} // namespace zanna::pkg
