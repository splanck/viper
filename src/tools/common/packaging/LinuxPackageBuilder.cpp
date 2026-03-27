//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/LinuxPackageBuilder.cpp
// Purpose: Assemble Linux .deb packages and .tar.gz archives from scratch.
//
// Key invariants:
//   - .deb member ordering: debian-binary, control.tar.gz, data.tar.gz.
//   - control file fields follow Debian Policy Manual format.
//   - md5sums: one line per data file, hex-digest + two-space + path.
//   - Architecture mapping: "x64" -> "amd64", "arm64" -> "arm64".
//   - FHS paths: /usr/bin/<exec>, /usr/share/<name>/<assets>,
//     /usr/share/applications/<name>.desktop,
//     /usr/share/icons/hicolor/<NxN>/apps/<name>.png.
//
// Ownership/Lifetime:
//   - Single-use builder.
//
// Links: LinuxPackageBuilder.hpp, ArWriter.hpp, TarWriter.hpp,
//        PkgGzip.hpp, PkgMD5.hpp, PkgPNG.hpp, DesktopEntryGenerator.hpp
//
//===----------------------------------------------------------------------===//

#include "LinuxPackageBuilder.hpp"
#include "ArWriter.hpp"
#include "DesktopEntryGenerator.hpp"
#include "IconGenerator.hpp"
#include "PkgGzip.hpp"
#include "PkgMD5.hpp"
#include "PkgUtils.hpp"
#include "TarWriter.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace viper::pkg
{
namespace
{

/// @brief Track a data file for md5sums generation.
struct DataFile
{
    std::string installPath; ///< e.g. "usr/bin/hello"
    std::vector<uint8_t> data;
};

} // namespace

void buildDebPackage(const LinuxBuildParams &params)
{
    const auto &pkg = params.pkgConfig;
    std::string pkgName = normalizeDebName(params.projectName);
    std::string exeName = normalizeExecName(params.projectName);
    std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;

    // Collect all data files (for md5sums and data.tar)
    std::vector<DataFile> dataFiles;

    // The executable
    auto execData = readFile(params.executablePath);
    dataFiles.push_back({"usr/bin/" + exeName, execData});

    // Assets
    for (const auto &asset : pkg.assets)
    {
        fs::path srcPath = fs::path(params.projectRoot) / asset.sourcePath;
        std::string targetDir = asset.targetPath;
        if (targetDir == ".")
            targetDir = "";

        std::string sharePrefix = "usr/share/" + pkgName + "/";
        if (!targetDir.empty())
            sharePrefix += targetDir + "/";

        if (!fs::exists(srcPath))
        {
            std::cerr << "warning: asset '" << asset.sourcePath << "' not found, skipping\n";
            continue;
        }

        if (fs::is_directory(srcPath))
        {
            safeDirectoryIterate(srcPath,
                                 params.projectRoot,
                                 [&](const fs::directory_entry &entry)
                                 {
                                     if (entry.is_regular_file())
                                     {
                                         auto relPath =
                                             fs::relative(entry.path(), srcPath).string();
                                         auto fileData = readFile(entry.path().string());
                                         dataFiles.push_back({sharePrefix + relPath, fileData});
                                     }
                                 });
        }
        else if (fs::is_regular_file(srcPath))
        {
            auto fileData = readFile(srcPath.string());
            dataFiles.push_back({sharePrefix + srcPath.filename().string(), fileData});
        }
    }

    // .desktop file
    if (pkg.shortcutMenu || pkg.shortcutDesktop)
    {
        DesktopEntryParams dep;
        dep.name = displayName;
        dep.comment = pkg.description;
        dep.execPath = "/usr/bin/" + exeName;
        dep.iconName = exeName;
        dep.categories = pkg.category;
        dep.terminal = false;
        dep.fileAssociations = pkg.fileAssociations;
        auto desktop = generateDesktopEntry(dep);
        std::vector<uint8_t> ddata(desktop.begin(), desktop.end());
        dataFiles.push_back({"usr/share/applications/" + pkgName + ".desktop", ddata});
    }

    // Icon PNGs at standard sizes (via IconGenerator)
    if (!pkg.iconPath.empty())
    {
        fs::path iconSrc = fs::path(params.projectRoot) / pkg.iconPath;
        if (fs::exists(iconSrc))
        {
            auto srcImage = pngRead(iconSrc.string());
            auto pngs = generateMultiSizePngs(srcImage);
            for (const auto &[sz, pngData] : pngs)
            {
                std::string iconPath = "usr/share/icons/hicolor/" + std::to_string(sz) + "x" +
                                       std::to_string(sz) + "/apps/" + exeName + ".png";
                dataFiles.push_back({iconPath, pngData});
            }
        }
        else
        {
            std::cerr << "warning: package-icon '" << pkg.iconPath
                      << "' not found, skipping icon generation\n";
        }
    }

    // MIME type XML
    if (!pkg.fileAssociations.empty())
    {
        auto mimeXml = generateMimeTypeXml(pkgName, pkg.fileAssociations);
        std::vector<uint8_t> mdata(mimeXml.begin(), mimeXml.end());
        dataFiles.push_back({"usr/share/mime/packages/" + pkgName + ".xml", mdata});
    }

    // ─── Build data.tar ────────────────────────────────────────────────

    TarWriter dataTar;

    // Collect unique directories
    std::vector<std::string> dirs;
    auto ensureDir = [&](const std::string &dirPath)
    {
        std::string d = dirPath;
        if (!d.empty() && d.back() != '/')
            d.push_back('/');
        for (const auto &existing : dirs)
        {
            if (existing == d)
                return;
        }
        dirs.push_back(d);
    };

    for (const auto &df : dataFiles)
    {
        // Ensure all parent directories exist
        std::string path = df.installPath;
        size_t pos = 0;
        while ((pos = path.find('/', pos)) != std::string::npos)
        {
            ensureDir("./" + path.substr(0, pos));
            pos++;
        }
    }

    // Add root directory
    dataTar.addDirectory("./", 0755);

    // Add directories in sorted order
    std::sort(dirs.begin(), dirs.end());
    for (const auto &d : dirs)
        dataTar.addDirectory(d, 0755);

    // Add files
    for (const auto &df : dataFiles)
    {
        uint32_t mode = 0644;
        // Executables get 0755
        if (df.installPath.find("usr/bin/") == 0)
            mode = 0755;
        dataTar.addFile("./" + df.installPath, df.data.data(), df.data.size(), mode);
    }

    auto dataTarBytes = dataTar.finish();
    auto dataTarGz = gzip(dataTarBytes.data(), dataTarBytes.size());

    // ─── Build control.tar ─────────────────────────────────────────────

    TarWriter controlTar;
    controlTar.addDirectory("./", 0755);

    // control file
    {
        std::ostringstream ctl;
        ctl << "Package: " << pkgName << "\n";
        ctl << "Version: " << params.version << "\n";
        if (!pkg.category.empty())
            ctl << "Section: " << pkg.category << "\n";
        else
            ctl << "Section: utils\n";
        ctl << "Priority: optional\n";
        ctl << "Architecture: " << params.archStr << "\n";
        if (!pkg.author.empty())
            ctl << "Maintainer: " << pkg.author << "\n";
        else
            ctl << "Maintainer: Unknown\n";

        // Installed-Size in KiB
        size_t totalBytes = 0;
        for (const auto &df : dataFiles)
            totalBytes += df.data.size();
        ctl << "Installed-Size: " << ((totalBytes + 1023) / 1024) << "\n";

        // Dependencies
        if (!pkg.depends.empty())
        {
            ctl << "Depends: ";
            for (size_t i = 0; i < pkg.depends.size(); ++i)
            {
                if (i > 0)
                    ctl << ", ";
                ctl << pkg.depends[i];
            }
            ctl << "\n";
        }

        ctl << "Description: ";
        if (!pkg.description.empty())
            ctl << pkg.description;
        else
            ctl << displayName;
        ctl << "\n";

        if (!pkg.homepage.empty())
            ctl << "Homepage: " << pkg.homepage << "\n";

        auto ctlStr = ctl.str();
        controlTar.addFileString("./control", ctlStr, 0644);
    }

    // md5sums file
    {
        std::ostringstream md5s;
        for (const auto &df : dataFiles)
        {
            auto hex = md5hex(df.data.data(), df.data.size());
            md5s << hex << "  " << df.installPath << "\n";
        }
        controlTar.addFileString("./md5sums", md5s.str(), 0644);
    }

    // postinst script (update MIME database, desktop database, custom hooks)
    bool needPostinst =
        !pkg.fileAssociations.empty() || pkg.shortcutMenu || !pkg.postInstallScript.empty();
    if (needPostinst)
    {
        std::ostringstream pi;
        pi << "#!/bin/sh\n";
        pi << "set -e\n";
        if (!pkg.fileAssociations.empty())
            pi << "if command -v update-mime-database >/dev/null 2>&1; then "
                  "update-mime-database /usr/share/mime; fi\n";
        if (pkg.shortcutMenu)
            pi << "if command -v update-desktop-database >/dev/null 2>&1; then "
                  "update-desktop-database /usr/share/applications; fi\n";
        if (!pkg.postInstallScript.empty())
            pi << pkg.postInstallScript << "\n";
        controlTar.addFileString("./postinst", pi.str(), 0755);
    }

    // prerm script (cleanup + custom hooks)
    bool needPrerm =
        !pkg.fileAssociations.empty() || pkg.shortcutMenu || !pkg.preUninstallScript.empty();
    if (needPrerm)
    {
        std::ostringstream pr;
        pr << "#!/bin/sh\n";
        pr << "set -e\n";
        if (!pkg.preUninstallScript.empty())
            pr << pkg.preUninstallScript << "\n";
        if (!pkg.fileAssociations.empty())
            pr << "if command -v update-mime-database >/dev/null 2>&1; then "
                  "update-mime-database /usr/share/mime; fi\n";
        if (pkg.shortcutMenu)
            pr << "if command -v update-desktop-database >/dev/null 2>&1; then "
                  "update-desktop-database /usr/share/applications; fi\n";
        controlTar.addFileString("./prerm", pr.str(), 0755);
    }

    auto controlTarBytes = controlTar.finish();
    auto controlTarGz = gzip(controlTarBytes.data(), controlTarBytes.size());

    // ─── Assemble .deb (ar archive) ────────────────────────────────────

    ArWriter ar;

    // debian-binary: "2.0\n"
    ar.addMemberString("debian-binary", "2.0\n");

    // control.tar.gz
    ar.addMemberVec("control.tar.gz", controlTarGz);

    // data.tar.gz
    ar.addMemberVec("data.tar.gz", dataTarGz);

    ar.finishToFile(params.outputPath);
}

void buildTarball(const LinuxBuildParams &params)
{
    const auto &pkg = params.pkgConfig;
    std::string pkgName = normalizeDebName(params.projectName);
    std::string exeName = normalizeExecName(params.projectName);
    std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;

    // Top-level directory in the tarball
    std::string topDir = pkgName + "-" + params.version + "/";

    TarWriter tar;
    tar.addDirectory(topDir, 0755);

    // Executable
    auto execData = readFile(params.executablePath);
    tar.addFile(topDir + exeName, execData.data(), execData.size(), 0755);

    // Assets
    for (const auto &asset : pkg.assets)
    {
        fs::path srcPath = fs::path(params.projectRoot) / asset.sourcePath;
        std::string targetDir = asset.targetPath;
        if (targetDir == ".")
            targetDir = "";

        std::string prefix = topDir;
        if (!targetDir.empty())
            prefix += targetDir + "/";

        if (!fs::exists(srcPath))
        {
            std::cerr << "warning: asset '" << asset.sourcePath << "' not found, skipping\n";
            continue;
        }

        if (fs::is_directory(srcPath))
        {
            safeDirectoryIterate(
                srcPath,
                params.projectRoot,
                [&](const fs::directory_entry &entry)
                {
                    if (entry.is_directory())
                    {
                        auto relPath = fs::relative(entry.path(), srcPath).string();
                        tar.addDirectory(prefix + relPath, 0755);
                    }
                    else if (entry.is_regular_file())
                    {
                        auto relPath = fs::relative(entry.path(), srcPath).string();
                        auto fileData = readFile(entry.path().string());
                        tar.addFile(prefix + relPath, fileData.data(), fileData.size(), 0644);
                    }
                });
        }
        else if (fs::is_regular_file(srcPath))
        {
            auto fileData = readFile(srcPath.string());
            tar.addFile(
                prefix + srcPath.filename().string(), fileData.data(), fileData.size(), 0644);
        }
    }

    // README / LICENSE
    auto tarBytes = tar.finish();
    auto tarGz = gzip(tarBytes.data(), tarBytes.size());

    std::ofstream f(params.outputPath, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot write tarball: " + params.outputPath);
    f.write(reinterpret_cast<const char *>(tarGz.data()),
            static_cast<std::streamsize>(tarGz.size()));
}

} // namespace viper::pkg
