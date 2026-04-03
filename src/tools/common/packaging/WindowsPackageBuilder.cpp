//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/WindowsPackageBuilder.cpp
// Purpose: Assemble a Windows self-extracting installer .exe.
//
// Key invariants:
//   - Overlay is a structurally valid ZIP archive using stored entries only.
//   - Installer/uninstaller behavior is driven by an explicit package layout,
//     not by parsing installer metadata at runtime.
//   - All package construction happens inside Viper; no external tools are used.
//
// Ownership/Lifetime:
//   - Single-use builder.
//
// Links: WindowsPackageBuilder.hpp, InstallerStub.hpp, PEBuilder.hpp, ZipWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "WindowsPackageBuilder.hpp"
#include "IconGenerator.hpp"
#include "InstallerStub.hpp"
#include "LnkWriter.hpp"
#include "PEBuilder.hpp"
#include "PkgUtils.hpp"
#include "ZipWriter.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace viper::pkg {

namespace {

void addUniqueDir(std::vector<WindowsPackageDirEntry> &out,
                  std::set<std::string> &seen,
                  WindowsInstallRoot root,
                  const std::string &relativePath) {
    const std::string clean = sanitizePackageRelativePath(relativePath, "windows package path");
    if (clean.empty())
        return;
    const std::string key =
        std::to_string(static_cast<unsigned long long>(root)) + ":" + clean;
    if (!seen.insert(key).second)
        return;
    out.push_back(WindowsPackageDirEntry{root, clean});
}

void addParentDirs(std::vector<WindowsPackageDirEntry> &out,
                   std::set<std::string> &seen,
                   WindowsInstallRoot root,
                   const std::string &relativeFilePath) {
    fs::path rel(relativeFilePath);
    fs::path cur = rel.parent_path();
    if (cur.empty())
        return;

    std::vector<std::string> dirs;
    while (!cur.empty() && cur != ".") {
        dirs.push_back(sanitizePackageRelativePath(cur.generic_string(), "windows package path"));
        cur = cur.parent_path();
    }
    std::reverse(dirs.begin(), dirs.end());
    for (const auto &dir : dirs)
        addUniqueDir(out, seen, root, dir);
}

void addOverlayFile(ZipWriter &zip,
                    const std::string &overlayName,
                    const uint8_t *data,
                    size_t len,
                    uint32_t unixMode,
                    WindowsPackageLayout &layout,
                    WindowsInstallRoot root,
                    const std::string &installRelativePath,
                    bool deleteOnUninstall) {
    zip.addFile(overlayName, data, len, unixMode);
    const auto &entry = zip.layoutEntries().back();
    layout.installFiles.push_back(WindowsPackageFileEntry{
        root, installRelativePath, entry.localDataOffset, entry.uncompressedSize});
    if (deleteOnUninstall) {
        layout.uninstallFiles.push_back(
            WindowsPackageFileEntry{root, installRelativePath, 0, entry.uncompressedSize});
    }
}

void finalizeUninstallDirs(WindowsPackageLayout &layout) {
    layout.uninstallDirectories = layout.installDirectories;
    std::stable_sort(layout.uninstallDirectories.begin(),
                     layout.uninstallDirectories.end(),
                     [](const WindowsPackageDirEntry &a, const WindowsPackageDirEntry &b) {
                         auto depth = [](const std::string &path) {
                             return static_cast<int>(std::count(path.begin(), path.end(), '/'));
                         };
                         const int da = depth(a.relativePath);
                         const int db = depth(b.relativePath);
                         if (da != db)
                             return da > db;
                         return a.relativePath.size() > b.relativePath.size();
                     });
}

} // namespace

void buildWindowsPackage(const WindowsBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    const std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    const std::string exec = normalizeExecName(params.projectName);
    const std::string installDir = displayName;

    WindowsPackageLayout layout;
    layout.displayName = displayName;
    layout.installDirName = installDir;
    layout.version = params.version;
    layout.identifier = pkg.identifier;
    layout.publisher = pkg.author;
    layout.executableName = exec + ".exe";
    layout.createDesktopShortcut = pkg.shortcutDesktop;
    layout.createStartMenuShortcut = pkg.shortcutMenu;

    std::set<std::string> installDirSet;

    ZipWriter zip;
    zip.setCompressionEnabled(false);
    zip.addDirectory("app/");
    zip.addDirectory("meta/");

    std::vector<uint8_t> icoData;
    if (!pkg.iconPath.empty()) {
        fs::path iconSrc = fs::path(params.projectRoot) / pkg.iconPath;
        if (fs::exists(iconSrc)) {
            auto srcImage = pngRead(iconSrc.string());
            icoData = generateIco(srcImage);
        } else {
            std::cerr << "warning: package-icon '" << pkg.iconPath
                      << "' not found, skipping icon generation\n";
        }
    }

    const auto execData = readFile(params.executablePath);
    addOverlayFile(zip,
                   "app/" + exec + ".exe",
                   execData.data(),
                   execData.size(),
                   0100755,
                   layout,
                   WindowsInstallRoot::InstallDir,
                   exec + ".exe",
                   true);

    for (const auto &asset : pkg.assets) {
        fs::path srcPath = fs::path(params.projectRoot) / asset.sourcePath;
        const std::string targetDir =
            sanitizePackageRelativePath(asset.targetPath, "asset target path");

        if (!fs::exists(srcPath)) {
            std::cerr << "warning: asset '" << asset.sourcePath << "' not found, skipping\n";
            continue;
        }

        if (fs::is_directory(srcPath)) {
            safeDirectoryIterate(
                srcPath, params.projectRoot, [&](const fs::directory_entry &entry) {
                    const auto relPath = sanitizePackageRelativePath(
                        fs::relative(entry.path(), srcPath).generic_string(), "asset path");
                    if (entry.is_directory()) {
                        const std::string relInstall =
                            joinPackageRelativePath(targetDir, relPath, "asset path");
                        addUniqueDir(
                            layout.installDirectories, installDirSet, WindowsInstallRoot::InstallDir, relInstall);
                        return;
                    }
                    if (!entry.is_regular_file())
                        return;

                    const std::string relInstall =
                        joinPackageRelativePath(targetDir, relPath, "asset path");
                    addParentDirs(
                        layout.installDirectories, installDirSet, WindowsInstallRoot::InstallDir, relInstall);
                    const auto data = readFile(entry.path().string());
                    addOverlayFile(zip,
                                   "app/" + relInstall,
                                   data.data(),
                                   data.size(),
                                   0100644,
                                   layout,
                                   WindowsInstallRoot::InstallDir,
                                   relInstall,
                                   true);
                });
        } else if (fs::is_regular_file(srcPath)) {
            const std::string relInstall = joinPackageRelativePath(
                targetDir, srcPath.filename().generic_string(), "asset path");
            addParentDirs(
                layout.installDirectories, installDirSet, WindowsInstallRoot::InstallDir, relInstall);
            const auto data = readFile(srcPath.string());
            addOverlayFile(zip,
                           "app/" + relInstall,
                           data.data(),
                           data.size(),
                           0100644,
                           layout,
                           WindowsInstallRoot::InstallDir,
                           relInstall,
                           true);
        }
    }

    if (pkg.shortcutMenu) {
        LnkParams lnk;
        lnk.targetPath = "C:\\Program Files\\" + installDir + "\\" + exec + ".exe";
        lnk.workingDir = "C:\\Program Files\\" + installDir;
        lnk.description = displayName;
        if (!pkg.iconPath.empty())
            lnk.iconPath = lnk.targetPath;
        const auto lnkData = generateLnk(lnk);
        addOverlayFile(zip,
                       "meta/start_menu.lnk",
                       lnkData.data(),
                       lnkData.size(),
                       0100644,
                       layout,
                       WindowsInstallRoot::StartMenuDir,
                       displayName + ".lnk",
                       true);
    }

    if (pkg.shortcutDesktop) {
        LnkParams lnk;
        lnk.targetPath = "C:\\Program Files\\" + installDir + "\\" + exec + ".exe";
        lnk.workingDir = "C:\\Program Files\\" + installDir;
        lnk.description = displayName;
        if (!pkg.iconPath.empty())
            lnk.iconPath = lnk.targetPath;
        const auto lnkData = generateLnk(lnk);
        addOverlayFile(zip,
                       "meta/desktop.lnk",
                       lnkData.data(),
                       lnkData.size(),
                       0100644,
                       layout,
                       WindowsInstallRoot::DesktopDir,
                       displayName + ".lnk",
                       true);
    }

    finalizeUninstallDirs(layout);

    auto uninstStub = buildUninstallerStub(layout, params.archStr);
    PEBuildParams uninstPe;
    uninstPe.arch = uninstStub.peArch;
    uninstPe.textSection = uninstStub.textSection;
    uninstPe.rdataSection = uninstStub.stubData;
    uninstPe.imports = uninstStub.imports;
    uninstPe.manifest = generateAsInvokerManifest();
    uninstPe.iconData = icoData;
    auto uninstBytes = buildPE(uninstPe);
    addOverlayFile(zip,
                   "app/uninstall.exe",
                   uninstBytes.data(),
                   uninstBytes.size(),
                   0100755,
                   layout,
                   WindowsInstallRoot::InstallDir,
                   "uninstall.exe",
                   false);

    const auto zipPayload = zip.finishToVector();

    layout.overlayFileOffset = 0;
    auto provisionalStub = buildInstallerStub(layout, params.archStr);
    PEBuildParams provisionalPe;
    provisionalPe.arch = provisionalStub.peArch;
    provisionalPe.textSection = provisionalStub.textSection;
    provisionalPe.rdataSection = provisionalStub.stubData;
    provisionalPe.imports = provisionalStub.imports;
    provisionalPe.manifest = generateUacManifest();
    provisionalPe.iconData = icoData;
    provisionalPe.overlay = zipPayload;
    const auto provisionalBytes = buildPE(provisionalPe);
    layout.overlayFileOffset =
        static_cast<uint32_t>(provisionalBytes.size() - zipPayload.size());

    auto instStub = buildInstallerStub(layout, params.archStr);
    PEBuildParams pe;
    pe.arch = instStub.peArch;
    pe.textSection = instStub.textSection;
    pe.rdataSection = instStub.stubData;
    pe.imports = instStub.imports;
    pe.manifest = generateUacManifest();
    pe.iconData = icoData;
    pe.overlay = zipPayload;

    const auto peBytes = buildPE(pe);
    writePEToFile(peBytes, params.outputPath);
}

} // namespace viper::pkg
