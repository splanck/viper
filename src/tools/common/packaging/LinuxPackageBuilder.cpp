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
#include "common/RunProcess.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace viper::pkg {
namespace {

/// @brief Track a data file for md5sums generation.
struct DataFile {
    std::string installPath; ///< e.g. "usr/bin/hello"
    std::vector<uint8_t> data;
    bool symlink{false};
    bool directory{false};
    std::string symlinkTarget;

    DataFile(std::string path, std::vector<uint8_t> bytes)
        : installPath(std::move(path)), data(std::move(bytes)) {}

    static DataFile link(std::string path, std::string target) {
        DataFile file(std::move(path), {});
        file.symlink = true;
        file.symlinkTarget = std::move(target);
        return file;
    }

    static DataFile dir(std::string path) {
        DataFile file(std::move(path), {});
        file.directory = true;
        return file;
    }
};

std::string debArchFor(const std::string &arch) {
    validateToolchainArchitecture(arch);
    return arch == "arm64" ? "arm64" : "amd64";
}

std::vector<DataFile> collectToolchainLinuxFiles(const ToolchainInstallManifest &manifest) {
    std::vector<DataFile> dataFiles;
    dataFiles.reserve(manifest.files.size());
    for (const auto &file : manifest.files) {
        const std::string installPath = mapInstallPath(file, InstallPathPolicy::LinuxUsrRoot);
        const std::string relInstall =
            sanitizePackageRelativePath(installPath.size() > 1 ? installPath.substr(1) : installPath,
                                        "linux install path");
        if (file.symlink)
            dataFiles.push_back(DataFile::link(relInstall, file.symlinkTarget));
        else
            dataFiles.emplace_back(relInstall, readFile(file.stagedAbsolutePath.string()));
    }
    return dataFiles;
}

void addDirectoriesForDataFiles(TarWriter &tar, const std::vector<DataFile> &dataFiles) {
    std::vector<std::string> dirs;
    auto ensureDir = [&](const std::string &dirPath) {
        std::string d = dirPath;
        if (!d.empty() && d.back() != '/')
            d.push_back('/');
        if (std::find(dirs.begin(), dirs.end(), d) == dirs.end())
            dirs.push_back(d);
    };

    for (const auto &df : dataFiles) {
        if (df.directory)
            ensureDir("./" + df.installPath);
        size_t pos = 0;
        while ((pos = df.installPath.find('/', pos)) != std::string::npos) {
            ensureDir("./" + df.installPath.substr(0, pos));
            ++pos;
        }
    }

    tar.addDirectory("./", 0755);
    std::sort(dirs.begin(), dirs.end());
    for (const auto &dir : dirs)
        tar.addDirectory(dir, 0755);
}

std::string rpmArchFor(const std::string &arch) {
    validateToolchainArchitecture(arch);
    return arch == "arm64" ? "aarch64" : "x86_64";
}

std::string debSectionFor(const std::string &category) {
    if (category.empty())
        return "utils";
    const size_t semi = category.find(';');
    const std::string first = semi == std::string::npos ? category : category.substr(0, semi);
    if (first.empty())
        return "utils";
    if (first == "Development" || first == "IDE" || first == "Debugger" ||
        first == "RevisionControl")
        return "devel";
    if (first == "Game")
        return "games";
    if (first == "Graphics" || first == "Photography" || first == "2DGraphics" ||
        first == "3DGraphics")
        return "graphics";
    if (first == "Network" || first == "WebBrowser" || first == "Email")
        return "net";
    if (first == "AudioVideo" || first == "Audio" || first == "Video" || first == "Player")
        return "sound";
    if (first == "Office" || first == "Spreadsheet" || first == "WordProcessor" ||
        first == "Presentation")
        return "editors";
    if (first == "Science" || first == "Education")
        return "science";
    if (first == "System" || first == "Settings" || first == "Utility")
        return "utils";
    return "utils";
}

void validateDebMetadata(const PackageConfig &pkg,
                         const std::string &displayName,
                         const std::string &version,
                         const std::string &arch) {
    validateSingleLineField(displayName, "package display name");
    validateDebVersion(version, "package version");
    validateSingleLineField(arch, "package architecture");
    validateSingleLineField(pkg.author, "package author");
    validateSingleLineField(pkg.description, "package description");
    validatePackageUrl(pkg.homepage, "package homepage");
    validateSingleLineField(pkg.license, "package license");
    validateDesktopCategories(pkg.category);
    for (const auto &dep : pkg.depends)
        validateDebDependency(dep);
    for (const auto &assoc : pkg.fileAssociations)
        validateFileAssociation(assoc.extension, assoc.description, assoc.mimeType);
}

void validatePortableMetadata(const PackageConfig &pkg,
                              const std::string &displayName,
                              const std::string &version) {
    validateSingleLineField(displayName, "package display name");
    validateDebVersion(version, "package version");
    validateSingleLineField(pkg.author, "package author");
    validateSingleLineField(pkg.description, "package description");
    validatePackageUrl(pkg.homepage, "package homepage");
    validateSingleLineField(pkg.license, "package license");
}

std::string debMaintainerFor(const PackageConfig &pkg, const std::string &displayName) {
    std::string maintainer = trimAsciiWhitespace(pkg.author);
    if (maintainer.empty())
        maintainer = displayName.empty() ? std::string("Viper Project") : displayName;
    validateSingleLineField(maintainer, "package maintainer");
    if (maintainer.find('<') != std::string::npos && maintainer.find('>') != std::string::npos)
        return maintainer;
    if (maintainer.find('@') != std::string::npos)
        return maintainer;
    return maintainer + " <noreply@example.invalid>";
}

void validateDataFilePaths(const std::vector<DataFile> &dataFiles) {
    std::set<std::string> seen;
    for (const auto &df : dataFiles) {
        const std::string clean = sanitizePackageRelativePath(df.installPath, "linux install path");
        if (clean != df.installPath)
            throw std::runtime_error("linux install path was not normalized: " + df.installPath);
        if (!seen.insert(clean).second)
            throw std::runtime_error("duplicate linux package path: " + df.installPath);
    }
}

void validateRpmSpecPath(const std::string &path) {
    const std::string clean = sanitizePackageRelativePath(path, "rpm payload path");
    if (clean != path)
        throw std::runtime_error("rpm payload path was not normalized: " + path);
    for (char c : path) {
        if (c == '%')
            throw std::runtime_error("rpm payload path must not contain '%': " + path);
    }
}

std::string portableArchivePlatformName() {
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
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

void buildDebPackage(const LinuxBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    std::string pkgName = normalizeDebName(params.projectName);
    std::string exeName = normalizeExecName(params.projectName);
    std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    const std::string version = params.version.empty() ? "0.0.0" : params.version;
    validateDebMetadata(pkg, displayName, version, params.archStr);
    if (params.archStr != "amd64" && params.archStr != "arm64" && params.archStr != "all")
        throw std::runtime_error("Debian package architecture must be amd64, arm64, or all: " +
                                 params.archStr);
    if (!fs::is_regular_file(params.executablePath))
        throw std::runtime_error("Linux package executable is not a regular file: " +
                                 params.executablePath);

    // Collect all data files (for md5sums and data.tar)
    std::vector<DataFile> dataFiles;

    // The executable
    auto execData = readFile(params.executablePath);
    dataFiles.push_back({"usr/bin/" + exeName, execData});

    // Assets
    for (const auto &asset : pkg.assets) {
        fs::path srcPath = resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
        std::string targetDir = sanitizePackageRelativePath(asset.targetPath, "asset target path");

        std::string sharePrefix = joinPackageRelativePath("usr/share/" + pkgName, targetDir);

        if (!fs::exists(srcPath))
            throw std::runtime_error("asset not found: " + asset.sourcePath);

        if (fs::is_directory(srcPath)) {
            dataFiles.push_back(DataFile::dir(sharePrefix));
            safeDirectoryIterate(
                srcPath, params.projectRoot, [&](const fs::directory_entry &entry) {
                    auto relPath = sanitizePackageRelativePath(
                        fs::relative(entry.path(), srcPath).generic_string(), "asset path");
                    if (entry.is_directory()) {
                        dataFiles.push_back(DataFile::dir(
                            joinPackageRelativePath(sharePrefix, relPath, "asset path")));
                    } else if (entry.is_regular_file()) {
                        auto fileData = readFile(entry.path().string());
                        dataFiles.push_back(
                            {joinPackageRelativePath(sharePrefix, relPath, "asset path"),
                             fileData});
                    }
                });
        } else if (fs::is_regular_file(srcPath)) {
            auto fileData = readFile(srcPath.string());
            dataFiles.push_back({joinPackageRelativePath(sharePrefix,
                                                         srcPath.filename().generic_string(),
                                                         "asset path"),
                                 fileData});
        } else {
            throw std::runtime_error("asset is not a regular file or directory: " +
                                     asset.sourcePath);
        }
    }

    // .desktop file. File associations require a desktop handler even if it is
    // hidden from normal application menus.
    const bool needDesktopEntry =
        pkg.shortcutMenu || pkg.shortcutDesktop || !pkg.fileAssociations.empty();
    if (needDesktopEntry) {
        DesktopEntryParams dep;
        dep.name = displayName;
        dep.comment = pkg.description;
        dep.execPath = "/usr/bin/" + exeName;
        dep.iconName = exeName;
        dep.categories = pkg.category;
        dep.terminal = false;
        dep.workingDir = "/usr/share/" + pkgName;
        dep.fileAssociations = pkg.fileAssociations;
        dep.acceptsFileArgument = !pkg.fileAssociations.empty();
        dep.noDisplay = !pkg.shortcutMenu && !pkg.shortcutDesktop && !pkg.fileAssociations.empty();
        auto desktop = generateDesktopEntry(dep);
        std::vector<uint8_t> ddata(desktop.begin(), desktop.end());
        dataFiles.push_back({"usr/share/applications/" + pkgName + ".desktop", ddata});
        if (pkg.shortcutDesktop)
            dataFiles.push_back({"usr/share/" + pkgName + "/" + pkgName + ".desktop", ddata});
    }

    // Icon PNGs at standard sizes (via IconGenerator)
    if (!pkg.iconPath.empty()) {
        fs::path iconSrc = resolvePackageSourcePath(params.projectRoot, pkg.iconPath, "package icon");
        if (!fs::is_regular_file(iconSrc))
            throw std::runtime_error("package icon not found: " + pkg.iconPath);
        auto srcImage = pngRead(iconSrc.string());
        auto pngs = generateMultiSizePngs(srcImage);
        for (const auto &[sz, pngData] : pngs) {
            std::string iconPath = "usr/share/icons/hicolor/" + std::to_string(sz) + "x" +
                                   std::to_string(sz) + "/apps/" + exeName + ".png";
            dataFiles.push_back({iconPath, pngData});
        }
    }

    // MIME type XML
    if (!pkg.fileAssociations.empty()) {
        auto mimeXml = generateMimeTypeXml(pkgName, pkg.fileAssociations);
        std::vector<uint8_t> mdata(mimeXml.begin(), mimeXml.end());
        dataFiles.push_back({"usr/share/mime/packages/" + pkgName + ".xml", mdata});
    }
    validateDataFilePaths(dataFiles);

    // ─── Build data.tar ────────────────────────────────────────────────

    TarWriter dataTar;

    // Collect unique directories
    std::vector<std::string> dirs;
    auto ensureDir = [&](const std::string &dirPath) {
        std::string d = dirPath;
        if (!d.empty() && d.back() != '/')
            d.push_back('/');
        for (const auto &existing : dirs) {
            if (existing == d)
                return;
        }
        dirs.push_back(d);
    };

    for (const auto &df : dataFiles) {
        // Ensure all parent directories exist
        std::string path = df.installPath;
        if (df.directory)
            ensureDir("./" + path);
        size_t pos = 0;
        while ((pos = path.find('/', pos)) != std::string::npos) {
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
    for (const auto &df : dataFiles) {
        uint32_t mode = 0644;
        // Executables get 0755
        if (df.directory)
            continue;
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
        ctl << "Version: " << version << "\n";
        ctl << "Section: " << debSectionFor(pkg.category) << "\n";
        ctl << "Priority: optional\n";
        ctl << "Architecture: " << params.archStr << "\n";
        ctl << "Maintainer: " << debMaintainerFor(pkg, displayName) << "\n";

        // Installed-Size in KiB
        size_t totalBytes = 0;
        for (const auto &df : dataFiles)
            totalBytes += df.data.size();
        ctl << "Installed-Size: " << ((totalBytes + 1023) / 1024) << "\n";

        // Dependencies
        if (!pkg.depends.empty()) {
            ctl << "Depends: ";
            for (size_t i = 0; i < pkg.depends.size(); ++i) {
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
        for (const auto &df : dataFiles) {
            if (!df.symlink && !df.directory) {
                auto hex = md5hex(df.data.data(), df.data.size());
                md5s << hex << "  " << df.installPath << "\n";
            }
        }
        controlTar.addFileString("./md5sums", md5s.str(), 0644);
    }

    // postinst script (update MIME database, desktop database, custom hooks)
    bool needPostinst =
        needDesktopEntry || pkg.shortcutDesktop ||
        !pkg.postInstallScript.empty();
    if (needPostinst) {
        std::ostringstream pi;
        pi << "#!/bin/sh\n";
        pi << "set -e\n";
        if (!pkg.fileAssociations.empty())
            pi << "if command -v update-mime-database >/dev/null 2>&1; then "
                  "update-mime-database /usr/share/mime; fi\n";
        if (needDesktopEntry)
            pi << "if command -v update-desktop-database >/dev/null 2>&1; then "
                  "update-desktop-database /usr/share/applications; fi\n";
        if (pkg.shortcutDesktop) {
            pi << "for d in /root/Desktop /home/*/Desktop; do "
                  "[ -d \"$d\" ] || continue; "
                  "(cp /usr/share/applications/" << pkgName << ".desktop \"$d/" << pkgName
               << ".desktop\" && chmod 755 \"$d/" << pkgName << ".desktop\") || true; done\n";
        }
        if (!pkg.postInstallScript.empty())
            pi << normalizePackageHookScript(pkg.postInstallScript, "post-install script") << "\n";
        controlTar.addFileString("./postinst", pi.str(), 0755);
    }

    // prerm script (custom hooks + desktop shortcut cleanup before files go away)
    bool needPrerm = pkg.shortcutDesktop || !pkg.preUninstallScript.empty();
    if (needPrerm) {
        std::ostringstream pr;
        pr << "#!/bin/sh\n";
        pr << "set -e\n";
        if (!pkg.preUninstallScript.empty())
            pr << normalizePackageHookScript(pkg.preUninstallScript, "pre-uninstall script") << "\n";
        if (pkg.shortcutDesktop) {
            pr << "for d in /root/Desktop /home/*/Desktop; do "
                  "[ -e \"$d/" << pkgName << ".desktop\" ] && rm -f \"$d/" << pkgName
               << ".desktop\" || true; done\n";
        }
        controlTar.addFileString("./prerm", pr.str(), 0755);
    }

    // postrm script refreshes caches after package payload files have been removed.
    const bool needPostrm = !pkg.fileAssociations.empty() || needDesktopEntry;
    if (needPostrm) {
        std::ostringstream po;
        po << "#!/bin/sh\n";
        po << "set -e\n";
        if (!pkg.fileAssociations.empty())
            po << "if command -v update-mime-database >/dev/null 2>&1; then "
                  "update-mime-database /usr/share/mime || true; fi\n";
        if (needDesktopEntry)
            po << "if command -v update-desktop-database >/dev/null 2>&1; then "
                  "update-desktop-database /usr/share/applications || true; fi\n";
        controlTar.addFileString("./postrm", po.str(), 0755);
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

void buildTarball(const LinuxBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    std::string pkgName = normalizeDebName(params.projectName);
    std::string exeName = normalizeExecName(params.projectName);
    std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    const std::string version = params.version.empty() ? "0.0.0" : params.version;
    validatePortableMetadata(pkg, displayName, version);
    if (!fs::is_regular_file(params.executablePath))
        throw std::runtime_error("tarball executable is not a regular file: " +
                                 params.executablePath);

    // Top-level directory in the tarball
    std::string topDir =
        sanitizePackageRelativePath(pkgName + "-" + version, "tarball top-level directory") + "/";

    TarWriter tar;
    tar.addDirectory(topDir, 0755);

    // Executable
    auto execData = readFile(params.executablePath);
    tar.addFile(topDir + exeName, execData.data(), execData.size(), 0755);

    // Assets
    for (const auto &asset : pkg.assets) {
        fs::path srcPath = resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
        std::string targetDir = sanitizePackageRelativePath(asset.targetPath, "asset target path");

        std::string prefix = joinPackageRelativePath(topDir, targetDir, "asset target path");

        if (!fs::exists(srcPath))
            throw std::runtime_error("asset not found: " + asset.sourcePath);

        if (fs::is_directory(srcPath)) {
            if (!targetDir.empty())
                tar.addDirectory(prefix, 0755);
            safeDirectoryIterate(
                srcPath, params.projectRoot, [&](const fs::directory_entry &entry) {
                    if (entry.is_directory()) {
                        auto relPath = sanitizePackageRelativePath(
                            fs::relative(entry.path(), srcPath).generic_string(), "asset path");
                        tar.addDirectory(joinPackageRelativePath(prefix, relPath, "asset path"),
                                         0755);
                    } else if (entry.is_regular_file()) {
                        auto relPath = sanitizePackageRelativePath(
                            fs::relative(entry.path(), srcPath).generic_string(), "asset path");
                        auto fileData = readFile(entry.path().string());
                        tar.addFile(joinPackageRelativePath(prefix, relPath, "asset path"),
                                    fileData.data(),
                                    fileData.size(),
                                    0644);
                    }
                });
        } else if (fs::is_regular_file(srcPath)) {
            auto fileData = readFile(srcPath.string());
            tar.addFile(
                joinPackageRelativePath(prefix, srcPath.filename().generic_string(), "asset path"),
                fileData.data(),
                fileData.size(),
                0644);
        } else {
            throw std::runtime_error("asset is not a regular file or directory: " +
                                     asset.sourcePath);
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

void buildToolchainDebPackage(const LinuxToolchainBuildParams &params) {
    const auto &manifest = params.manifest;
    const std::string packageName =
        params.packageName.empty() ? std::string("viper") : normalizeDebName(params.packageName);
    const std::string version = manifest.version.empty() ? std::string("0.0.0") : manifest.version;
    validateDebVersion(version, "toolchain package version");
    validateToolchainArchitecture(manifest.arch, "toolchain package architecture");
    const std::string archStr = debArchFor(manifest.arch);
    const auto dataFiles = collectToolchainLinuxFiles(manifest);
    validateDataFilePaths(dataFiles);

    TarWriter dataTar;
    addDirectoriesForDataFiles(dataTar, dataFiles);
    for (const auto &df : dataFiles) {
        const bool executable = df.installPath.rfind("usr/bin/", 0) == 0;
        if (df.symlink)
            dataTar.addSymlink("./" + df.installPath, df.symlinkTarget);
        else
            dataTar.addFile("./" + df.installPath,
                            df.data.data(),
                            df.data.size(),
                            executable ? 0755 : 0644);
    }
    const auto dataTarBytes = dataTar.finish();
    const auto dataTarGz = gzip(dataTarBytes.data(), dataTarBytes.size());

    TarWriter controlTar;
    controlTar.addDirectory("./", 0755);
    {
        std::ostringstream ctl;
        ctl << "Package: " << packageName << "\n";
        ctl << "Version: " << version << "\n";
        ctl << "Section: devel\n";
        ctl << "Priority: optional\n";
        ctl << "Architecture: " << archStr << "\n";
        ctl << "Maintainer: Viper Project\n";
        ctl << "Installed-Size: " << ((manifest.totalSizeBytes() + 1023) / 1024) << "\n";
        ctl << "Description: Viper compiler toolchain\n";
        controlTar.addFileString("./control", ctl.str(), 0644);
    }
    {
        std::ostringstream md5s;
        for (const auto &df : dataFiles)
            if (!df.symlink)
                md5s << md5hex(df.data.data(), df.data.size()) << "  " << df.installPath << "\n";
        controlTar.addFileString("./md5sums", md5s.str(), 0644);
    }
    if (std::any_of(manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &entry) {
            return entry.kind == ToolchainFileKind::ManPage;
        })) {
        controlTar.addFileString(
            "./postinst",
            "#!/bin/sh\nset -e\nif command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n",
            0755);
        controlTar.addFileString(
            "./prerm",
            "#!/bin/sh\nset -e\nif command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n",
            0755);
    }
    const auto controlTarBytes = controlTar.finish();
    const auto controlTarGz = gzip(controlTarBytes.data(), controlTarBytes.size());

    ArWriter ar;
    ar.addMemberString("debian-binary", "2.0\n");
    ar.addMemberVec("control.tar.gz", controlTarGz);
    ar.addMemberVec("data.tar.gz", dataTarGz);
    ar.finishToFile(params.outputPath);
}

void buildToolchainTarball(const LinuxToolchainBuildParams &params) {
    const auto &manifest = params.manifest;
    const std::string packageName =
        params.packageName.empty() ? std::string("viper") : normalizeDebName(params.packageName);
    const std::string version = manifest.version.empty() ? std::string("0.0.0") : manifest.version;
    validateDebVersion(version, "toolchain package version");
    validateToolchainArchitecture(manifest.arch, "toolchain package architecture");
    const std::string topDir =
        sanitizePackageRelativePath(packageName + "-" + version + "-" +
                                        portableArchivePlatformName() + "-" + manifest.arch,
                                    "toolchain tarball top-level directory") +
        "/";

    TarWriter tar;
    tar.addDirectory(topDir, 0755);
    for (const auto &file : manifest.files) {
        const std::string relPath = mapInstallPath(file, InstallPathPolicy::PortableArchive);
        validateRpmSpecPath(relPath);
        if (file.symlink) {
            tar.addSymlink(topDir + relPath, file.symlinkTarget);
        } else {
            const auto data = readFile(file.stagedAbsolutePath.string());
            tar.addFile(topDir + relPath,
                        data.data(),
                        data.size(),
                        file.executable ? 0755 : 0644);
        }
    }

    const auto tarBytes = tar.finish();
    const auto tarGz = gzip(tarBytes.data(), tarBytes.size());
    std::ofstream out(params.outputPath, std::ios::binary);
    if (!out)
        throw std::runtime_error("cannot write toolchain tarball: " + params.outputPath);
    out.write(reinterpret_cast<const char *>(tarGz.data()),
              static_cast<std::streamsize>(tarGz.size()));
}

void buildToolchainRpmPackage(const LinuxToolchainBuildParams &params) {
    const auto &manifest = params.manifest;
    const std::string packageName =
        params.packageName.empty() ? std::string("viper") : normalizeDebName(params.packageName);
    const std::string version = manifest.version.empty() ? std::string("0.0.0") : manifest.version;
    validateRpmVersion(version, "toolchain RPM version");
    validateToolchainArchitecture(manifest.arch, "toolchain package architecture");
    const std::string arch = rpmArchFor(manifest.arch);

    const fs::path tmpRoot = uniqueTempPackagingDir("viper-rpm-" + version + "-" + arch);
    TempDirGuard cleanup(tmpRoot);
    fs::remove_all(tmpRoot);
    fs::create_directories(tmpRoot / "BUILD");
    fs::create_directories(tmpRoot / "BUILDROOT");
    fs::create_directories(tmpRoot / "RPMS");
    fs::create_directories(tmpRoot / "SOURCES");
    fs::create_directories(tmpRoot / "SPECS");
    fs::create_directories(tmpRoot / "SRPMS");

    const std::string sourceTopDir = packageName + "-" + version + "/";
    TarWriter tar;
    tar.addDirectory(sourceTopDir, 0755);
    for (const auto &file : manifest.files) {
        validateRpmSpecPath(file.stagedRelativePath);
        if (file.symlink) {
            tar.addSymlink(sourceTopDir + file.stagedRelativePath, file.symlinkTarget);
        } else {
            const auto data = readFile(file.stagedAbsolutePath.string());
            tar.addFile(sourceTopDir + file.stagedRelativePath,
                        data.data(),
                        data.size(),
                        file.executable ? 0755 : 0644);
        }
    }
    const auto tarBytes = tar.finish();
    const auto tarGz = gzip(tarBytes.data(), tarBytes.size());
    const fs::path sourceTar = tmpRoot / "SOURCES" / (packageName + "-" + version + ".tar.gz");
    {
        std::ofstream out(sourceTar, std::ios::binary);
        if (!out)
            throw std::runtime_error("cannot write rpm source tarball: " + sourceTar.string());
        out.write(reinterpret_cast<const char *>(tarGz.data()),
                  static_cast<std::streamsize>(tarGz.size()));
    }

    std::ostringstream spec;
    spec << "Name: " << packageName << "\n";
    spec << "Version: " << version << "\n";
    spec << "Release: 1%{?dist}\n";
    spec << "Summary: Viper compiler toolchain\n";
    spec << "License: GPL-3.0-only\n";
    spec << "BuildArch: " << arch << "\n";
    spec << "Source0: %{name}-%{version}.tar.gz\n\n";
    spec << "%description\nViper compiler toolchain\n\n";
    spec << "%prep\n%setup -q\n\n";
    spec << "%build\n:\n\n";
    spec << "%install\nrm -rf %{buildroot}\nmkdir -p %{buildroot}/usr\ncp -a * %{buildroot}/usr/\n\n";
    if (std::any_of(manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &entry) {
            return entry.kind == ToolchainFileKind::ManPage;
        })) {
        spec << "%post\nif command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n\n";
        spec << "%preun\nif command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n\n";
    }
    spec << "%files\n";
    for (const auto &file : manifest.files) {
        validateRpmSpecPath(file.stagedRelativePath);
        spec << "/usr/" << file.stagedRelativePath << "\n";
    }

    const fs::path specPath = tmpRoot / "SPECS" / (packageName + ".spec");
    {
        std::ofstream out(specPath);
        if (!out)
            throw std::runtime_error("cannot write rpm spec file: " + specPath.string());
        out << spec.str();
    }

    const RunResult rr = run_process({"rpmbuild",
                                      "--define",
                                      "_topdir " + tmpRoot.string(),
                                      "--define",
                                      "_sourcedir " + (tmpRoot / "SOURCES").string(),
                                      "--define",
                                      "_specdir " + (tmpRoot / "SPECS").string(),
                                      "-bb",
                                      specPath.string()});
    if (rr.exit_code != 0) {
        throw std::runtime_error("rpmbuild failed while generating toolchain rpm:\n" +
                                 rr.out + rr.err);
    }

    const fs::path rpmPath =
        tmpRoot / "RPMS" / arch / (packageName + "-" + version + "-1." + arch + ".rpm");
    if (!fs::exists(rpmPath))
        throw std::runtime_error("rpmbuild did not produce expected rpm: " + rpmPath.string());
    fs::copy_file(rpmPath, params.outputPath, fs::copy_options::overwrite_existing);
}

} // namespace viper::pkg
