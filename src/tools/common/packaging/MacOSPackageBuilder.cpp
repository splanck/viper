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

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace viper::pkg {

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
        std::string targetDir = asset.targetPath;
        if (targetDir == ".")
            targetDir = "";

        if (!fs::exists(srcPath)) {
            std::cerr << "warning: asset '" << asset.sourcePath << "' not found, skipping\n";
            continue;
        }

        if (fs::is_directory(srcPath)) {
            // Recurse directory (symlink-safe)
            safeDirectoryIterate(
                srcPath, params.projectRoot, [&](const fs::directory_entry &entry) {
                    auto relPath = fs::relative(entry.path(), srcPath).string();
                    std::string zipPath = resourcesPrefix;
                    if (!targetDir.empty())
                        zipPath += targetDir + "/";
                    zipPath += relPath;

                    if (entry.is_directory()) {
                        zip.addDirectory(zipPath);
                    } else if (entry.is_regular_file()) {
                        auto data = readFile(entry.path().string());
                        zip.addFile(zipPath, data.data(), data.size());
                    }
                });
        } else if (fs::is_regular_file(srcPath)) {
            auto data = readFile(srcPath.string());
            std::string zipPath = resourcesPrefix;
            if (!targetDir.empty())
                zipPath += targetDir + "/";
            zipPath += srcPath.filename().string();
            zip.addFile(zipPath, data.data(), data.size());
        }
    }

    // Write the ZIP
    zip.finish(params.outputPath);
}

} // namespace viper::pkg
