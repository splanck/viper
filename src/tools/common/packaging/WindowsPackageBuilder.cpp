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
//   - ZIP overlay is appended after the PE sections.
//   - PE .text section contains a minimal stub (ret instruction).
//   - RT_MANIFEST resource requests admin elevation for installation.
//   - install.ini metadata file describes installation layout for extraction.
//   - .lnk shortcuts use StringData-only format (no ItemIDList).
//
// Ownership/Lifetime:
//   - Single-use builder.
//
// Links: WindowsPackageBuilder.hpp, PEBuilder.hpp, ZipWriter.hpp,
//        LnkWriter.hpp, IconGenerator.hpp
//
//===----------------------------------------------------------------------===//

#include "WindowsPackageBuilder.hpp"
#include "IconGenerator.hpp"
#include "InstallerStub.hpp"
#include "InstallerStubGen.hpp"
#include "LnkWriter.hpp"
#include "PEBuilder.hpp"
#include "PkgUtils.hpp"
#include "ZipWriter.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace viper::pkg {

namespace {

/// @brief Generate the install.ini metadata file.
///
/// Format:
///   [install]
///   name=ViperIDE
///   version=1.2.0
///   executable=viperide.exe
///   install_dir=ViperIDE
///   author=Stephen Smith
///   description=A lightweight code editor
///   homepage=https://viper-lang.org/viperide
///   identifier=org.viper-lang.viperide
///   shortcut_desktop=1
///   shortcut_menu=1
///   [files]
///   app/viperide.exe=.
///   app/themes/dark.json=themes
///   ...
std::string generateInstallIni(const std::string &displayName,
                               const std::string &version,
                               const std::string &executableName,
                               const PackageConfig &pkg) {
    std::ostringstream ini;
    ini << "[install]\n";
    ini << "name=" << displayName << "\n";
    ini << "version=" << version << "\n";
    ini << "executable=" << executableName << ".exe\n";
    ini << "install_dir=" << displayName << "\n";

    if (!pkg.author.empty())
        ini << "author=" << pkg.author << "\n";
    if (!pkg.description.empty())
        ini << "description=" << pkg.description << "\n";
    if (!pkg.homepage.empty())
        ini << "homepage=" << pkg.homepage << "\n";
    if (!pkg.identifier.empty())
        ini << "identifier=" << pkg.identifier << "\n";

    ini << "shortcut_desktop=" << (pkg.shortcutDesktop ? "1" : "0") << "\n";
    ini << "shortcut_menu=" << (pkg.shortcutMenu ? "1" : "0") << "\n";
    ini << "uninstall_exe=uninstall.exe\n";

    // File associations
    if (!pkg.fileAssociations.empty()) {
        ini << "[associations]\n";
        for (const auto &assoc : pkg.fileAssociations) {
            ini << assoc.extension << "=" << assoc.description;
            if (!assoc.mimeType.empty())
                ini << "|" << assoc.mimeType;
            ini << "\n";
        }
    }

    return ini.str();
}

} // namespace

void buildWindowsPackage(const WindowsBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    std::string exec = normalizeExecName(params.projectName);

    // ─── Build ZIP payload ─────────────────────────────────────────────

    ZipWriter zip;

    // Create directory structure in ZIP
    zip.addDirectory("app/");
    zip.addDirectory("meta/");

    // Application executable
    auto execData = readFile(params.executablePath);
    zip.addFile("app/" + exec + ".exe", execData.data(), execData.size(), 0100755);

    // Assets
    for (const auto &asset : pkg.assets) {
        fs::path srcPath = fs::path(params.projectRoot) / asset.sourcePath;
        std::string targetDir = asset.targetPath;
        if (targetDir == ".")
            targetDir = "";

        std::string prefix = "app/";
        if (!targetDir.empty())
            prefix += targetDir + "/";

        if (!fs::exists(srcPath)) {
            std::cerr << "warning: asset '" << asset.sourcePath << "' not found, skipping\n";
            continue;
        }

        if (fs::is_directory(srcPath)) {
            safeDirectoryIterate(
                srcPath, params.projectRoot, [&](const fs::directory_entry &entry) {
                    auto relPath = fs::relative(entry.path(), srcPath).string();
                    if (entry.is_directory()) {
                        zip.addDirectory(prefix + relPath);
                    } else if (entry.is_regular_file()) {
                        auto data = readFile(entry.path().string());
                        zip.addFile(prefix + relPath, data.data(), data.size());
                    }
                });
        } else if (fs::is_regular_file(srcPath)) {
            auto data = readFile(srcPath.string());
            zip.addFile(prefix + srcPath.filename().string(), data.data(), data.size());
        }
    }

    // Icon (ICO format) — also embedded as PE resource for Explorer
    std::vector<uint8_t> icoData;
    if (!pkg.iconPath.empty()) {
        fs::path iconSrc = fs::path(params.projectRoot) / pkg.iconPath;
        if (fs::exists(iconSrc)) {
            auto srcImage = pngRead(iconSrc.string());
            icoData = generateIco(srcImage);
            zip.addFile("meta/icon.ico", icoData.data(), icoData.size());
        } else {
            std::cerr << "warning: package-icon '" << pkg.iconPath
                      << "' not found, skipping icon generation\n";
        }
    }

    // Start Menu shortcut (.lnk)
    if (pkg.shortcutMenu) {
        LnkParams lnk;
        // Target path will be set by the installer — store template path
        lnk.targetPath = "C:\\Program Files\\" + displayName + "\\" + exec + ".exe";
        lnk.workingDir = "C:\\Program Files\\" + displayName;
        lnk.description = displayName;
        if (!pkg.iconPath.empty())
            lnk.iconPath = lnk.targetPath; // Use exe as icon source

        auto lnkData = generateLnk(lnk);
        zip.addFile("meta/shortcut.lnk", lnkData.data(), lnkData.size());
    }

    // Desktop shortcut (.lnk)
    if (pkg.shortcutDesktop) {
        LnkParams lnk;
        lnk.targetPath = "C:\\Program Files\\" + displayName + "\\" + exec + ".exe";
        lnk.workingDir = "C:\\Program Files\\" + displayName;
        lnk.description = displayName;

        auto lnkData = generateLnk(lnk);
        zip.addFile("meta/desktop.lnk", lnkData.data(), lnkData.size());
    }

    // Installation metadata
    auto installIni = generateInstallIni(displayName, params.version, exec, pkg);
    zip.addFileString("meta/install.ini", installIni);

    // ─── Build uninstaller PE using generated stub ──────────────────────

    {
        auto uninstStub = buildUninstallerStub(displayName, params.archStr);

        // For x64 stubs: finalize with proper RVAs using the IAT fixup system.
        // The uninstaller is a simple PE so we let PEBuilder handle everything.
        PEBuildParams uninstPe;
        uninstPe.arch = params.archStr;
        uninstPe.textSection = uninstStub.textSection;
        uninstPe.imports = uninstStub.imports;
        uninstPe.manifest = generateAsInvokerManifest();
        uninstPe.iconData = icoData;
        auto uninstBytes = buildPE(uninstPe);
        zip.addFile("app/uninstall.exe", uninstBytes.data(), uninstBytes.size(), 0100755);
    }

    auto zipPayload = zip.finishToVector();

    // ─── Build installer PE using generated stub ─────────────────────

    auto instStub = buildInstallerStub(displayName, displayName, params.archStr);

    PEBuildParams pe;
    pe.arch = params.archStr;
    pe.textSection = instStub.textSection;
    pe.imports = instStub.imports;
    pe.manifest = generateUacManifest();
    pe.iconData = icoData;
    pe.overlay = std::move(zipPayload);

    auto peBytes = buildPE(pe);
    writePEToFile(peBytes, params.outputPath);
}

} // namespace viper::pkg
