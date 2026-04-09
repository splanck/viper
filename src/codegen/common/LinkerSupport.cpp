//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/LinkerSupport.cpp
// Purpose: Implementation of shared linker utilities.
// Cross-platform touchpoints: runtime archive discovery, system-library
//                             naming, tool invocation, and host linker flags.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/LinkerSupport.hpp"

#include "common/RunProcess.hpp"

#include <cctype>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdlib.h>
#else
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#endif

namespace viper::codegen::common {
namespace {

#ifdef _WIN32
std::string archiveFileName(std::string_view libBaseName) {
    return std::string(libBaseName) + ".lib";
}
#else
std::string archiveFileName(std::string_view libBaseName) {
    return "lib" + std::string(libBaseName) + ".a";
}
#endif

bool dirHasArchiveProbe(const std::filesystem::path &dir) {
    return fileExists(dir / archiveFileName("viper_rt_base"));
}

std::optional<std::filesystem::path> installedLibraryPath(std::string_view libBaseName) {
    if (const auto installedLibDir = findInstalledLibDir())
        return *installedLibDir / archiveFileName(libBaseName);
    return std::nullopt;
}

std::filesystem::path fallbackSupportLibraryPath(std::string_view libBaseName) {
#ifdef _WIN32
    if (libBaseName == "vipergui")
        return std::filesystem::path("src") / "lib" / "gui" / archiveFileName(libBaseName);
    return std::filesystem::path("lib") / archiveFileName(libBaseName);
#else
    if (libBaseName == "vipergui")
        return std::filesystem::path("src") / "lib" / "gui" / archiveFileName(libBaseName);
    return std::filesystem::path("lib") / archiveFileName(libBaseName);
#endif
}

std::filesystem::path buildTreeSupportLibraryPath(const std::filesystem::path &buildDir,
                                                  std::string_view libBaseName) {
    auto pickFirstExisting =
        [](std::initializer_list<std::filesystem::path> candidates) -> std::filesystem::path {
        for (const auto &candidate : candidates) {
            if (fileExists(candidate))
                return candidate;
        }
        return std::filesystem::path{};
    };

#ifdef _WIN32
    if (libBaseName == "vipergui") {
        return pickFirstExisting({buildDir / "src/lib/gui/Release" / archiveFileName(libBaseName),
                                  buildDir / "src/lib/gui/Debug" / archiveFileName(libBaseName),
                                  buildDir / "src/lib/gui" / archiveFileName(libBaseName)});
    }
    return pickFirstExisting({buildDir / "lib/Release" / archiveFileName(libBaseName),
                              buildDir / "lib/Debug" / archiveFileName(libBaseName),
                              buildDir / "lib" / archiveFileName(libBaseName)});
#else
    if (libBaseName == "vipergui")
        return buildDir / "src" / "lib" / "gui" / archiveFileName(libBaseName);
    return buildDir / "lib" / archiveFileName(libBaseName);
#endif
}

std::vector<std::filesystem::path> standardInstalledLibDirs() {
    std::vector<std::filesystem::path> dirs;
#if defined(__APPLE__)
    dirs.emplace_back("/usr/local/viper/lib");
#elif !defined(_WIN32)
    dirs.emplace_back("/usr/lib");
    dirs.emplace_back("/usr/local/lib");
    dirs.emplace_back("/usr/lib64");
    dirs.emplace_back("/usr/local/lib64");
#endif
    return dirs;
}

} // namespace

// =========================================================================
// Pure utilities
// =========================================================================

bool fileExists(const std::filesystem::path &path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool readFileToString(const std::filesystem::path &path, std::string &dst) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    dst = ss.str();
    return true;
}

bool writeTextFile(const std::filesystem::path &path, std::string_view text, std::ostream &err) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        err << "error: unable to open '" << path.string() << "' for writing\n";
        return false;
    }
    out << text;
    if (!out) {
        err << "error: failed to write file '" << path.string() << "'\n";
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> findBuildDir() {
    std::error_code ec;
    std::filesystem::path cur = std::filesystem::current_path(ec);
    if (!ec) {
        for (int depth = 0; depth < 8; ++depth) {
            if (fileExists(cur / "CMakeCache.txt"))
                return cur;
            if (!cur.has_parent_path())
                break;
            cur = cur.parent_path();
        }
    }

    const std::filesystem::path defaultBuild = std::filesystem::path("build");
    if (fileExists(defaultBuild / "CMakeCache.txt"))
        return defaultBuild;

    return std::nullopt;
}

std::optional<std::filesystem::path> currentExecutablePath() {
#if defined(_WIN32)
    std::wstring buf(MAX_PATH, L'\0');
    DWORD len = 0;
    while (true) {
        len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0)
            return std::nullopt;
        if (len < buf.size() - 1)
            break;
        buf.resize(buf.size() * 2);
    }
    buf.resize(len);
    return std::filesystem::path(buf);
#elif defined(__APPLE__)
    uint32_t size = 0;
    if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0)
        return std::nullopt;
    std::vector<char> raw(size + 1, '\0');
    if (_NSGetExecutablePath(raw.data(), &size) != 0)
        return std::nullopt;
    char resolved[PATH_MAX];
    if (!realpath(raw.data(), resolved))
        return std::nullopt;
    return std::filesystem::path(resolved);
#else
    char resolved[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", resolved, sizeof(resolved) - 1);
    if (len <= 0)
        return std::nullopt;
    resolved[len] = '\0';
    return std::filesystem::path(resolved);
#endif
}

std::optional<std::filesystem::path> findInstalledLibDir() {
    if (const char *env = std::getenv("VIPER_LIB_PATH")) {
        std::filesystem::path candidate(env);
        if (fileExists(candidate) && !std::filesystem::is_directory(candidate))
            candidate = candidate.parent_path();
        if (!candidate.empty() && dirHasArchiveProbe(candidate))
            return candidate;
    }

    if (const auto exePath = currentExecutablePath()) {
        std::error_code ec;
        std::filesystem::path exeDir = exePath->parent_path();
        std::filesystem::path candidate = std::filesystem::weakly_canonical(exeDir / ".." / "lib", ec);
        if (!ec && dirHasArchiveProbe(candidate))
            return candidate;
    }

    for (const auto &candidate : standardInstalledLibDirs()) {
        if (dirHasArchiveProbe(candidate))
            return candidate;
    }

    return std::nullopt;
}

std::unordered_set<std::string> parseRuntimeSymbols(std::string_view text) {
    auto isIdent = [](unsigned char c) -> bool { return std::isalnum(c) || c == '_'; };
    // Namespace-qualified symbols also contain dots.
    auto isNsIdent = [](unsigned char c) -> bool {
        return std::isalnum(c) || c == '_' || c == '.';
    };

    std::unordered_set<std::string> symbols;

    // Pass 1: Scan for rt_* symbols (existing logic).
    for (std::size_t i = 0; i + 3 < text.size(); ++i) {
        std::size_t start = std::string_view::npos;
        std::size_t boundary = std::string_view::npos;
        if (text[i] == 'r' && text[i + 1] == 't' && text[i + 2] == '_') {
            start = i;
            boundary = (start == 0) ? std::string_view::npos : (start - 1);
        } else if (text[i] == '_' && text[i + 1] == 'r' && text[i + 2] == 't' &&
                   text[i + 3] == '_') {
            start = i + 1;
            boundary = (i == 0) ? std::string_view::npos : (i - 1);
        }

        if (start == std::string_view::npos)
            continue;
        if (boundary != std::string_view::npos &&
            isIdent(static_cast<unsigned char>(text[boundary])))
            continue;

        std::size_t j = start;
        while (j < text.size() && isIdent(static_cast<unsigned char>(text[j])))
            ++j;

        if (j > start)
            symbols.emplace(text.substr(start, j - start));
        i = j;
    }

    // Pass 2: Scan for Viper.* namespace-qualified symbols (OOP-style IL).
    // These appear as _Viper.Terminal.PrintStr or Viper.Collections.List.Add
    // in the emitted assembly.
    static constexpr std::string_view kViperPrefix = "Viper.";
    for (std::size_t i = 0; i + kViperPrefix.size() < text.size(); ++i) {
        // Match "Viper." or "_Viper." at a word boundary.
        std::size_t start = std::string_view::npos;
        if (text.substr(i, kViperPrefix.size()) == kViperPrefix) {
            if (i == 0 || !isIdent(static_cast<unsigned char>(text[i - 1])))
                start = i;
        } else if (text[i] == '_' && i + 1 + kViperPrefix.size() <= text.size() &&
                   text.substr(i + 1, kViperPrefix.size()) == kViperPrefix) {
            if (i == 0 || !isIdent(static_cast<unsigned char>(text[i - 1])))
                start = i + 1; // Skip the leading underscore
        }

        if (start == std::string_view::npos)
            continue;

        // Read the full namespace-qualified identifier (alphanumeric, _, .).
        std::size_t j = start;
        while (j < text.size() && isNsIdent(static_cast<unsigned char>(text[j])))
            ++j;

        if (j > start)
            symbols.emplace(text.substr(start, j - start));
        i = j;
    }

    return symbols;
}

std::filesystem::path runtimeArchivePath(const std::filesystem::path &buildDir,
                                         std::string_view libBaseName) {
    const std::string libName = archiveFileName(libBaseName);
    if (const auto installedPath = installedLibraryPath(libBaseName); installedPath &&
                                                               fileExists(*installedPath)) {
        return *installedPath;
    }
#ifdef _WIN32
    const std::string objLibName = std::string(libBaseName) + "_obj.lib";

    auto pickFirstExisting =
        [](std::initializer_list<std::filesystem::path> candidates) -> std::filesystem::path {
        for (const auto &candidate : candidates) {
            if (fileExists(candidate))
                return candidate;
        }
        return candidates.size() ? *candidates.begin() : std::filesystem::path{};
    };
#endif
#ifdef _WIN32
    if (!buildDir.empty()) {
#if defined(NDEBUG)
        return pickFirstExisting(
            {buildDir / "src/runtime" / (std::string(libBaseName) + "_obj.dir") / "Release" /
                 objLibName,
             buildDir / "src/runtime" / (std::string(libBaseName) + "_obj.dir") / "Debug" /
                 objLibName,
             buildDir / "src/runtime/Release" / libName,
             buildDir / "src/runtime/Debug" / libName,
             buildDir / "src/runtime" / libName});
#else
        return pickFirstExisting(
            {buildDir / "src/runtime" / (std::string(libBaseName) + "_obj.dir") / "Debug" /
                 objLibName,
             buildDir / "src/runtime" / (std::string(libBaseName) + "_obj.dir") / "Release" /
                 objLibName,
             buildDir / "src/runtime/Debug" / libName,
             buildDir / "src/runtime/Release" / libName,
             buildDir / "src/runtime" / libName});
#endif
    }
#else
    if (!buildDir.empty())
        return buildDir / "src/runtime" / libName;
#endif
    if (const auto installedPath = installedLibraryPath(libBaseName))
        return *installedPath;
    return std::filesystem::path("src/runtime") / libName;
}

std::filesystem::path supportLibraryPath(const std::filesystem::path &buildDir,
                                         std::string_view libBaseName) {
    if (const auto installedPath = installedLibraryPath(libBaseName); installedPath &&
                                                               fileExists(*installedPath)) {
        return *installedPath;
    }
    if (!buildDir.empty()) {
        const auto path = buildTreeSupportLibraryPath(buildDir, libBaseName);
        if (!path.empty())
            return path;
    }
    if (const auto installedPath = installedLibraryPath(libBaseName))
        return *installedPath;
    return fallbackSupportLibraryPath(libBaseName);
}

// =========================================================================
// Link context
// =========================================================================

bool hasComponent(const LinkContext &ctx, RtComponent c) {
    for (auto rc : ctx.requiredComponents)
        if (rc == c)
            return true;
    return false;
}

/// @brief Common implementation: resolve components, discover archives, rebuild missing.
/// @details Shared by both prepareLinkContext (file-based) and
///          prepareLinkContextFromSymbols (symbol-set-based).
static int resolveAndBuildArchives(const std::unordered_set<std::string> &symbols,
                                   LinkContext &ctx,
                                   std::ostream &out,
                                   std::ostream &err) {
    ctx.requiredComponents = resolveRequiredComponents(symbols);

    const std::optional<std::filesystem::path> buildDirOpt = findBuildDir();
    ctx.buildDir = buildDirOpt.value_or(std::filesystem::path{});

    ctx.requiredArchives.clear();
    for (auto comp : ctx.requiredComponents) {
        auto name = archiveNameForComponent(comp);
        ctx.requiredArchives.emplace_back(std::string(name),
                                          runtimeArchivePath(ctx.buildDir, name));
    }

    // Build missing targets if we have a build directory.
    std::vector<std::string> missingTargets;
    if (!ctx.buildDir.empty()) {
        for (const auto &[tgt, path] : ctx.requiredArchives) {
            if (!fileExists(path))
                missingTargets.push_back(tgt);
        }
        if (hasComponent(ctx, RtComponent::Graphics)) {
            const std::filesystem::path gfxLib = supportLibraryPath(ctx.buildDir, "vipergfx");
            if (!fileExists(gfxLib))
                missingTargets.push_back("vipergfx");
            const std::filesystem::path guiLib = supportLibraryPath(ctx.buildDir, "vipergui");
            if (!fileExists(guiLib))
                missingTargets.push_back("vipergui");
        }
        if (hasComponent(ctx, RtComponent::Audio)) {
            const std::filesystem::path audLib = supportLibraryPath(ctx.buildDir, "viperaud");
            if (!fileExists(audLib))
                missingTargets.push_back("viperaud");
        }
        if (!missingTargets.empty()) {
            std::sort(missingTargets.begin(), missingTargets.end());
            missingTargets.erase(std::unique(missingTargets.begin(), missingTargets.end()),
                                 missingTargets.end());
            std::vector<std::string> cmd = {"cmake", "--build", ctx.buildDir.string(), "--target"};
            cmd.insert(cmd.end(), missingTargets.begin(), missingTargets.end());
            const RunResult build = run_process(cmd);
            if (!build.out.empty())
                out << build.out;
#if defined(_WIN32)
            if (!build.err.empty())
                err << build.err;
#endif
            if (build.exit_code != 0) {
                err << "error: failed to build required runtime libraries in '"
                    << ctx.buildDir.string() << "'\n";
                return 1;
            }
        }
    }

    return 0;
}

int prepareLinkContext(const std::string &asmPath,
                       LinkContext &ctx,
                       std::ostream &out,
                       std::ostream &err) {
    std::string asmText;
    if (!readFileToString(asmPath, asmText)) {
        err << "error: unable to read '" << asmPath << "' for runtime library selection\n";
        return 1;
    }

    const std::unordered_set<std::string> symbols = parseRuntimeSymbols(asmText);
    return resolveAndBuildArchives(symbols, ctx, out, err);
}

int prepareLinkContextFromSymbols(const std::unordered_set<std::string> &symbols,
                                  LinkContext &ctx,
                                  std::ostream &out,
                                  std::ostream &err) {
    return resolveAndBuildArchives(symbols, ctx, out, err);
}

void appendArchives(const LinkContext &ctx, std::vector<std::string> &cmd) {
    for (auto it = ctx.requiredArchives.rbegin(); it != ctx.requiredArchives.rend(); ++it)
        if (fileExists(it->second))
            cmd.push_back(it->second.string());
}

void appendGraphicsLibs(const LinkContext &ctx,
                        std::vector<std::string> &cmd,
                        const std::vector<std::string> &frameworks) {
    if (!hasComponent(ctx, RtComponent::Graphics))
        return;

    // vipergui (widget implementations) must come before vipergfx (primitives)
    // because libviper_rt_graphics calls vg_* from vipergui, which in turn
    // calls the lower-level drawing APIs in vipergfx.
    const std::filesystem::path guiLib = supportLibraryPath(ctx.buildDir, "vipergui");
    const std::filesystem::path gfxLib = supportLibraryPath(ctx.buildDir, "vipergfx");
    if (fileExists(guiLib))
        cmd.push_back(guiLib.string());
    if (fileExists(gfxLib))
        cmd.push_back(gfxLib.string());

    for (const auto &fw : frameworks) {
        cmd.push_back("-framework");
        cmd.push_back(fw);
    }

#if !defined(__APPLE__) && !defined(_WIN32)
    cmd.push_back("-lX11");
#endif
}

void appendAudioLibs(const LinkContext &ctx, std::vector<std::string> &cmd) {
    if (!hasComponent(ctx, RtComponent::Audio))
        return;
    const std::filesystem::path audLib = supportLibraryPath(ctx.buildDir, "viperaud");
    if (fileExists(audLib))
        cmd.push_back(audLib.string());

#if defined(__APPLE__)
    cmd.push_back("-framework");
    cmd.push_back("AudioToolbox");
#elif !defined(_WIN32)
    // Linux: ALSA
    cmd.push_back("-lasound");
#endif
}

std::vector<std::string> defaultGraphicsFrameworks() {
#if defined(__APPLE__)
    return {"Cocoa", "IOKit", "CoreFoundation", "UniformTypeIdentifiers", "Metal", "QuartzCore"};
#else
    return {};
#endif
}

void appendSystemLinkInputs(const LinkContext &ctx, std::vector<std::string> &cmd) {
    appendArchives(ctx, cmd);
    appendGraphicsLibs(ctx, cmd, defaultGraphicsFrameworks());
    appendAudioLibs(ctx, cmd);

    if (hasComponent(ctx, RtComponent::Threads))
        cmd.push_back("-lc++");
}

void appendSystemLinkFlags(const LinkContext &ctx,
                           std::vector<std::string> &cmd,
                           std::size_t stackSize,
                           bool useElfPie,
                           bool useElfMath) {
#if defined(__APPLE__)
    cmd.push_back("-Wl,-dead_strip");
    if (stackSize > 0) {
        std::ostringstream stackArg;
        stackArg << "-Wl,-stack_size,0x" << std::hex << stackSize;
        cmd.push_back(stackArg.str());
    }
#elif !defined(_WIN32)
    cmd.push_back("-Wl,--gc-sections");
    if (useElfPie)
        cmd.push_back("-pie");
    if (hasComponent(ctx, RtComponent::Threads))
        cmd.push_back("-pthread");
    if (useElfMath)
        cmd.push_back("-lm");
    if (stackSize > 0)
        cmd.push_back("-Wl,-z,stack-size=" + std::to_string(stackSize));
#else
    (void)ctx;
    (void)cmd;
    (void)stackSize;
    (void)useElfPie;
    (void)useElfMath;
#endif
}

// =========================================================================
// Tool invocation
// =========================================================================

int invokeAssembler(const std::vector<std::string> &ccArgs,
                    const std::string &asmPath,
                    const std::string &objPath,
                    std::ostream &out,
                    std::ostream &err) {
    std::vector<std::string> cmd = ccArgs;
    cmd.push_back("-c");
    cmd.push_back(asmPath);
    cmd.push_back("-o");
    cmd.push_back(objPath);

    const RunResult rr = run_process(cmd);
    if (rr.exit_code == -1) {
        err << "error: failed to launch system assembler command\n";
        return -1;
    }
    if (!rr.out.empty())
        out << rr.out;
#if defined(_WIN32)
    if (!rr.err.empty())
        err << rr.err;
#endif
    return rr.exit_code;
}

int runExecutable(const std::string &exePath, std::ostream &out, std::ostream &err) {
    const RunResult rr = run_process({exePath});
    if (rr.exit_code == -1) {
        err << "error: failed to execute '" << exePath << "'\n";
        return -1;
    }
    if (!rr.out.empty())
        out << rr.out;
#if defined(_WIN32)
    if (!rr.err.empty())
        err << rr.err;
#endif
    return rr.exit_code;
}

} // namespace viper::codegen::common
