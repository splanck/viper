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
#include <cctype>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace viper::pkg {

namespace {

constexpr size_t kInstallerStubPathCharLimit = 32768;
constexpr uint64_t kInstallerStackReserve = 0x200000;
constexpr uint64_t kInstallerStackCommit = 0x100000;

/// @brief Apply a larger-than-default stack to all installer/uninstaller PEs.
/// The installer recursively creates directory trees and copies large files;
/// the default 1 MB reserve is too small for deeply nested install paths.
void configureInstallerStack(PEBuildParams &pe) {
    pe.stackReserve = kInstallerStackReserve;
    pe.stackCommit = kInstallerStackCommit;
}

/// @brief Append a directory entry to out only if it has not already been seen.
/// The seen set keys on root+path so the same relative path under different
/// roots (e.g. InstallDir vs StartMenuDir) is treated as two distinct entries.
void addUniqueDir(std::vector<WindowsPackageDirEntry> &out,
                  std::set<std::string> &seen,
                  WindowsInstallRoot root,
                  const std::string &relativePath) {
    const std::string clean = sanitizePackageRelativePath(relativePath, "windows package path");
    if (clean.empty())
        return;
    const std::string key = std::to_string(static_cast<unsigned long long>(root)) + ":" + clean;
    if (!seen.insert(key).second)
        return;
    out.push_back(WindowsPackageDirEntry{root, clean});
}

/// @brief Validate every path segment in a slash-separated relative Windows path.
/// Each segment must pass validateWindowsFileName to reject reserved device names
/// (CON, NUL, COM1…), illegal characters (<, >, :, |, ?, *), and empty components.
void validateWindowsRelativePath(const std::string &relativePath, const char *fieldName) {
    const std::string clean = sanitizePackageRelativePath(relativePath, fieldName);
    size_t pos = 0;
    while (pos < clean.size()) {
        const size_t next = clean.find('/', pos);
        const std::string segment =
            next == std::string::npos ? clean.substr(pos) : clean.substr(pos, next - pos);
        validateWindowsFileName(segment, fieldName);
        if (next == std::string::npos)
            break;
        pos = next + 1;
    }
}

/// @brief Ensure that an absolute-expanded Windows path (e.g. %ProgramFiles%\App\bin\viper.exe)
/// fits within the installer stub's fixed-size WCHAR path buffer (32768 code units).
/// The check uses UTF-16 unit count so multi-byte UTF-8 characters are counted correctly.
void validateStubPathFits(const std::string &path, const char *fieldName) {
    if (utf16CodeUnitCountFromUtf8(path) + 1 > kInstallerStubPathCharLimit)
        throw std::runtime_error(std::string(fieldName) +
                                 " exceeds the Windows installer long-path stub limit: " + path);
}

/// @brief Pre-flight every path that the installer stub will write at runtime to
/// ensure none exceed the stub's WCHAR buffer limit. Checks: install root,
/// all directories, all files, the optional PATH entry, the file association
/// executable path, and all file association ProgID command arguments.
void validateWindowsLayoutFitsStub(const WindowsPackageLayout &layout) {
    const std::string installDir = layout.installDirName.empty() ? layout.displayName
                                                                 : layout.installDirName;
    validateWindowsFileName(installDir, "Windows install directory");
    const std::string rootProbe = "%ProgramFiles%\\" + installDir;
    validateStubPathFits(rootProbe, "Windows install directory");
    for (const auto &dir : layout.installDirectories) {
        validateWindowsRelativePath(dir.relativePath, "Windows install directory path");
        validateStubPathFits(rootProbe + "\\" + dir.relativePath, "Windows install directory path");
    }
    for (const auto &file : layout.installFiles) {
        validateWindowsRelativePath(file.relativePath, "Windows install file path");
        validateStubPathFits(rootProbe + "\\" + file.relativePath, "Windows install file path");
    }
    if (layout.addToPath && !layout.pathRelativePath.empty()) {
        validateWindowsRelativePath(layout.pathRelativePath, "Windows PATH entry path");
        validateStubPathFits(rootProbe + "\\" + layout.pathRelativePath, "Windows PATH entry path");
    }
    if (!layout.fileAssociationExecutableRelativePath.empty()) {
        validateWindowsRelativePath(layout.fileAssociationExecutableRelativePath,
                                    "Windows file association executable path");
        validateStubPathFits(rootProbe + "\\" + layout.fileAssociationExecutableRelativePath,
                             "Windows file association executable path");
    }
    for (const auto &file : layout.uninstallFiles)
        validateWindowsRelativePath(file.relativePath, "Windows uninstall file path");
    for (const auto &assoc : layout.fileAssociations) {
        validateSingleLineField(assoc.openCommandArguments,
                                "Windows file association command arguments");
        if (assoc.openCommandArguments.find('"') != std::string::npos) {
            throw std::runtime_error("Windows file association command arguments must not "
                                     "contain quotes: " +
                                     assoc.openCommandArguments);
        }
    }
}

/// @brief Return text lowercased for case-insensitive filename comparisons (LICENSE, README.MD).
/// Only transforms ASCII letters so it is safe for arbitrary UTF-8 filenames.
std::string lowerAscii(std::string text) {
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// @brief Build the Windows ProgID string for a file association in app packages.
/// Format: "<pkg.identifier>.<ext>" — e.g. "com.example.myapp.zia".
/// ProgIDs are registered in HKEY_CLASSES_ROOT and link the extension to the app.
std::string windowsProgIdFor(const PackageConfig &pkg,
                             const std::string &exec,
                             const FileAssoc &assoc) {
    std::string ext = assoc.extension;
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::string base = pkg.identifier.empty() ? ("viper." + exec) : pkg.identifier;
    validateWindowsProgIdBase(base, "Windows file association ProgID base");
    validateFileAssociation(assoc.extension, assoc.description, assoc.mimeType);
    return base + "." + ext;
}

/// @brief Build a %ProgramFiles%\<installDir>[\<leaf>] path string for use in .lnk
/// shortcut files and installer metadata. The %ProgramFiles% token is expanded
/// by Windows at shortcut resolution time, so the path is machine-independent.
std::string programFilesEnvPath(const std::string &installDir, const std::string &leaf = {}) {
    std::string path = "%ProgramFiles%\\" + installDir;
    if (!leaf.empty())
        path += "\\" + leaf;
    return path;
}

/// @brief Build the Windows ProgID for a toolchain file association.
/// Equivalent to windowsProgIdFor but takes an explicit identifier string instead
/// of a full PackageConfig; used for toolchain installer builds.
std::string toolchainProgIdFor(const std::string &identifier, const FileAssoc &assoc) {
    validateWindowsProgIdBase(identifier, "Windows file association ProgID base");
    validateFileAssociation(assoc.extension, assoc.description, assoc.mimeType);
    std::string ext = assoc.extension;
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    return identifier + "." + ext;
}

/// @brief Return the command-line arguments the viper binary uses to open files of
/// this extension: "-run" for pre-compiled .il modules, "run" for source files.
std::string toolchainOpenCommandArgsFor(const FileAssoc &assoc) {
    std::string ext = assoc.extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".il" ? "-run" : "run";
}

/// @brief Walk every parent directory segment of relativeFilePath and ensure each one
/// is present in out. Directories are added shallowest-first so the installer
/// creates parent directories before their children. Duplicate entries are
/// suppressed via the seen set.
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

/// @brief Add a file to the ZIP overlay and register corresponding install/uninstall
/// entries in layout. The layout entry captures the local-data offset and CRC-32
/// from the freshly-written ZIP entry so the installer stub can locate and verify
/// each file without parsing the central directory at runtime.
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
        root, installRelativePath, entry.localDataOffset, entry.uncompressedSize, entry.crc32});
    if (deleteOnUninstall) {
        layout.uninstallFiles.push_back(
            WindowsPackageFileEntry{root, installRelativePath, 0, entry.uncompressedSize, 0});
    }
}

/// @brief Populate uninstallDirectories from installDirectories, then sort deepest-first
/// so the uninstaller removes leaf directories before their parents. Sorting by
/// depth first, then path length, ensures correct order when depths are equal.
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

/// @brief Build a Windows self-extracting installer for a single application binary.
/// Assembly is a two-pass process: Pass 1 measures the exact overlay offset with
/// overlayFileOffset=0; Pass 2 bakes the measured offset into the stub and produces
/// the final PE. The uninstaller is built first so it can be included in the ZIP.
void buildWindowsPackage(const WindowsBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    const std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    const std::string exec = normalizeExecName(params.projectName);
    const std::string installDir = displayName;
    validateWindowsFileName(displayName, "Windows display name");
    validateWindowsProgIdBase(pkg.identifier, "Windows package identifier");
    validateDottedNumericVersion(params.version.empty() ? "0.0.0" : params.version,
                                 "Windows package version");
    if (!pkg.minOsWindows.empty())
        validateDottedNumericVersion(pkg.minOsWindows, "minimum Windows version");
    validateSingleLineField(pkg.author, "Windows package author");
    validateSingleLineField(pkg.description, "Windows package description");
    validatePackageUrl(pkg.homepage, "package homepage");
    validatePackageFileAssociations(pkg.fileAssociations);
    if (!fs::is_regular_file(params.executablePath))
        throw std::runtime_error("Windows package executable is not a regular file: " +
                                 params.executablePath);

    WindowsPackageLayout layout;
    layout.displayName = displayName;
    layout.installDirName = installDir;
    layout.version = params.version;
    layout.identifier = pkg.identifier;
    layout.publisher = pkg.author;
    layout.executableName = exec + ".exe";
    layout.createDesktopShortcut = pkg.shortcutDesktop;
    layout.createStartMenuShortcut = pkg.shortcutMenu;
    for (const auto &assoc : pkg.fileAssociations) {
        layout.fileAssociations.push_back(
            {assoc.extension,
             assoc.description,
             assoc.mimeType,
             windowsProgIdFor(pkg, exec, assoc),
             {}});
    }

    std::set<std::string> installDirSet;

    ZipWriter zip;
    zip.setCompressionEnabled(false);
    zip.addDirectory("app/");
    zip.addDirectory("meta/");

    std::vector<uint8_t> icoData;
    if (!pkg.iconPath.empty()) {
        fs::path iconSrc = resolvePackageSourcePath(params.projectRoot, pkg.iconPath, "package icon");
        if (!fs::is_regular_file(iconSrc))
            throw std::runtime_error("package icon not found: " + pkg.iconPath);
        auto srcImage = pngRead(iconSrc.string());
        icoData = generateIco(srcImage);
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

    if (!icoData.empty()) {
        addOverlayFile(zip,
                       "app/" + exec + ".ico",
                       icoData.data(),
                       icoData.size(),
                       0100644,
                       layout,
                       WindowsInstallRoot::InstallDir,
                       exec + ".ico",
                       true);
    }

    for (const auto &asset : pkg.assets) {
        fs::path srcPath = resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
        const std::string targetDir =
            sanitizePackageRelativePath(asset.targetPath, "asset target path");
        validateWindowsRelativePath(targetDir, "asset target path");

        if (!fs::exists(srcPath))
            throw std::runtime_error("asset not found: " + asset.sourcePath);

        if (fs::is_directory(srcPath)) {
            if (!targetDir.empty()) {
                addUniqueDir(layout.installDirectories,
                             installDirSet,
                             WindowsInstallRoot::InstallDir,
                             targetDir);
            }
            safeDirectoryIterate(
                srcPath, params.projectRoot, [&](const fs::directory_entry &entry) {
                    const auto relPath = sanitizePackageRelativePath(
                        entry.path().lexically_relative(srcPath).generic_string(), "asset path");
                    if (fs::is_directory(entry.path())) {
                        const std::string relInstall =
                            joinPackageRelativePath(targetDir, relPath, "asset path");
                        addUniqueDir(layout.installDirectories,
                                     installDirSet,
                                     WindowsInstallRoot::InstallDir,
                                     relInstall);
                        return;
                    }
                    if (!fs::is_regular_file(entry.path()))
                        return;

                    const std::string relInstall =
                        joinPackageRelativePath(targetDir, relPath, "asset path");
                    addParentDirs(layout.installDirectories,
                                  installDirSet,
                                  WindowsInstallRoot::InstallDir,
                                  relInstall);
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
            addParentDirs(layout.installDirectories,
                          installDirSet,
                          WindowsInstallRoot::InstallDir,
                          relInstall);
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
        } else {
            throw std::runtime_error("asset is not a regular file or directory: " +
                                     asset.sourcePath);
        }
    }

    if (pkg.shortcutMenu) {
        LnkParams lnk;
        lnk.targetPath = programFilesEnvPath(installDir, exec + ".exe");
        lnk.workingDir = programFilesEnvPath(installDir);
        lnk.description = displayName;
        if (!icoData.empty())
            lnk.iconPath = programFilesEnvPath(installDir, exec + ".ico");
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
        lnk.targetPath = programFilesEnvPath(installDir, exec + ".exe");
        lnk.workingDir = programFilesEnvPath(installDir);
        lnk.description = displayName;
        if (!icoData.empty())
            lnk.iconPath = programFilesEnvPath(installDir, exec + ".ico");
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
    validateWindowsLayoutFitsStub(layout);

    auto uninstStub = buildUninstallerStub(layout, params.archStr);
    PEBuildParams uninstPe;
    uninstPe.arch = uninstStub.peArch;
    uninstPe.textSection = uninstStub.textSection;
    uninstPe.rdataSection = uninstStub.stubData;
    uninstPe.imports = uninstStub.imports;
    uninstPe.manifest = generateUacManifest(pkg.minOsWindows);
    uninstPe.iconData = icoData;
    configureInstallerStack(uninstPe);
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
    provisionalPe.manifest = generateUacManifest(pkg.minOsWindows);
    provisionalPe.iconData = icoData;
    provisionalPe.overlay = zipPayload;
    configureInstallerStack(provisionalPe);
    const auto provisionalBytes = buildPE(provisionalPe);
    layout.overlayFileOffset = static_cast<uint32_t>(provisionalBytes.size() - zipPayload.size());

    auto instStub = buildInstallerStub(layout, params.archStr);
    PEBuildParams pe;
    pe.arch = instStub.peArch;
    pe.textSection = instStub.textSection;
    pe.rdataSection = instStub.stubData;
    pe.imports = instStub.imports;
    pe.manifest = generateUacManifest(pkg.minOsWindows);
    pe.iconData = icoData;
    pe.overlay = zipPayload;
    configureInstallerStack(pe);

    const auto peBytes = buildPE(pe);
    writePEToFile(peBytes, params.outputPath);
}

/// @brief Build a Windows toolchain installer from a pre-validated staged manifest.
/// Unlike buildWindowsPackage, all files come from the manifest (no asset globs, no
/// PNG icon conversion, no desktop/Start Menu shortcuts). PATH registration and file
/// associations for .zia/.bas/.il are always enabled. Symlinks are dereferenced.
void buildWindowsToolchainInstaller(const WindowsToolchainBuildParams &params) {
    validateToolchainInstallManifest(params.manifest);
    if (params.manifest.platform != "windows") {
        throw std::runtime_error("Windows toolchain installer requires a Windows staged "
                                 "toolchain manifest, got '" +
                                 params.manifest.platform + "'");
    }
    validateWindowsFileName(params.displayName, "Windows display name");
    validateWindowsProgIdBase(params.identifier, "Windows package identifier");
    validateSingleLineField(params.publisher, "Windows package publisher");
    if (params.manifest.version.empty())
        throw std::runtime_error("toolchain package version is required");
    validateSingleLineField(params.manifest.version, "Windows package version");
    WindowsPackageLayout layout;
    layout.displayName = params.displayName;
    layout.installDirName = "Viper";
    layout.version = params.manifest.version;
    layout.identifier = params.identifier;
    layout.publisher = params.publisher;
    layout.executableName = "viper.exe";
    layout.createDesktopShortcut = false;
    layout.createStartMenuShortcut = false;
    layout.addToPath = true;
    layout.cleanInstallRootBeforeInstall = true;
    layout.pathRelativePath = "bin";
    layout.fileAssociationExecutableRelativePath = "bin\\viper.exe";
    for (const auto &assoc : params.manifest.fileAssociations) {
        layout.fileAssociations.push_back(
            {assoc.extension,
             assoc.description,
             assoc.mimeType,
             toolchainProgIdFor(params.identifier, assoc),
             toolchainOpenCommandArgsFor(assoc)});
    }

    std::set<std::string> installDirSet;

    ZipWriter zip;
    zip.setCompressionEnabled(false);
    zip.addDirectory("app/");
    zip.addDirectory("meta/");

    for (const auto &file : params.manifest.files) {
        const std::string relInstall =
            sanitizePackageRelativePath(file.stagedRelativePath, "windows toolchain path");
        validateWindowsRelativePath(relInstall, "windows toolchain path");
        if (file.symlink && !fs::is_regular_file(file.stagedAbsolutePath)) {
            throw std::runtime_error("Windows toolchain installers can only dereference "
                                     "symlinks to regular files: " +
                                     file.stagedRelativePath);
        }
        addParentDirs(layout.installDirectories,
                      installDirSet,
                      WindowsInstallRoot::InstallDir,
                      relInstall);
        const auto data = readFile(file.stagedAbsolutePath.string());
        addOverlayFile(zip,
                       "app/" + relInstall,
                       data.data(),
                       data.size(),
                       file.executable ? 0100755 : 0100644,
                       layout,
                       WindowsInstallRoot::InstallDir,
                       relInstall,
                       true);

        const std::string lowerName = lowerAscii(fs::path(relInstall).filename().generic_string());
        if (lowerName == "license" || lowerName == "readme.md") {
            const std::string overlayName =
                lowerName == "license" ? "meta/license.txt" : "meta/readme.txt";
            zip.addFile(overlayName, data.data(), data.size(), 0100644);
        }
    }

    finalizeUninstallDirs(layout);
    validateWindowsLayoutFitsStub(layout);

    auto uninstStub = buildUninstallerStub(layout, params.archStr);
    PEBuildParams uninstPe;
    uninstPe.arch = uninstStub.peArch;
    uninstPe.textSection = uninstStub.textSection;
    uninstPe.rdataSection = uninstStub.stubData;
    uninstPe.imports = uninstStub.imports;
    uninstPe.manifest = generateUacManifest();
    configureInstallerStack(uninstPe);
    const auto uninstBytes = buildPE(uninstPe);
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
    provisionalPe.overlay = zipPayload;
    configureInstallerStack(provisionalPe);
    const auto provisionalBytes = buildPE(provisionalPe);
    layout.overlayFileOffset = static_cast<uint32_t>(provisionalBytes.size() - zipPayload.size());

    auto instStub = buildInstallerStub(layout, params.archStr);
    PEBuildParams pe;
    pe.arch = instStub.peArch;
    pe.textSection = instStub.textSection;
    pe.rdataSection = instStub.stubData;
    pe.imports = instStub.imports;
    pe.manifest = generateUacManifest();
    pe.overlay = zipPayload;
    configureInstallerStack(pe);
    const auto peBytes = buildPE(pe);
    writePEToFile(peBytes, params.outputPath);
}

} // namespace viper::pkg
