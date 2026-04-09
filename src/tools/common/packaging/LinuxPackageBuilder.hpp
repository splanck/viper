//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/LinuxPackageBuilder.hpp
// Purpose: Build Linux .deb packages and .tar.gz archives from scratch.
//
// Key invariants:
//   - .deb = ar(debian-binary + control.tar.gz + data.tar.gz).
//   - .tar.gz uses FHS-compliant paths (/usr/bin, /usr/share, etc.).
//   - All format bytes emitted directly — no dpkg-deb or tar dependency.
//   - md5sums file contains hex digest + two-space + path for every data file.
//
// Ownership/Lifetime:
//   - Builder is single-use: call build() once.
//
// Links: ArWriter.hpp, TarWriter.hpp, PkgGzip.hpp, PkgMD5.hpp,
//        DesktopEntryGenerator.hpp, PackageConfig.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "PackageConfig.hpp"
#include "ToolchainInstallManifest.hpp"

#include <string>

namespace viper::pkg {

/// @brief Parameters for building a Linux .deb or .tar.gz package.
struct LinuxBuildParams {
    std::string projectName;    ///< Project name (lowercase, no spaces)
    std::string version;        ///< Version string (e.g. "1.2.0")
    std::string executablePath; ///< Path to the compiled native binary
    std::string projectRoot;    ///< Absolute path to project root directory
    PackageConfig pkgConfig;    ///< Package configuration from manifest
    std::string outputPath;     ///< Output file path
    std::string archStr;        ///< Architecture string: "amd64" or "arm64"
};

/// @brief Build a Debian .deb package.
/// @param params Build parameters.
/// @throws std::runtime_error on failure.
void buildDebPackage(const LinuxBuildParams &params);

/// @brief Build a portable .tar.gz archive.
/// @param params Build parameters.
/// @throws std::runtime_error on failure.
void buildTarball(const LinuxBuildParams &params);

/// @brief Parameters for building Linux toolchain packages from a staged install tree.
struct LinuxToolchainBuildParams {
    ToolchainInstallManifest manifest;
    std::string outputPath;
    std::string packageName{"viper"};
};

/// @brief Build a Debian toolchain package from a staged install manifest.
void buildToolchainDebPackage(const LinuxToolchainBuildParams &params);

/// @brief Build an RPM toolchain package from a staged install manifest.
void buildToolchainRpmPackage(const LinuxToolchainBuildParams &params);

/// @brief Build a portable toolchain tarball from a staged install manifest.
void buildToolchainTarball(const LinuxToolchainBuildParams &params);

} // namespace viper::pkg
