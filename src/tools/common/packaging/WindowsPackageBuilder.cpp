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
#include "LnkWriter.hpp"
#include "PEBuilder.hpp"
#include "ZipWriter.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace viper::pkg {

namespace {

/// @brief Read a file into a byte vector.
std::vector<uint8_t> readFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("cannot read file: " + path);
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char *>(data.data()), size);
    if (!f)
        throw std::runtime_error("failed to read file: " + path);
    return data;
}

/// @brief Normalize a project name to a lowercase executable name.
std::string exeName(const std::string &name)
{
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        if (c == ' ')
            result.push_back('_');
        else
            result.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

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
                                const PackageConfig &pkg)
{
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

void buildWindowsPackage(const WindowsBuildParams &params)
{
    const auto &pkg = params.pkgConfig;
    std::string displayName = pkg.displayName.empty()
                                  ? params.projectName
                                  : pkg.displayName;
    std::string exec = exeName(params.projectName);

    // ─── Build ZIP payload ─────────────────────────────────────────────

    ZipWriter zip;

    // Create directory structure in ZIP
    zip.addDirectory("app/");
    zip.addDirectory("meta/");

    // Application executable
    auto execData = readFile(params.executablePath);
    zip.addFile("app/" + exec + ".exe", execData.data(), execData.size(),
                0100755);

    // Assets
    for (const auto &asset : pkg.assets) {
        fs::path srcPath = fs::path(params.projectRoot) / asset.sourcePath;
        std::string targetDir = asset.targetPath;
        if (targetDir == ".")
            targetDir = "";

        std::string prefix = "app/";
        if (!targetDir.empty())
            prefix += targetDir + "/";

        if (fs::is_directory(srcPath)) {
            for (auto &entry : fs::recursive_directory_iterator(srcPath)) {
                auto relPath = fs::relative(entry.path(), srcPath).string();
                if (entry.is_directory()) {
                    zip.addDirectory(prefix + relPath);
                } else if (entry.is_regular_file()) {
                    auto data = readFile(entry.path().string());
                    zip.addFile(prefix + relPath, data.data(), data.size());
                }
            }
        } else if (fs::is_regular_file(srcPath)) {
            auto data = readFile(srcPath.string());
            zip.addFile(prefix + srcPath.filename().string(),
                        data.data(), data.size());
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

    // Uninstaller PE — a minimal executable that reads install.ini and
    // reverses the installation (delete files, remove registry entries,
    // clean up shortcuts). The stub code is a placeholder; the actual
    // uninstall logic reads meta/install.ini from the install directory.
    {
        PEBuildParams uninstPe;
        // Minimal stub: sub rsp,40; xor ecx,ecx; ret
        // In a full implementation this would:
        //   1. Read install.ini from own directory
        //   2. Remove installed files listed in [files] section
        //   3. Remove registry entries (HKLM\...\Uninstall\<name>)
        //   4. Delete Start Menu / Desktop shortcuts
        //   5. Remove the install directory
        //   6. Schedule self-deletion via MoveFileEx(MOVEFILE_DELAY_UNTIL_REBOOT)
        uninstPe.textSection = {
            0x48, 0x83, 0xEC, 0x28, // sub rsp, 40
            0x48, 0x31, 0xC9,       // xor rcx, rcx
            0xC3                     // ret
        };
        uninstPe.imports.push_back(
            {"kernel32.dll", {"ExitProcess", "DeleteFileW", "RemoveDirectoryW",
                              "GetModuleFileNameW"}});
        // asInvoker — uninstaller launched from Add/Remove Programs which
        // already has appropriate privileges if installed per-machine.
        uninstPe.manifest = generateUacManifest();
        uninstPe.iconData = icoData; // Same icon as installer
        auto uninstBytes = buildPE(uninstPe);
        zip.addFile("app/uninstall.exe", uninstBytes.data(), uninstBytes.size(),
                    0100755);
    }

    auto zipPayload = zip.finishToVector();

    // ─── Build PE ──────────────────────────────────────────────────────

    PEBuildParams pe;

    // Minimal .text section: just a RET instruction (0xC3)
    // In a full implementation, this would be the installer stub that:
    //   1. Reads ZIP from own overlay (after PE sections)
    //   2. Extracts files to Program Files
    //   3. Creates registry entries
    //   4. Creates shortcuts
    // For now, it's a placeholder — the ZIP payload can be extracted
    // with any ZIP tool or a companion installer script.
    pe.textSection = {
        0x48, 0x83, 0xEC, 0x28,   // sub rsp, 40   (align stack + shadow space)
        0x48, 0x31, 0xC9,         // xor rcx, rcx   (uType = MB_OK)
        0xC3                       // ret
    };

    // Import kernel32.dll (minimal)
    pe.imports.push_back({"kernel32.dll", {"ExitProcess", "GetModuleFileNameW"}});

    // UAC manifest for admin elevation
    pe.manifest = generateUacManifest();

    // Embed ICO as RT_ICON + RT_GROUP_ICON resource (Explorer icon)
    pe.iconData = icoData;

    // ZIP payload as overlay
    pe.overlay = std::move(zipPayload);

    // Build and write
    auto peBytes = buildPE(pe);
    writePEToFile(peBytes, params.outputPath);
}

} // namespace viper::pkg
