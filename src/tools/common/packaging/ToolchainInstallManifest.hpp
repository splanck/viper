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

struct ToolchainFileEntry {
    ToolchainFileKind kind{ToolchainFileKind::Extra};
    std::filesystem::path stagedAbsolutePath;
    std::string stagedRelativePath;
    uint64_t sizeBytes{0};
    uint32_t unixMode{0};
    bool executable{false};
};

struct ToolchainInstallManifest {
    std::string version;
    std::string arch;
    std::vector<ToolchainFileEntry> files;
    std::vector<FileAssoc> fileAssociations;

    uint64_t totalSizeBytes() const;
};

enum class InstallPathPolicy {
    WindowsProgramFilesRoot,
    MacOSUsrLocalViperRoot,
    LinuxUsrRoot,
    PortableArchive,
};

ToolchainInstallManifest gatherToolchainInstallManifest(
    const std::filesystem::path &stagePrefix,
    std::optional<std::filesystem::path> installManifestPath = std::nullopt);

void validateToolchainInstallManifest(const ToolchainInstallManifest &manifest);

std::string mapInstallPath(const ToolchainFileEntry &file, InstallPathPolicy policy);

std::vector<FileAssoc> defaultToolchainFileAssociations();

} // namespace viper::pkg
