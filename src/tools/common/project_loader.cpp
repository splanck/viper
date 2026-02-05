//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/common/project_loader.cpp
// Purpose: Universal project system — manifest parsing, convention-based
//          file discovery, language detection, and entry point resolution.
//
//===----------------------------------------------------------------------===//

#include "tools/common/project_loader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace il::tools::common
{

namespace
{

/// @brief Make a diagnostic error with a message.
il::support::Diag makeErr(const std::string &msg)
{
    return il::support::Diagnostic{il::support::Severity::Error, msg, {}, {}};
}

/// @brief Make a diagnostic error with file:line context.
il::support::Diag makeManifestErr(const std::string &path, int line, const std::string &msg)
{
    std::string full = path + ":" + std::to_string(line) + ": " + msg;
    return il::support::Diagnostic{il::support::Severity::Error, full, {}, {}};
}

/// @brief Recursively collect files with a given extension under a directory.
/// @param dir Root directory to scan.
/// @param ext Extension including dot (e.g. ".zia").
/// @param excludes Directories to skip (relative to dir or absolute).
/// @return Sorted list of absolute file paths.
std::vector<std::string> collectFiles(const fs::path &dir,
                                      const std::string &ext,
                                      const std::vector<std::string> &excludes)
{
    std::vector<std::string> result;
    if (!fs::is_directory(dir))
        return result;

    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator();
         ++it)
    {
        // Check excludes against the relative path from dir
        if (it->is_directory())
        {
            auto rel = fs::relative(it->path(), dir).string();
            for (const auto &ex : excludes)
            {
                // Match if the relative path starts with the exclude prefix
                if (rel == ex || rel.rfind(ex, 0) == 0)
                {
                    it.disable_recursion_pending();
                    break;
                }
            }
            continue;
        }

        if (it->is_regular_file() && it->path().extension() == ext)
        {
            result.push_back(fs::canonical(it->path()).string());
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

/// @brief Check if a file contains a Zia entry point (func start() or func main()).
/// Uses lightweight text scanning, not full parsing.
bool hasZiaEntryPoint(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    // Match "func start(" or "func main(" with optional whitespace
    std::regex entryPattern(R"(^\s*func\s+(start|main)\s*\()");
    std::string line;
    while (std::getline(file, line))
    {
        if (std::regex_search(line, entryPattern))
            return true;
    }
    return false;
}

/// @brief Check if a BASIC file has AddFile directives (indicating a root file).
bool hasBasicAddFile(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    std::regex addFilePattern(R"(^\s*AddFile\s+)");
    std::string line;
    while (std::getline(file, line))
    {
        if (std::regex_search(line, addFilePattern))
            return true;
    }
    return false;
}

/// @brief Check if a BASIC file has top-level executable statements
/// (not just SUB/FUNCTION definitions).
bool hasBasicTopLevelCode(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    // Simple heuristic: look for lines that aren't blank, comments,
    // SUB/FUNCTION/END SUB/END FUNCTION definitions, or AddFile/Dim
    std::regex subFuncPattern(R"(^\s*(Sub|Function|End Sub|End Function)\b)", std::regex::icase);
    std::regex execPattern(R"(^\s*(Print|Input|If|For|While|Do|Call|Let|Dim|Goto|GoSub|Return|AddFile)\b)",
                           std::regex::icase);

    std::string line;
    while (std::getline(file, line))
    {
        // Skip blank lines and comments
        if (line.empty())
            continue;
        auto trimmed = line;
        auto pos = trimmed.find_first_not_of(" \t");
        if (pos == std::string::npos)
            continue;
        trimmed = trimmed.substr(pos);
        if (trimmed[0] == '\'' || trimmed.rfind("REM", 0) == 0 || trimmed.rfind("rem", 0) == 0)
            continue;

        // If it matches an executable statement, this file has top-level code
        if (std::regex_search(line, execPattern))
            return true;
    }
    return false;
}

/// @brief Find the Zia entry file from a list of source files.
il::support::Expected<std::string> findZiaEntry(const std::vector<std::string> &files)
{
    // Priority 1: file named main.zia
    for (const auto &f : files)
    {
        if (fs::path(f).filename() == "main.zia")
            return f;
    }

    // Priority 2: scan for func start() or func main()
    std::vector<std::string> candidates;
    for (const auto &f : files)
    {
        if (hasZiaEntryPoint(f))
            candidates.push_back(f);
    }

    if (candidates.size() == 1)
        return candidates[0];
    if (candidates.size() > 1)
    {
        std::string msg = "multiple entry points found:";
        for (const auto &c : candidates)
            msg += " " + fs::path(c).filename().string();
        msg += "; specify entry in viper.project";
        return makeErr(msg);
    }

    return makeErr("no entry point found; expected func start() or func main() in a .zia file, "
                   "or a file named main.zia");
}

/// @brief Find the BASIC entry file from a list of source files.
il::support::Expected<std::string> findBasicEntry(const std::vector<std::string> &files)
{
    // Priority 1: file named main.bas
    for (const auto &f : files)
    {
        if (fs::path(f).filename() == "main.bas")
            return f;
    }

    // Priority 2: look for files with AddFile directives (root files)
    std::vector<std::string> roots;
    for (const auto &f : files)
    {
        if (hasBasicAddFile(f))
            roots.push_back(f);
    }

    if (roots.size() == 1)
        return roots[0];
    if (roots.size() > 1)
    {
        std::string msg = "multiple root files found:";
        for (const auto &r : roots)
            msg += " " + fs::path(r).filename().string();
        msg += "; specify entry in viper.project";
        return makeErr(msg);
    }

    // Priority 3: look for files with top-level executable statements
    std::vector<std::string> execFiles;
    for (const auto &f : files)
    {
        if (hasBasicTopLevelCode(f))
            execFiles.push_back(f);
    }

    if (execFiles.size() == 1)
        return execFiles[0];

    if (files.size() == 1)
        return files[0];

    return makeErr("no entry point found; expected a .bas file with top-level statements, "
                   "or a file named main.bas");
}

/// @brief Discover project configuration by convention (no manifest).
il::support::Expected<ProjectConfig> discoverConvention(const fs::path &dir,
                                                        const std::vector<std::string> &excludes)
{
    auto ziaFiles = collectFiles(dir, ".zia", excludes);
    auto basFiles = collectFiles(dir, ".bas", excludes);

    // Language detection
    if (ziaFiles.empty() && basFiles.empty())
        return makeErr("no source files found in " + dir.string());

    if (!ziaFiles.empty() && !basFiles.empty())
        return makeErr("mixed .zia and .bas files in " + dir.string() +
                       "; specify language with viper.project");

    ProjectConfig config;
    config.rootDir = fs::canonical(dir).string();
    config.name = dir.filename().string();

    if (!ziaFiles.empty())
    {
        config.lang = ProjectLang::Zia;
        config.sourceFiles = std::move(ziaFiles);

        auto entry = findZiaEntry(config.sourceFiles);
        if (!entry)
            return il::support::Expected<ProjectConfig>(entry.error());
        config.entryFile = std::move(entry.value());
    }
    else
    {
        config.lang = ProjectLang::Basic;
        config.sourceFiles = std::move(basFiles);

        auto entry = findBasicEntry(config.sourceFiles);
        if (!entry)
            return il::support::Expected<ProjectConfig>(entry.error());
        config.entryFile = std::move(entry.value());
    }

    return config;
}

/// @brief Parse an on/off boolean value.
il::support::Expected<bool> parseBool(const std::string &val,
                                      const std::string &manifestPath,
                                      int line,
                                      const std::string &directive)
{
    if (val == "on" || val == "true" || val == "yes")
        return true;
    if (val == "off" || val == "false" || val == "no")
        return false;
    return makeManifestErr(manifestPath, line,
                           "invalid value '" + val + "' for " + directive + "; expected on or off");
}

} // anonymous namespace

il::support::Expected<ProjectConfig> parseManifest(const std::string &manifestPath)
{
    std::ifstream file(manifestPath);
    if (!file.is_open())
        return makeErr("cannot open manifest: " + manifestPath);

    fs::path manifestDir = fs::path(manifestPath).parent_path();
    if (manifestDir.empty())
        manifestDir = fs::current_path();
    manifestDir = fs::canonical(manifestDir);

    ProjectConfig config;
    config.rootDir = manifestDir.string();
    config.name = manifestDir.filename().string();

    std::vector<std::string> sourceDirs;
    std::vector<std::string> excludes;
    bool hasProject = false;
    bool hasVersion = false;
    bool hasLang = false;
    bool hasEntry = false;
    bool hasOptimize = false;
    bool hasBoundsChecks = false;
    bool hasOverflowChecks = false;
    bool hasNullChecks = false;

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line))
    {
        ++lineNum;

        // Strip leading/trailing whitespace
        auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue; // blank line
        line = line.substr(start);
        auto end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos)
            line = line.substr(0, end + 1);

        // Skip comments
        if (line.empty() || line[0] == '#')
            continue;

        // Parse directive and value
        auto spacePos = line.find_first_of(" \t");
        if (spacePos == std::string::npos)
            return makeManifestErr(manifestPath, lineNum, "directive missing value: '" + line + "'");

        std::string directive = line.substr(0, spacePos);
        std::string value = line.substr(line.find_first_not_of(" \t", spacePos));

        if (directive == "project")
        {
            if (hasProject)
                return makeManifestErr(manifestPath, lineNum, "duplicate directive 'project'");
            hasProject = true;
            config.name = value;
        }
        else if (directive == "version")
        {
            if (hasVersion)
                return makeManifestErr(manifestPath, lineNum, "duplicate directive 'version'");
            hasVersion = true;
            config.version = value;
        }
        else if (directive == "lang")
        {
            if (hasLang)
                return makeManifestErr(manifestPath, lineNum, "duplicate directive 'lang'");
            hasLang = true;
            if (value == "zia")
                config.lang = ProjectLang::Zia;
            else if (value == "basic")
                config.lang = ProjectLang::Basic;
            else
                return makeManifestErr(manifestPath, lineNum,
                                       "invalid language '" + value + "'; expected 'zia' or 'basic'");
        }
        else if (directive == "entry")
        {
            if (hasEntry)
                return makeManifestErr(manifestPath, lineNum, "duplicate directive 'entry'");
            hasEntry = true;
            config.entryFile = (manifestDir / value).string();
        }
        else if (directive == "sources")
        {
            sourceDirs.push_back(value);
        }
        else if (directive == "exclude")
        {
            excludes.push_back(value);
        }
        else if (directive == "optimize")
        {
            if (hasOptimize)
                return makeManifestErr(manifestPath, lineNum, "duplicate directive 'optimize'");
            hasOptimize = true;
            if (value != "O0" && value != "O1" && value != "O2")
                return makeManifestErr(manifestPath, lineNum,
                                       "invalid optimize level '" + value + "'; expected O0, O1, or O2");
            config.optimizeLevel = value;
        }
        else if (directive == "bounds-checks")
        {
            if (hasBoundsChecks)
                return makeManifestErr(manifestPath, lineNum, "duplicate directive 'bounds-checks'");
            hasBoundsChecks = true;
            auto b = parseBool(value, manifestPath, lineNum, "bounds-checks");
            if (!b)
                return il::support::Expected<ProjectConfig>(b.error());
            config.boundsChecks = b.value();
        }
        else if (directive == "overflow-checks")
        {
            if (hasOverflowChecks)
                return makeManifestErr(manifestPath, lineNum, "duplicate directive 'overflow-checks'");
            hasOverflowChecks = true;
            auto b = parseBool(value, manifestPath, lineNum, "overflow-checks");
            if (!b)
                return il::support::Expected<ProjectConfig>(b.error());
            config.overflowChecks = b.value();
        }
        else if (directive == "null-checks")
        {
            if (hasNullChecks)
                return makeManifestErr(manifestPath, lineNum, "duplicate directive 'null-checks'");
            hasNullChecks = true;
            auto b = parseBool(value, manifestPath, lineNum, "null-checks");
            if (!b)
                return il::support::Expected<ProjectConfig>(b.error());
            config.nullChecks = b.value();
        }
        else
        {
            return makeManifestErr(manifestPath, lineNum,
                                   "unknown directive '" + directive + "'");
        }
    }

    // Collect source files from declared directories (or project root by default)
    if (sourceDirs.empty())
        sourceDirs.push_back(".");

    std::string ext = (config.lang == ProjectLang::Zia) ? ".zia" : ".bas";

    // If no lang specified, auto-detect before collecting
    if (!hasLang)
    {
        // Scan all source dirs for both extensions
        std::vector<std::string> allZia, allBas;
        for (const auto &sd : sourceDirs)
        {
            fs::path srcDir = manifestDir / sd;
            auto zia = collectFiles(srcDir, ".zia", excludes);
            auto bas = collectFiles(srcDir, ".bas", excludes);
            allZia.insert(allZia.end(), zia.begin(), zia.end());
            allBas.insert(allBas.end(), bas.begin(), bas.end());
        }

        if (!allZia.empty() && allBas.empty())
        {
            config.lang = ProjectLang::Zia;
            ext = ".zia";
        }
        else if (!allBas.empty() && allZia.empty())
        {
            config.lang = ProjectLang::Basic;
            ext = ".bas";
        }
        else if (!allZia.empty() && !allBas.empty())
        {
            return makeErr("mixed .zia and .bas files; specify lang in viper.project");
        }
        else
        {
            return makeErr("no source files found in project directories");
        }
    }

    for (const auto &sd : sourceDirs)
    {
        fs::path srcDir = manifestDir / sd;
        if (!fs::is_directory(srcDir))
            return makeErr("sources directory not found: " + srcDir.string());
        auto files = collectFiles(srcDir, ext, excludes);
        config.sourceFiles.insert(config.sourceFiles.end(), files.begin(), files.end());
    }

    // Deduplicate
    std::sort(config.sourceFiles.begin(), config.sourceFiles.end());
    config.sourceFiles.erase(std::unique(config.sourceFiles.begin(), config.sourceFiles.end()),
                             config.sourceFiles.end());

    if (config.sourceFiles.empty())
        return makeErr("no source files found in project directories");

    // Entry point resolution
    if (!hasEntry)
    {
        il::support::Expected<std::string> entry =
            (config.lang == ProjectLang::Zia) ? findZiaEntry(config.sourceFiles)
                                              : findBasicEntry(config.sourceFiles);
        if (!entry)
            return il::support::Expected<ProjectConfig>(entry.error());
        config.entryFile = std::move(entry.value());
    }
    else
    {
        // Verify entry file exists
        if (!fs::exists(config.entryFile))
            return makeErr("entry file not found: " + config.entryFile);
    }

    return config;
}

il::support::Expected<ProjectConfig> resolveProject(const std::string &target)
{
    // Determine what the target is
    fs::path targetPath(target);

    // Handle relative paths
    if (targetPath.is_relative())
        targetPath = fs::current_path() / targetPath;

    if (!fs::exists(targetPath))
        return makeErr("target not found: " + target);

    // Case 1: Single .zia file
    if (fs::is_regular_file(targetPath) && targetPath.extension() == ".zia")
    {
        ProjectConfig config;
        auto canonical = fs::canonical(targetPath);
        config.lang = ProjectLang::Zia;
        config.entryFile = canonical.string();
        config.sourceFiles = {config.entryFile};
        config.rootDir = canonical.parent_path().string();
        config.name = canonical.stem().string();
        return config;
    }

    // Case 2: Single .bas file
    if (fs::is_regular_file(targetPath) && targetPath.extension() == ".bas")
    {
        ProjectConfig config;
        auto canonical = fs::canonical(targetPath);
        config.lang = ProjectLang::Basic;
        config.entryFile = canonical.string();
        config.sourceFiles = {config.entryFile};
        config.rootDir = canonical.parent_path().string();
        config.name = canonical.stem().string();
        return config;
    }

    // Case 3: Explicit manifest file
    if (fs::is_regular_file(targetPath))
    {
        auto filename = targetPath.filename().string();
        if (filename == "viper.project" || targetPath.extension() == ".project")
        {
            return parseManifest(fs::canonical(targetPath).string());
        }
        return makeErr(target + " is not a .zia, .bas, or viper.project file");
    }

    // Case 4: Directory
    if (fs::is_directory(targetPath))
    {
        auto canonical = fs::canonical(targetPath);

        // Check for viper.project in directory
        auto manifestPath = canonical / "viper.project";
        if (fs::exists(manifestPath))
            return parseManifest(manifestPath.string());

        // Convention discovery
        return discoverConvention(canonical, {});
    }

    return makeErr(target + " is not a source file, directory, or project manifest");
}

} // namespace il::tools::common
