//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/ToolchainInstallManifest.hpp
// Purpose: Gather and validate the staged Viper toolchain install tree for
//          native installer packaging.
//
// Key invariants:
//   - The staged tree is the source of truth for shipped files.
//   - Relative paths are normalized and preserved for platform packagers.
//   - Validation fails fast when required toolchain files are missing.
//
// Links: PackageConfig.hpp, PkgUtils.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "PackageConfig.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace viper::pkg {

/// @brief Classification of a file in the Viper toolchain install tree.
/// Used by mapInstallPath to assign the correct platform-specific destination
/// directory (bin/, lib/, include/, share/, etc.) for each file type.
enum class ToolchainFileKind {
    Binary,
    RuntimeArchive,
    SupportLibrary,
    Library,
    Header,
    CMakeConfig,
    ManPage,
    Doc,
    Extra,
};

/// @brief A single file or symlink entry in the toolchain install manifest.
struct ToolchainFileEntry {
    ToolchainFileKind kind{ToolchainFileKind::Extra};
    std::filesystem::path stagedAbsolutePath; ///< Full path in the staging directory.
    std::string stagedRelativePath;           ///< Path relative to the staging root.
    uint64_t sizeBytes{0};                    ///< File size in bytes (0 for symlinks).
    uint32_t unixMode{0};                     ///< POSIX permission bits.
    bool executable{false};                   ///< True if the file should be +x on install.
    bool symlink{false};                      ///< True if this entry is a symbolic link.
    std::string symlinkTarget;                ///< Non-empty only when symlink == true.
};

/// @brief The full Viper toolchain install manifest for one (arch, platform) pair.
struct ToolchainInstallManifest {
    std::string version;
    std::string arch;
    std::string platform;
    std::vector<ToolchainFileEntry> files;
    std::vector<FileAssoc> fileAssociations;

    /// @brief Sum of sizeBytes for all non-symlink entries.
    uint64_t totalSizeBytes() const;
};

/// @brief Destination layout policy for mapInstallPath.
enum class InstallPathPolicy {
    WindowsProgramFilesRoot, ///< Flat layout under %ProgramFiles%\Viper
    MacOSUsrLocalViperRoot,  ///< FHS-like layout under /usr/local/viper/
    LinuxUsrRoot,            ///< FHS layout under /usr/ (bin/, lib/, include/, etc.)
    PortableArchive,         ///< Relative paths for a platform-neutral .zip or .tar.gz
};

/// @brief Walk stagePrefix and build a ToolchainInstallManifest from its contents.
/// If installManifestPath is given, the file at that path provides version/arch/platform
/// metadata; otherwise these fields are inferred from the staged directory tree.
ToolchainInstallManifest gatherToolchainInstallManifest(
    const std::filesystem::path &stagePrefix,
    std::optional<std::filesystem::path> installManifestPath = std::nullopt);

/// @brief Validate that a manifest contains all required toolchain binaries and
/// libraries. Throws std::runtime_error describing the first missing required entry.
void validateToolchainInstallManifest(const ToolchainInstallManifest &manifest);

/// @brief Map a toolchain file entry to its destination install path string under policy.
/// Returns a relative path (no leading "/") for all policies except LinuxUsrRoot which
/// returns a path rooted at "/usr/".
std::string mapInstallPath(const ToolchainFileEntry &file, InstallPathPolicy policy);

/// @brief Return the default file associations for Viper toolchain packages
/// (.zia and .bas source files).
std::vector<FileAssoc> defaultToolchainFileAssociations();

} // namespace viper::pkg
