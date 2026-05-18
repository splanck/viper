//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/ToolchainInstallManifest.cpp
// Purpose: Gather and validate the Viper toolchain staged install tree, classify
//          each file by kind, and map files to platform-specific install paths.
//
// Key invariants:
//   - Paths are always normalized via sanitizePackageRelativePath before use.
//   - Symlinks that escape the stage prefix are rejected at scan time.
//   - The viper binary, CMake config files, and all runtime archives must be
//     present for validateToolchainInstallManifest to succeed.
//
// Ownership/Lifetime:
//   - ToolchainInstallManifest owns its file entries; no external references.
//
// Links: ToolchainInstallManifest.hpp, PkgUtils.hpp,
//        viper/runtime/RuntimeComponentManifest.hpp
//
//===----------------------------------------------------------------------===//

#include "ToolchainInstallManifest.hpp"

#include "PkgUtils.hpp"

#include "viper/runtime/RuntimeComponentManifest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace viper::pkg {
namespace {

/// @brief Replace every backslash in text with a forward slash.
/// Normalizes Windows-style paths from cmake_install.cmake to POSIX form.
std::string toForwardSlashes(std::string text) {
    for (char &ch : text) {
        if (ch == '\\')
            ch = '/';
    }
    return text;
}

/// @brief Return a copy of text with all ASCII letters lowercased.
std::string lowerCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

/// @brief Strip the platform-specific extension and "lib" prefix from a filename to
/// get the canonical base name used for manifest lookups.
/// e.g. "libvipergfx.a" → "vipergfx", "viper.exe" → "viper".
std::string toolchainBaseNameFromFilename(std::string filename) {
    filename = lowerCopy(filename);
    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".lib")
        filename.resize(filename.size() - 4);
    else if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".exe")
        filename.resize(filename.size() - 4);
    else if (filename.size() > 2 && filename.substr(filename.size() - 2) == ".a")
        filename.resize(filename.size() - 2);
    if (filename.rfind("lib", 0) == 0)
        filename.erase(0, 3);
    return filename;
}

/// @brief Return true if base (after toolchainBaseNameFromFilename) matches a known
/// Viper runtime component archive as listed in RuntimeComponentManifest.hpp.
bool isRuntimeArchiveBaseName(const std::string &base) {
    return std::find(runtime_manifest::kRuntimeComponentArchives.begin(),
                     runtime_manifest::kRuntimeComponentArchives.end(),
                     base) != runtime_manifest::kRuntimeComponentArchives.end();
}

/// @brief Return true if base is a Viper optional support library (graphics, GUI, audio).
bool isSupportLibraryBaseName(const std::string &base) {
    return base == "vipergfx" || base == "vipergui" || base == "viperaud";
}

/// @brief Thin wrapper around isPathWithin for stage-prefix boundary checks.
bool pathWithinStage(const fs::path &stage, const fs::path &path) {
    return isPathWithin(stage, path);
}

/// @brief Verify that path (after resolving any symlink) lies within stagePrefix.
/// Throws std::runtime_error if the path escapes the stage boundary or if
/// canonical resolution fails (e.g. dangling symlink).
void validateStagedPathDoesNotEscape(const fs::path &stagePrefix, const fs::path &path) {
    std::error_code ec;
    if (fs::is_symlink(path, ec)) {
        const fs::path resolved = fs::canonical(path, ec);
        if (ec)
            throw std::runtime_error("cannot resolve staged symlink: " + path.string());
        if (!pathWithinStage(stagePrefix, resolved))
            throw std::runtime_error("staged symlink escapes install prefix: " + path.string());
        return;
    }

    const fs::path resolved = fs::canonical(path, ec);
    if (ec)
        throw std::runtime_error("cannot resolve staged file: " + path.string());
    if (!pathWithinStage(stagePrefix, resolved))
        throw std::runtime_error("staged file escapes install prefix: " + path.string());
}

/// @brief Compute path's lexical relative path from stagePrefix without touching the
/// filesystem. Throws if the result would start with ".." (escapes the prefix)
/// or if the paths have no common root.
fs::path stagedLexicalRelativePath(const fs::path &stagePrefix, const fs::path &path) {
    const fs::path normalizedStage = stagePrefix.lexically_normal();
    const fs::path normalizedPath = path.lexically_normal();
    const fs::path rel = normalizedPath.lexically_relative(normalizedStage);
    if (rel.empty() || rel == fs::path("."))
        throw std::runtime_error("failed to compute staged relative path for " + path.string());
    auto relIt = rel.begin();
    if (relIt == rel.end() || *relIt == fs::path(".."))
        throw std::runtime_error("staged path escapes install prefix: " + path.string());
    return rel;
}

/// @brief Read the real POSIX permission bits from the filesystem and return them as
/// a USTAR-compatible uint32_t (regular file type bits ORed in). Falls back to
/// 0100755 or 0100644 if stat fails so the archive is still well-formed.
uint32_t unixModeFor(const fs::path &path, bool executable) {
    std::error_code ec;
    const fs::file_status status = fs::status(path, ec);
    if (ec)
        return executable ? 0100755u : 0100644u;

    using perms = fs::perms;
    uint32_t mode = fs::is_directory(status) ? 0040000u : 0100000u;
    const auto p = status.permissions();
    if ((p & perms::owner_read) != perms::none)
        mode |= 0400u;
    if ((p & perms::owner_write) != perms::none)
        mode |= 0200u;
    if ((p & perms::owner_exec) != perms::none)
        mode |= 0100u;
    if ((p & perms::group_read) != perms::none)
        mode |= 0040u;
    if ((p & perms::group_write) != perms::none)
        mode |= 0020u;
    if ((p & perms::group_exec) != perms::none)
        mode |= 0010u;
    if ((p & perms::others_read) != perms::none)
        mode |= 0004u;
    if ((p & perms::others_write) != perms::none)
        mode |= 0002u;
    if ((p & perms::others_exec) != perms::none)
        mode |= 0001u;
    return mode;
}

/// @brief Scan text for a version string using two heuristic patterns:
/// CMake: set(PACKAGE_VERSION "<version>"), C++ header: #define VIPER_VERSION_STR "<version>".
/// Returns the extracted version or empty string if neither pattern is found.
std::string parseVersionFromText(const std::string &text) {
    const char *patterns[] = {"set(PACKAGE_VERSION \"", "#define VIPER_VERSION_STR \""};
    for (const char *pattern : patterns) {
        const std::size_t start = text.find(pattern);
        if (start == std::string::npos)
            continue;
        const std::size_t valueStart = start + std::char_traits<char>::length(pattern);
        const std::size_t end = text.find('"', valueStart);
        if (end != std::string::npos && end > valueStart)
            return text.substr(valueStart, end - valueStart);
    }
    return {};
}

/// @brief Probe well-known files in the staged install tree to infer the toolchain
/// version without requiring a separate manifest argument. Tries CMake config
/// first, then the C++ version header. Returns empty if neither file exists.
std::string detectManifestVersion(const fs::path &stagePrefix) {
    const fs::path versionCandidates[] = {
        stagePrefix / "lib" / "cmake" / "Viper" / "ViperConfigVersion.cmake",
        stagePrefix / "include" / "viper" / "version.hpp",
    };
    for (const auto &candidate : versionCandidates) {
        std::error_code ec;
        if (!fs::exists(candidate, ec))
            continue;
        std::ifstream in(candidate, std::ios::binary);
        if (!in)
            continue;
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string version = parseVersionFromText(ss.str());
        if (!version.empty())
            return version;
    }
    return {};
}

/// @brief Return the canonical architecture string for the host CPU. Used when no
/// explicit arch is recorded in the staged manifest. Throws for unsupported CPUs
/// so callers get an early diagnostic rather than silently packaging the wrong arch.
std::string detectHostArch() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
    return "x64";
#else
    throw std::runtime_error("unsupported host CPU for toolchain packaging; use a staged "
                             "toolchain with a recognized native viper binary");
#endif
}

/// @brief Return the canonical platform string for the host OS ("windows", "macos",
/// or "linux"). Like detectHostArch, throws immediately for unrecognized hosts.
std::string detectHostPlatform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    throw std::runtime_error("unsupported host platform for toolchain packaging; use a staged "
                             "toolchain with a recognized native viper binary");
#endif
}

/// @brief Map a staged relative path to a ToolchainFileKind by inspecting its directory
/// prefix, then its lowercased filename base when the prefix alone is ambiguous
/// (lib/ can contain runtime archives, support libs, or generic libraries).
/// The order of checks matters: more specific prefixes must come before general ones.
ToolchainFileKind classifyFileKind(const std::string &relativePath) {
    const std::string rel = lowerCopy(relativePath);
    const fs::path relPath(relativePath);
    const std::string filenameBase = toolchainBaseNameFromFilename(relPath.filename().string());

    if (rel.rfind("bin/", 0) == 0)
        return ToolchainFileKind::Binary;
    if (rel.rfind("include/", 0) == 0)
        return ToolchainFileKind::Header;
    if (rel.rfind("lib/cmake/viper/", 0) == 0)
        return ToolchainFileKind::CMakeConfig;
    if (rel.rfind("share/man/", 0) == 0)
        return ToolchainFileKind::ManPage;
    if (rel == "license" || rel == "readme.md" || rel.rfind("share/doc/", 0) == 0)
        return ToolchainFileKind::Doc;
    if (rel.rfind("share/viper/", 0) == 0 || rel.rfind("share/", 0) == 0)
        return ToolchainFileKind::Extra;
    if ((rel.rfind("lib/", 0) == 0 || rel.find('/') == std::string::npos) &&
        isRuntimeArchiveBaseName(filenameBase)) {
        return ToolchainFileKind::RuntimeArchive;
    }
    if ((rel.rfind("lib/", 0) == 0 || rel.find('/') == std::string::npos) &&
        isSupportLibraryBaseName(filenameBase)) {
        return ToolchainFileKind::SupportLibrary;
    }
    if (rel.rfind("lib/", 0) == 0)
        return ToolchainFileKind::Library;
    return ToolchainFileKind::Extra;
}

/// @brief Return path's lexical relative path from prefix, or std::nullopt if path is
/// not under prefix. Used to remap cmake_install.cmake paths that were written
/// with a build-time alias prefix instead of the final staging root.
std::optional<fs::path> lexicalRelativeIfUnder(const fs::path &prefix, const fs::path &path) {
    const fs::path rel = path.lexically_normal().lexically_relative(prefix.lexically_normal());
    if (rel.empty() || rel == fs::path("."))
        return std::nullopt;
    auto it = rel.begin();
    if (it == rel.end() || *it == fs::path(".."))
        return std::nullopt;
    return rel;
}

/// @brief Parse cmake_install.cmake's install_manifest.txt and return the absolute
/// paths to all regular files and symlinks it lists. Handles both absolute paths
/// (emitted by cmake) and paths written with a build-alias prefix by remapping
/// them through stageAlias → stagePrefix. Duplicate entries are silently dropped.
std::vector<fs::path> gatherFromInstallManifest(const fs::path &stagePrefix,
                                                const fs::path &stageAliasPrefix,
                                                const fs::path &installManifestPath) {
    std::ifstream in(installManifestPath);
    if (!in)
        throw std::runtime_error("cannot read install manifest: " + installManifestPath.string());

    std::vector<fs::path> files;
    std::set<std::string> seen;
    const fs::path normalizedStage = fs::weakly_canonical(stagePrefix);
    const fs::path normalizedAlias = stageAliasPrefix.lexically_normal();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        fs::path filePath = fs::path(line);
        if (filePath.is_relative()) {
            filePath = normalizedStage / filePath;
        } else if (auto relViaAlias = lexicalRelativeIfUnder(normalizedAlias, filePath)) {
            filePath = normalizedStage / *relViaAlias;
        }
        filePath = filePath.lexically_normal();
        std::error_code ec;
        if (!fs::is_regular_file(filePath, ec) && !fs::is_symlink(filePath, ec))
            continue;
        validateStagedPathDoesNotEscape(normalizedStage, filePath);
        fs::path rel;
        try {
            rel = stagedLexicalRelativePath(normalizedStage, filePath);
        } catch (const std::runtime_error &) {
            continue;
        }
        const std::string relKey = sanitizePackageRelativePath(toForwardSlashes(rel.generic_string()),
                                                               "staged install path");
        if (seen.insert(relKey).second)
            files.push_back(filePath);
    }
    return files;
}

/// @brief Fallback when no install manifest is provided: recursively enumerate all
/// regular files and symlinks under stagePrefix. Traversal errors are fatal so
/// packaging cannot silently omit unreadable files from a release installer.
std::vector<fs::path> gatherFromStageWalk(const fs::path &stagePrefix) {
    std::vector<fs::path> files;
    std::set<std::string> seen;
    std::error_code ec;
    const fs::path normalizedStage = fs::weakly_canonical(stagePrefix);
    for (fs::recursive_directory_iterator it(stagePrefix, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec)
            throw std::runtime_error("cannot enumerate staged install tree: " + ec.message());
        std::error_code typeEc;
        const bool regular = it->is_regular_file(typeEc);
        if (typeEc)
            throw std::runtime_error("cannot inspect staged path: " + it->path().string());
        const bool symlink = it->is_symlink(typeEc);
        if (typeEc)
            throw std::runtime_error("cannot inspect staged path: " + it->path().string());
        if (!regular && !symlink)
            continue;
        validateStagedPathDoesNotEscape(normalizedStage, it->path());
        fs::path rel;
        try {
            rel = stagedLexicalRelativePath(normalizedStage, it->path());
        } catch (const std::runtime_error &) {
            continue;
        }
        const std::string relKey = sanitizePackageRelativePath(toForwardSlashes(rel.generic_string()),
                                                               "staged install path");
        if (seen.insert(relKey).second)
            files.push_back(it->path());
    }
    if (ec)
        throw std::runtime_error("cannot enumerate staged install tree: " + ec.message());
    return files;
}

/// @brief Construct a ToolchainFileEntry for filePath by computing its relative path,
/// classifying its kind, reading its POSIX mode, and — for symlinks — rebasing
/// absolute link targets to relative ones so they stay valid after installation.
ToolchainFileEntry makeEntry(const fs::path &stagePrefix, const fs::path &filePath) {
    std::error_code ec;
    const fs::path rel = stagedLexicalRelativePath(stagePrefix, filePath);

    ToolchainFileEntry entry;
    entry.stagedAbsolutePath = filePath;
    entry.stagedRelativePath =
        sanitizePackageRelativePath(toForwardSlashes(rel.generic_string()), "staged install path");
    entry.kind = classifyFileKind(entry.stagedRelativePath);
    entry.symlink = fs::is_symlink(filePath, ec);
    ec.clear();
    if (entry.symlink) {
        const fs::path rawTarget = fs::read_symlink(filePath, ec);
        if (ec)
            throw std::runtime_error("cannot read staged symlink target: " + filePath.string());
        const fs::path resolved = fs::canonical(filePath, ec);
        if (ec)
            throw std::runtime_error("cannot resolve staged symlink: " + filePath.string());
        const fs::path parent = fs::canonical(filePath.parent_path(), ec);
        if (ec)
            throw std::runtime_error("cannot resolve staged symlink parent: " + filePath.string());
        if (rawTarget.is_absolute()) {
            entry.symlinkTarget =
                toForwardSlashes(fs::relative(resolved, parent, ec).generic_string());
            if (ec)
                throw std::runtime_error("cannot compute staged symlink target: " +
                                         filePath.string());
        } else {
            entry.symlinkTarget = toForwardSlashes(rawTarget.generic_string());
        }
        validateSingleLineField(entry.symlinkTarget, "staged symlink target");
        if (entry.symlinkTarget.empty() || entry.symlinkTarget.front() == '/' ||
            (entry.symlinkTarget.size() >= 2 &&
             std::isalpha(static_cast<unsigned char>(entry.symlinkTarget[0])) &&
             entry.symlinkTarget[1] == ':')) {
            throw std::runtime_error("staged symlink target must be relative: " +
                                     filePath.string());
        }
        entry.sizeBytes = 0;
    } else {
        entry.sizeBytes = static_cast<uint64_t>(fs::file_size(filePath, ec));
        if (ec)
            entry.sizeBytes = 0;
    }
    const std::string lower = lowerCopy(entry.stagedRelativePath);
    entry.executable = entry.kind == ToolchainFileKind::Binary ||
                       (lower.size() > 4 && lower.substr(lower.size() - 4) == ".exe");
    entry.unixMode = entry.symlink ? 0120777u : unixModeFor(filePath, entry.executable);
    return entry;
}

/// @brief Return true if the manifest contains an entry whose stagedRelativePath equals
/// relPath. Used by validateToolchainInstallManifest to check for specific
/// required files like ViperConfig.cmake and ViperTargets.cmake.
bool manifestHasRelativePath(const ToolchainInstallManifest &manifest, std::string_view relPath) {
    const std::string needle = lowerCopy(std::string(relPath));
    return std::any_of(manifest.files.begin(), manifest.files.end(), [&](const ToolchainFileEntry &entry) {
        return lowerCopy(entry.stagedRelativePath) == needle;
    });
}

/// @brief Return true if any manifest entry has both the given kind and the given base
/// name (after stripping extension and "lib" prefix). Used to detect that all
/// required runtime archives and support libraries are present regardless of
/// platform-specific naming ("librt_core.a" vs "rt_core.lib").
bool manifestHasBaseNameKind(const ToolchainInstallManifest &manifest,
                             ToolchainFileKind kind,
                             std::string_view baseName) {
    return std::any_of(manifest.files.begin(), manifest.files.end(), [&](const ToolchainFileEntry &entry) {
        if (entry.kind != kind)
            return false;
        return toolchainBaseNameFromFilename(fs::path(entry.stagedRelativePath).filename().string()) ==
               baseName;
    });
}

/// @brief Scan every CMakeConfig entry in the manifest for needle (case-insensitive).
/// Used to detect optional support-library components declared in ViperTargets.cmake
/// so validation can require their corresponding library archives to be present.
bool stagedCMakeMetadataMentions(const ToolchainInstallManifest &manifest, std::string_view needle) {
    const std::string lowerNeedle = lowerCopy(std::string(needle));
    for (const auto &entry : manifest.files) {
        if (entry.kind != ToolchainFileKind::CMakeConfig || entry.symlink)
            continue;
        std::ifstream in(entry.stagedAbsolutePath, std::ios::binary);
        if (!in)
            continue;
        std::ostringstream ss;
        ss << in.rdbuf();
        if (lowerCopy(ss.str()).find(lowerNeedle) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

/// @brief Return the sum of sizeBytes across all non-symlink entries. Used by package
/// builders to populate the "Installed-Size" field in control files and to
/// estimate required disk space in installer UI dialogs.
uint64_t ToolchainInstallManifest::totalSizeBytes() const {
    uint64_t total = 0;
    for (const auto &file : files) {
        if (file.sizeBytes > std::numeric_limits<uint64_t>::max() - total)
            throw std::overflow_error("toolchain manifest total size overflow");
        total += file.sizeBytes;
    }
    return total;
}

/// @brief Return the canonical set of file type associations for the Viper toolchain:
/// .zia (Zia source), .bas (BASIC source), .il (Viper IL module). These are
/// registered with the OS by all platform package builders (deb, pkg, msi).
std::vector<FileAssoc> defaultToolchainFileAssociations() {
    return {
        {".zia", "Zia Source File", "text/x-zia", ""},
        {".bas", "BASIC Source File", "text/x-basic", ""},
        {".il", "Viper IL Module", "text/x-viper-il", ""},
    };
}

/// @brief Walk stagePrefix and build a ToolchainInstallManifest.
/// If installManifestPath names a cmake install_manifest.txt, its listed files
/// are used directly (faster, avoids scanning the entire tree). If that path is
/// absent or yields no files, falls back to a full recursive walk. After gathering,
/// each entry is classified, validated against stage-escape rules, and the
/// manifest is sorted by stagedRelativePath before being returned.
ToolchainInstallManifest gatherToolchainInstallManifest(
    const fs::path &stagePrefix,
    std::optional<fs::path> installManifestPath) {
    std::error_code ec;
    const fs::path absoluteStagePrefix = fs::absolute(stagePrefix, ec);
    const fs::path stageAlias = (ec ? stagePrefix : absoluteStagePrefix).lexically_normal();
    ec.clear();
    const fs::path normalizedStage = fs::weakly_canonical(stagePrefix, ec);
    const fs::path stage = ec ? stagePrefix.lexically_normal() : normalizedStage;
    if (!fs::exists(stage) || !fs::is_directory(stage))
        throw std::runtime_error("staged install prefix does not exist: " + stage.string());

    std::vector<fs::path> files;
    if (installManifestPath && fs::exists(*installManifestPath))
        files = gatherFromInstallManifest(stage, stageAlias, *installManifestPath);
    if (files.empty())
        files = gatherFromStageWalk(stage);

    ToolchainInstallManifest manifest;
    manifest.version = detectManifestVersion(stage);
    manifest.arch = detectHostArch();
    manifest.platform = detectHostPlatform();
    manifest.fileAssociations = defaultToolchainFileAssociations();
    manifest.files.reserve(files.size());
    for (const auto &file : files)
        manifest.files.push_back(makeEntry(stage, file));

    std::sort(manifest.files.begin(), manifest.files.end(), [](const ToolchainFileEntry &a,
                                                               const ToolchainFileEntry &b) {
        return a.stagedRelativePath < b.stagedRelativePath;
    });

    validateToolchainInstallManifest(manifest);
    return manifest;
}

/// @brief Validate that the manifest contains a complete, shippable toolchain.
/// Checks arch/platform/version strings, required binaries and cmake configs, all
/// runtime archives from RuntimeComponentManifest, and any support libraries
/// (gfx/gui/aud) referenced by CMake metadata. Throws on the first violation.
void validateToolchainInstallManifest(const ToolchainInstallManifest &manifest) {
    validateToolchainPlatform(manifest.platform);
    if (manifest.arch == "universal") {
        if (manifest.platform != "macos") {
            throw std::runtime_error("universal toolchain architecture is only valid for macOS "
                                     "toolchain manifests");
        }
    } else {
        validateToolchainArchitecture(manifest.arch);
    }
    validateDebVersion(manifest.version, "toolchain package version");
    validatePackageFileAssociations(manifest.fileAssociations);

    auto hasBinary = [&](const char *nameNoExt) {
        return std::any_of(manifest.files.begin(), manifest.files.end(), [&](const ToolchainFileEntry &entry) {
            if (entry.kind != ToolchainFileKind::Binary)
                return false;
            const std::string base = toolchainBaseNameFromFilename(
                fs::path(entry.stagedRelativePath).filename().string());
            return base == nameNoExt;
        });
    };

    if (!hasBinary("viper"))
        throw std::runtime_error("staged toolchain is missing the viper binary");
    if (!manifestHasRelativePath(manifest, "lib/cmake/Viper/ViperConfig.cmake"))
        throw std::runtime_error("staged toolchain is missing lib/cmake/Viper/ViperConfig.cmake");
    if (!manifestHasRelativePath(manifest, "lib/cmake/Viper/ViperTargets.cmake"))
        throw std::runtime_error("staged toolchain is missing lib/cmake/Viper/ViperTargets.cmake");

    for (std::string_view archive : runtime_manifest::kRuntimeComponentArchives) {
        if (!manifestHasBaseNameKind(manifest, ToolchainFileKind::RuntimeArchive, archive)) {
            throw std::runtime_error("staged toolchain is missing runtime archive " +
                                     std::string(archive));
        }
    }

    if (stagedCMakeMetadataMentions(manifest, "vipergfx") &&
        !manifestHasBaseNameKind(manifest, ToolchainFileKind::SupportLibrary, "vipergfx")) {
        throw std::runtime_error("staged toolchain is missing support library vipergfx");
    }
    if (stagedCMakeMetadataMentions(manifest, "vipergui") &&
        !manifestHasBaseNameKind(manifest, ToolchainFileKind::SupportLibrary, "vipergui")) {
        throw std::runtime_error("staged toolchain is missing support library vipergui");
    }
    if (stagedCMakeMetadataMentions(manifest, "viperaud") &&
        !manifestHasBaseNameKind(manifest, ToolchainFileKind::SupportLibrary, "viperaud")) {
        throw std::runtime_error("staged toolchain is missing support library viperaud");
    }
}

/// @brief Map a file's staged relative path to its final install destination under policy.
/// Windows: "bin/viper.exe" → "C:\Program Files\Viper\bin\viper.exe";
/// macOS: "bin/viper" → "/usr/local/viper/bin/viper";
/// Linux: "bin/viper" → "/usr/bin/viper" (FHS merge); PortableArchive: unchanged.
std::string mapInstallPath(const ToolchainFileEntry &file, InstallPathPolicy policy) {
    const std::string rel = sanitizePackageRelativePath(file.stagedRelativePath, "staged install path");
    switch (policy) {
        case InstallPathPolicy::WindowsProgramFilesRoot: {
            std::string path = "C:\\Program Files\\Viper";
            if (!rel.empty()) {
                path.push_back('\\');
                for (char ch : rel)
                    path.push_back(ch == '/' ? '\\' : ch);
            }
            return path;
        }
        case InstallPathPolicy::MacOSUsrLocalViperRoot:
            return rel.empty() ? "/usr/local/viper" : "/usr/local/viper/" + rel;
        case InstallPathPolicy::LinuxUsrRoot:
            if (file.kind == ToolchainFileKind::Doc && rel.rfind("share/doc/", 0) != 0) {
                const std::string name = fs::path(rel).filename().generic_string();
                return name.empty() ? "/usr/share/doc/viper" : "/usr/share/doc/viper/" + name;
            }
            return rel.empty() ? "/usr" : "/usr/" + rel;
        case InstallPathPolicy::PortableArchive:
        default:
            return rel;
    }
}

} // namespace viper::pkg
