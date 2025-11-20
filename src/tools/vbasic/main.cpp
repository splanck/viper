//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Main entry point for the vbasic command-line tool.
// Provides a user-friendly interface to run and compile BASIC programs.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point for the vbasic tool - a simplified interface to Viper BASIC.
/// @details Translates user-friendly vbasic arguments into ilc front basic subcommands
///          and delegates to the existing BASIC frontend implementation.

#include "tools/ilc/cli.hpp"
#include "usage.hpp"
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/// @brief Configuration parsed from vbasic command-line arguments.
struct VBasicConfig
{
    std::string sourcePath;
    std::string outputPath;
    bool emitIl = false;
    bool run = false; // Default action if nothing else specified
    std::vector<std::string> forwardedArgs;
};

/// @brief Parse vbasic-specific arguments and translate to ilc frontend config.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Parsed configuration or empty config on error.
///
/// @details Handles vbasic-specific flags like --emit-il and -o, while forwarding
///          shared options (--trace, --bounds-checks, etc.) to the underlying
///          ilc front basic implementation.
VBasicConfig parseArgs(int argc, char **argv)
{
    VBasicConfig config{};

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            vbasic::printUsage();
            std::exit(0);
        }
        else if (arg == "--version")
        {
            vbasic::printVersion();
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
                vbasic::printUsage();
                std::exit(1);
            }
            config.outputPath = argv[++i];
            config.emitIl = true; // -o implies emit-il
        }
        else if (arg.starts_with("-"))
        {
            // Forward other flags to underlying ilc implementation
            // These include: --trace, --bounds-checks, --stdin-from, --max-steps, --dump-trap
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
                    vbasic::printUsage();
                    std::exit(1);
                }
            }
        }
        else if (arg.ends_with(".bas"))
        {
            if (!config.sourcePath.empty())
            {
                std::cerr << "error: multiple source files not supported\n\n";
                vbasic::printUsage();
                std::exit(1);
            }
            config.sourcePath = std::string(arg);
        }
        else
        {
            std::cerr << "error: unknown argument or file type: " << arg << "\n";
            std::cerr << "       (expected .bas file)\n\n";
            vbasic::printUsage();
            std::exit(1);
        }
    }

    // Validate configuration
    if (config.sourcePath.empty())
    {
        std::cerr << "error: no input file specified\n\n";
        vbasic::printUsage();
        std::exit(1);
    }

    // Default action: run the program
    if (!config.emitIl)
    {
        config.run = true;
    }

    return config;
}

/// @brief Build argument vector for ilc front basic subcommand.
///
/// @param config Parsed vbasic configuration.
/// @param outStorage Output parameter: storage for argument strings.
/// @return Argument vector suitable for cmdFrontBasic.
///
/// @details Translates vbasic-style arguments into the format expected by
///          the existing ilc front basic implementation. The caller must
///          keep outStorage alive while using the returned char* pointers.
std::vector<char *> buildIlcArgs(const VBasicConfig &config, std::vector<std::string> &outStorage)
{
    outStorage.clear();

    // Reserve capacity upfront to prevent reallocation and pointer invalidation
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

} // namespace

/// @brief Main entry point for vbasic command-line tool.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Exit status: 0 on success, non-zero on error.
///
/// @details Provides a simplified, user-friendly interface to Viper BASIC:
///          - vbasic script.bas           -> runs the program
///          - vbasic script.bas --emit-il -> shows generated IL
///          - vbasic script.bas -o file   -> saves IL to file
///
///          All arguments are translated and delegated to the existing
///          ilc front basic implementation for actual compilation/execution.
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        vbasic::printUsage();
        return 1;
    }

    // Parse vbasic-specific arguments
    VBasicConfig config = parseArgs(argc, argv);

    // Build argument vector for ilc front basic
    std::vector<std::string> argStorage;
    std::vector<char *> ilcArgs = buildIlcArgs(config, argStorage);

    // Handle -o output redirection at the shell level
    // We need to redirect stdout before calling cmdFrontBasic
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

    // Delegate to existing BASIC frontend implementation
    int result = cmdFrontBasic(static_cast<int>(ilcArgs.size()), ilcArgs.data());

    // Restore stdout if we redirected it
    if (outputFile)
    {
        std::fclose(outputFile);
    }

    return result;
}
