//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "ToolchainInstallManifest.hpp"

#include "PkgUtils.hpp"

#include "viper/platform/Capabilities.hpp"
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
        std::error_code ec;
        const fs::path normalized = fs::weakly_canonical(filePath, ec);
        const fs::path effective = ec ? filePath.lexically_normal() : normalized;
        if (!fs::is_regular_file(effective, ec) && !fs::is_symlink(effective, ec))
            continue;
        fs::path rel = fs::relative(effective, normalizedStage, ec);
        if (ec || rel.empty() || rel.native().find("..") == 0)
            continue;
        const std::string relKey = sanitizePackageRelativePath(toForwardSlashes(rel.generic_string()),
                                                               "staged install path");
        if (seen.insert(relKey).second)
            files.push_back(effective);
    }
    return files;
}

std::vector<fs::path> gatherFromStageWalk(const fs::path &stagePrefix) {
    std::vector<fs::path> files;
    std::set<std::string> seen;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(stagePrefix, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file() && !it->is_symlink())
            continue;
        fs::path rel = fs::relative(it->path(), stagePrefix, ec);
        if (ec)
            continue;
        const std::string relKey = sanitizePackageRelativePath(toForwardSlashes(rel.generic_string()),
                                                               "staged install path");
        if (seen.insert(relKey).second)
            files.push_back(it->path());
    }
    return files;
}

ToolchainFileEntry makeEntry(const fs::path &stagePrefix, const fs::path &filePath) {
    std::error_code ec;
    const fs::path rel = fs::relative(filePath, stagePrefix, ec);
    if (ec || rel.empty())
        throw std::runtime_error("failed to compute staged relative path for " + filePath.string());

    ToolchainFileEntry entry;
    entry.stagedAbsolutePath = filePath;
    entry.stagedRelativePath =
        sanitizePackageRelativePath(toForwardSlashes(rel.generic_string()), "staged install path");
    entry.kind = classifyFileKind(entry.stagedRelativePath);
    entry.sizeBytes = static_cast<uint64_t>(fs::file_size(filePath, ec));
    if (ec)
        entry.sizeBytes = 0;
    const std::string lower = lowerCopy(entry.stagedRelativePath);
    entry.executable = entry.kind == ToolchainFileKind::Binary ||
                       (lower.size() > 4 && lower.substr(lower.size() - 4) == ".exe");
    entry.unixMode = unixModeFor(filePath, entry.executable);
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

    if (platform::kBuildHasGraphics &&
        !manifestHasBaseNameKind(manifest, ToolchainFileKind::SupportLibrary, "vipergfx")) {
        throw std::runtime_error("staged toolchain is missing support library vipergfx");
    }
    if (platform::kBuildHasGUI &&
        !manifestHasBaseNameKind(manifest, ToolchainFileKind::SupportLibrary, "vipergui")) {
        throw std::runtime_error("staged toolchain is missing support library vipergui");
    }
    if (platform::kBuildHasAudio &&
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
