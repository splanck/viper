//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Main entry point for the vpascal command-line tool.
// Provides a user-friendly interface to run and compile Pascal programs.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point for the vpascal tool - a simplified interface to Viper Pascal.
/// @details Translates user-friendly vpascal arguments into ilc front pascal subcommands
///          and delegates to the existing Pascal frontend implementation.

#include "tools/ilc/cli.hpp"
#include "usage.hpp"
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/// @brief Configuration parsed from vpascal command-line arguments.
struct VPascalConfig
{
    std::string sourcePath;
    std::string outputPath;
    bool emitIl = false;
    bool run = false; // Default action if nothing else specified
    std::vector<std::string> forwardedArgs;
};

/// @brief Parse vpascal-specific arguments and translate to ilc frontend config.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Parsed configuration or empty config on error.
///
/// @details Handles vpascal-specific flags like --emit-il and -o, while forwarding
///          shared options (--trace, --bounds-checks, etc.) to the underlying
///          ilc front pascal implementation.
VPascalConfig parseArgs(int argc, char **argv)
{
    VPascalConfig config{};

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            vpascal::printUsage();
            std::exit(0);
        }
        else if (arg == "--version")
        {
            vpascal::printVersion();
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
                vpascal::printUsage();
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
                    vpascal::printUsage();
                    std::exit(1);
                }
            }
        }
        else if (arg.ends_with(".pas"))
        {
            if (!config.sourcePath.empty())
            {
                std::cerr << "error: multiple source files not supported\n\n";
                vpascal::printUsage();
                std::exit(1);
            }
            config.sourcePath = std::string(arg);
        }
        else
        {
            std::cerr << "error: unknown argument or file type: " << arg << "\n";
            std::cerr << "       (expected .pas file)\n\n";
            vpascal::printUsage();
            std::exit(1);
        }
    }

    // Validate configuration
    if (config.sourcePath.empty())
    {
        std::cerr << "error: no input file specified\n\n";
        vpascal::printUsage();
        std::exit(1);
    }

    // Default action: run the program
    if (!config.emitIl)
    {
        config.run = true;
    }

    return config;
}

/// @brief Build argument vector for ilc front pascal subcommand.
///
/// @param config Parsed vpascal configuration.
/// @param outStorage Output parameter: storage for argument strings.
/// @return Argument vector suitable for cmdFrontPascal.
///
/// @details Translates vpascal-style arguments into the format expected by
///          the existing ilc front pascal implementation. The caller must
///          keep outStorage alive while using the returned char* pointers.
std::vector<char *> buildIlcArgs(const VPascalConfig &config, std::vector<std::string> &outStorage)
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

/// @brief Main entry point for vpascal command-line tool.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Exit status: 0 on success, non-zero on error.
///
/// @details Provides a simplified, user-friendly interface to Viper Pascal:
///          - vpascal program.pas           -> runs the program
///          - vpascal program.pas --emit-il -> shows generated IL
///          - vpascal program.pas -o file   -> saves IL to file
///
///          All arguments are translated and delegated to the existing
///          ilc front pascal implementation for actual compilation/execution.
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        vpascal::printUsage();
        return 1;
    }

    // Parse vpascal-specific arguments
    VPascalConfig config = parseArgs(argc, argv);

    // Build argument vector for ilc front pascal
    std::vector<std::string> argStorage;
    std::vector<char *> ilcArgs = buildIlcArgs(config, argStorage);

    // Handle -o output redirection at the shell level
    // We need to redirect stdout before calling cmdFrontPascal
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

    // Delegate to existing Pascal frontend implementation
    int result = cmdFrontPascal(static_cast<int>(ilcArgs.size()), ilcArgs.data());

    // Restore stdout if we redirected it
    if (outputFile)
    {
        std::fclose(outputFile);
    }

    return result;
}
