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

/// @brief Build a self-extracting Linux `.run` bundle for an end-user application.
/// @details Lays the payload out as a portable tree (app binary at `usr/bin/<exe>`
///          with an `AppRun` symlink entry point, bundled assets under
///          `usr/share/<pkg>/`, and a `.desktop` launcher plus icon at the payload
///          root), then wraps it in the shared FUSE-less self-extracting runtime
///          stub. `params.archStr` must be the portable form ("x64" or "arm64").
/// @param params Build parameters.
/// @throws std::runtime_error on failure.
void buildAppImage(const LinuxBuildParams &params);

/// @brief Build an RPM package for an end-user application (requires rpmbuild on PATH).
/// @details Reuses the shared FHS layout from the Debian application builder. Throws
///          a clear diagnostic when rpmbuild is unavailable. `params.archStr` must be
///          the portable form ("x64" or "arm64").
/// @param params Build parameters.
/// @throws std::runtime_error on failure.
void buildRpmPackage(const LinuxBuildParams &params);

/// @brief GPG-sign a built Debian (.deb) or RPM (.rpm) package in place.
/// @details Shells out to the standard signing tool (rpmsign for RPM, dpkg-sig for
///          Debian), mirroring the macOS/Windows external-tool signing approach.
///          Throws a clear diagnostic when the tool is not on PATH or signing fails.
/// @param packagePath Path to the built package (signed in place).
/// @param gpgKeyId GPG key id or name to sign with.
/// @param isRpm True to sign an RPM, false to sign a Debian package.
/// @throws std::runtime_error on failure.
void signLinuxPackage(const std::string &packagePath, const std::string &gpgKeyId, bool isRpm);

/// @brief Parameters for building Linux toolchain packages from a staged install tree.
struct LinuxToolchainBuildParams {
    ToolchainInstallManifest manifest; ///< Staged files and metadata to package.
    std::string outputPath;            ///< Output package file path.
    std::string packageName{"viper"};  ///< Package/base name (default "viper").
};

/// @brief Build a Debian toolchain package from a staged install manifest.
/// @param params Manifest, output path, and package name.
/// @throws std::runtime_error on failure.
void buildToolchainDebPackage(const LinuxToolchainBuildParams &params);

/// @brief Build an RPM toolchain package from a staged install manifest.
/// @param params Manifest, output path, and package name.
/// @throws std::runtime_error on failure.
void buildToolchainRpmPackage(const LinuxToolchainBuildParams &params);

/// @brief Build a portable toolchain tarball from a staged install manifest.
/// @param params Manifest, output path, and package name.
/// @throws std::runtime_error on failure.
void buildToolchainTarball(const LinuxToolchainBuildParams &params);

/// @brief Build a self-extracting Linux `.run` bundle from a staged install manifest.
/// @param params Manifest, output path, and package name.
/// @throws std::runtime_error on failure.
void buildToolchainBundle(const LinuxToolchainBuildParams &params);

} // namespace viper::pkg
