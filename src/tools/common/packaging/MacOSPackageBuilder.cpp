//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/MacOSPackageBuilder.cpp
// Purpose: Assemble macOS .app ZIPs and native flat .pkg toolchain installers
//          with proper Unix permissions and package metadata.
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
#include "CpioWriter.hpp"
#include "IconGenerator.hpp"
#include "PkgGzip.hpp"
#include "PkgUtils.hpp"
#include "PlistGenerator.hpp"
#include "XarWriter.hpp"
#include "ZipWriter.hpp"
#include "common/RunProcess.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace viper::pkg {
namespace {

/// @brief Generate a unique temp directory path combining `stem`, PID, and steady-clock tick.
/// Avoids collisions between concurrent packaging invocations.
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

/// @brief RAII guard that removes the directory tree at `path_` on destruction.
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

/// @brief Validate that `name` is a legal macOS bundle display name.
/// Must be non-empty, single-line, free of path separators (`/`, `\`, `:`), and pass Windows filename checks.
void validateBundleDisplayName(const std::string &name) {
    if (name.empty())
        throw std::runtime_error("macOS bundle display name must not be empty");
    validateSingleLineField(name, "macOS bundle display name");
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos)
        throw std::runtime_error("macOS bundle display name must not contain path separators: " +
                                 name);
    validateWindowsFileName(name, "macOS bundle display name");
}

/// @brief Write `data` to `path`, creating parent directories as needed, and apply `perms`.
void writeFileBytes(const fs::path &path, const std::vector<uint8_t> &data, fs::perms perms) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write macOS package file: " + path.string());
    out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out)
        throw std::runtime_error("failed while writing macOS package file: " + path.string());
    out.close();
    fs::permissions(path, perms, fs::perm_options::replace);
}

/// @brief Write `text` to `path` as UTF-8 bytes with the given Unix permissions.
void writeFileString(const fs::path &path, const std::string &text, fs::perms perms) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(text.data());
    writeFileBytes(path, std::vector<uint8_t>(bytes, bytes + text.size()), perms);
}

/// @brief Copy a package asset (file or directory tree) from `srcPath` into the .app `Resources` dir.
/// All copied regular files get mode 0644; directories are created as needed.
void copyPackageAssetToResources(const fs::path &srcPath,
                                 const fs::path &projectRoot,
                                 const fs::path &resourcesDir,
                                 const std::string &targetDir,
                                 const std::string &sourceText) {
    const fs::path targetRoot = resourcesDir / fs::path(targetDir);
    const std::string sourceRel = sanitizePackageRelativePath(sourceText, "asset source path");
    const fs::path sourceLeaf = fs::path(sourceRel).filename();
    if (!fs::exists(srcPath))
        throw std::runtime_error("asset not found: " + sourceText);

    if (fs::is_directory(srcPath)) {
        if (!targetDir.empty())
            fs::create_directories(targetRoot);
        safeDirectoryIterateResolved(
            srcPath, projectRoot, [&](const SafeDirectoryEntry &entry) {
                const auto relPath = sanitizePackageRelativePath(
                    entry.logicalPath.lexically_relative(srcPath).generic_string(), "asset path");
                const fs::path dst = targetRoot / fs::path(relPath);
                if (entry.directory) {
                    fs::create_directories(dst);
                } else if (entry.regularFile) {
                    writeFileBytes(dst,
                                   readFile(entry.resolvedPath.string()),
                                   fs::perms::owner_read | fs::perms::owner_write |
                                       fs::perms::group_read | fs::perms::others_read);
                }
            });
    } else if (fs::is_regular_file(srcPath)) {
        writeFileBytes(targetRoot / sourceLeaf,
                       readFile(srcPath.string()),
                       fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                           fs::perms::others_read);
    } else {
        throw std::runtime_error("asset is not a regular file or directory: " + sourceText);
    }
}

/// @brief Run a command and throw `std::runtime_error` if it exits non-zero.
/// The error message includes `what` plus the captured stdout and stderr.
void runChecked(const std::vector<std::string> &args, const std::string &what) {
    RunResult rr = run_process(args);
    if (rr.exit_code != 0)
        throw std::runtime_error(what + " failed:\n" + rr.out + rr.err);
}

/// @brief Resolve the version string for a macOS toolchain package.
/// Returns the validated override if non-empty; otherwise validates and returns the manifest version.
std::string resolveMacOSToolchainPackageVersion(const std::string &manifestVersion,
                                                const std::string &packageVersionOverride) {
    if (!packageVersionOverride.empty()) {
        validateDottedNumericVersion(packageVersionOverride,
                                     "macOS toolchain package version override");
        return packageVersionOverride;
    }
    validateDottedNumericVersion(manifestVersion, "macOS toolchain package version");
    return manifestVersion;
}

/// @brief Return a shell-safe single-quoted string for generated package scripts.
std::string shQuote(const std::string &value) {
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'')
            out += "'\\''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

std::string xmlEscape(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
    return out;
}

/// @brief Convert filesystem permissions to low POSIX permission bits.
uint32_t modeBitsForPath(const fs::path &path, bool executableFallback) {
    std::error_code ec;
    const auto status = fs::status(path, ec);
    if (ec)
        return executableFallback ? 0755u : 0644u;
    uint32_t mode = 0;
    const auto perms = status.permissions();
    using p = fs::perms;
    if ((perms & p::owner_read) != p::none)
        mode |= 0400u;
    if ((perms & p::owner_write) != p::none)
        mode |= 0200u;
    if ((perms & p::owner_exec) != p::none)
        mode |= 0100u;
    if ((perms & p::group_read) != p::none)
        mode |= 0040u;
    if ((perms & p::group_write) != p::none)
        mode |= 0020u;
    if ((perms & p::group_exec) != p::none)
        mode |= 0010u;
    if ((perms & p::others_read) != p::none)
        mode |= 0004u;
    if ((perms & p::others_write) != p::none)
        mode |= 0002u;
    if ((perms & p::others_exec) != p::none)
        mode |= 0001u;
    return mode == 0 ? (executableFallback ? 0755u : 0644u) : mode;
}

/// @brief Return a sorted list of paths under root, including root itself.
std::vector<fs::path> sortedTreeEntries(const fs::path &root) {
    std::vector<fs::path> entries;
    entries.push_back(root);
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        entries.push_back(it->path());
    }
    std::sort(entries.begin(), entries.end(), [](const fs::path &a, const fs::path &b) {
        return a.generic_string() < b.generic_string();
    });
    return entries;
}

/// @brief Add a staged filesystem tree to a portable ASCII CPIO archive with root-owned metadata.
void addFilesystemTreeToCpio(CpioWriter &cpio, const fs::path &root) {
    std::set<std::string> emittedPaths;
    for (const fs::path &entryPath : sortedTreeEntries(root)) {
        std::error_code ec;
        const fs::path relPath = entryPath.lexically_relative(root);
        if (relPath.empty()) {
            throw std::runtime_error("cannot compute macOS package payload path for: " +
                                     entryPath.string());
        }
        auto relIt = relPath.begin();
        if (relIt != relPath.end() && *relIt == fs::path("..")) {
            throw std::runtime_error("macOS package payload entry escapes staging root: " +
                                     entryPath.string());
        }
        const std::string archivePath = relPath.empty() || relPath == fs::path(".")
                                            ? "."
                                            : relPath.generic_string();
        if (!emittedPaths.insert(archivePath).second)
            continue;
        const auto symlinkStatus = fs::symlink_status(entryPath, ec);
        if (ec)
            throw std::runtime_error("cannot stat macOS package payload entry: " + ec.message());
        if (fs::is_symlink(symlinkStatus)) {
            const fs::path target = fs::read_symlink(entryPath, ec);
            if (ec)
                throw std::runtime_error("cannot read macOS package symlink: " + ec.message());
            try {
                cpio.addSymlink(archivePath, target.generic_string());
            } catch (const std::exception &ex) {
                throw std::runtime_error("cannot add macOS CPIO symlink '" +
                                         entryPath.string() + "' as '" + archivePath + "': " +
                                         ex.what());
            }
        } else if (fs::is_directory(symlinkStatus)) {
            try {
                cpio.addDirectory(archivePath, modeBitsForPath(entryPath, true));
            } catch (const std::exception &ex) {
                throw std::runtime_error("cannot add macOS CPIO directory '" +
                                         entryPath.string() + "' as '" + archivePath + "': " +
                                         ex.what());
            }
        } else if (fs::is_regular_file(symlinkStatus)) {
            const auto data = readFile(entryPath.string());
            try {
                cpio.addFileVec(archivePath, data, modeBitsForPath(entryPath, false));
            } catch (const std::exception &ex) {
                throw std::runtime_error("cannot add macOS CPIO file '" + entryPath.string() +
                                         "' as '" + archivePath + "': " + ex.what());
            }
        }
    }
}

/// @brief Create or replace a symlink, making parent directories first.
void createPackageSymlink(const fs::path &linkPath, const fs::path &target) {
    fs::create_directories(linkPath.parent_path());
    std::error_code ec;
    fs::remove(linkPath, ec);
    ec.clear();
    fs::create_symlink(target, linkPath, ec);
    if (ec) {
        throw std::runtime_error("cannot create package symlink '" + linkPath.string() + "' -> '" +
                                 target.generic_string() + "': " + ec.message());
    }
}

/// @brief Write text file and mark it executable.
void writeExecutableScript(const fs::path &path, const std::string &text) {
    writeFileString(path,
                    text,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                        fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                        fs::perms::others_exec);
}

std::vector<std::string> macOSToolNames(const ToolchainInstallManifest &manifest) {
    std::vector<std::string> names;
    for (const auto &file : manifest.files) {
        const std::string rel =
            sanitizePackageRelativePath(file.stagedRelativePath, "macOS tool path");
        if (file.kind != ToolchainFileKind::Binary && rel.rfind("bin/", 0) != 0)
            continue;
        names.push_back(fs::path(rel).filename().generic_string());
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

std::vector<std::string> macOSManPagePaths(const ToolchainInstallManifest &manifest) {
    static constexpr std::string_view kManPrefix = "share/man/";
    std::vector<std::string> paths;
    for (const auto &file : manifest.files) {
        const std::string rel =
            sanitizePackageRelativePath(file.stagedRelativePath, "macOS manpage path");
        if (file.kind != ToolchainFileKind::ManPage && rel.rfind(kManPrefix, 0) != 0)
            continue;
        if (rel.rfind(kManPrefix, 0) != 0)
            continue;
        paths.push_back(rel.substr(kManPrefix.size()));
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

void appendLeafNamesFromDirectory(std::vector<std::string> &names, const fs::path &dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto status = it->symlink_status(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (!fs::is_regular_file(status) && !fs::is_symlink(status))
            continue;
        names.push_back(it->path().filename().generic_string());
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
}

void appendRelativeFilePathsFromDirectory(std::vector<std::string> &paths, const fs::path &dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto status = it->symlink_status(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (!fs::is_regular_file(status) && !fs::is_symlink(status))
            continue;
        const fs::path rel = it->path().lexically_relative(dir);
        paths.push_back(sanitizePackageRelativePath(rel.generic_string(), "macOS manpage path"));
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
}

fs::path macOSManSymlinkTarget(const std::string &manRelPath) {
    const fs::path relPath = fs::path(manRelPath);
    const fs::path parentPath = relPath.parent_path();
    size_t upLevels = 2; // share/man
    for (auto it = parentPath.begin(); it != parentPath.end(); ++it)
        ++upLevels;
    fs::path target;
    for (size_t i = 0; i < upLevels; ++i)
        target /= "..";
    target /= "viper";
    target /= "share";
    target /= "man";
    target /= relPath;
    return target;
}

std::string fileAssociationExtensionWithoutDot(const FileAssoc &assoc) {
    std::string ext = assoc.extension;
    while (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    return ext;
}

std::string macOSFileAssociationUTI(const FileAssoc &assoc) {
    const std::string ext = fileAssociationExtensionWithoutDot(assoc);
    if (ext == "zia")
        return "org.viper.zia-source";
    if (ext == "bas")
        return "org.viper.basic-source";
    if (ext == "il")
        return "org.viper.il-module";
    return "org.viper." + ext;
}

const ToolchainFileEntry *findMacOSFileHandler(const ToolchainInstallManifest &manifest) {
    auto it = std::find_if(manifest.files.begin(), manifest.files.end(), [](const auto &file) {
        return !file.symlink &&
               sanitizePackageRelativePath(file.stagedRelativePath, "macOS file handler path") ==
                   "libexec/viper/viper-file-handler";
    });
    return it == manifest.files.end() ? nullptr : &*it;
}

std::string generateMacOSFileHandlerInfoPlist(const MacOSToolchainBuildParams &params,
                                              const std::string &pkgVersion) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    xml << "<plist version=\"1.0\">\n";
    xml << "<dict>\n";
    xml << "  <key>CFBundleDevelopmentRegion</key><string>en</string>\n";
    xml << "  <key>CFBundleDisplayName</key><string>Viper Toolchain</string>\n";
    xml << "  <key>CFBundleExecutable</key><string>viper-file-handler</string>\n";
    xml << "  <key>CFBundleIdentifier</key><string>"
        << xmlEscape(params.identifier + ".filehandler") << "</string>\n";
    xml << "  <key>CFBundleName</key><string>Viper Toolchain</string>\n";
    xml << "  <key>CFBundlePackageType</key><string>APPL</string>\n";
    xml << "  <key>CFBundleShortVersionString</key><string>" << xmlEscape(pkgVersion)
        << "</string>\n";
    xml << "  <key>CFBundleVersion</key><string>" << xmlEscape(pkgVersion) << "</string>\n";
    xml << "  <key>LSUIElement</key><true/>\n";
    xml << "  <key>CFBundleDocumentTypes</key>\n";
    xml << "  <array>\n";
    for (const auto &assoc : params.manifest.fileAssociations) {
        const std::string ext = fileAssociationExtensionWithoutDot(assoc);
        const std::string uti = macOSFileAssociationUTI(assoc);
        const bool sourceType = ext == "zia" || ext == "bas";
        xml << "    <dict>\n";
        xml << "      <key>CFBundleTypeName</key><string>" << xmlEscape(assoc.description)
            << "</string>\n";
        xml << "      <key>CFBundleTypeRole</key><string>"
            << (sourceType ? "Editor" : "Viewer") << "</string>\n";
        xml << "      <key>LSHandlerRank</key><string>Default</string>\n";
        xml << "      <key>LSItemContentTypes</key><array><string>" << xmlEscape(uti)
            << "</string></array>\n";
        xml << "    </dict>\n";
    }
    xml << "  </array>\n";
    xml << "  <key>UTExportedTypeDeclarations</key>\n";
    xml << "  <array>\n";
    for (const auto &assoc : params.manifest.fileAssociations) {
        const std::string ext = fileAssociationExtensionWithoutDot(assoc);
        const std::string uti = macOSFileAssociationUTI(assoc);
        const bool sourceType = ext == "zia" || ext == "bas";
        xml << "    <dict>\n";
        xml << "      <key>UTTypeIdentifier</key><string>" << xmlEscape(uti)
            << "</string>\n";
        xml << "      <key>UTTypeDescription</key><string>" << xmlEscape(assoc.description)
            << "</string>\n";
        xml << "      <key>UTTypeConformsTo</key><array><string>"
            << (sourceType ? "public.source-code" : "public.data") << "</string></array>\n";
        xml << "      <key>UTTypeTagSpecification</key>\n";
        xml << "      <dict>\n";
        xml << "        <key>public.filename-extension</key><array><string>"
            << xmlEscape(ext) << "</string></array>\n";
        if (!assoc.mimeType.empty()) {
            xml << "        <key>public.mime-type</key><array><string>"
                << xmlEscape(assoc.mimeType) << "</string></array>\n";
        }
        xml << "      </dict>\n";
        xml << "    </dict>\n";
    }
    xml << "  </array>\n";
    xml << "</dict>\n";
    xml << "</plist>\n";
    return xml.str();
}

std::string macOSInstallManifestText(const ToolchainInstallManifest &manifest) {
    std::vector<std::string> paths;
    paths.reserve(manifest.files.size() + 2);
    for (const auto &file : manifest.files)
        paths.push_back(sanitizePackageRelativePath(file.stagedRelativePath, "macOS manifest path"));
    paths.push_back("share/viper/install_manifest.txt");
    paths.push_back("share/viper/uninstall.sh");
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    std::ostringstream out;
    for (const auto &path : paths)
        out << path << "\n";
    return out.str();
}

std::string generateMacOSUninstallScript(const std::string &packageIdentifier) {
    std::ostringstream sh;
    sh << "#!/bin/sh\n";
    sh << "set -eu\n";
    sh << "ROOT=/usr/local/viper\n";
    sh << "MANIFEST=\"$ROOT/share/viper/install_manifest.txt\"\n";
    sh << "APP=\"/Applications/Viper Toolchain.app\"\n";
    sh << "LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister\n";
    sh << "if [ \"$(id -u)\" != \"0\" ]; then echo \"Run with sudo to remove Viper Toolchain\" >&2; exit 1; fi\n";
    sh << "if [ -x \"$LSREGISTER\" ] && [ -d \"$APP\" ]; then \"$LSREGISTER\" -u \"$APP\" >/dev/null 2>&1 || true; fi\n";
    sh << "if [ -f \"$MANIFEST\" ]; then\n";
    sh << "  while IFS= read -r rel || [ -n \"$rel\" ]; do\n";
    sh << "    [ -n \"$rel\" ] || continue\n";
    sh << "    case \"$rel\" in /*|..|../*|*/../*|*/..) echo \"Unsafe manifest path: $rel\" >&2; exit 2 ;; esac\n";
    sh << "    case \"$rel\" in\n";
    sh << "      bin/*) link=\"/usr/local/bin/${rel#bin/}\"; if [ -L \"$link\" ]; then target=$(readlink \"$link\" || true); case \"$target\" in ../viper/bin/*|/usr/local/viper/bin/*) rm -f \"$link\" ;; esac; fi ;;\n";
    sh << "      share/man/*) link=\"/usr/local/share/man/${rel#share/man/}\"; if [ -L \"$link\" ]; then target=$(readlink \"$link\" || true); case \"$target\" in *../viper/share/man/*|/usr/local/viper/share/man/*) rm -f \"$link\" ;; esac; fi ;;\n";
    sh << "    esac\n";
    sh << "    rm -f \"$ROOT/$rel\"\n";
    sh << "  done < \"$MANIFEST\"\n";
    sh << "fi\n";
    sh << "rm -rf \"$APP\"\n";
    sh << "rm -f /usr/local/lib/cmake/Viper/ViperConfig.cmake /usr/local/lib/cmake/Viper/ViperConfigVersion.cmake\n";
    sh << "if [ -d \"$ROOT\" ]; then find \"$ROOT\" -depth -type d -empty -delete 2>/dev/null || true; fi\n";
    sh << "for dir in /usr/local/lib/cmake/Viper /usr/local/share/man/man1 /usr/local/share/man/man7 /usr/local/viper; do rmdir \"$dir\" 2>/dev/null || true; done\n";
    sh << "pkgutil --forget " << shQuote(packageIdentifier) << " >/dev/null 2>&1 || true\n";
    sh << "if command -v mandb >/dev/null 2>&1; then mandb -q /usr/local/share/man >/dev/null 2>&1 || true; fi\n";
    sh << "if command -v makewhatis >/dev/null 2>&1; then makewhatis /usr/local/share/man >/dev/null 2>&1 || true; fi\n";
    sh << "exit 0\n";
    return sh.str();
}

std::string generateMacOSPreinstallScript(const std::vector<std::string> &toolNames) {
    std::ostringstream sh;
    sh << "#!/bin/sh\n";
    sh << "set -eu\n";
    sh << "ROOT=/usr/local/viper\n";
    sh << "OLD=\"$ROOT/share/viper/install_manifest.txt\"\n";
    sh << "SCRIPT_DIR=$(cd \"$(dirname \"$0\")\" && pwd)\n";
    sh << "NEW=\"$SCRIPT_DIR/install_manifest.txt\"\n";
    sh << "if [ -f \"$OLD\" ] && [ -f \"$NEW\" ]; then\n";
    sh << "  while IFS= read -r rel; do\n";
    sh << "    [ -n \"$rel\" ] || continue\n";
    sh << "    case \"$rel\" in /*|*..*) continue ;; esac\n";
    sh << "    if ! grep -F -x -- \"$rel\" \"$NEW\" >/dev/null 2>&1; then\n";
    sh << "      case \"$rel\" in\n";
    sh << "        bin/*)\n";
    sh << "          link=\"/usr/local/bin/${rel#bin/}\"\n";
    sh << "          if [ -L \"$link\" ]; then\n";
    sh << "            target=$(readlink \"$link\" || true)\n";
    sh << "            case \"$target\" in ../viper/bin/*|/usr/local/viper/bin/*) rm -f \"$link\" ;; esac\n";
    sh << "          fi\n";
    sh << "          ;;\n";
    sh << "        share/man/*)\n";
    sh << "          link=\"/usr/local/share/man/${rel#share/man/}\"\n";
    sh << "          if [ -L \"$link\" ]; then\n";
    sh << "            target=$(readlink \"$link\" || true)\n";
    sh << "            case \"$target\" in *../viper/share/man/*|/usr/local/viper/share/man/*) rm -f \"$link\" ;; esac\n";
    sh << "          fi\n";
    sh << "          ;;\n";
    sh << "      esac\n";
    sh << "      rm -f \"$ROOT/$rel\"\n";
    sh << "    fi\n";
    sh << "  done < \"$OLD\"\n";
    sh << "fi\n";
    for (const auto &name : toolNames) {
        sh << "link=/usr/local/bin/" << shQuote(name) << "\n";
        sh << "if [ -L \"$link\" ]; then\n";
        sh << "  target=$(readlink \"$link\" || true)\n";
        sh << "  case \"$target\" in ../viper/bin/*|/usr/local/viper/bin/*) rm -f \"$link\" ;; esac\n";
        sh << "fi\n";
    }
    sh << "if [ -L /usr/local/lib/cmake/Viper ]; then rm -f /usr/local/lib/cmake/Viper; fi\n";
    sh << "exit 0\n";
    return sh.str();
}

std::string generateMacOSPostinstallScript(const std::vector<std::string> &toolNames,
                                           const std::vector<std::string> &manPagePaths,
                                           bool registerFileAssociationApp) {
    std::ostringstream sh;
    sh << "#!/bin/sh\n";
    sh << "set -eu\n";
    sh << "mkdir -p /usr/local/bin /usr/local/lib/cmake/Viper /usr/local/share/man\n";
    for (const auto &name : toolNames) {
        sh << "if [ -e /usr/local/viper/bin/" << shQuote(name) << " ]; then\n";
        sh << "  link=/usr/local/bin/" << shQuote(name) << "\n";
        sh << "  if [ ! -e \"$link\" ] || [ -L \"$link\" ]; then\n";
        sh << "    ln -sfn ../viper/bin/" << shQuote(name) << " \"$link\"\n";
        sh << "  fi\n";
        sh << "fi\n";
    }
    for (const auto &page : manPagePaths) {
        const std::string source = "/usr/local/viper/share/man/" + page;
        const std::string link = "/usr/local/share/man/" + page;
        const std::string parent = fs::path(link).parent_path().generic_string();
        sh << "if [ -e " << shQuote(source) << " ]; then\n";
        sh << "  mkdir -p " << shQuote(parent) << "\n";
        sh << "  if [ ! -e " << shQuote(link) << " ] || [ -L " << shQuote(link)
           << " ]; then\n";
        sh << "    ln -sfn " << shQuote(macOSManSymlinkTarget(page).generic_string()) << " "
           << shQuote(link) << "\n";
        sh << "  fi\n";
        sh << "fi\n";
    }
    if (registerFileAssociationApp) {
        sh << "APP=\"/Applications/Viper Toolchain.app\"\n";
        sh << "LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister\n";
        sh << "if [ -x \"$LSREGISTER\" ] && [ -d \"$APP\" ]; then \"$LSREGISTER\" -f \"$APP\" >/dev/null 2>&1 || true; fi\n";
    }
    sh << "if command -v mandb >/dev/null 2>&1; then mandb -q /usr/local/share/man >/dev/null 2>&1 || true; fi\n";
    sh << "if command -v makewhatis >/dev/null 2>&1; then makewhatis /usr/local/share/man >/dev/null 2>&1 || true; fi\n";
    sh << "find /usr/local/viper -type d -empty -delete 2>/dev/null || true\n";
    sh << "exit 0\n";
    return sh.str();
}

std::string generateMacOSToolchainPackageInfo(const MacOSToolchainBuildParams &params,
                                              const std::string &pkgVersion,
                                              size_t payloadEntryCount) {
    std::ostringstream xml;
    const uint64_t kbytes = (params.manifest.totalSizeBytes() + 1023u) / 1024u;
    xml << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    xml << "<pkg-info overwrite-permissions=\"true\" relocatable=\"false\" identifier=\""
        << xmlEscape(params.identifier) << "\" postinstall-action=\"none\" version=\""
        << xmlEscape(pkgVersion) << "\" format-version=\"2\" auth=\"root\" install-location=\"/\">\n";
    xml << "    <payload numberOfFiles=\"" << payloadEntryCount << "\" installKBytes=\"" << kbytes
        << "\"/>\n";
    xml << "    <bundle-version/>\n";
    xml << "    <upgrade-bundle/>\n";
    xml << "    <update-bundle/>\n";
    xml << "    <atomic-update-bundle/>\n";
    xml << "    <strict-identifier/>\n";
    xml << "    <relocate/>\n";
    xml << "    <scripts>\n";
    xml << "        <preinstall file=\"./preinstall\" timeout=\"600\"/>\n";
    xml << "        <postinstall file=\"./postinstall\" timeout=\"600\"/>\n";
    xml << "    </scripts>\n";
    xml << "</pkg-info>\n";
    return xml.str();
}

std::string macOSHostArchitectures(const std::string &arch) {
    if (arch == "arm64")
        return "arm64";
    if (arch == "x64" || arch == "x86_64")
        return "x86_64";
    return "x86_64,arm64";
}

std::string generateMacOSToolchainDistribution(const MacOSToolchainBuildParams &params,
                                               const std::string &pkgVersion,
                                               uint64_t installKBytes) {
    std::ostringstream xml;
    const std::string escapedId = xmlEscape(params.identifier);
    const std::string escapedProductId = xmlEscape(params.identifier + ".product");
    const std::string escapedName = xmlEscape(params.displayName);
    const std::string escapedVersion = xmlEscape(pkgVersion);
    xml << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    xml << "<installer-gui-script minSpecVersion=\"1\">\n";
    xml << "  <title>" << escapedName << "</title>\n";
    xml << "  <pkg-ref id=\"" << escapedId << "\">\n";
    xml << "    <bundle-version/>\n";
    xml << "  </pkg-ref>\n";
    xml << "  <options customize=\"never\" require-scripts=\"false\" hostArchitectures=\""
        << xmlEscape(macOSHostArchitectures(params.manifest.arch)) << "\"/>\n";
    xml << "  <choices-outline>\n";
    xml << "    <line choice=\"default\">\n";
    xml << "      <line choice=\"" << escapedId << "\"/>\n";
    xml << "    </line>\n";
    xml << "  </choices-outline>\n";
    xml << "  <choice id=\"default\" title=\"" << escapedName << "\"/>\n";
    xml << "  <choice id=\"" << escapedId << "\" title=\"" << escapedName
        << "\" visible=\"false\">\n";
    xml << "    <pkg-ref id=\"" << escapedId << "\"/>\n";
    xml << "  </choice>\n";
    xml << "  <pkg-ref id=\"" << escapedId << "\" version=\"" << escapedVersion
        << "\" onConclusion=\"none\" installKBytes=\"" << installKBytes
        << "\" updateKBytes=\"0\" auth=\"Root\">#ViperToolchain.pkg</pkg-ref>\n";
    xml << "  <product id=\"" << escapedProductId << "\" version=\"" << escapedVersion
        << "\"/>\n";
    xml << "</installer-gui-script>\n";
    return xml.str();
}

/// @brief Walk the staged .app bundle and write all entries to a ZIP at `outputPath`.
/// The file at `execPath` gets Unix mode 0100755; all other files get 0100644.
void addStagedAppToZip(const fs::path &stageRoot,
                       const fs::path &appPath,
                       const fs::path &execPath,
                       const std::string &outputPath) {
    ZipWriter zip;
    const std::string appEntry = fs::relative(appPath, stageRoot).generic_string();
    zip.addDirectory(appEntry);

    std::error_code ec;
    auto it = fs::recursive_directory_iterator(
        appPath, fs::directory_options::skip_permission_denied, ec);
    if (ec)
        throw std::runtime_error("cannot iterate staged macOS app bundle: " + ec.message());
    const auto end = fs::recursive_directory_iterator();
    while (it != end) {
        const fs::path entryPath = it->path();
        const std::string rel = fs::relative(entryPath, stageRoot, ec).generic_string();
        if (ec)
            throw std::runtime_error("cannot compute macOS app ZIP path: " + ec.message());
        ec.clear();
        const fs::file_status symlinkStatus = it->symlink_status(ec);
        if (ec)
            throw std::runtime_error("cannot stat staged macOS app entry: " + ec.message());
        if (fs::is_symlink(symlinkStatus)) {
            const fs::path target = fs::read_symlink(entryPath, ec);
            if (ec)
                throw std::runtime_error("cannot read staged macOS app symlink: " +
                                         ec.message());
            zip.addSymlink(rel, target.generic_string());
        } else if (it->is_directory(ec)) {
            if (ec)
                throw std::runtime_error("cannot stat staged macOS app directory: " +
                                         ec.message());
            zip.addDirectory(rel);
        } else if (it->is_regular_file(ec)) {
            if (ec)
                throw std::runtime_error("cannot stat staged macOS app file: " + ec.message());
            const auto data = readFile(entryPath.string());
            const bool isExec = fs::equivalent(entryPath, execPath, ec);
            if (ec)
                throw std::runtime_error("cannot compare staged macOS executable path: " +
                                         ec.message());
            const uint32_t mode = isExec ? 0100755 : 0100644;
            zip.addFile(rel, data.data(), data.size(), mode);
        }
        it.increment(ec);
        if (ec)
            throw std::runtime_error("cannot advance staged macOS app iterator: " + ec.message());
    }
    zip.finish(outputPath);
}

/// @brief Verify the codesign signature of `appPath` using `codesign --verify --deep --strict`.
/// Only runs on Apple hosts; no-op on all other platforms.
void verifyMacOSBundleSignatureIfAvailable(const fs::path &appPath) {
#if defined(__APPLE__)
    runChecked({"codesign", "--verify", "--deep", "--strict", "--verbose=2", appPath.string()},
               "macOS code signature verification");
#else
    (void)appPath;
#endif
}

/// @brief Sign the .app bundle using `codesign` with the mode resolved from `pkg`.
/// Supports Developer ID (with optional notarization/stapling), ad-hoc, and no-op modes.
/// "ad-hoc" and "developer-id" modes throw on non-Apple hosts.
void signMacOSBundle(const fs::path &stageRoot,
                     const fs::path &appPath,
                     const fs::path &execPath,
                     const fs::path &projectRoot,
                     const PackageConfig &pkg) {
    validateMacOSSigningConfig(pkg);
    const std::string mode = resolveMacOSSignModeForHost(pkg);
    if (mode == "none" || mode == "preserve")
        return;

#if !defined(__APPLE__)
    (void)stageRoot;
    (void)appPath;
    (void)execPath;
    (void)projectRoot;
    (void)pkg;
    throw std::runtime_error("macOS signing mode '" + mode + "' requires running on macOS");
#else
    const bool developerId = mode == "developer-id";
    if (developerId && pkg.macosSignIdentity.empty())
        throw std::runtime_error("macOS Developer ID signing requires macos-sign-identity");
    if (!pkg.macosNotaryProfile.empty() && !developerId)
        throw std::runtime_error("macOS notarization requires macos-sign-mode developer-id");

    std::vector<std::string> args = {"codesign", "--force", "--sign",
                                     developerId ? pkg.macosSignIdentity : std::string("-")};
    if (developerId)
        args.push_back("--timestamp");
    if (pkg.macosHardenedRuntime || !pkg.macosNotaryProfile.empty()) {
        args.push_back("--options");
        args.push_back("runtime");
    }
    if (!pkg.macosEntitlements.empty()) {
        const fs::path entitlements =
            resolvePackageSourcePath(projectRoot, pkg.macosEntitlements, "macOS entitlements");
        args.push_back("--entitlements");
        args.push_back(entitlements.string());
    }
    args.push_back(appPath.string());
    runChecked(args, "macOS code signing");
    verifyMacOSBundleSignatureIfAvailable(appPath);

    if (!pkg.macosNotaryProfile.empty()) {
        const fs::path notaryZip = stageRoot / "notary-submit.zip";
        addStagedAppToZip(stageRoot, appPath, execPath, notaryZip.string());
        runChecked({"xcrun",
                    "notarytool",
                    "submit",
                    notaryZip.string(),
                    "--keychain-profile",
                    pkg.macosNotaryProfile,
                    "--wait"},
                   "macOS notarization");
        if (pkg.macosStaple) {
            runChecked({"xcrun", "stapler", "staple", appPath.string()},
                       "macOS notarization stapling");
            verifyMacOSBundleSignatureIfAvailable(appPath);
        }
    }
#endif
}

} // namespace

//=============================================================================
// MacOS Package Builder
//=============================================================================

/// @brief Build a macOS .app bundle inside a ZIP archive from the given build parameters.
/// Stages the bundle in a temp directory (exec, Resources, PkgInfo, Info.plist, ICNS),
/// optionally signs it with codesign, then packs the result into a ZIP at `params.outputPath`.
void buildMacOSPackage(const MacOSBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    const std::string version = params.version.empty() ? "0.0.0" : params.version;
    validateBundleDisplayName(displayName);
    validateMacOSBundleIdentifier(pkg.identifier, "macOS bundle identifier");
    validateDottedNumericVersion(version, "macOS package version");
    if (!pkg.minOsMacos.empty())
        validateDottedNumericVersion(pkg.minOsMacos, "minimum macOS version");
    validatePackageFileAssociations(pkg.fileAssociations);
    validateMacOSSigningConfig(pkg);
    if (!fs::is_regular_file(params.executablePath))
        throw std::runtime_error("macOS package executable is not a regular file: " +
                                 params.executablePath);

    // Determine executable name (lowercase, no spaces)
    std::string execName = normalizeExecName(params.projectName);

    std::string appName = displayName + ".app";
    const fs::path stageRoot = uniqueTempPackagingDir("viper-macos-app-" + execName);
    TempDirGuard cleanup(stageRoot);
    fs::remove_all(stageRoot);

    const fs::path appPath = stageRoot / appName;
    const fs::path contentsDir = appPath / "Contents";
    const fs::path macosDir = contentsDir / "MacOS";
    const fs::path resourcesDir = contentsDir / "Resources";
    fs::create_directories(macosDir);
    fs::create_directories(resourcesDir);

    writeFileString(contentsDir / "PkgInfo",
                    generatePkgInfo(),
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                        fs::perms::others_read);

    const fs::path stagedExec = macosDir / execName;
    writeFileBytes(stagedExec,
                   readFile(params.executablePath),
                   fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                       fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                       fs::perms::others_exec);

    std::string iconFileName;
    if (!pkg.iconPath.empty()) {
        fs::path iconSrc = resolvePackageSourcePath(params.projectRoot, pkg.iconPath, "package icon");
        if (!fs::is_regular_file(iconSrc))
            throw std::runtime_error("package icon not found: " + pkg.iconPath);
        auto srcImage = pngRead(iconSrc.string());
        auto icnsData = generateIcns(srcImage);
        iconFileName = execName;
        writeFileBytes(resourcesDir / (execName + ".icns"),
                       icnsData,
                       fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                           fs::perms::others_read);
    }

    PlistParams plistParams;
    plistParams.executableName = execName;
    plistParams.bundleId = pkg.identifier.empty() ? ("com.viper." + execName) : pkg.identifier;
    plistParams.bundleName = displayName;
    plistParams.version = version;
    plistParams.iconFile = iconFileName;
    plistParams.minOsVersion = pkg.minOsMacos;
    plistParams.fileAssociations = pkg.fileAssociations;
    writeFileString(contentsDir / "Info.plist",
                    generatePlist(plistParams),
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                        fs::perms::others_read);

    for (const auto &asset : pkg.assets) {
        fs::path srcPath = resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
        std::string targetDir = sanitizePackageRelativePath(asset.targetPath, "asset target path");
        copyPackageAssetToResources(srcPath, params.projectRoot, resourcesDir, targetDir, asset.sourcePath);
    }

    signMacOSBundle(stageRoot, appPath, stagedExec, params.projectRoot, pkg);
    addStagedAppToZip(stageRoot, appPath, stagedExec, params.outputPath);
}

/// @brief Build a macOS `.pkg` installer for the staged toolchain.
/// Stages files under `/usr/local/viper/`, creates receipt-owned command and
/// manpage symlinks, emits native CPIO/XAR archives, and wraps the component in
/// a product distribution package.
void buildMacOSToolchainPackage(const MacOSToolchainBuildParams &params) {
    namespace fs = std::filesystem;
    validateToolchainInstallManifest(params.manifest);
    if (params.manifest.platform != "macos") {
        throw std::runtime_error("macOS toolchain package requires a macOS staged toolchain "
                                 "manifest, got '" +
                                 params.manifest.platform + "'");
    }
    if (params.manifest.version.empty())
        throw std::runtime_error("toolchain package version is required");
    const std::string version = params.manifest.version;
    const std::string pkgVersion =
        resolveMacOSToolchainPackageVersion(version, params.packageVersion);
    validateMacOSBundleIdentifier(params.identifier, "macOS package identifier");
    validateDebVersion(version, "macOS toolchain manifest version");

    const fs::path tmpRoot = uniqueTempPackagingDir("viper-macos-toolchain-" + version);
    TempDirGuard cleanup(tmpRoot);
    fs::remove_all(tmpRoot);

    const fs::path payloadRoot = tmpRoot / "root";
    const fs::path installRoot = payloadRoot / "usr" / "local" / "viper";
    fs::create_directories(installRoot);
    fs::create_directories(payloadRoot / "usr" / "local" / "bin");

    for (const auto &file : params.manifest.files) {
        const fs::path dst = installRoot / fs::path(file.stagedRelativePath);
        fs::create_directories(dst.parent_path());
        if (file.symlink) {
            std::error_code ec;
            fs::remove(dst, ec);
            ec.clear();
            fs::create_symlink(fs::path(file.symlinkTarget), dst, ec);
            if (ec)
                throw std::runtime_error("cannot create package symlink '" + dst.string() +
                                         "' -> '" + file.symlinkTarget + "': " + ec.message());
            continue;
        }
        fs::copy_file(file.stagedAbsolutePath, dst, fs::copy_options::overwrite_existing);
        if (file.executable) {
            fs::permissions(dst,
                            fs::perms::owner_read | fs::perms::owner_write |
                                fs::perms::owner_exec | fs::perms::group_read |
                                fs::perms::group_exec | fs::perms::others_read |
                                fs::perms::others_exec,
                            fs::perm_options::replace);
        } else {
            fs::permissions(dst,
                            fs::perms::owner_read | fs::perms::owner_write |
                                fs::perms::group_read | fs::perms::others_read,
                            fs::perm_options::replace);
        }
    }

    std::vector<std::string> toolNames = macOSToolNames(params.manifest);
    appendLeafNamesFromDirectory(toolNames, installRoot / "bin");
    for (const auto &name : toolNames)
        createPackageSymlink(payloadRoot / "usr" / "local" / "bin" / name,
                             fs::path("../viper/bin") / name);

    writeFileString(payloadRoot / "usr" / "local" / "lib" / "cmake" / "Viper" /
                        "ViperConfig.cmake",
                    "include(\"/usr/local/viper/lib/cmake/Viper/ViperConfig.cmake\")\n",
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                        fs::perms::others_read);
    writeFileString(payloadRoot / "usr" / "local" / "lib" / "cmake" / "Viper" /
                        "ViperConfigVersion.cmake",
                    "include(\"/usr/local/viper/lib/cmake/Viper/ViperConfigVersion.cmake\")\n",
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                        fs::perms::others_read);

    std::vector<std::string> manPagePaths = macOSManPagePaths(params.manifest);
    appendRelativeFilePathsFromDirectory(manPagePaths, installRoot / "share" / "man");
    for (const auto &page : manPagePaths)
        createPackageSymlink(payloadRoot / "usr" / "local" / "share" / "man" / page,
                             macOSManSymlinkTarget(page));

    const bool registerFileAssociationApp = !params.manifest.fileAssociations.empty();
    if (registerFileAssociationApp) {
        const ToolchainFileEntry *handler = findMacOSFileHandler(params.manifest);
        if (handler == nullptr) {
            throw std::runtime_error("macOS toolchain file associations require staged "
                                     "libexec/viper/viper-file-handler");
        }
        const fs::path appContents =
            payloadRoot / "Applications" / "Viper Toolchain.app" / "Contents";
        const fs::path appMacOS = appContents / "MacOS";
        fs::create_directories(appMacOS);
        writeFileString(appContents / "Info.plist",
                        generateMacOSFileHandlerInfoPlist(params, pkgVersion),
                        fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                            fs::perms::others_read);
        writeFileString(appContents / "PkgInfo",
                        generatePkgInfo(),
                        fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                            fs::perms::others_read);
        const fs::path appHandler = appMacOS / "viper-file-handler";
        fs::copy_file(handler->stagedAbsolutePath, appHandler, fs::copy_options::overwrite_existing);
        fs::permissions(appHandler,
                        fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                            fs::perms::group_read | fs::perms::group_exec |
                            fs::perms::others_read | fs::perms::others_exec,
                        fs::perm_options::replace);
    }

    writeExecutableScript(installRoot / "share" / "viper" / "uninstall.sh",
                          generateMacOSUninstallScript(params.identifier));
    const std::string installManifest = macOSInstallManifestText(params.manifest);
    writeFileString(installRoot / "share" / "viper" / "install_manifest.txt",
                    installManifest,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                        fs::perms::others_read);

    const fs::path scriptsRoot = tmpRoot / "scripts";
    fs::create_directories(scriptsRoot);
    writeExecutableScript(scriptsRoot / "preinstall", generateMacOSPreinstallScript(toolNames));
    writeExecutableScript(scriptsRoot / "postinstall",
                          generateMacOSPostinstallScript(toolNames,
                                                         manPagePaths,
                                                         registerFileAssociationApp));
    writeFileString(scriptsRoot / "install_manifest.txt",
                    installManifest,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                        fs::perms::others_read);

    const fs::path bomPath = tmpRoot / "Bom";
    runChecked({"mkbom", "-s", payloadRoot.string(), bomPath.string()},
               "macOS bill-of-materials generation");

    CpioWriter payloadCpio;
    addFilesystemTreeToCpio(payloadCpio, payloadRoot);
    const auto payloadArchive = payloadCpio.finish();
    const auto payloadGzip = gzip(payloadArchive.data(), payloadArchive.size());

    CpioWriter scriptsCpio;
    addFilesystemTreeToCpio(scriptsCpio, scriptsRoot);
    const auto scriptsArchive = scriptsCpio.finish();
    const auto scriptsGzip = gzip(scriptsArchive.data(), scriptsArchive.size());

    const size_t payloadEntryCount = sortedTreeEntries(payloadRoot).size();
    const uint64_t installKBytes = (params.manifest.totalSizeBytes() + 1023u) / 1024u;
    const std::string packageInfo =
        generateMacOSToolchainPackageInfo(params, pkgVersion, payloadEntryCount);
    const std::string distribution =
        generateMacOSToolchainDistribution(params, pkgVersion, installKBytes);

    const fs::path output = fs::path(params.outputPath);
    if (!output.parent_path().empty())
        fs::create_directories(output.parent_path());
    XarWriter product;
    product.addDirectory("ViperToolchain.pkg", 0700);
    product.addFileVec("ViperToolchain.pkg/Bom", readFile(bomPath.string()), false);
    product.addFileVec("ViperToolchain.pkg/Payload", payloadGzip, false);
    product.addFileVec("ViperToolchain.pkg/Scripts", scriptsGzip, false);
    product.addFileString("ViperToolchain.pkg/PackageInfo", packageInfo, true);
    product.addFileString("Distribution", distribution, true);
    product.finishToFile(params.outputPath);
}

} // namespace viper::pkg
