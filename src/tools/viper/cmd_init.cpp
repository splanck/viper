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
static bool writeFile(const fs::path &path, const std::string &content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "error: could not write " << path.string() << "\n";
        return false;
    }
    out << content;
    return true;
}

/// @brief Validate a project name supplied to `viper init`.
/// @details Rejects "."/"..", path separators, control characters, and double
///          quotes, printing a specific error to stderr for each case so the
///          generated directory and manifest cannot be corrupted or escape.
/// @param projectName Candidate project name from the command line.
/// @return true if the name is safe to use; false (with an error printed) otherwise.
static bool validateProjectName(const std::string &projectName) {
    if (projectName == "." || projectName == "..") {
        std::cerr << "error: project name must not be '.' or '..'\n";
        return false;
    }
    if (projectName.find('/') != std::string::npos || projectName.find('\\') != std::string::npos) {
        std::cerr << "error: project name must not contain path separators\n";
        return false;
    }
    for (char c : projectName) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F) {
            std::cerr << "error: project name must not contain control characters\n";
            return false;
        }
        if (c == '"') {
            std::cerr << "error: project name must not contain double quotes\n";
            return false;
        }
    }
    return true;
}

/// @brief Print usage for the `viper init` subcommand to stderr.
static void printInitUsage() {
    std::cerr << "Usage: viper init <project-name> [--lang zia|basic]\n"
              << "\n"
              << "Create a new Viper project directory with a viper.project manifest.\n"
              << "\n"
              << "Options:\n"
              << "  --lang zia|basic   Source language for the generated entry file\n"
              << "  -h, --help         Show this help\n";
}

int cmdInit(int argc, char **argv) {
    std::string projectName;
    std::string lang = "zia";

    // Parse arguments.
    for (int i = 0; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--lang") {
            if (i + 1 >= argc) {
                std::cerr << "error: --lang requires a value (zia or basic)\n";
                printInitUsage();
                return 1;
            }
            lang = argv[++i];
            if (lang != "zia" && lang != "basic") {
                std::cerr << "error: --lang must be 'zia' or 'basic', got '" << lang << "'\n";
                printInitUsage();
                return 1;
            }
        } else if (arg == "--help" || arg == "-h") {
            printInitUsage();
            return 0;
        } else if (arg.size() > 0 && arg[0] == '-') {
            std::cerr << "error: unknown option: " << arg << "\n";
            printInitUsage();
            return 1;
        } else {
            if (!projectName.empty()) {
                std::cerr << "error: unexpected argument: " << arg << "\n";
                return 1;
            }
            projectName = std::string(arg);
        }
    }

    if (projectName.empty()) {
        printInitUsage();
        return 1;
    }

    if (!validateProjectName(projectName))
        return 1;

    fs::path projectDir = fs::current_path() / projectName;

    if (fs::exists(projectDir)) {
        std::cerr << "error: directory '" << projectName << "' already exists\n";
        return 1;
    }

    // Create project directory.
    std::error_code ec;
    if (!fs::create_directory(projectDir, ec) || ec) {
        std::cerr << "error: could not create directory '" << projectName << "': " << ec.message()
                  << "\n";
        return 1;
    }

    // Generate viper.project manifest.
    std::string entryFile = (lang == "basic") ? "main.bas" : "main.zia";
    std::string manifest = "project " + projectName + "\n" + "version 0.1.0\n" + "lang " + lang +
                           "\n" + "entry " + entryFile + "\n" + "profile balanced\n" +
                           "optimize O1\n";

    if (!writeFile(projectDir / "viper.project", manifest)) {
        return 1;
    }

    // Generate entry-point source file.
    std::string source;
    if (lang == "zia") {
        source = "module main;\n"
                 "\n"
                 "bind Viper.Terminal;\n"
                 "\n"
                 "func start() {\n"
                 "    Say(\"Hello from " +
                 projectName +
                 "!\");\n"
                 "}\n";
    } else {
        source = "PRINT \"Hello from " + projectName + "!\"\n";
    }

    if (!writeFile(projectDir / entryFile, source)) {
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
