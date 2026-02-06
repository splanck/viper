//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/LinkerSupport.cpp
// Purpose: Implementation of shared linker utilities.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/LinkerSupport.hpp"

#include "common/RunProcess.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace viper::codegen::common
{

// =========================================================================
// Pure utilities
// =========================================================================

bool fileExists(const std::filesystem::path &path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool readFileToString(const std::filesystem::path &path, std::string &dst)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    dst = ss.str();
    return true;
}

std::optional<std::filesystem::path> findBuildDir()
{
    std::error_code ec;
    std::filesystem::path cur = std::filesystem::current_path(ec);
    if (!ec)
    {
        for (int depth = 0; depth < 8; ++depth)
        {
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

std::unordered_set<std::string> parseRuntimeSymbols(std::string_view text)
{
    auto isIdent = [](unsigned char c) -> bool { return std::isalnum(c) || c == '_'; };

    std::unordered_set<std::string> symbols;
    for (std::size_t i = 0; i + 3 < text.size(); ++i)
    {
        std::size_t start = std::string_view::npos;
        std::size_t boundary = std::string_view::npos;
        if (text[i] == 'r' && text[i + 1] == 't' && text[i + 2] == '_')
        {
            start = i;
            boundary = (start == 0) ? std::string_view::npos : (start - 1);
        }
        else if (text[i] == '_' && text[i + 1] == 'r' && text[i + 2] == 't' &&
                 text[i + 3] == '_')
        {
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
    return symbols;
}

std::filesystem::path runtimeArchivePath(const std::filesystem::path &buildDir,
                                         std::string_view libBaseName)
{
    if (!buildDir.empty())
        return buildDir / "src/runtime" /
               (std::string("lib") + std::string(libBaseName) + ".a");
    return std::filesystem::path("src/runtime") /
           (std::string("lib") + std::string(libBaseName) + ".a");
}

// =========================================================================
// Link context
// =========================================================================

bool hasComponent(const LinkContext &ctx, RtComponent c)
{
    for (auto rc : ctx.requiredComponents)
        if (rc == c)
            return true;
    return false;
}

int prepareLinkContext(const std::string &asmPath,
                       LinkContext &ctx,
                       std::ostream &out,
                       std::ostream &err)
{
    std::string asmText;
    if (!readFileToString(asmPath, asmText))
    {
        err << "error: unable to read '" << asmPath << "' for runtime library selection\n";
        return 1;
    }

    const std::unordered_set<std::string> symbols = parseRuntimeSymbols(asmText);
    ctx.requiredComponents = resolveRequiredComponents(symbols);

    const std::optional<std::filesystem::path> buildDirOpt = findBuildDir();
    ctx.buildDir = buildDirOpt.value_or(std::filesystem::path{});

    ctx.requiredArchives.clear();
    for (auto comp : ctx.requiredComponents)
    {
        auto name = archiveNameForComponent(comp);
        ctx.requiredArchives.emplace_back(std::string(name),
                                          runtimeArchivePath(ctx.buildDir, name));
    }

    // Build missing targets if we have a build directory.
    std::vector<std::string> missingTargets;
    if (!ctx.buildDir.empty())
    {
        for (const auto &[tgt, path] : ctx.requiredArchives)
        {
            if (!fileExists(path))
                missingTargets.push_back(tgt);
        }
        if (hasComponent(ctx, RtComponent::Graphics))
        {
            const std::filesystem::path gfxLib = ctx.buildDir / "lib" / "libvipergfx.a";
            if (!fileExists(gfxLib))
                missingTargets.push_back("vipergfx");
        }
        if (!missingTargets.empty())
        {
            std::vector<std::string> cmd = {"cmake", "--build", ctx.buildDir.string(), "--target"};
            cmd.insert(cmd.end(), missingTargets.begin(), missingTargets.end());
            const RunResult build = run_process(cmd);
            if (!build.out.empty())
                out << build.out;
#if defined(_WIN32)
            if (!build.err.empty())
                err << build.err;
#endif
            if (build.exit_code != 0)
            {
                err << "error: failed to build required runtime libraries in '"
                    << ctx.buildDir.string() << "'\n";
                return 1;
            }
        }
    }

    return 0;
}

void appendArchives(const LinkContext &ctx, std::vector<std::string> &cmd)
{
    for (auto it = ctx.requiredComponents.rbegin(); it != ctx.requiredComponents.rend(); ++it)
    {
        const std::filesystem::path path = runtimeArchivePath(ctx.buildDir,
                                                              archiveNameForComponent(*it));
        if (fileExists(path))
            cmd.push_back(path.string());
    }
}

void appendGraphicsLibs(const LinkContext &ctx,
                        std::vector<std::string> &cmd,
                        const std::vector<std::string> &frameworks)
{
    if (!hasComponent(ctx, RtComponent::Graphics))
        return;

    std::filesystem::path gfxLib;
    if (!ctx.buildDir.empty())
        gfxLib = ctx.buildDir / "lib" / "libvipergfx.a";
    else
        gfxLib = std::filesystem::path("lib") / "libvipergfx.a";
    if (fileExists(gfxLib))
        cmd.push_back(gfxLib.string());

    for (const auto &fw : frameworks)
    {
        cmd.push_back("-framework");
        cmd.push_back(fw);
    }
}

// =========================================================================
// Tool invocation
// =========================================================================

int invokeAssembler(const std::vector<std::string> &ccArgs,
                    const std::string &asmPath,
                    const std::string &objPath,
                    std::ostream &out,
                    std::ostream &err)
{
    std::vector<std::string> cmd = ccArgs;
    cmd.push_back("-c");
    cmd.push_back(asmPath);
    cmd.push_back("-o");
    cmd.push_back(objPath);

    const RunResult rr = run_process(cmd);
    if (rr.exit_code == -1)
    {
        err << "error: failed to launch system assembler command\n";
        return -1;
    }
    if (!rr.out.empty())
        out << rr.out;
#if defined(_WIN32)
    if (!rr.err.empty())
        err << rr.err;
#endif
    return rr.exit_code == 0 ? 0 : 1;
}

int runExecutable(const std::string &exePath,
                  std::ostream &out,
                  std::ostream &err)
{
    const RunResult rr = run_process({exePath});
    if (rr.exit_code == -1)
    {
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
