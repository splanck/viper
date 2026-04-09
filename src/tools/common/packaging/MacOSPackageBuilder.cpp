//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/MacOSPackageBuilder.cpp
// Purpose: Assemble a macOS .app bundle inside a ZIP with proper Unix
//          permissions for executable binaries.
//
// Key invariants:
//   - .app/Contents/MacOS/<name> has mode 0100755.
//   - All other regular files have 0100644.
//   - Directories have 040755.
//   - ICNS icon generated from source PNG with multiple resolutions.
//
// Ownership/Lifetime:
//   - Single-use builder, writes output ZIP file.
//
// Links: MacOSPackageBuilder.hpp, ZipWriter.hpp, PlistGenerator.hpp,
//        IconGenerator.hpp
//
//===----------------------------------------------------------------------===//

#include "MacOSPackageBuilder.hpp"
#include "IconGenerator.hpp"
#include "PkgUtils.hpp"
#include "PlistGenerator.hpp"
#include "ZipWriter.hpp"
#include "common/RunProcess.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace viper::pkg {
namespace {

void writeScriptFile(const fs::path &path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write macOS packaging script: " + path.string());
    out << text;
    out.close();
    fs::permissions(path,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                        fs::perms::group_read | fs::perms::group_exec |
                        fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace);
}

fs::path uniqueTempPackagingDir(std::string_view stem) {
    const auto tick =
        static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto pid =
#if defined(_WIN32)
        static_cast<unsigned long long>(_getpid());
#else
        static_cast<unsigned long long>(::getpid());
#endif
    return fs::temp_directory_path() /
           (std::string(stem) + "-" + std::to_string(pid) + "-" + std::to_string(tick));
}

class TempDirGuard {
  public:
    explicit TempDirGuard(fs::path path) : path_(std::move(path)) {}
    ~TempDirGuard() {
        if (!path_.empty()) {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
    }

  private:
    fs::path path_;
};

} // namespace

//=============================================================================
// MacOS Package Builder
//=============================================================================

void buildMacOSPackage(const MacOSBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;

    // Determine executable name (lowercase, no spaces)
    std::string execName = normalizeExecName(params.projectName);

    std::string appName = displayName + ".app";
    std::string contentsPrefix = appName + "/Contents/";
    std::string macosPrefix = contentsPrefix + "MacOS/";
    std::string resourcesPrefix = contentsPrefix + "Resources/";
    const std::string resourcesRoot =
        sanitizePackageRelativePath(resourcesPrefix, "bundle resource path");

    ZipWriter zip;

    // Create directory structure
    zip.addDirectory(appName);
    zip.addDirectory(contentsPrefix);
    zip.addDirectory(macosPrefix);
    zip.addDirectory(resourcesPrefix);

    // PkgInfo
    zip.addFileString(contentsPrefix + "PkgInfo", generatePkgInfo());

    // Executable (with 0755 permission!)
    auto execData = readFile(params.executablePath);
    zip.addFile(macosPrefix + execName, execData.data(), execData.size(), 0100755);

    // Icon (ICNS from source PNG)
    std::string iconFileName;
    if (!pkg.iconPath.empty()) {
        fs::path iconSrc = fs::path(params.projectRoot) / pkg.iconPath;
        if (fs::exists(iconSrc)) {
            auto srcImage = pngRead(iconSrc.string());
            auto icnsData = generateIcns(srcImage);
            iconFileName = execName;
            zip.addFile(resourcesPrefix + execName + ".icns", icnsData.data(), icnsData.size());
        } else {
            std::cerr << "warning: package-icon '" << pkg.iconPath
                      << "' not found, skipping icon generation\n";
        }
    }

    // Info.plist
    PlistParams plistParams;
    plistParams.executableName = execName;
    plistParams.bundleId = pkg.identifier.empty() ? ("com.viper." + execName) : pkg.identifier;
    plistParams.bundleName = displayName;
    plistParams.version = params.version;
    plistParams.iconFile = iconFileName;
    plistParams.minOsVersion = pkg.minOsMacos;
    plistParams.fileAssociations = pkg.fileAssociations;
    zip.addFileString(contentsPrefix + "Info.plist", generatePlist(plistParams));

    // Assets
    for (const auto &asset : pkg.assets) {
        fs::path srcPath = fs::path(params.projectRoot) / asset.sourcePath;
        std::string targetDir = sanitizePackageRelativePath(asset.targetPath, "asset target path");

        if (!fs::exists(srcPath)) {
            std::cerr << "warning: asset '" << asset.sourcePath << "' not found, skipping\n";
            continue;
        }

        if (fs::is_directory(srcPath)) {
            // Recurse directory (symlink-safe)
            safeDirectoryIterate(
                srcPath, params.projectRoot, [&](const fs::directory_entry &entry) {
                    auto relPath = sanitizePackageRelativePath(
                        fs::relative(entry.path(), srcPath).generic_string(), "asset path");
                    std::string assetBase =
                        joinPackageRelativePath(resourcesRoot, targetDir, "asset target path");
                    std::string zipPath = joinPackageRelativePath(assetBase, relPath, "asset path");

                    if (entry.is_directory()) {
                        zip.addDirectory(zipPath);
                    } else if (entry.is_regular_file()) {
                        auto data = readFile(entry.path().string());
                        zip.addFile(zipPath, data.data(), data.size());
                    }
                });
        } else if (fs::is_regular_file(srcPath)) {
            auto data = readFile(srcPath.string());
            std::string assetBase =
                joinPackageRelativePath(resourcesRoot, targetDir, "asset target path");
            std::string zipPath = joinPackageRelativePath(
                assetBase, srcPath.filename().generic_string(), "asset path");
            zip.addFile(zipPath, data.data(), data.size());
        }
    }

    // Write the ZIP
    zip.finish(params.outputPath);
}

void buildMacOSToolchainPackage(const MacOSToolchainBuildParams &params) {
    namespace fs = std::filesystem;
    const std::string version = params.manifest.version.empty() ? "0.0.0" : params.manifest.version;

    const fs::path tmpRoot = uniqueTempPackagingDir("viper-macos-toolchain-" + version);
    TempDirGuard cleanup(tmpRoot);
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "root" / "usr" / "local" / "viper");
    fs::create_directories(tmpRoot / "scripts");

    const fs::path installRoot = tmpRoot / "root" / "usr" / "local" / "viper";
    for (const auto &file : params.manifest.files) {
        const fs::path dst = installRoot / fs::path(file.stagedRelativePath);
        fs::create_directories(dst.parent_path());
        fs::copy_file(file.stagedAbsolutePath, dst, fs::copy_options::overwrite_existing);
        if (file.executable) {
            fs::permissions(dst,
                            fs::perms::owner_read | fs::perms::owner_write |
                                fs::perms::owner_exec | fs::perms::group_read |
                                fs::perms::group_exec | fs::perms::others_read |
                                fs::perms::others_exec,
                            fs::perm_options::replace);
        }
    }

    std::ostringstream postinstall;
    postinstall << "#!/bin/sh\nset -e\nmkdir -p /usr/local/bin\n";
    for (const auto &file : params.manifest.files) {
        if (file.kind != ToolchainFileKind::Binary)
            continue;
        const std::string name = fs::path(file.stagedRelativePath).filename().string();
        postinstall << "ln -sf /usr/local/viper/bin/" << name << " /usr/local/bin/" << name << "\n";
    }
    writeScriptFile(tmpRoot / "scripts" / "postinstall", postinstall.str());

    const RunResult rr = run_process(
        {"pkgbuild",
         "--root",
         (tmpRoot / "root").string(),
         "--scripts",
         (tmpRoot / "scripts").string(),
         "--identifier",
         params.identifier,
         "--version",
         version,
         params.outputPath});
    if (rr.exit_code != 0) {
        throw std::runtime_error("pkgbuild failed while generating macOS toolchain package:\n" +
                                 rr.out + rr.err);
    }
}

} // namespace viper::pkg
