//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "ToolchainInstallManifest.hpp"

#include "PkgUtils.hpp"

#include "viper/runtime/RuntimeComponentManifest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace viper::pkg {
namespace {

std::string toForwardSlashes(std::string text) {
    for (char &ch : text) {
        if (ch == '\\')
            ch = '/';
    }
    return text;
}

std::string lowerCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

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

bool isRuntimeArchiveBaseName(const std::string &base) {
    return std::find(runtime_manifest::kRuntimeComponentArchives.begin(),
                     runtime_manifest::kRuntimeComponentArchives.end(),
                     base) != runtime_manifest::kRuntimeComponentArchives.end();
}

bool isSupportLibraryBaseName(const std::string &base) {
    return base == "vipergfx" || base == "vipergui" || base == "viperaud";
}

bool pathWithinStage(const fs::path &stage, const fs::path &path) {
    return isPathWithin(stage, path);
}

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

std::string detectHostArch() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#else
    return "x64";
#endif
}

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

std::vector<fs::path> gatherFromInstallManifest(const fs::path &stagePrefix, const fs::path &installManifestPath) {
    std::ifstream in(installManifestPath);
    if (!in)
        throw std::runtime_error("cannot read install manifest: " + installManifestPath.string());

    std::vector<fs::path> files;
    std::set<std::string> seen;
    const fs::path normalizedStage = fs::weakly_canonical(stagePrefix);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        fs::path filePath = fs::path(line);
        if (filePath.is_relative())
            filePath = normalizedStage / filePath;
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

std::vector<fs::path> gatherFromStageWalk(const fs::path &stagePrefix) {
    std::vector<fs::path> files;
    std::set<std::string> seen;
    std::error_code ec;
    const fs::path normalizedStage = fs::weakly_canonical(stagePrefix);
    for (fs::recursive_directory_iterator it(stagePrefix, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file() && !it->is_symlink())
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
    return files;
}

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

bool manifestHasRelativePath(const ToolchainInstallManifest &manifest, std::string_view relPath) {
    return std::any_of(manifest.files.begin(), manifest.files.end(), [&](const ToolchainFileEntry &entry) {
        return entry.stagedRelativePath == relPath;
    });
}

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

uint64_t ToolchainInstallManifest::totalSizeBytes() const {
    uint64_t total = 0;
    for (const auto &file : files)
        total += file.sizeBytes;
    return total;
}

std::vector<FileAssoc> defaultToolchainFileAssociations() {
    return {
        {".zia", "Zia Source File", "text/x-zia"},
        {".bas", "BASIC Source File", "text/x-basic"},
        {".il", "Viper IL Module", "text/x-viper-il"},
    };
}

ToolchainInstallManifest gatherToolchainInstallManifest(
    const fs::path &stagePrefix,
    std::optional<fs::path> installManifestPath) {
    std::error_code ec;
    const fs::path normalizedStage = fs::weakly_canonical(stagePrefix, ec);
    const fs::path stage = ec ? stagePrefix.lexically_normal() : normalizedStage;
    if (!fs::exists(stage) || !fs::is_directory(stage))
        throw std::runtime_error("staged install prefix does not exist: " + stage.string());

    std::vector<fs::path> files;
    if (installManifestPath && fs::exists(*installManifestPath))
        files = gatherFromInstallManifest(stage, *installManifestPath);
    if (files.empty())
        files = gatherFromStageWalk(stage);

    ToolchainInstallManifest manifest;
    manifest.version = detectManifestVersion(stage);
    manifest.arch = detectHostArch();
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

void validateToolchainInstallManifest(const ToolchainInstallManifest &manifest) {
    validateToolchainArchitecture(manifest.arch);
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
            return rel.empty() ? "/usr" : "/usr/" + rel;
        case InstallPathPolicy::PortableArchive:
        default:
            return rel;
    }
}

} // namespace viper::pkg
