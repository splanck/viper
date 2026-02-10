//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/viper/cmd_init.cpp
// Purpose: Implements `viper init` to scaffold a new Viper project.
//
//===----------------------------------------------------------------------===//

#include "cli.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

/// @brief Write a text file, returning false on failure.
static bool writeFile(const fs::path &path, const std::string &content)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        std::cerr << "error: could not write " << path.string() << "\n";
        return false;
    }
    out << content;
    return true;
}

int cmdInit(int argc, char **argv)
{
    std::string projectName;
    std::string lang = "zia";

    // Parse arguments.
    for (int i = 0; i < argc; ++i)
    {
        std::string_view arg = argv[i];
        if (arg == "--lang")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "error: --lang requires a value (zia or basic)\n";
                return 1;
            }
            lang = argv[++i];
            if (lang != "zia" && lang != "basic")
            {
                std::cerr << "error: --lang must be 'zia' or 'basic', got '"
                          << lang << "'\n";
                return 1;
            }
        }
        else if (arg.size() > 0 && arg[0] == '-')
        {
            std::cerr << "error: unknown option: " << arg << "\n";
            return 1;
        }
        else
        {
            if (!projectName.empty())
            {
                std::cerr << "error: unexpected argument: " << arg << "\n";
                return 1;
            }
            projectName = std::string(arg);
        }
    }

    if (projectName.empty())
    {
        std::cerr << "Usage: viper init <project-name> [--lang zia|basic]\n";
        return 1;
    }

    // Validate project name doesn't contain path separators.
    if (projectName.find('/') != std::string::npos ||
        projectName.find('\\') != std::string::npos)
    {
        std::cerr << "error: project name must not contain path separators\n";
        return 1;
    }

    fs::path projectDir = fs::current_path() / projectName;

    if (fs::exists(projectDir))
    {
        std::cerr << "error: directory '" << projectName
                  << "' already exists\n";
        return 1;
    }

    // Create project directory.
    std::error_code ec;
    if (!fs::create_directory(projectDir, ec) || ec)
    {
        std::cerr << "error: could not create directory '" << projectName
                  << "': " << ec.message() << "\n";
        return 1;
    }

    // Generate viper.project manifest.
    std::string entryFile = (lang == "basic") ? "main.bas" : "main.zia";
    std::string manifest =
        "project " + projectName + "\n" +
        "version 0.1.0\n" +
        "lang " + lang + "\n" +
        "entry " + entryFile + "\n";

    if (!writeFile(projectDir / "viper.project", manifest))
    {
        return 1;
    }

    // Generate entry-point source file.
    std::string source;
    if (lang == "zia")
    {
        source =
            "module main;\n"
            "\n"
            "bind Viper.Terminal;\n"
            "\n"
            "func start() {\n"
            "    Say(\"Hello from " + projectName + "!\");\n"
            "}\n";
    }
    else
    {
        source = "PRINT \"Hello from " + projectName + "!\"\n";
    }

    if (!writeFile(projectDir / entryFile, source))
    {
        return 1;
    }

    std::cerr << "Created " << lang << " project '" << projectName << "'\n"
              << "\n"
              << "  " << projectName << "/viper.project\n"
              << "  " << projectName << "/" << entryFile << "\n"
              << "\n"
              << "Run with:  viper run " << projectName << "\n";

    return 0;
}
