//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/common/frontend_tool.hpp
// Purpose: Shared infrastructure for language frontend CLI tools (vbasic, vpascal).
// Key invariants: All frontend tools share the same argument parsing logic.
// Ownership/Lifetime: N/A.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace viper::tools
{

/// @brief Configuration parsed from frontend tool command-line arguments.
struct FrontendToolConfig
{
    std::string sourcePath;
    std::string outputPath;
    bool emitIl = false;
    bool run = false;
    std::vector<std::string> forwardedArgs;
};

/// @brief Callbacks for language-specific behavior in frontend tools.
struct FrontendToolCallbacks
{
    /// File extension for this language (e.g., ".bas", ".pas")
    std::string_view fileExtension;

    /// Language name for error messages (e.g., "BASIC", "Pascal")
    std::string_view languageName;

    /// Print usage/help information
    std::function<void()> printUsage;

    /// Print version information
    std::function<void()> printVersion;

    /// The ilc frontend command to invoke
    std::function<int(int, char **)> frontendCommand;
};

/// @brief Parse frontend tool arguments.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @param callbacks Language-specific callbacks.
/// @return Parsed configuration or exits on error/help/version.
inline FrontendToolConfig parseArgs(int argc, char **argv, const FrontendToolCallbacks &callbacks)
{
    FrontendToolConfig config{};

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            callbacks.printUsage();
            std::exit(0);
        }
        else if (arg == "--version")
        {
            callbacks.printVersion();
            std::exit(0);
        }
        else if (arg == "--emit-il")
        {
            config.emitIl = true;
        }
        else if (arg == "-o" || arg == "--output")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "error: " << arg << " requires an output path\n\n";
                callbacks.printUsage();
                std::exit(1);
            }
            config.outputPath = argv[++i];
            config.emitIl = true; // -o implies emit-il
        }
        else if (arg.starts_with("-"))
        {
            // Forward other flags to underlying ilc implementation
            config.forwardedArgs.push_back(std::string(arg));

            // Check if this flag takes an argument
            if (arg == "--trace" || arg == "--stdin-from" || arg == "--max-steps")
            {
                if (i + 1 < argc && argv[i + 1][0] != '-')
                {
                    config.forwardedArgs.push_back(argv[++i]);
                }
                else if (arg == "--trace")
                {
                    // --trace is optional parameter, continue
                }
                else
                {
                    std::cerr << "error: " << arg << " requires an argument\n\n";
                    callbacks.printUsage();
                    std::exit(1);
                }
            }
        }
        else if (arg.ends_with(callbacks.fileExtension))
        {
            if (!config.sourcePath.empty())
            {
                std::cerr << "error: multiple source files not supported\n\n";
                callbacks.printUsage();
                std::exit(1);
            }
            config.sourcePath = std::string(arg);
        }
        else
        {
            std::cerr << "error: unknown argument or file type: " << arg << "\n";
            std::cerr << "       (expected " << callbacks.fileExtension << " file)\n\n";
            callbacks.printUsage();
            std::exit(1);
        }
    }

    // Validate configuration
    if (config.sourcePath.empty())
    {
        std::cerr << "error: no input file specified\n\n";
        callbacks.printUsage();
        std::exit(1);
    }

    // Default action: run the program
    if (!config.emitIl)
    {
        config.run = true;
    }

    return config;
}

/// @brief Build argument vector for ilc frontend subcommand.
///
/// @param config Parsed frontend configuration.
/// @param outStorage Output parameter: storage for argument strings.
/// @return Argument vector suitable for frontend command.
inline std::vector<char *> buildIlcArgs(const FrontendToolConfig &config,
                                        std::vector<std::string> &outStorage)
{
    outStorage.clear();
    outStorage.reserve(2 + config.forwardedArgs.size());

    std::vector<char *> args;

    // Add mode flag
    if (config.emitIl)
    {
        outStorage.push_back("-emit-il");
    }
    else
    {
        outStorage.push_back("-run");
    }

    // Add source path
    outStorage.push_back(config.sourcePath);

    // Add forwarded arguments
    for (const auto &fwd : config.forwardedArgs)
    {
        outStorage.push_back(fwd);
    }

    // Build char* array from stable strings
    for (auto &str : outStorage)
    {
        args.push_back(str.data());
    }

    return args;
}

/// @brief Run a frontend tool with the given callbacks.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @param callbacks Language-specific callbacks.
/// @return Exit status: 0 on success, non-zero on error.
inline int runFrontendTool(int argc, char **argv, const FrontendToolCallbacks &callbacks)
{
    if (argc < 2)
    {
        callbacks.printUsage();
        return 1;
    }

    // Parse arguments
    FrontendToolConfig config = parseArgs(argc, argv, callbacks);

    // Build argument vector for ilc frontend
    std::vector<std::string> argStorage;
    std::vector<char *> ilcArgs = buildIlcArgs(config, argStorage);

    // Handle -o output redirection
    FILE *outputFile = nullptr;
    if (!config.outputPath.empty())
    {
        outputFile = std::freopen(config.outputPath.c_str(), "w", stdout);
        if (!outputFile)
        {
            std::cerr << "error: failed to open output file: " << config.outputPath << "\n";
            return 1;
        }
    }

    // Delegate to frontend implementation
    int result = callbacks.frontendCommand(static_cast<int>(ilcArgs.size()), ilcArgs.data());

    // Restore stdout if we redirected it
    if (outputFile)
    {
        std::fclose(outputFile);
    }

    return result;
}

} // namespace viper::tools
