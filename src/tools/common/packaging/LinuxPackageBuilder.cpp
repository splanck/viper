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
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>
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
    uint32_t mode{0644};
    bool symlink{false};
    bool directory{false};
    std::string symlinkTarget;

    DataFile(std::string path, std::vector<uint8_t> bytes)
        : installPath(std::move(path)), data(std::move(bytes)) {}

    DataFile(std::string path, std::vector<uint8_t> bytes, uint32_t modeBits)
        : installPath(std::move(path)), data(std::move(bytes)), mode(modeBits) {}

    /// @brief Create a symbolic link entry pointing to `target`.
    static DataFile link(std::string path, std::string target) {
        DataFile file(std::move(path), {});
        file.symlink = true;
        file.symlinkTarget = std::move(target);
        file.mode = 0777;
        return file;
    }

    /// @brief Create a directory entry (no data; mode 0755).
    static DataFile dir(std::string path) {
        DataFile file(std::move(path), {});
        file.directory = true;
        file.mode = 0755;
        return file;
    }
};

/// @brief Map a Viper arch string ("x64", "arm64") to the Debian architecture field value.
std::string debArchFor(const std::string &arch) {
    validateToolchainArchitecture(arch);
    return arch == "arm64" ? "arm64" : "amd64";
}

/// @brief Return `text` with all ASCII letters converted to lowercase.
std::string lowerAscii(std::string text) {
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// @brief Return Unix permission bits for a toolchain file.
/// Uses the stored `unixMode` if non-zero; otherwise defaults to 0755 for executables and 0644 for data files.
uint32_t permissionBitsFor(const ToolchainFileEntry &file) {
    const uint32_t bits = file.unixMode & 07777u;
    if (bits != 0)
        return bits;
    return file.executable ? 0755u : 0644u;
}

uint32_t permissionBitsForFilesystemPath(const fs::path &path) {
    std::error_code ec;
    const fs::perms perms = fs::status(path, ec).permissions();
    if (ec)
        return 0644u;
    const bool executable =
        (perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) !=
        fs::perms::none;
    return executable ? 0755u : 0644u;
}

std::string portableArchiveVersionComponent(const std::string &version) {
    validateDebVersion(version, "package version");
    std::string out;
    out.reserve(version.size());
    for (char c : version) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '.' || c == '+' || c == '~' || c == '-')
            out.push_back(c);
        else
            out.push_back('_');
    }
    if (out.empty() || out == "." || out == "..")
        throw std::runtime_error("package version does not form a portable archive path");
    return out;
}

/// @brief Append a hidden terminal-type .desktop file to `dataFiles` for the given file associations.
/// Writes a `noDisplay=true` desktop entry under `usr/share/applications/<desktopName>`.
/// No-op when `associations` is empty.
void addToolchainDesktopMetadata(std::vector<DataFile> &dataFiles,
                                 const std::string &desktopName,
                                 const std::string &execPath,
                                 const std::vector<FileAssoc> &associations) {
    if (associations.empty())
        return;

    DesktopEntryParams desktop;
    desktop.name = "Viper Toolchain";
    desktop.comment = "Viper source and IL tools";
    desktop.execPath = execPath;
    desktop.iconName = "viper";
    desktop.categories = "Development;";
    desktop.terminal = true;
    desktop.fileAssociations = associations;
    desktop.acceptsFileArgument = true;
    desktop.noDisplay = true;

    const std::string desktopText = generateDesktopEntry(desktop);
    dataFiles.emplace_back("usr/share/applications/" + desktopName,
                           std::vector<uint8_t>(desktopText.begin(), desktopText.end()),
                           0644);
}

/// @brief Append MIME XML and separate .desktop files (one for .il, one for source types) to `dataFiles`.
/// Splits `manifest.fileAssociations` into IL vs. source groups so each gets its own desktop handler.
/// No-op when `manifest.fileAssociations` is empty.
void addToolchainFileAssociationMetadata(std::vector<DataFile> &dataFiles,
                                         const ToolchainInstallManifest &manifest,
                                         const std::string &packageName,
                                         const std::string &viperExecPath) {
    if (manifest.fileAssociations.empty())
        return;

    std::vector<FileAssoc> sourceAssociations;
    std::vector<FileAssoc> ilAssociations;
    for (const auto &assoc : manifest.fileAssociations) {
        if (lowerAscii(assoc.extension) == ".il")
            ilAssociations.push_back(assoc);
        else
            sourceAssociations.push_back(assoc);
    }

    addToolchainDesktopMetadata(dataFiles,
                                packageName + "-source.desktop",
                                viperExecPath + " run",
                                sourceAssociations);
    addToolchainDesktopMetadata(
        dataFiles, packageName + "-il.desktop", viperExecPath + " -run", ilAssociations);

    const std::string mimeXml = generateMimeTypeXml(packageName, manifest.fileAssociations);
    dataFiles.emplace_back("usr/share/mime/packages/" + packageName + ".xml",
                           std::vector<uint8_t>(mimeXml.begin(), mimeXml.end()),
                           0644);
}

/// @brief Collect all Linux install files from the manifest, mapping each to its FHS path under /usr
/// via `LinuxUsrRoot` policy, then appending generated file-association metadata entries.
std::vector<DataFile> collectToolchainLinuxFiles(const ToolchainInstallManifest &manifest,
                                                 const std::string &packageName) {
    std::vector<DataFile> dataFiles;
    dataFiles.reserve(manifest.files.size() + 2);
    for (const auto &file : manifest.files) {
        const std::string installPath = mapInstallPath(file, InstallPathPolicy::LinuxUsrRoot);
        const std::string relInstall =
            sanitizePackageRelativePath(installPath.size() > 1 ? installPath.substr(1) : installPath,
                                        "linux install path");
        if (file.symlink)
            dataFiles.push_back(DataFile::link(relInstall, file.symlinkTarget));
        else
            dataFiles.emplace_back(relInstall,
                                   readFile(file.stagedAbsolutePath.string()),
                                   permissionBitsFor(file));
    }
    addToolchainFileAssociationMetadata(dataFiles, manifest, packageName, "/usr/bin/viper");
    return dataFiles;
}

/// @brief Add all parent directory entries to `tar` for every path in `dataFiles`.
/// Deduplicates entries and sorts them so parent directories always precede their children.
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

/// @brief Map a Viper arch string ("x64", "arm64") to the RPM architecture name ("x86_64"/"aarch64").
std::string rpmArchFor(const std::string &arch) {
    validateToolchainArchitecture(arch);
    return arch == "arm64" ? "aarch64" : "x86_64";
}

/// @brief Validate that `manifest` is a well-formed Linux toolchain manifest.
/// Throws if the manifest platform is not "linux", naming `packageKind` in the error message.
void requireLinuxToolchainManifest(const ToolchainInstallManifest &manifest,
                                   const char *packageKind) {
    validateToolchainInstallManifest(manifest);
    if (manifest.platform != "linux") {
        throw std::runtime_error(std::string(packageKind) +
                                 " requires a Linux staged toolchain manifest, got '" +
                                 manifest.platform + "'");
    }
}

std::string joinCommaSeparated(const std::vector<std::string> &items) {
    std::ostringstream out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0)
            out << ", ";
        out << items[i];
    }
    return out.str();
}

bool manifestHasSupportLibrary(const ToolchainInstallManifest &manifest, std::string_view name) {
    for (const auto &file : manifest.files) {
        if (file.kind != ToolchainFileKind::SupportLibrary &&
            file.kind != ToolchainFileKind::Library)
            continue;
        std::string filename = fs::path(file.stagedRelativePath).filename().string();
        filename = lowerAscii(std::move(filename));
        if (filename.find(name) != std::string::npos)
            return true;
    }
    return false;
}

bool manifestNeedsX11(const ToolchainInstallManifest &manifest) {
    return manifestHasSupportLibrary(manifest, "vipergfx") ||
           manifestHasSupportLibrary(manifest, "vipergui");
}

bool manifestNeedsAlsa(const ToolchainInstallManifest &manifest) {
    return manifestHasSupportLibrary(manifest, "viperaud");
}

/// @brief Return the Debian Depends line for a toolchain .deb.
std::string toolchainDebDepends(const ToolchainInstallManifest &manifest) {
    std::vector<std::string> deps = {
        "libc6",
        "libstdc++6 | libc++1",
        "libgcc-s1",
    };
    if (manifestNeedsX11(manifest))
        deps.push_back("libx11-6");
    if (manifestNeedsAlsa(manifest))
        deps.push_back("libasound2 | libasound2t64");
    return joinCommaSeparated(deps);
}

std::string toolchainDebRecommends() {
    return "cmake, g++ | clang++";
}

std::vector<std::string> toolchainRpmRequires(const ToolchainInstallManifest &manifest) {
    std::vector<std::string> deps = {
        "glibc",
        "libstdc++",
        "libgcc",
    };
    if (manifestNeedsX11(manifest))
        deps.push_back("libX11");
    if (manifestNeedsAlsa(manifest))
        deps.push_back("alsa-lib");
    return deps;
}

std::vector<std::string> sortedUniquePaths(std::vector<std::string> paths) {
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

std::string renderInstallManifest(std::vector<std::string> paths) {
    paths = sortedUniquePaths(std::move(paths));
    std::ostringstream out;
    out << "# Viper toolchain installed files, relative to PREFIX.\n";
    for (const auto &path : paths)
        out << path << "\n";
    return out.str();
}

std::string linuxTarballInstallScript() {
    return R"VIPER_SCRIPT(#!/bin/sh
set -eu

prefix=${PREFIX:-/usr/local}
destdir=${DESTDIR:-}

case "$prefix" in
    /*) ;;
    *) echo "PREFIX must be an absolute path" >&2; exit 2 ;;
esac

root=$(CDPATH= cd "$(dirname "$0")" && pwd)
install_root=${destdir%/}$prefix

set --
for dir in bin include lib share; do
    if [ -e "$root/$dir" ]; then
        set -- "$@" "$dir"
    fi
done

if [ "$#" -eq 0 ]; then
    echo "No installable Viper payload directories were found" >&2
    exit 1
fi

mkdir -p "$install_root"
(cd "$root" && tar cf - "$@") | (cd "$install_root" && tar xpf -)

if [ -z "$destdir" ]; then
    if command -v mandb >/dev/null 2>&1; then
        mandb >/dev/null 2>&1 || true
    fi
    if [ -d "$install_root/share/mime" ] && command -v update-mime-database >/dev/null 2>&1; then
        update-mime-database "$install_root/share/mime" || true
    fi
    if [ -d "$install_root/share/applications" ] && command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$install_root/share/applications" || true
    fi
fi

echo "Installed Viper toolchain under $install_root"
)VIPER_SCRIPT";
}

std::string linuxTarballUninstallScript() {
    return R"VIPER_SCRIPT(#!/bin/sh
set -eu

prefix=${PREFIX:-/usr/local}
destdir=${DESTDIR:-}

case "$prefix" in
    /*) ;;
    *) echo "PREFIX must be an absolute path" >&2; exit 2 ;;
esac

install_root=${destdir%/}$prefix
manifest="$install_root/share/viper/install_manifest.txt"

if [ ! -f "$manifest" ]; then
    echo "Viper install manifest not found: $manifest" >&2
    exit 1
fi

while IFS= read -r rel || [ -n "$rel" ]; do
    case "$rel" in
        ""|\#*) continue ;;
        /*|..|../*|*/../*|*/..) echo "Unsafe manifest path: $rel" >&2; exit 2 ;;
    esac
    rm -f "$install_root/$rel"
done < "$manifest"

rm -f "$manifest"

for dir in \
    share/viper \
    share/applications \
    share/mime/packages \
    share/mime \
    share/doc/viper \
    lib/cmake/Viper \
    lib/cmake \
    bin lib include share/doc share; do
    rmdir "$install_root/$dir" 2>/dev/null || true
done

if [ -z "$destdir" ]; then
    if command -v mandb >/dev/null 2>&1; then
        mandb >/dev/null 2>&1 || true
    fi
    if [ -d "$install_root/share/mime" ] && command -v update-mime-database >/dev/null 2>&1; then
        update-mime-database "$install_root/share/mime" || true
    fi
    if [ -d "$install_root/share/applications" ] && command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$install_root/share/applications" || true
    fi
fi

echo "Removed Viper toolchain files listed in $manifest"
)VIPER_SCRIPT";
}

std::string linuxTarballReadme() {
    return R"VIPER_TEXT(Viper Toolchain Tarball

Install:
  sudo ./install.sh

Install under a custom prefix:
  PREFIX=/opt/viper sudo ./install.sh

Stage into a package root without refreshing system caches:
  DESTDIR=/tmp/viper-root PREFIX=/usr ./install.sh

Uninstall:
  sudo ./uninstall.sh

The uninstaller removes only files listed in share/viper/install_manifest.txt.
)VIPER_TEXT";
}

/// @brief Return true if the `rpmbuild` tool is available on PATH (exit code 0).
bool rpmbuildAvailable() {
    const RunResult rr = run_process({"rpmbuild", "--version"});
    return rr.exit_code == 0;
}

/// @brief Find the .rpm produced by rpmbuild under `tmpRoot/RPMS/<arch>/`.
/// Expects exactly one file matching `<packageName>-<version>-*.<arch>.rpm`; throws if none or more than one.
fs::path findGeneratedRpm(const fs::path &tmpRoot,
                          const std::string &packageName,
                          const std::string &version,
                          const std::string &arch) {
    const fs::path rpmDir = tmpRoot / "RPMS" / arch;
    const std::string prefix = packageName + "-" + version + "-";
    const std::string suffix = "." + arch + ".rpm";
    std::vector<fs::path> matches;
    std::error_code ec;
    if (fs::exists(rpmDir, ec)) {
        for (const auto &entry : fs::directory_iterator(rpmDir, ec)) {
            if (ec)
                break;
            if (!entry.is_regular_file(ec) || ec)
                continue;
            const std::string name = entry.path().filename().string();
            if (name.rfind(prefix, 0) == 0 && name.size() >= suffix.size() &&
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                matches.push_back(entry.path());
            }
        }
    }
    if (matches.empty()) {
        throw std::runtime_error("rpmbuild did not produce an rpm matching " + prefix + "*" +
                                 suffix + " under " + rpmDir.string());
    }
    if (matches.size() > 1) {
        std::sort(matches.begin(), matches.end());
        throw std::runtime_error("rpmbuild produced multiple matching rpm artifacts; first was " +
                                 matches.front().string());
    }
    return matches.front();
}

/// @brief Map a freedesktop.org Category string to the closest Debian section name.
/// Falls back to "utils" for unrecognized or empty categories.
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

/// @brief Validate all metadata fields required for a Debian package.
/// Checks display name, version format, architecture, author, description, homepage URL,
/// license, categories, dependency syntax, and file association entries.
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
    validatePackageFileAssociations(pkg.fileAssociations);
}

/// @brief Validate metadata fields required for a portable tarball.
/// Checks display name, version format, author, description, homepage URL, and license.
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

/// @brief Build the Debian `Maintainer:` field from `pkg.author`.
/// Appends `<noreply@example.invalid>` when the author string does not already contain an email,
/// satisfying the required `Name <email>` format.
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

/// @brief Validate all install paths in `dataFiles` are normalized and unique.
/// Throws on path traversal, duplicate paths, or non-normalized separators.
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

/// @brief Validate that `path` is normalized for portable archive use (no `..`, no absolute prefix).
/// Throws with `fieldName` in the error message if the path is not canonical.
void validatePortableArchivePath(const std::string &path, const char *fieldName) {
    const std::string clean = sanitizePackageRelativePath(path, fieldName);
    if (clean != path)
        throw std::runtime_error(std::string(fieldName) + " was not normalized: " + path);
}

/// @brief Format a relative install path for use in an RPM spec `%files` section.
/// Prepends `/`, escapes `%` characters (RPM macro start), and quotes the result if it
/// contains spaces or tabs.
std::string rpmSpecFilePath(const std::string &path) {
    const std::string clean = sanitizePackageRelativePath(path, "rpm payload path");
    if (clean != path)
        throw std::runtime_error("rpm payload path was not normalized: " + path);

    std::string escaped = "/" + path;
    bool needsQuotes = false;
    std::string out;
    out.reserve(escaped.size() + 8);
    for (char c : escaped) {
        if (c == ' ' || c == '\t')
            needsQuotes = true;
        if (c == '%') {
            out += "%%";
        } else if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
            needsQuotes = true;
        } else {
            out.push_back(c);
        }
    }
    if (!needsQuotes)
        return out;
    return "\"" + out + "\"";
}

/// @brief Validate that `path` can safely appear in an RPM spec `%files` section.
/// Must pass the standard normalize check and must not contain embedded line breaks.
void validateRpmSpecPath(const std::string &path) {
    (void)rpmSpecFilePath(path);
    for (char c : path) {
        if (c == '\n' || c == '\r')
            throw std::runtime_error("rpm payload path must not contain line breaks: " + path);
    }
}

/// @brief Return the platform name used in portable archive filenames for the current host.
std::string portableArchivePlatformName() {
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

/// @brief Generate a unique temp directory path combining `stem`, PID, and steady-clock tick.
/// The PID+tick combination avoids collisions between concurrent packaging invocations.
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

/// @brief RAII guard that removes the given directory tree on destruction.
/// Used to clean up the rpmbuild temp workspace on success or failure.
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

/// @brief Build a Debian .deb package from the given build parameters.
/// Assembles control.tar.gz (control + md5sums + maintainer scripts) and data.tar.gz
/// (binary, assets, .desktop, icons, MIME XML) then wraps them in an ar archive.
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
    dataFiles.emplace_back("usr/bin/" + exeName, std::move(execData), 0755);

    // Assets
    for (const auto &asset : pkg.assets) {
        const std::string sourceRel =
            sanitizePackageRelativePath(asset.sourcePath, "asset source path");
        const std::string sourceLeaf = fs::path(sourceRel).filename().generic_string();
        fs::path srcPath = resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
        std::string targetDir = sanitizePackageRelativePath(asset.targetPath, "asset target path");

        std::string sharePrefix = joinPackageRelativePath("usr/share/" + pkgName, targetDir);

        if (!fs::exists(srcPath))
            throw std::runtime_error("asset not found: " + asset.sourcePath);

        if (fs::is_directory(srcPath)) {
            dataFiles.push_back(DataFile::dir(sharePrefix));
            safeDirectoryIterateResolved(
                srcPath, params.projectRoot, [&](const SafeDirectoryEntry &entry) {
                    auto relPath = sanitizePackageRelativePath(
                        entry.logicalPath.lexically_relative(srcPath).generic_string(), "asset path");
                    if (entry.directory) {
                        dataFiles.push_back(DataFile::dir(
                            joinPackageRelativePath(sharePrefix, relPath, "asset path")));
                    } else if (entry.regularFile) {
                        auto fileData = readFile(entry.resolvedPath.string());
                        dataFiles.emplace_back(
                            joinPackageRelativePath(sharePrefix, relPath, "asset path"),
                            std::move(fileData),
                            permissionBitsForFilesystemPath(entry.resolvedPath));
                    }
                });
        } else if (fs::is_regular_file(srcPath)) {
            auto fileData = readFile(srcPath.string());
            dataFiles.emplace_back(joinPackageRelativePath(sharePrefix,
                                                           sourceLeaf,
                                                           "asset path"),
                                   std::move(fileData),
                                   permissionBitsForFilesystemPath(srcPath));
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
        if (df.directory)
            continue;
        dataTar.addFile("./" + df.installPath, df.data.data(), df.data.size(), df.mode);
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
        uint64_t totalBytes = 0;
        for (const auto &df : dataFiles) {
            if (df.data.size() > std::numeric_limits<uint64_t>::max() - totalBytes)
                throw std::runtime_error("Debian package installed size overflow");
            totalBytes += static_cast<uint64_t>(df.data.size());
        }
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

/// @brief Build a portable .tar.gz archive from the given build parameters.
/// Creates a top-level `<name>-<version>/` directory containing the binary and assets.
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
    std::string topDir = sanitizePackageRelativePath(
                             pkgName + "-" + portableArchiveVersionComponent(version),
                             "tarball top-level directory") +
                         "/";

    TarWriter tar;
    tar.addDirectory(topDir, 0755);

    // Executable
    auto execData = readFile(params.executablePath);
    tar.addFile(topDir + exeName, execData.data(), execData.size(), 0755);

    // Assets
    for (const auto &asset : pkg.assets) {
        const std::string sourceRel =
            sanitizePackageRelativePath(asset.sourcePath, "asset source path");
        const std::string sourceLeaf = fs::path(sourceRel).filename().generic_string();
        fs::path srcPath = resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
        std::string targetDir = sanitizePackageRelativePath(asset.targetPath, "asset target path");

        std::string prefix = joinPackageRelativePath(topDir, targetDir, "asset target path");

        if (!fs::exists(srcPath))
            throw std::runtime_error("asset not found: " + asset.sourcePath);

        if (fs::is_directory(srcPath)) {
            if (!targetDir.empty())
                tar.addDirectory(prefix, 0755);
            safeDirectoryIterateResolved(
                srcPath, params.projectRoot, [&](const SafeDirectoryEntry &entry) {
                    if (entry.directory) {
                        auto relPath = sanitizePackageRelativePath(
                            entry.logicalPath.lexically_relative(srcPath).generic_string(),
                            "asset path");
                        tar.addDirectory(joinPackageRelativePath(prefix, relPath, "asset path"),
                                         0755);
                    } else if (entry.regularFile) {
                        auto relPath = sanitizePackageRelativePath(
                            entry.logicalPath.lexically_relative(srcPath).generic_string(),
                            "asset path");
                        auto fileData = readFile(entry.resolvedPath.string());
                        tar.addFile(joinPackageRelativePath(prefix, relPath, "asset path"),
                                    fileData.data(),
                                    fileData.size(),
                                    permissionBitsForFilesystemPath(entry.resolvedPath));
                    }
                });
        } else if (fs::is_regular_file(srcPath)) {
            auto fileData = readFile(srcPath.string());
            tar.addFile(
                joinPackageRelativePath(prefix, sourceLeaf, "asset path"),
                fileData.data(),
                fileData.size(),
                permissionBitsForFilesystemPath(srcPath));
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
    if (!f)
        throw std::runtime_error("failed to write tarball: " + params.outputPath);
}

/// @brief Build a Debian .deb toolchain package from a staged install manifest.
/// Validates the manifest, collects FHS-mapped files, generates control/md5sums/postinst/postrm,
/// and assembles the ar-format .deb output file.
void buildToolchainDebPackage(const LinuxToolchainBuildParams &params) {
    const auto &manifest = params.manifest;
    requireLinuxToolchainManifest(manifest, "Debian toolchain package");
    const std::string packageName =
        params.packageName.empty() ? std::string("viper") : normalizeDebName(params.packageName);
    if (manifest.version.empty())
        throw std::runtime_error("toolchain package version is required");
    const std::string version = manifest.version;
    validateDebVersion(version, "toolchain package version");
    validateToolchainArchitecture(manifest.arch, "toolchain package architecture");
    const std::string archStr = debArchFor(manifest.arch);
    const auto dataFiles = collectToolchainLinuxFiles(manifest, packageName);
    validateDataFilePaths(dataFiles);

    TarWriter dataTar;
    addDirectoriesForDataFiles(dataTar, dataFiles);
    for (const auto &df : dataFiles) {
        if (df.symlink)
            dataTar.addSymlink("./" + df.installPath, df.symlinkTarget);
        else if (df.directory)
            continue;
        else
            dataTar.addFile("./" + df.installPath,
                            df.data.data(),
                            df.data.size(),
                            df.mode);
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
        ctl << "Maintainer: Viper Project <noreply@example.invalid>\n";
        ctl << "Depends: " << toolchainDebDepends(manifest) << "\n";
        ctl << "Recommends: " << toolchainDebRecommends() << "\n";
        uint64_t totalBytes = 0;
        for (const auto &df : dataFiles) {
            if (df.data.size() > std::numeric_limits<uint64_t>::max() - totalBytes)
                throw std::runtime_error("Debian toolchain package installed size overflow");
            totalBytes += static_cast<uint64_t>(df.data.size());
        }
        ctl << "Installed-Size: " << ((totalBytes + 1023) / 1024) << "\n";
        ctl << "Description: Viper compiler toolchain\n";
        controlTar.addFileString("./control", ctl.str(), 0644);
    }
    {
        std::ostringstream md5s;
        for (const auto &df : dataFiles)
            if (!df.symlink && !df.directory)
                md5s << md5hex(df.data.data(), df.data.size()) << "  " << df.installPath << "\n";
        controlTar.addFileString("./md5sums", md5s.str(), 0644);
    }
    const bool hasManPages =
        std::any_of(manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &entry) {
            return entry.kind == ToolchainFileKind::ManPage;
        });
    const bool hasFileAssociations = !manifest.fileAssociations.empty();
    if (hasManPages || hasFileAssociations) {
        std::ostringstream postinst;
        postinst << "#!/bin/sh\nset -e\n";
        if (hasManPages)
            postinst << "if command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n";
        if (hasFileAssociations) {
            postinst << "if command -v update-mime-database >/dev/null 2>&1; then "
                        "update-mime-database /usr/share/mime || true; fi\n";
            postinst << "if command -v update-desktop-database >/dev/null 2>&1; then "
                        "update-desktop-database /usr/share/applications || true; fi\n";
        }
        controlTar.addFileString(
            "./postinst",
            postinst.str(),
            0755);
        std::ostringstream postrm;
        postrm << "#!/bin/sh\nset -e\n";
        if (hasManPages)
            postrm << "if command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n";
        if (hasFileAssociations) {
            postrm << "if command -v update-mime-database >/dev/null 2>&1; then "
                      "update-mime-database /usr/share/mime || true; fi\n";
            postrm << "if command -v update-desktop-database >/dev/null 2>&1; then "
                      "update-desktop-database /usr/share/applications || true; fi\n";
        }
        controlTar.addFileString("./postrm", postrm.str(), 0755);
    }
    const auto controlTarBytes = controlTar.finish();
    const auto controlTarGz = gzip(controlTarBytes.data(), controlTarBytes.size());

    ArWriter ar;
    ar.addMemberString("debian-binary", "2.0\n");
    ar.addMemberVec("control.tar.gz", controlTarGz);
    ar.addMemberVec("data.tar.gz", dataTarGz);
    ar.finishToFile(params.outputPath);
}

/// @brief Build a portable toolchain tarball from a staged install manifest.
/// Supports Linux, macOS, and Windows payloads; universal arch is accepted for macOS only.
/// Writes a `<name>-<version>-<platform>-<arch>/` top-level directory into a .tar.gz file.
void buildToolchainTarball(const LinuxToolchainBuildParams &params) {
    const auto &manifest = params.manifest;
    validateToolchainInstallManifest(manifest);
    const std::string packageName =
        params.packageName.empty() ? std::string("viper") : normalizeDebName(params.packageName);
    if (manifest.version.empty())
        throw std::runtime_error("toolchain package version is required");
    const std::string version = manifest.version;
    validateDebVersion(version, "toolchain package version");
    const std::string platform =
        manifest.platform.empty() ? portableArchivePlatformName() : manifest.platform;
    validateToolchainPlatform(platform);
    if (manifest.arch == "universal") {
        if (platform != "macos") {
            throw std::runtime_error("universal toolchain tarball architecture is only valid "
                                     "for macOS payloads");
        }
    } else {
        validateToolchainArchitecture(manifest.arch, "toolchain package architecture");
    }
    const std::string topDir =
        sanitizePackageRelativePath(packageName + "-" + portableArchiveVersionComponent(version) +
                                        "-" + platform + "-" + manifest.arch,
                                    "toolchain tarball top-level directory") +
        "/";

    TarWriter tar;
    tar.addDirectory(topDir, 0755);
    std::vector<std::string> installManifestPaths;
    for (const auto &file : manifest.files) {
        const std::string relPath = mapInstallPath(file, InstallPathPolicy::PortableArchive);
        validatePortableArchivePath(relPath, "toolchain tarball path");
        if (platform == "linux")
            installManifestPaths.push_back(relPath);
        if (file.symlink) {
            tar.addSymlink(topDir + relPath, file.symlinkTarget);
        } else {
            const auto data = readFile(file.stagedAbsolutePath.string());
            tar.addFile(topDir + relPath,
                        data.data(),
                        data.size(),
                        permissionBitsFor(file));
        }
    }
    if (platform == "linux" && !manifest.fileAssociations.empty()) {
        std::vector<DataFile> generated;
        addToolchainFileAssociationMetadata(generated, manifest, packageName, "viper");
        for (const auto &df : generated) {
            const std::string portablePath =
                sanitizePackageRelativePath(df.installPath.rfind("usr/", 0) == 0
                                                ? df.installPath.substr(4)
                                                : df.installPath,
                                            "toolchain tarball generated metadata path");
            installManifestPaths.push_back(portablePath);
            tar.addFile(topDir + portablePath, df.data.data(), df.data.size(), df.mode);
        }
    }
    if (platform == "linux") {
        installManifestPaths.push_back("share/viper/install_manifest.txt");
        const std::string manifestText = renderInstallManifest(installManifestPaths);
        tar.addFile(topDir + "share/viper/install_manifest.txt",
                    reinterpret_cast<const uint8_t *>(manifestText.data()),
                    manifestText.size(),
                    0644);

        const std::string installScript = linuxTarballInstallScript();
        tar.addFile(topDir + "install.sh",
                    reinterpret_cast<const uint8_t *>(installScript.data()),
                    installScript.size(),
                    0755);
        const std::string uninstallScript = linuxTarballUninstallScript();
        tar.addFile(topDir + "uninstall.sh",
                    reinterpret_cast<const uint8_t *>(uninstallScript.data()),
                    uninstallScript.size(),
                    0755);
        const std::string readme = linuxTarballReadme();
        tar.addFile(topDir + "README.install",
                    reinterpret_cast<const uint8_t *>(readme.data()),
                    readme.size(),
                    0644);
    }

    const auto tarBytes = tar.finish();
    const auto tarGz = gzip(tarBytes.data(), tarBytes.size());
    std::ofstream out(params.outputPath, std::ios::binary);
    if (!out)
        throw std::runtime_error("cannot write toolchain tarball: " + params.outputPath);
    out.write(reinterpret_cast<const char *>(tarGz.data()),
              static_cast<std::streamsize>(tarGz.size()));
    if (!out)
        throw std::runtime_error("failed to write toolchain tarball: " + params.outputPath);
}

/// @brief Build an RPM toolchain package from a staged install manifest using rpmbuild.
/// Creates a temporary rpmbuild workspace, generates a source tarball and .spec file,
/// invokes rpmbuild, then copies the resulting .rpm to `params.outputPath`.
/// Throws if rpmbuild is not on PATH or if the build fails.
void buildToolchainRpmPackage(const LinuxToolchainBuildParams &params) {
    const auto &manifest = params.manifest;
    requireLinuxToolchainManifest(manifest, "RPM toolchain package");
    const std::string packageName =
        params.packageName.empty() ? std::string("viper") : normalizeDebName(params.packageName);
    if (manifest.version.empty())
        throw std::runtime_error("toolchain package version is required");
    const std::string version = manifest.version;
    validateRpmVersion(version, "toolchain RPM version");
    validateToolchainArchitecture(manifest.arch, "toolchain package architecture");
    const std::string arch = rpmArchFor(manifest.arch);
    const auto dataFiles = collectToolchainLinuxFiles(manifest, packageName);
    validateDataFilePaths(dataFiles);
    if (!rpmbuildAvailable()) {
        throw std::runtime_error(
            "rpmbuild is required to generate RPM toolchain packages; install rpm-build "
            "or request linux-deb/tarball output");
    }

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
    for (const auto &file : dataFiles) {
        std::string sourcePath = file.installPath;
        if (sourcePath.rfind("usr/", 0) == 0)
            sourcePath = sourcePath.substr(4);
        validateRpmSpecPath(file.installPath);
        if (file.symlink) {
            tar.addSymlink(sourceTopDir + sourcePath, file.symlinkTarget);
        } else if (!file.directory) {
            tar.addFile(sourceTopDir + sourcePath,
                        file.data.data(),
                        file.data.size(),
                        file.mode);
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
        if (!out)
            throw std::runtime_error("failed to write rpm source tarball: " + sourceTar.string());
    }

    std::ostringstream spec;
    spec << "Name: " << packageName << "\n";
    spec << "Version: " << version << "\n";
    spec << "Release: 1%{?dist}\n";
    spec << "Summary: Viper compiler toolchain\n";
    spec << "License: GPL-3.0-only\n";
    spec << "BuildArch: " << arch << "\n";
    spec << "Source0: %{name}-%{version}.tar.gz\n";
    for (const auto &dep : toolchainRpmRequires(manifest))
        spec << "Requires: " << dep << "\n";
    spec << "Recommends: cmake\n";
    spec << "Recommends: gcc-c++\n\n";
    spec << "%description\nViper compiler toolchain\n\n";
    spec << "%prep\n%setup -q\n\n";
    spec << "%build\n:\n\n";
    spec << "%install\nrm -rf %{buildroot}\nmkdir -p %{buildroot}/usr\ncp -a . %{buildroot}/usr/\n\n";
    const bool hasManPages =
        std::any_of(manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &entry) {
            return entry.kind == ToolchainFileKind::ManPage;
        });
    const bool hasFileAssociations = !manifest.fileAssociations.empty();
    if (hasManPages || hasFileAssociations) {
        spec << "%post\n";
        if (hasManPages)
            spec << "if command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n";
        if (hasFileAssociations) {
            spec << "if command -v update-mime-database >/dev/null 2>&1; then update-mime-database /usr/share/mime || true; fi\n";
            spec << "if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database /usr/share/applications || true; fi\n\n";
        } else {
            spec << "\n";
        }
        if (hasFileAssociations) {
            spec << "%postun\n";
            if (hasManPages)
                spec << "if command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n";
            spec << "if command -v update-mime-database >/dev/null 2>&1; then update-mime-database /usr/share/mime || true; fi\n";
            spec << "if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database /usr/share/applications || true; fi\n\n";
        } else if (hasManPages) {
            spec << "%postun\nif command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n\n";
        }
    }
    spec << "%files\n";
    for (const auto &file : dataFiles) {
        validateRpmSpecPath(file.installPath);
        spec << rpmSpecFilePath(file.installPath) << "\n";
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

    const fs::path rpmPath = findGeneratedRpm(tmpRoot, packageName, version, arch);
    std::error_code copyEc;
    fs::copy_file(rpmPath, params.outputPath, fs::copy_options::overwrite_existing, copyEc);
    if (copyEc)
        throw std::runtime_error("cannot copy generated rpm to " + params.outputPath + ": " +
                                 copyEc.message());
}

} // namespace viper::pkg
