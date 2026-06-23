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
#include "LinuxRuntimeStubGen.hpp"
#include "PkgGzip.hpp"
#include "PkgMD5.hpp"
#include "PkgPNG.hpp"
#include "PkgUtils.hpp"
#include "TarWriter.hpp"
#include "common/RunProcess.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

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

/// @brief Read a little-endian 16-bit integer from an ELF byte buffer.
uint16_t readLe16(const std::vector<uint8_t> &data, size_t off) {
    if (off + 2u > data.size())
        return 0;
    return (uint16_t)data[off] | ((uint16_t)data[off + 1u] << 8);
}

/// @brief Read a little-endian 32-bit integer from an ELF byte buffer.
uint32_t readLe32(const std::vector<uint8_t> &data, size_t off) {
    if (off + 4u > data.size())
        return 0;
    return (uint32_t)data[off] | ((uint32_t)data[off + 1u] << 8) |
           ((uint32_t)data[off + 2u] << 16) | ((uint32_t)data[off + 3u] << 24);
}

/// @brief Read a little-endian 64-bit integer from an ELF byte buffer.
uint64_t readLe64(const std::vector<uint8_t> &data, size_t off) {
    uint64_t lo = readLe32(data, off);
    uint64_t hi = readLe32(data, off + 4u);
    return lo | (hi << 32);
}

/// @brief Return a file slice corresponding to an ELF virtual address range.
/// @details Maps @p vaddr through PT_LOAD program headers. This is enough for
///          the package dependency scanner to resolve DT_STRTAB into the
///          dynamic string table without invoking host tools.
/// @param data Full ELF file bytes.
/// @param vaddr Virtual address to map.
/// @param size Number of bytes needed.
/// @param phoff ELF program-header table offset.
/// @param phentsize Program-header entry size.
/// @param phnum Program-header count.
/// @return File offset on success, std::nullopt when no load segment covers it.
std::optional<size_t> elfFileOffsetForVaddr(const std::vector<uint8_t> &data,
                                            uint64_t vaddr,
                                            uint64_t size,
                                            uint64_t phoff,
                                            uint16_t phentsize,
                                            uint16_t phnum) {
    for (uint16_t i = 0; i < phnum; ++i) {
        const size_t off = (size_t)phoff + (size_t)i * phentsize;
        if (off + 56u > data.size())
            break;
        const uint32_t type = readLe32(data, off);
        if (type != 1u)
            continue;
        const uint64_t p_offset = readLe64(data, off + 8u);
        const uint64_t p_vaddr = readLe64(data, off + 16u);
        const uint64_t p_filesz = readLe64(data, off + 32u);
        if (vaddr < p_vaddr || vaddr - p_vaddr > p_filesz)
            continue;
        const uint64_t delta = vaddr - p_vaddr;
        if (size > p_filesz - delta)
            continue;
        if (p_offset > data.size() || delta > data.size() - (size_t)p_offset)
            continue;
        const size_t fileOff = (size_t)(p_offset + delta);
        if (fileOff <= data.size() && size <= data.size() - fileOff)
            return fileOff;
    }
    return std::nullopt;
}

/// @brief Read DT_NEEDED library names from a little-endian ELF64 binary.
/// @details The parser intentionally supports the ELF64 shape produced by
///          Viper's Linux x64/arm64 toolchains. Unsupported or malformed inputs
///          simply return an empty list so callers can retain conservative
///          fallback dependencies.
/// @param data Full ELF file bytes.
/// @return SONAMEs referenced by DT_NEEDED entries.
std::vector<std::string> elfNeededLibraries(const std::vector<uint8_t> &data) {
    if (data.size() < 64u || data[0] != 0x7F || data[1] != 'E' || data[2] != 'L' ||
        data[3] != 'F' || data[4] != 2u || data[5] != 1u)
        return {};
    const uint64_t phoff = readLe64(data, 32u);
    const uint16_t phentsize = readLe16(data, 54u);
    const uint16_t phnum = readLe16(data, 56u);
    if (phoff == 0 || phentsize < 56u || phnum == 0)
        return {};

    uint64_t dynamicOff = 0;
    uint64_t dynamicSize = 0;
    for (uint16_t i = 0; i < phnum; ++i) {
        const size_t off = (size_t)phoff + (size_t)i * phentsize;
        if (off + 56u > data.size())
            return {};
        if (readLe32(data, off) == 2u) {
            dynamicOff = readLe64(data, off + 8u);
            dynamicSize = readLe64(data, off + 32u);
            break;
        }
    }
    if (dynamicOff == 0 || dynamicOff > data.size() ||
        dynamicSize > data.size() - (size_t)dynamicOff)
        return {};

    uint64_t strtabVaddr = 0;
    uint64_t strtabSize = 0;
    std::vector<uint64_t> neededOffsets;
    for (size_t off = (size_t)dynamicOff; off + 16u <= (size_t)(dynamicOff + dynamicSize);
         off += 16u) {
        const uint64_t tag = readLe64(data, off);
        const uint64_t value = readLe64(data, off + 8u);
        if (tag == 0)
            break;
        if (tag == 1)
            neededOffsets.push_back(value);
        else if (tag == 5)
            strtabVaddr = value;
        else if (tag == 10)
            strtabSize = value;
    }
    if (strtabVaddr == 0 || strtabSize == 0 || neededOffsets.empty())
        return {};
    const auto strtabOff =
        elfFileOffsetForVaddr(data, strtabVaddr, strtabSize, phoff, phentsize, phnum);
    if (!strtabOff)
        return {};

    std::vector<std::string> names;
    for (uint64_t nameOff : neededOffsets) {
        if (nameOff >= strtabSize)
            continue;
        const size_t start = *strtabOff + (size_t)nameOff;
        size_t end = start;
        while (end < data.size() && end < *strtabOff + (size_t)strtabSize && data[end] != 0)
            ++end;
        if (end > start)
            names.emplace_back(reinterpret_cast<const char *>(data.data() + start), end - start);
    }
    return names;
}

/// @brief Return Unix permission bits for a toolchain file.
/// Uses the stored `unixMode` if non-zero; otherwise defaults to 0755 for executables and 0644 for
/// data files.
uint32_t permissionBitsFor(const ToolchainFileEntry &file) {
    const uint32_t bits = file.unixMode & 07777u;
    if (bits != 0)
        return bits;
    return file.executable ? 0755u : 0644u;
}

/// @brief Choose archive permission bits for a staged file from its on-disk mode.
/// @return 0755 when any execute bit is set on the source file, else 0644 (also
///         the fallback when the file cannot be stat'd).
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

/// @brief Validate a version string that will be normalized into a portable archive name.
/// @details Portable tarballs preserve existing support for Debian epoch versions
///          such as `2:1.0` by accepting `:` here, then mapping it to `_` in the
///          filename component. Path separators and special path components remain rejected.
/// @param version Version text from project metadata or a toolchain manifest.
void validatePortableTarballVersion(const std::string &version) {
    if (version.empty())
        throw std::runtime_error("package version must not be empty");
    validateSingleLineField(version, "package version");
    if (version == "." || version == "..")
        throw std::runtime_error("package version must not be a special path segment");
    if (version.find('/') != std::string::npos || version.find('\\') != std::string::npos)
        throw std::runtime_error("package version must not contain path separators: " + version);
}

/// @brief Sanitize a package version into a portable filename component.
/// @details Keeps alphanumerics and `._+~-`, and replaces other safe metadata
///          separators such as Debian epoch `:` with `_` so the version can
///          appear safely in a tarball filename.
std::string portableArchiveVersionComponent(const std::string &version) {
    validatePortableTarballVersion(version);
    std::string out;
    out.reserve(version.size());
    for (char c : version) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '.' || c == '_' || c == '+' || c == '~' || c == '-')
            out.push_back(c);
        else
            out.push_back('_');
    }
    if (out.empty() || out == "." || out == "..")
        throw std::runtime_error("package version does not form a portable archive path");
    return out;
}

/// @brief Append a hidden terminal-type .desktop file to `dataFiles` for the given file
/// associations. Writes a `noDisplay=true` desktop entry under
/// `usr/share/applications/<desktopName>`. No-op when `associations` is empty.
void addToolchainDesktopMetadata(std::vector<DataFile> &dataFiles,
                                 const std::string &desktopName,
                                 const std::string &execPath,
                                 const std::string &execArguments,
                                 const std::vector<FileAssoc> &associations) {
    if (associations.empty())
        return;

    DesktopEntryParams desktop;
    desktop.name = "Viper Toolchain";
    desktop.comment = "Viper source and IL tools";
    desktop.execPath = execPath;
    desktop.execArguments = execArguments;
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

/// @brief Append MIME XML and separate .desktop files (one for .il, one for source types) to
/// `dataFiles`. Splits `manifest.fileAssociations` into IL vs. source groups so each gets its own
/// desktop handler. No-op when `manifest.fileAssociations` is empty.
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

    addToolchainDesktopMetadata(
        dataFiles, packageName + "-source.desktop", viperExecPath, "run", sourceAssociations);
    addToolchainDesktopMetadata(
        dataFiles, packageName + "-il.desktop", viperExecPath, "-run", ilAssociations);

    const std::string mimeXml = generateMimeTypeXml(packageName, manifest.fileAssociations);
    dataFiles.emplace_back("usr/share/mime/packages/" + packageName + ".xml",
                           std::vector<uint8_t>(mimeXml.begin(), mimeXml.end()),
                           0644);
}

/// @brief Collect all Linux install files from the manifest, mapping each to its FHS path under
/// /usr via `LinuxUsrRoot` policy, then appending generated file-association metadata entries.
std::vector<DataFile> collectToolchainLinuxFiles(const ToolchainInstallManifest &manifest,
                                                 const std::string &packageName) {
    std::vector<DataFile> dataFiles;
    dataFiles.reserve(manifest.files.size() + 2);
    for (const auto &file : manifest.files) {
        const std::string installPath = mapInstallPath(file, InstallPathPolicy::LinuxUsrRoot);
        const std::string relInstall = sanitizePackageRelativePath(
            installPath.size() > 1 ? installPath.substr(1) : installPath, "linux install path");
        if (file.symlink)
            dataFiles.push_back(DataFile::link(relInstall, file.symlinkTarget));
        else
            dataFiles.emplace_back(
                relInstall, readFile(file.stagedAbsolutePath.string()), permissionBitsFor(file));
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

/// @brief Map a Viper arch string ("x64", "arm64") to the RPM architecture name
/// ("x86_64"/"aarch64").
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

/// @brief Join strings with ", " — used to render Debian Depends/Requires lines.
std::string joinCommaSeparated(const std::vector<std::string> &items) {
    std::ostringstream out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0)
            out << ", ";
        out << items[i];
    }
    return out.str();
}

/// @brief Test whether the manifest stages a support/library file whose filename
///        contains @p name (case-insensitive substring match).
/// @details Used to derive runtime package dependencies from which Viper support
///          libraries are actually included in the staged tree.
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

/// @brief Return every DT_NEEDED SONAME discovered in staged ELF files.
/// @details Reads binary/support/library files from the manifest and parses
///          ELF64 dynamic sections. Malformed or non-ELF files are ignored so
///          package generation remains conservative rather than brittle.
/// @param manifest Toolchain staging manifest to inspect.
/// @return Sorted unique library SONAMEs.
std::vector<std::string> manifestNeededLibraries(const ToolchainInstallManifest &manifest) {
    std::vector<std::string> needed;
    for (const auto &file : manifest.files) {
        if (file.symlink || (file.kind != ToolchainFileKind::Binary &&
                             file.kind != ToolchainFileKind::SupportLibrary &&
                             file.kind != ToolchainFileKind::Library)) {
            continue;
        }
        std::error_code ec;
        if (!fs::is_regular_file(file.stagedAbsolutePath, ec) || ec)
            continue;
        try {
            std::vector<uint8_t> data = readFile(file.stagedAbsolutePath.string());
            std::vector<std::string> fileNeeded = elfNeededLibraries(data);
            needed.insert(needed.end(), fileNeeded.begin(), fileNeeded.end());
        } catch (const std::exception &) {
            continue;
        }
    }
    std::sort(needed.begin(), needed.end());
    needed.erase(std::unique(needed.begin(), needed.end()), needed.end());
    return needed;
}

/// @brief True if the manifest includes graphics/GUI libraries that need X11.
bool manifestNeedsX11(const ToolchainInstallManifest &manifest) {
    const auto needed = manifestNeededLibraries(manifest);
    if (std::find(needed.begin(), needed.end(), "libX11.so.6") != needed.end())
        return true;
    return manifestHasSupportLibrary(manifest, "vipergfx") ||
           manifestHasSupportLibrary(manifest, "vipergui");
}

/// @brief True if the manifest includes the audio library that needs ALSA.
bool manifestNeedsAlsa(const ToolchainInstallManifest &manifest) {
    const auto needed = manifestNeededLibraries(manifest);
    if (std::find(needed.begin(), needed.end(), "libasound.so.2") != needed.end())
        return true;
    return manifestHasSupportLibrary(manifest, "viperaud");
}

/// @brief Which C++ runtime the staged toolchain dynamically links.
enum class ToolchainCxxRuntime { Unknown, LibStdCxx, LibCxx };

/// @brief Detect the linked C++ runtime by scanning the primary staged ELF binary for the
///        libstdc++ / libc++ SONAME in its dynamic string table. Returns Unknown when the
///        binary is absent, not an ELF, or references neither/both — callers then keep the
///        conservative default dependency.
ToolchainCxxRuntime detectToolchainCxxRuntime(const ToolchainInstallManifest &manifest) {
    const ToolchainFileEntry *primary = nullptr;
    for (const auto &entry : manifest.files) {
        if (entry.symlink || entry.kind != ToolchainFileKind::Binary)
            continue;
        const std::string base = fs::path(entry.stagedRelativePath).filename().string();
        if (base == "viper" || base == "viper.exe") {
            primary = &entry;
            break;
        }
        if (!primary)
            primary = &entry;
    }
    if (!primary)
        return ToolchainCxxRuntime::Unknown;
    std::error_code ec;
    if (!fs::is_regular_file(primary->stagedAbsolutePath, ec))
        return ToolchainCxxRuntime::Unknown;
    std::vector<uint8_t> data;
    try {
        data = readFile(primary->stagedAbsolutePath.string());
    } catch (const std::exception &) {
        return ToolchainCxxRuntime::Unknown;
    }
    if (data.size() < 4 || data[0] != 0x7F || data[1] != 'E' || data[2] != 'L' || data[3] != 'F')
        return ToolchainCxxRuntime::Unknown;
    const auto needed = elfNeededLibraries(data);
    bool hasStdCxx = std::find(needed.begin(), needed.end(), "libstdc++.so.6") != needed.end();
    bool hasLibCxx = std::find(needed.begin(), needed.end(), "libc++.so.1") != needed.end();
    if (needed.empty()) {
        const std::string_view view(reinterpret_cast<const char *>(data.data()), data.size());
        hasStdCxx = view.find("libstdc++.so.6") != std::string_view::npos;
        hasLibCxx = view.find("libc++.so.1") != std::string_view::npos;
    }
    if (hasStdCxx && !hasLibCxx)
        return ToolchainCxxRuntime::LibStdCxx;
    if (hasLibCxx && !hasStdCxx)
        return ToolchainCxxRuntime::LibCxx;
    return ToolchainCxxRuntime::Unknown;
}

/// @brief Return the Debian Depends line for a toolchain .deb.
/// @details Narrows the C++ runtime dependency to the library the staged binary actually links
///          (libstdc++6 or libc++1); falls back to the `libstdc++6 | libc++1` alternative when
///          detection is inconclusive.
std::string toolchainDebDepends(const ToolchainInstallManifest &manifest) {
    std::string cxxRuntime = "libstdc++6 | libc++1";
    switch (detectToolchainCxxRuntime(manifest)) {
        case ToolchainCxxRuntime::LibStdCxx:
            cxxRuntime = "libstdc++6";
            break;
        case ToolchainCxxRuntime::LibCxx:
            cxxRuntime = "libc++1";
            break;
        case ToolchainCxxRuntime::Unknown:
            break;
    }
    std::vector<std::string> deps = {
        "libc6",
        cxxRuntime,
        "libgcc-s1",
        "cmake",
        "g++ | clang++",
        "make",
    };
    if (manifestNeedsX11(manifest))
        deps.push_back("libx11-6");
    if (manifestNeedsAlsa(manifest))
        deps.push_back("libasound2 | libasound2t64");
    return joinCommaSeparated(deps);
}

/// @brief Build the Debian `Depends:` list for application packages.
/// @details Adds conservative base runtime dependencies for generated native
///          applications, then appends validated user-specified dependencies
///          without duplicating exact entries.
/// @param pkg Package configuration containing any additional dependency terms.
/// @return Ordered dependency list suitable for a Debian control file.
std::vector<std::string> appDebDepends(const PackageConfig &pkg) {
    std::vector<std::string> deps = {
        "libc6",
        "libstdc++6 | libc++1",
    };
    for (const auto &dep : pkg.depends) {
        validateDebDependency(dep);
        if (std::find(deps.begin(), deps.end(), dep) == deps.end())
            deps.push_back(dep);
    }
    return deps;
}

/// @brief Build the RPM "Requires" list for a toolchain package.
/// @details Always requires the base C/C++ runtime and build tools, and adds
///          libX11 / alsa-lib when the manifest stages the graphics/audio support
///          libraries.
std::vector<std::string> toolchainRpmRequires(const ToolchainInstallManifest &manifest) {
    std::string cxxRuntime = "libstdc++";
    if (detectToolchainCxxRuntime(manifest) == ToolchainCxxRuntime::LibCxx)
        cxxRuntime = "libcxx";
    std::vector<std::string> deps = {
        "glibc",
        cxxRuntime,
        "libgcc",
        "cmake",
        "gcc-c++",
        "make",
    };
    if (manifestNeedsX11(manifest))
        deps.push_back("libX11");
    if (manifestNeedsAlsa(manifest))
        deps.push_back("alsa-lib");
    return deps;
}

/// @brief Sort @p paths and remove exact duplicates (taken by value).
std::vector<std::string> sortedUniquePaths(std::vector<std::string> paths) {
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

/// @brief Render the PREFIX-relative installed-files manifest written into tarballs.
/// @details Sorts and de-duplicates @p paths and emits one path per line under a
///          header comment, so the uninstall script knows what to remove.
std::string renderInstallManifest(std::vector<std::string> paths) {
    paths = sortedUniquePaths(std::move(paths));
    std::ostringstream out;
    out << "# Viper toolchain installed files, relative to PREFIX.\n";
    for (const auto &path : paths)
        out << path << "\n";
    return out.str();
}

/// @brief Shared shell helpers for generated Linux install/uninstall scripts.
std::string linuxPathSafetyShellFunctions() {
    return R"VIPER_SCRIPT(
validate_manifest_relpath() {
    rel=$1
    case "$rel" in
        ""|\#*) return 1 ;;
        /*|..|../*|*/../*|*/..) echo "Unsafe manifest path: $rel" >&2; exit 2 ;;
    esac
    return 0
}

check_no_symlink_path() {
    path=$1
    case "$path" in
        /*) current=/; rest=${path#/} ;;
        *) current=.; rest=$path ;;
    esac
    while [ -n "$rest" ]; do
        component=${rest%%/*}
        if [ "$component" = "$rest" ]; then
            rest=
        else
            rest=${rest#*/}
        fi
        [ -z "$component" ] && continue
        if [ "$current" = "/" ]; then
            current="/$component"
        else
            current="$current/$component"
        fi
        if [ -L "$current" ]; then
            echo "Refusing to operate through symlink path component: $current" >&2
            exit 2
        fi
    done
}

)VIPER_SCRIPT";
}

/// @brief Return the POSIX `install.sh` script bundled in the portable tarball.
/// @details Copies the staged tree under PREFIX (default /usr/local), honoring
///          DESTDIR, and records what it installed for the matching uninstaller.
std::string linuxTarballInstallScript() {
    return std::string(R"VIPER_SCRIPT(#!/bin/sh
set -eu

prefix=${PREFIX:-/usr/local}
destdir=${DESTDIR:-}

case "$prefix" in
    /*) ;;
    *) echo "PREFIX must be an absolute path" >&2; exit 2 ;;
esac
case "$destdir" in
    ""|/*) ;;
    *) echo "DESTDIR must be empty or an absolute path" >&2; exit 2 ;;
esac

root=$(CDPATH= cd "$(dirname "$0")" && pwd)
install_root=${destdir%/}$prefix
old_manifest="$install_root/share/viper/install_manifest.txt"
new_manifest="$root/share/viper/install_manifest.txt"
)VIPER_SCRIPT") +
           linuxPathSafetyShellFunctions() + R"VIPER_SCRIPT(
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

if [ -f "$old_manifest" ] && [ -f "$new_manifest" ] && [ "$old_manifest" != "$new_manifest" ]; then
    while IFS= read -r rel || [ -n "$rel" ]; do
        validate_manifest_relpath "$rel" || continue
        if ! grep -F -x -- "$rel" "$new_manifest" >/dev/null 2>&1; then
            check_no_symlink_path "$install_root/$rel"
            rm -f "$install_root/$rel"
        fi
    done < "$old_manifest"
fi

check_no_symlink_path "$install_root"
for dir do
    check_no_symlink_path "$install_root/$dir"
done

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

/// @brief Return the POSIX `uninstall.sh` script bundled in the portable tarball.
/// @details Removes the files recorded by install.sh's manifest under PREFIX.
std::string linuxTarballUninstallScript() {
    return std::string(R"VIPER_SCRIPT(#!/bin/sh
set -eu

prefix=${PREFIX:-/usr/local}
destdir=${DESTDIR:-}

case "$prefix" in
    /*) ;;
    *) echo "PREFIX must be an absolute path" >&2; exit 2 ;;
esac
case "$destdir" in
    ""|/*) ;;
    *) echo "DESTDIR must be empty or an absolute path" >&2; exit 2 ;;
esac

install_root=${destdir%/}$prefix
manifest="$install_root/share/viper/install_manifest.txt"
)VIPER_SCRIPT") +
           linuxPathSafetyShellFunctions() + R"VIPER_SCRIPT(
if [ ! -f "$manifest" ]; then
    echo "Viper install manifest not found: $manifest" >&2
    exit 1
fi

while IFS= read -r rel || [ -n "$rel" ]; do
    validate_manifest_relpath "$rel" || continue
    check_no_symlink_path "$install_root/$rel"
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

/// @brief Return the README text bundled in the portable toolchain tarball.
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

Before copying a new tarball payload, install.sh removes files listed in the
currently installed manifest when those files are absent from the new manifest.
The uninstaller removes only files listed in share/viper/install_manifest.txt.
)VIPER_TEXT";
}

/// @brief Return true if the `rpmbuild` tool is available on PATH (exit code 0).
bool rpmbuildAvailable() {
    const RunResult rr = run_process({"rpmbuild", "--version"});
    return rr.exit_code == 0;
}

/// @brief Find the .rpm produced by rpmbuild under `tmpRoot/RPMS/<arch>/`.
/// Expects exactly one file matching `<packageName>-<version>-*.<arch>.rpm`; throws if none or more
/// than one.
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
    validatePortableTarballVersion(version);
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
    std::string email = trimAsciiWhitespace(pkg.maintainerEmail);
    if (email.empty())
        email = "noreply@example.invalid";
    validateSingleLineField(email, "package maintainer email");
    return maintainer + " <" + email + ">";
}

/// @brief Build the Debian/RPM maintainer string for a toolchain package from the manifest.
/// @details Uses manifest.maintainer (default "Viper Project") and manifest.maintainerEmail,
///          falling back to the RFC-2606 reserved `noreply@example.invalid` only when no email
///          is configured. Always yields the required `Name <email>` form.
std::string toolchainMaintainer(const ToolchainInstallManifest &manifest) {
    std::string name = trimAsciiWhitespace(manifest.maintainer);
    if (name.empty())
        name = "Viper Project";
    validateSingleLineField(name, "toolchain package maintainer");
    if (name.find('<') != std::string::npos && name.find('>') != std::string::npos)
        return name;
    if (name.find('@') != std::string::npos)
        return name;
    std::string email = trimAsciiWhitespace(manifest.maintainerEmail);
    if (email.empty())
        email = "noreply@example.invalid";
    validateSingleLineField(email, "toolchain package maintainer email");
    return name + " <" + email + ">";
}

/// @brief Return @p value as a POSIX shell single-quoted literal.
/// @details Escapes embedded single quotes using the standard close/escape/reopen
///          sequence. Used for package-generated maintainer scripts where
///          normalized package names are still embedded as data, not syntax.
std::string shellSingleQuote(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (char c : value) {
        if (c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

/// @brief Append the opt-in Linux desktop-shortcut install snippet.
/// @details Copies the packaged .desktop launcher into existing root/home
///          Desktop directories and then restores ownership to the directory's
///          owner when `stat` is available. Failures remain non-fatal because
///          per-user desktop folders vary widely across Linux systems.
void appendHomeDesktopShortcutInstallScript(std::ostream &script, const std::string &pkgName) {
    const std::string desktopName = shellSingleQuote(pkgName + ".desktop");
    const std::string source = shellSingleQuote("/usr/share/applications/" + pkgName + ".desktop");
    script << "for d in /root/Desktop /home/*/Desktop; do\n"
              "    [ -d \"$d\" ] && [ ! -L \"$d\" ] || continue\n"
              "    target=\"$d/\""
           << desktopName
           << "\n"
              "    [ ! -L \"$target\" ] || continue\n"
              "    tmp=$(mktemp \"$d/.viper-desktop.XXXXXX\" 2>/dev/null) || continue\n"
              "    owner=$(stat -c %U \"$d\" 2>/dev/null || printf '')\n"
              "    group=$(stat -c %G \"$d\" 2>/dev/null || printf '')\n"
              "    cp "
           << source
           << " \"$tmp\" || { rm -f \"$tmp\"; continue; }\n"
              "    chmod 755 \"$tmp\" || true\n"
              "    if [ -n \"$owner\" ] && [ \"$owner\" != \"UNKNOWN\" ]; then\n"
              "        if [ -n \"$group\" ] && [ \"$group\" != \"UNKNOWN\" ]; then\n"
              "            chown \"$owner:$group\" \"$tmp\" || true\n"
              "        else\n"
              "            chown \"$owner\" \"$tmp\" || true\n"
              "        fi\n"
              "    fi\n"
              "    mv -f \"$tmp\" \"$target\" || rm -f \"$tmp\"\n"
              "done\n";
}

/// @brief Append the opt-in Linux desktop-shortcut removal snippet.
/// @details Removes only the exact launcher filename emitted by the matching
///          install script, ignoring missing or inaccessible user Desktop
///          directories so package removal does not fail on home-directory
///          permission edge cases.
void appendHomeDesktopShortcutRemovalScript(std::ostream &script, const std::string &pkgName) {
    const std::string desktopName = shellSingleQuote(pkgName + ".desktop");
    script << "for d in /root/Desktop /home/*/Desktop; do\n"
              "    [ -d \"$d\" ] && [ ! -L \"$d\" ] || continue\n"
              "    target=\"$d/\""
           << desktopName
           << "\n"
              "    [ -f \"$target\" ] && [ ! -L \"$target\" ] && rm -f \"$target\" || true\n"
              "done\n";
}

/// @brief Return README text bundled in portable application tarballs.
/// @details The tarball layout is intentionally self-contained; this text tells
///          users where the executable and optional asset payloads live without
///          requiring an installer script.
std::string appTarballReadme(const std::string &displayName,
                             const std::string &version,
                             const std::string &exeName,
                             const PackageConfig &pkg) {
    std::ostringstream out;
    out << displayName << " " << version << "\n\n";
    if (!pkg.description.empty())
        out << pkg.description << "\n\n";
    out << "Run:\n  ./" << exeName << "\n";
    if (!pkg.assets.empty())
        out << "\nBundled assets are stored alongside the executable under their configured "
               "relative paths.\n";
    if (!pkg.homepage.empty())
        out << "\nHomepage: " << pkg.homepage << "\n";
    if (!pkg.author.empty())
        out << "Author: " << pkg.author << "\n";
    if (!pkg.license.empty())
        out << "License: " << pkg.license << "\n";
    return out.str();
}

/// @brief Return the license metadata text bundled in portable application tarballs.
/// @details Project manifests currently carry an SPDX-style license identifier,
///          not full license body text, so the packaged file records the declared
///          identifier and points consumers back to the project distribution for
///          complete terms when needed.
std::string appTarballLicenseText(const std::string &displayName, const PackageConfig &pkg) {
    std::ostringstream out;
    out << displayName << "\n";
    if (!pkg.license.empty())
        out << "SPDX-License-Identifier: " << pkg.license << "\n";
    else
        out << "SPDX-License-Identifier: NOASSERTION\n";
    return out.str();
}

/// @brief Return a small generated PNG used for toolchain AppImage desktop metadata.
std::vector<uint8_t> defaultViperAppImageIconPng() {
    PkgImage img;
    img.width = 64;
    img.height = 64;
    img.pixels.resize(static_cast<size_t>(img.width) * img.height * 4u);
    for (uint32_t y = 0; y < img.height; ++y) {
        for (uint32_t x = 0; x < img.width; ++x) {
            uint8_t *px = img.at(x, y);
            const bool border = x < 4 || y < 4 || x >= img.width - 4 || y >= img.height - 4;
            const bool diagonal = x > y ? x - y < 6 : y - x < 6;
            px[0] = border ? 30 : (diagonal ? 40 : 15);
            px[1] = border ? 90 : (diagonal ? 150 : 120);
            px[2] = border ? 80 : (diagonal ? 120 : 170);
            px[3] = 255;
        }
    }
    return pngEncode(img);
}

/// @brief Append AppImage desktop/icon metadata at the payload root.
void addToolchainAppImageMetadata(TarWriter &tar, const std::string &packageName) {
    DesktopEntryParams desktop;
    desktop.name = "Viper Toolchain";
    desktop.comment = "Viper source and IL tools";
    desktop.execPath = "AppRun";
    desktop.iconName = packageName;
    desktop.categories = "Development;";
    desktop.terminal = true;
    const std::string desktopText = generateDesktopEntry(desktop);
    tar.addFileString(packageName + ".desktop", desktopText, 0644);
    const auto icon = defaultViperAppImageIconPng();
    tar.addFileVec(packageName + ".png", icon, 0644);
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

/// @brief Validate that `path` is normalized for portable archive use (no `..`, no absolute
/// prefix). Throws with `fieldName` in the error message if the path is not canonical.
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

/// @brief Create a unique temporary packaging directory under the system temp root.
/// @details Delegates to the shared exclusive-creation helper so concurrent packaging
///          invocations never remove or reuse another process's workspace.
/// @param stem Prefix to use for the generated directory name.
/// @return Path to the newly-created directory.
fs::path uniqueTempPackagingDir(std::string_view stem) {
    return createUniqueTempDirectory(fs::temp_directory_path(), stem);
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

/// @brief Collect the FHS-mapped data files (executable, assets, .desktop launcher,
///        icons, MIME XML) for an end-user Linux application package.
/// @details Shared by the Debian (.deb) and RPM application builders so both emit
///          an identical install layout (`/usr/bin/<exe>`, `/usr/share/<pkg>/...`,
///          `/usr/share/applications/<pkg>.desktop`, hicolor icons, MIME XML).
static std::vector<DataFile> collectAppLinuxDataFiles(const LinuxBuildParams &params,
                                                      const std::string &pkgName,
                                                      const std::string &exeName,
                                                      const std::string &displayName) {
    const auto &pkg = params.pkgConfig;
    std::vector<DataFile> dataFiles;

    // The executable
    auto execData = readFile(params.executablePath);
    dataFiles.emplace_back("usr/bin/" + exeName, std::move(execData), 0755);

    // Assets
    for (const auto &asset : pkg.assets) {
        const std::string sourceRel =
            sanitizePackageRelativePath(asset.sourcePath, "asset source path");
        const std::string sourceLeaf = fs::path(sourceRel).filename().generic_string();
        fs::path srcPath =
            resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
        std::string targetDir = sanitizePackageRelativePath(asset.targetPath, "asset target path");

        std::string sharePrefix = joinPackageRelativePath("usr/share/" + pkgName, targetDir);

        if (!fs::exists(srcPath))
            throw std::runtime_error("asset not found: " + asset.sourcePath);

        if (fs::is_directory(srcPath)) {
            dataFiles.push_back(DataFile::dir(sharePrefix));
            safeDirectoryIterateResolved(
                srcPath, params.projectRoot, [&](const SafeDirectoryEntry &entry) {
                    auto relPath = sanitizePackageRelativePath(
                        entry.logicalPath.lexically_relative(srcPath).generic_string(),
                        "asset path");
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
            dataFiles.emplace_back(joinPackageRelativePath(sharePrefix, sourceLeaf, "asset path"),
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
        fs::path iconSrc =
            resolvePackageSourcePath(params.projectRoot, pkg.iconPath, "package icon");
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
    return dataFiles;
}

/// @brief Build a Debian .deb package from the given build parameters.
/// Assembles control.tar.gz (control + md5sums + maintainer scripts) and data.tar.gz
/// (binary, assets, .desktop, icons, MIME XML) then wraps them in an ar archive.
void buildDebPackage(const LinuxBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    std::string pkgName = normalizeDebName(params.projectName);
    std::string exeName = normalizeExecName(params.projectName);
    std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    const std::string version = params.version.empty() ? "0.0.0" : params.version;
    validatePackageHooksAllowed(pkg);
    validateDebMetadata(pkg, displayName, version, params.archStr);
    if (params.archStr != "amd64" && params.archStr != "arm64" && params.archStr != "all")
        throw std::runtime_error("Debian package architecture must be amd64, arm64, or all: " +
                                 params.archStr);
    if (!fs::is_regular_file(params.executablePath))
        throw std::runtime_error("Linux package executable is not a regular file: " +
                                 params.executablePath);

    // Collect all data files (for md5sums and data.tar)
    std::vector<DataFile> dataFiles =
        collectAppLinuxDataFiles(params, pkgName, exeName, displayName);
    // Recomputed here for the maintainer-script section below; the shared helper
    // applies the same condition when it emits the .desktop entry.
    const bool needDesktopEntry =
        pkg.shortcutMenu || pkg.shortcutDesktop || !pkg.fileAssociations.empty();

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
        if (df.symlink)
            dataTar.addSymlink("./" + df.installPath, df.symlinkTarget);
        else
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
            checkedAddU64(
                totalBytes, static_cast<uint64_t>(df.data.size()), "Debian package installed size");
        }
        ctl << "Installed-Size: " << roundedKiB(totalBytes, "Debian package") << "\n";

        // Dependencies
        ctl << "Depends: " << joinCommaSeparated(appDebDepends(pkg)) << "\n";

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
    bool needPostinst = needDesktopEntry || pkg.shortcutDesktop || !pkg.postInstallScript.empty();
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
        if (pkg.shortcutDesktop && pkg.allowHomeDesktopShortcuts)
            appendHomeDesktopShortcutInstallScript(pi, pkgName);
        if (!pkg.postInstallScript.empty()) {
            pi << "# viper user post-install hook begin\n";
            pi << normalizePackageHookScript(pkg.postInstallScript, "post-install script") << "\n";
            pi << "# viper user post-install hook end\n";
        }
        controlTar.addFileString("./postinst", pi.str(), 0755);
    }

    // prerm script (custom hooks + desktop shortcut cleanup before files go away)
    bool needPrerm = pkg.shortcutDesktop || !pkg.preUninstallScript.empty();
    if (needPrerm) {
        std::ostringstream pr;
        pr << "#!/bin/sh\n";
        pr << "set -e\n";
        if (!pkg.preUninstallScript.empty()) {
            pr << "# viper user pre-uninstall hook begin\n";
            pr << normalizePackageHookScript(pkg.preUninstallScript, "pre-uninstall script")
               << "\n";
            pr << "# viper user pre-uninstall hook end\n";
        }
        if (pkg.shortcutDesktop && pkg.allowHomeDesktopShortcuts)
            appendHomeDesktopShortcutRemovalScript(pr, pkgName);
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
    std::string topDir =
        sanitizePackageRelativePath(pkgName + "-" + portableArchiveVersionComponent(version),
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
        fs::path srcPath =
            resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
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
            tar.addFile(joinPackageRelativePath(prefix, sourceLeaf, "asset path"),
                        fileData.data(),
                        fileData.size(),
                        permissionBitsForFilesystemPath(srcPath));
        } else {
            throw std::runtime_error("asset is not a regular file or directory: " +
                                     asset.sourcePath);
        }
    }

    const std::string readme = appTarballReadme(displayName, version, exeName, pkg);
    tar.addFile(topDir + "README.install",
                reinterpret_cast<const uint8_t *>(readme.data()),
                readme.size(),
                0644);
    const std::string licenseText = appTarballLicenseText(displayName, pkg);
    tar.addFile(topDir + "LICENSE",
                reinterpret_cast<const uint8_t *>(licenseText.data()),
                licenseText.size(),
                0644);

    auto tarBytes = tar.finish();
    auto tarGz = gzip(tarBytes.data(), tarBytes.size());

    writeFileAtomic(params.outputPath, tarGz);
}

/// @brief Build a self-extracting Linux AppImage for an end-user application.
/// @details Reuses the same runtime stub and gzip-tar payload format as the
///          toolchain AppImage path (buildToolchainAppImage), but sources its
///          single executable, bundled assets, `.desktop` launcher, and icon from
///          an application's `LinuxBuildParams`/`PackageConfig` rather than a
///          staged toolchain manifest.
void buildAppImage(const LinuxBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    const std::string pkgName = normalizeDebName(params.projectName);
    const std::string exeName = normalizeExecName(params.projectName);
    const std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    const std::string version = params.version.empty() ? "0.0.0" : params.version;
    validatePortableMetadata(pkg, displayName, version);
    if (params.archStr != "x64" && params.archStr != "arm64")
        throw std::runtime_error("AppImage architecture must be x64 or arm64: " + params.archStr);
    if (!fs::is_regular_file(params.executablePath))
        throw std::runtime_error("AppImage executable is not a regular file: " +
                                 params.executablePath);

    TarWriter tar;
    tar.addDirectory("./", 0755);
    // Entry point: AppRun -> usr/bin/<exe>, matching the toolchain AppImage layout.
    tar.addSymlink("AppRun", "usr/bin/" + exeName);

    // Application executable.
    auto execData = readFile(params.executablePath);
    tar.addFile("usr/bin/" + exeName, execData.data(), execData.size(), 0755);

    // Bundled assets under usr/share/<pkg>/ (mirrors the .deb layout).
    const std::string assetBase = "usr/share/" + pkgName;
    for (const auto &asset : pkg.assets) {
        const std::string sourceRel =
            sanitizePackageRelativePath(asset.sourcePath, "asset source path");
        const std::string sourceLeaf = fs::path(sourceRel).filename().generic_string();
        const fs::path srcPath =
            resolvePackageSourcePath(params.projectRoot, asset.sourcePath, "asset source path");
        const std::string targetDir =
            sanitizePackageRelativePath(asset.targetPath, "asset target path");
        const std::string prefix =
            joinPackageRelativePath(assetBase, targetDir, "asset target path");

        if (!fs::exists(srcPath))
            throw std::runtime_error("asset not found: " + asset.sourcePath);

        if (fs::is_directory(srcPath)) {
            tar.addDirectory(prefix, 0755);
            safeDirectoryIterateResolved(
                srcPath, params.projectRoot, [&](const SafeDirectoryEntry &entry) {
                    const auto relPath = sanitizePackageRelativePath(
                        entry.logicalPath.lexically_relative(srcPath).generic_string(),
                        "asset path");
                    if (entry.directory) {
                        tar.addDirectory(joinPackageRelativePath(prefix, relPath, "asset path"),
                                         0755);
                    } else if (entry.regularFile) {
                        auto fileData = readFile(entry.resolvedPath.string());
                        tar.addFile(joinPackageRelativePath(prefix, relPath, "asset path"),
                                    fileData.data(),
                                    fileData.size(),
                                    permissionBitsForFilesystemPath(entry.resolvedPath));
                    }
                });
        } else if (fs::is_regular_file(srcPath)) {
            auto fileData = readFile(srcPath.string());
            tar.addFile(joinPackageRelativePath(prefix, sourceLeaf, "asset path"),
                        fileData.data(),
                        fileData.size(),
                        permissionBitsForFilesystemPath(srcPath));
        } else {
            throw std::runtime_error("asset is not a regular file or directory: " +
                                     asset.sourcePath);
        }
    }

    // Desktop launcher at the payload root (Exec points at the AppRun entry).
    {
        DesktopEntryParams dep;
        dep.name = displayName;
        dep.comment = pkg.description;
        dep.execPath = "AppRun";
        dep.iconName = exeName;
        dep.categories = pkg.category;
        dep.terminal = false;
        dep.fileAssociations = pkg.fileAssociations;
        dep.acceptsFileArgument = !pkg.fileAssociations.empty();
        const std::string desktopText = generateDesktopEntry(dep);
        tar.addFileString(exeName + ".desktop", desktopText, 0644);
    }

    // Icon at the payload root (<exe>.png); falls back to a generated default icon.
    {
        std::vector<uint8_t> iconPng;
        if (!pkg.iconPath.empty()) {
            const fs::path iconSrc =
                resolvePackageSourcePath(params.projectRoot, pkg.iconPath, "package icon");
            if (!fs::is_regular_file(iconSrc))
                throw std::runtime_error("package icon not found: " + pkg.iconPath);
            const auto srcImage = pngRead(iconSrc.string());
            iconPng = pngEncode(srcImage);
        } else {
            iconPng = defaultViperAppImageIconPng();
        }
        tar.addFileVec(exeName + ".png", iconPng, 0644);
    }

    const auto tarBytes = tar.finish();
    const auto tarGz = gzip(tarBytes.data(), tarBytes.size());
    LinuxRuntimeStubParams stub;
    stub.cacheName =
        pkgName + "-" + portableArchiveVersionComponent(version) + "-linux-" + params.archStr;
    stub.entryPath = "AppRun";
    const auto appImage = buildLinuxAppImage(stub, tarGz);
    writeFileAtomic(params.outputPath, appImage);
    std::error_code ec;
    fs::permissions(params.outputPath,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add,
                    ec);
    if (ec)
        throw std::runtime_error("cannot mark AppImage executable: " + ec.message());
}

/// @brief Build an RPM package for an end-user application using rpmbuild.
/// @details Mirrors the toolchain RPM path (buildToolchainRpmPackage): assembles a
///          source tarball and .spec from the application's shared FHS data files
///          (collectAppLinuxDataFiles), then invokes rpmbuild. Requires rpmbuild on
///          PATH (Fedora/RHEL build hosts) and throws a clear diagnostic otherwise.
void buildRpmPackage(const LinuxBuildParams &params) {
    const auto &pkg = params.pkgConfig;
    const std::string pkgName = normalizeDebName(params.projectName);
    const std::string exeName = normalizeExecName(params.projectName);
    const std::string displayName = pkg.displayName.empty() ? params.projectName : pkg.displayName;
    const std::string version = params.version.empty() ? "0.0.0" : params.version;
    if (params.archStr != "x64" && params.archStr != "arm64")
        throw std::runtime_error("RPM architecture must be x64 or arm64: " + params.archStr);
    validateRpmVersion(version, "package version");
    if (!fs::is_regular_file(params.executablePath))
        throw std::runtime_error("RPM package executable is not a regular file: " +
                                 params.executablePath);
    const std::string arch = rpmArchFor(params.archStr);
    const auto dataFiles = collectAppLinuxDataFiles(params, pkgName, exeName, displayName);
    if (!rpmbuildAvailable()) {
        throw std::runtime_error(
            "rpmbuild is required to generate RPM application packages; install rpm-build "
            "or use --target deb, appimage, or tarball");
    }

    const fs::path tmpRoot = uniqueTempPackagingDir("viper-app-rpm-" + version + "-" + arch);
    TempDirGuard cleanup(tmpRoot);
    fs::create_directories(tmpRoot / "BUILD");
    fs::create_directories(tmpRoot / "BUILDROOT");
    fs::create_directories(tmpRoot / "RPMS");
    fs::create_directories(tmpRoot / "SOURCES");
    fs::create_directories(tmpRoot / "SPECS");
    fs::create_directories(tmpRoot / "SRPMS");

    const std::string sourceTopDir = pkgName + "-" + version + "/";
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
            tar.addFile(sourceTopDir + sourcePath, file.data.data(), file.data.size(), file.mode);
        }
    }
    const auto tarBytes = tar.finish();
    const auto tarGz = gzip(tarBytes.data(), tarBytes.size());
    const fs::path sourceTar = tmpRoot / "SOURCES" / (pkgName + "-" + version + ".tar.gz");
    writeFileAtomic(sourceTar, tarGz);

    const std::string summary = pkg.description.empty() ? displayName : pkg.description;
    std::ostringstream spec;
    spec << "Name: " << pkgName << "\n";
    spec << "Version: " << version << "\n";
    spec << "Release: 1%{?dist}\n";
    {
        std::string summaryLine = summary;
        validateSingleLineField(summaryLine, "package summary");
        spec << "Summary: " << summaryLine << "\n";
    }
    {
        std::string license = trimAsciiWhitespace(pkg.license);
        if (license.empty())
            license = "Proprietary";
        validateSingleLineField(license, "package license");
        spec << "License: " << license << "\n";
    }
    if (!pkg.homepage.empty()) {
        validatePackageUrl(pkg.homepage, "package homepage");
        spec << "URL: " << pkg.homepage << "\n";
    }
    spec << "BuildArch: " << arch << "\n";
    spec << "Source0: %{name}-%{version}.tar.gz\n";
    spec << "\n";
    spec << "%description\n" << summary << "\n\n";
    spec << "%prep\n%setup -q\n\n";
    spec << "%build\n:\n\n";
    spec << "%install\nrm -rf %{buildroot}\nmkdir -p %{buildroot}/usr\ncp -a . "
            "%{buildroot}/usr/\n\n";
    if (!pkg.fileAssociations.empty()) {
        spec << "%post\n";
        spec << "if command -v update-mime-database >/dev/null 2>&1; then update-mime-database "
                "/usr/share/mime || true; fi\n";
        spec << "if command -v update-desktop-database >/dev/null 2>&1; then "
                "update-desktop-database /usr/share/applications || true; fi\n\n";
        spec << "%postun\n";
        spec << "if command -v update-mime-database >/dev/null 2>&1; then update-mime-database "
                "/usr/share/mime || true; fi\n";
        spec << "if command -v update-desktop-database >/dev/null 2>&1; then "
                "update-desktop-database /usr/share/applications || true; fi\n\n";
    }
    spec << "%files\n";
    for (const auto &file : dataFiles) {
        validateRpmSpecPath(file.installPath);
        if (file.directory)
            spec << "%dir " << rpmSpecFilePath(file.installPath) << "\n";
        else
            spec << rpmSpecFilePath(file.installPath) << "\n";
    }

    const fs::path specPath = tmpRoot / "SPECS" / (pkgName + ".spec");
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
        throw std::runtime_error("rpmbuild failed while generating application rpm:\n" + rr.out +
                                 rr.err);
    }

    const fs::path rpmPath = findGeneratedRpm(tmpRoot, pkgName, version, arch);
    std::error_code copyEc;
    fs::copy_file(rpmPath, params.outputPath, fs::copy_options::overwrite_existing, copyEc);
    if (copyEc)
        throw std::runtime_error("cannot copy generated rpm to " + params.outputPath + ": " +
                                 copyEc.message());
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
            dataTar.addFile("./" + df.installPath, df.data.data(), df.data.size(), df.mode);
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
        ctl << "Maintainer: " << toolchainMaintainer(manifest) << "\n";
        ctl << "Depends: " << toolchainDebDepends(manifest) << "\n";
        uint64_t totalBytes = 0;
        for (const auto &df : dataFiles) {
            checkedAddU64(totalBytes,
                          static_cast<uint64_t>(df.data.size()),
                          "Debian toolchain package installed size");
        }
        ctl << "Installed-Size: " << roundedKiB(totalBytes, "Debian toolchain package") << "\n";
        if (!manifest.homepage.empty()) {
            validatePackageUrl(manifest.homepage, "toolchain package homepage");
            ctl << "Homepage: " << manifest.homepage << "\n";
        }
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
    const bool hasManPages = std::any_of(
        manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &entry) {
            return entry.kind == ToolchainFileKind::ManPage;
        });
    const bool hasFileAssociations = !manifest.fileAssociations.empty();
    if (hasManPages || hasFileAssociations) {
        std::ostringstream postinst;
        postinst << "#!/bin/sh\nset -e\n";
        if (hasManPages)
            postinst
                << "if command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n";
        if (hasFileAssociations) {
            postinst << "if command -v update-mime-database >/dev/null 2>&1; then "
                        "update-mime-database /usr/share/mime || true; fi\n";
            postinst << "if command -v update-desktop-database >/dev/null 2>&1; then "
                        "update-desktop-database /usr/share/applications || true; fi\n";
        }
        controlTar.addFileString("./postinst", postinst.str(), 0755);
        std::ostringstream postrm;
        postrm << "#!/bin/sh\nset -e\n";
        if (hasManPages)
            postrm
                << "if command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n";
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
            tar.addFile(topDir + relPath, data.data(), data.size(), permissionBitsFor(file));
        }
    }
    if (platform == "linux" && !manifest.fileAssociations.empty()) {
        std::vector<DataFile> generated;
        addToolchainFileAssociationMetadata(generated, manifest, packageName, "viper");
        for (const auto &df : generated) {
            const std::string portablePath = sanitizePackageRelativePath(
                df.installPath.rfind("usr/", 0) == 0 ? df.installPath.substr(4) : df.installPath,
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
    writeFileAtomic(params.outputPath, tarGz);
}

/// @brief Build a FUSE-less self-extracting Linux AppImage from a staged install manifest.
void buildToolchainAppImage(const LinuxToolchainBuildParams &params) {
    const auto &manifest = params.manifest;
    requireLinuxToolchainManifest(manifest, "Linux AppImage");
    const std::string packageName =
        params.packageName.empty() ? std::string("viper") : normalizeDebName(params.packageName);
    if (manifest.version.empty())
        throw std::runtime_error("toolchain package version is required");
    const std::string version = manifest.version;
    validateDebVersion(version, "toolchain package version");
    validateToolchainArchitecture(manifest.arch, "toolchain package architecture");

    TarWriter tar;
    tar.addDirectory("./", 0755);
    tar.addSymlink("AppRun", "bin/viper");
    for (const auto &file : manifest.files) {
        const std::string relPath = mapInstallPath(file, InstallPathPolicy::PortableArchive);
        validatePortableArchivePath(relPath, "AppImage payload path");
        if (file.symlink) {
            tar.addSymlink(relPath, file.symlinkTarget);
        } else {
            const auto data = readFile(file.stagedAbsolutePath.string());
            tar.addFile(relPath, data.data(), data.size(), permissionBitsFor(file));
        }
    }
    if (!manifest.fileAssociations.empty()) {
        std::vector<DataFile> generated;
        addToolchainFileAssociationMetadata(generated, manifest, packageName, "viper");
        for (const auto &df : generated) {
            const std::string portablePath = sanitizePackageRelativePath(
                df.installPath.rfind("usr/", 0) == 0 ? df.installPath.substr(4) : df.installPath,
                "AppImage generated metadata path");
            tar.addFile(portablePath, df.data.data(), df.data.size(), df.mode);
        }
    }
    addToolchainAppImageMetadata(tar, packageName);

    const auto tarBytes = tar.finish();
    const auto tarGz = gzip(tarBytes.data(), tarBytes.size());
    LinuxRuntimeStubParams stub;
    stub.cacheName =
        packageName + "-" + portableArchiveVersionComponent(version) + "-linux-" + manifest.arch;
    stub.entryPath = "AppRun";
    const auto appImage = buildLinuxAppImage(stub, tarGz);
    writeFileAtomic(params.outputPath, appImage);
    std::error_code ec;
    fs::permissions(params.outputPath,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add,
                    ec);
    if (ec)
        throw std::runtime_error("cannot mark AppImage executable: " + ec.message());
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
            tar.addFile(sourceTopDir + sourcePath, file.data.data(), file.data.size(), file.mode);
        }
    }
    const auto tarBytes = tar.finish();
    const auto tarGz = gzip(tarBytes.data(), tarBytes.size());
    const fs::path sourceTar = tmpRoot / "SOURCES" / (packageName + "-" + version + ".tar.gz");
    writeFileAtomic(sourceTar, tarGz);

    std::ostringstream spec;
    spec << "Name: " << packageName << "\n";
    spec << "Version: " << version << "\n";
    spec << "Release: 1%{?dist}\n";
    spec << "Summary: Viper compiler toolchain\n";
    {
        std::string license = trimAsciiWhitespace(manifest.license);
        if (license.empty())
            license = "GPL-3.0-only";
        validateSingleLineField(license, "toolchain package license");
        spec << "License: " << license << "\n";
    }
    if (!manifest.homepage.empty()) {
        validatePackageUrl(manifest.homepage, "toolchain package homepage");
        spec << "URL: " << manifest.homepage << "\n";
    }
    spec << "BuildArch: " << arch << "\n";
    spec << "Source0: %{name}-%{version}.tar.gz\n";
    for (const auto &dep : toolchainRpmRequires(manifest))
        spec << "Requires: " << dep << "\n";
    spec << "\n";
    spec << "%description\nViper compiler toolchain\n\n";
    spec << "%prep\n%setup -q\n\n";
    spec << "%build\n:\n\n";
    spec << "%install\nrm -rf %{buildroot}\nmkdir -p %{buildroot}/usr\ncp -a . "
            "%{buildroot}/usr/\n\n";
    const bool hasManPages = std::any_of(
        manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &entry) {
            return entry.kind == ToolchainFileKind::ManPage;
        });
    const bool hasFileAssociations = !manifest.fileAssociations.empty();
    if (hasManPages || hasFileAssociations) {
        spec << "%post\n";
        if (hasManPages)
            spec << "if command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; fi\n";
        if (hasFileAssociations) {
            spec << "if command -v update-mime-database >/dev/null 2>&1; then update-mime-database "
                    "/usr/share/mime || true; fi\n";
            spec << "if command -v update-desktop-database >/dev/null 2>&1; then "
                    "update-desktop-database /usr/share/applications || true; fi\n\n";
        } else {
            spec << "\n";
        }
        if (hasFileAssociations) {
            spec << "%postun\n";
            if (hasManPages)
                spec << "if command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || true; "
                        "fi\n";
            spec << "if command -v update-mime-database >/dev/null 2>&1; then update-mime-database "
                    "/usr/share/mime || true; fi\n";
            spec << "if command -v update-desktop-database >/dev/null 2>&1; then "
                    "update-desktop-database /usr/share/applications || true; fi\n\n";
        } else if (hasManPages) {
            spec << "%postun\nif command -v mandb >/dev/null 2>&1; then mandb >/dev/null 2>&1 || "
                    "true; fi\n\n";
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
        throw std::runtime_error("rpmbuild failed while generating toolchain rpm:\n" + rr.out +
                                 rr.err);
    }

    const fs::path rpmPath = findGeneratedRpm(tmpRoot, packageName, version, arch);
    std::error_code copyEc;
    fs::copy_file(rpmPath, params.outputPath, fs::copy_options::overwrite_existing, copyEc);
    if (copyEc)
        throw std::runtime_error("cannot copy generated rpm to " + params.outputPath + ": " +
                                 copyEc.message());
}

/// @brief GPG-sign a built Debian (.deb) or RPM (.rpm) package in place.
/// @details rpmsign and dpkg-sig are the standard signing tools and are not present on
///          every host. run_process reports a missing binary via launch_failed, which is
///          surfaced here distinctly from a signing failure.
void signLinuxPackage(const std::string &packagePath, const std::string &gpgKeyId, bool isRpm) {
    if (gpgKeyId.empty())
        throw std::runtime_error("Linux package signing key must not be empty");
    validateSingleLineField(gpgKeyId, "Linux signing key");

    const char *tool = isRpm ? "rpmsign" : "dpkg-sig";
    std::vector<std::string> argv;
    if (isRpm)
        argv = {"rpmsign", "--addsign", "--define", "_gpg_name " + gpgKeyId, packagePath};
    else
        argv = {"dpkg-sig", "--sign", "builder", "-k", gpgKeyId, packagePath};

    const RunResult rr = run_process(argv);
    if (rr.launch_failed) {
        throw std::runtime_error(
            std::string(tool) + " is required to sign " + (isRpm ? "RPM" : "Debian") +
            " packages but was not found on PATH; install " + tool + " or omit --linux-sign-key");
    }
    if (rr.exit_code != 0) {
        throw std::runtime_error(std::string(tool) + " failed to sign " + packagePath +
                                 " (check that GPG key '" + gpgKeyId + "' is available):\n" +
                                 rr.out + rr.err);
    }
}

} // namespace viper::pkg
