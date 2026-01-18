//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the `ilc front pascal` subcommand. The driver parses Pascal source,
// optionally emits IL, or executes the compiled program inside the VM.
//
//===----------------------------------------------------------------------===//

#include "cli.hpp"
#include "frontends/pascal/Compiler.hpp"
#include "il/api/expected_api.hpp"
#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"
#include "tools/common/source_loader.hpp"
#include "tools/common/vm_executor.hpp"
#include "viper/il/IO.hpp"
#include "viper/il/Verify.hpp"
#include "viper/vm/VM.hpp"
#include <cstdio>
#include <iostream>
#include <string>

using namespace il;
using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

struct FrontPascalConfig
{
    bool emitIl{false};
    bool run{false};
    bool debugVm{false};                  ///< True to use standard VM for debugging.
    std::vector<std::string> sourcePaths; // Multiple source files
    ilc::SharedCliOptions shared;
    std::vector<std::string> programArgs;
};

/// @brief Parse CLI arguments for the Pascal frontend subcommand.
/// Supports multiple source files: ilc front pascal -run main.pas unit1.pas unit2.pas
il::support::Expected<FrontPascalConfig> parseFrontPascalArgs(int argc, char **argv)
{
    FrontPascalConfig config{};
    bool parsingPaths = false;

    for (int i = 0; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-emit-il")
        {
            config.emitIl = true;
            parsingPaths = true;
        }
        else if (arg == "-run")
        {
            config.run = true;
            parsingPaths = true;
        }
        else if (arg == "--debug-vm")
        {
            config.debugVm = true;
        }
        else if (arg == "--")
        {
            // Everything after -- is program arguments
            for (int j = i + 1; j < argc; ++j)
                config.programArgs.emplace_back(argv[j]);
            break;
        }
        else if (parsingPaths && arg[0] != '-')
        {
            // Collect source file paths
            config.sourcePaths.push_back(arg);
        }
        else
        {
            switch (ilc::parseSharedOption(i, argc, argv, config.shared))
            {
                case ilc::SharedOptionParseResult::Parsed:
                    continue;
                case ilc::SharedOptionParseResult::Error:
                    return il::support::Expected<FrontPascalConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, "failed to parse shared option", {}, {}});
                case ilc::SharedOptionParseResult::NotMatched:
                    // Might be a source path without -emit-il/-run before it
                    if (!arg.empty() && arg[0] != '-')
                    {
                        config.sourcePaths.push_back(arg);
                    }
                    else
                    {
                        return il::support::Expected<FrontPascalConfig>(il::support::Diagnostic{
                            il::support::Severity::Error, "unknown flag: " + arg, {}, {}});
                    }
                    break;
            }
        }
    }

    if ((config.emitIl == config.run) || config.sourcePaths.empty())
    {
        return il::support::Expected<FrontPascalConfig>(il::support::Diagnostic{
            il::support::Severity::Error,
            "specify exactly one of -emit-il or -run, followed by source file(s)",
            {},
            {}});
    }

    return il::support::Expected<FrontPascalConfig>(std::move(config));
}

/// @brief Detect if source begins with 'unit' keyword (vs 'program').
/// Uses simple keyword detection - not full lexing.
bool isUnitSource(const std::string &source)
{
    // Skip whitespace and comments to find first keyword
    size_t i = 0;
    while (i < source.size())
    {
        // Skip whitespace
        if (std::isspace(static_cast<unsigned char>(source[i])))
        {
            ++i;
            continue;
        }

        // Skip single-line comments: //
        if (i + 1 < source.size() && source[i] == '/' && source[i + 1] == '/')
        {
            while (i < source.size() && source[i] != '\n')
                ++i;
            continue;
        }

        // Skip block comments: { } or (* *)
        if (source[i] == '{')
        {
            ++i;
            while (i < source.size() && source[i] != '}')
                ++i;
            if (i < source.size())
                ++i;
            continue;
        }
        if (i + 1 < source.size() && source[i] == '(' && source[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == ')'))
                ++i;
            if (i + 1 < source.size())
                i += 2;
            continue;
        }

        // Found first non-comment, non-whitespace - check for 'unit' keyword
        if (i + 4 <= source.size())
        {
            char c0 = static_cast<char>(std::tolower(static_cast<unsigned char>(source[i])));
            char c1 = static_cast<char>(std::tolower(static_cast<unsigned char>(source[i + 1])));
            char c2 = static_cast<char>(std::tolower(static_cast<unsigned char>(source[i + 2])));
            char c3 = static_cast<char>(std::tolower(static_cast<unsigned char>(source[i + 3])));

            if (c0 == 'u' && c1 == 'n' && c2 == 'i' && c3 == 't')
            {
                // Check it's followed by whitespace or end (not part of identifier)
                if (i + 4 >= source.size() ||
                    !std::isalnum(static_cast<unsigned char>(source[i + 4])))
                {
                    return true;
                }
            }
        }
        return false; // First keyword is not 'unit'
    }
    return false; // Empty/comment-only source
}

/// @brief Compile (and optionally execute) Pascal source according to config.
/// Handles both single-file and multi-file compilation.
int runFrontPascal(const FrontPascalConfig &config,
                   const std::vector<il::tools::common::LoadedSource> &sources,
                   il::support::SourceManager &sm)
{
    PascalCompilerOptions compilerOpts{};
    compilerOpts.boundsChecks = config.shared.boundsChecks;

    PascalCompilerResult result;

    if (sources.size() == 1)
    {
        // Single file - use simple compiler
        PascalCompilerInput compilerInput{sources[0].buffer, config.sourcePaths[0]};
        compilerInput.fileId = sources[0].fileId;
        result = compilePascal(compilerInput, compilerOpts, sm);
    }
    else
    {
        // Multiple files - separate units from program
        PascalMultiFileInput multiInput;
        bool foundProgram = false;

        for (size_t i = 0; i < sources.size(); ++i)
        {
            PascalCompilerInput input{sources[i].buffer, config.sourcePaths[i]};
            input.fileId = sources[i].fileId;

            if (isUnitSource(sources[i].buffer))
            {
                // This is a unit - add to units list
                multiInput.units.push_back(input);
            }
            else
            {
                // This is a program
                if (foundProgram)
                {
                    std::cerr << "error: multiple program files specified\n";
                    return 1;
                }
                multiInput.program = input;
                foundProgram = true;
            }
        }

        if (!foundProgram)
        {
            std::cerr << "error: no program file found (only units specified)\n";
            return 1;
        }

        result = compilePascalMultiFile(multiInput, compilerOpts, sm);
    }

    if (!result.succeeded())
    {
        result.diagnostics.printAll(std::cerr, &sm);
        return 1;
    }

    core::Module module = std::move(result.module);

    if (config.emitIl)
    {
        io::Serializer::write(module, std::cout);
        return 0;
    }

    // Verify IL structure before running
    auto verification = il::verify::Verifier::verify(module);
    if (!verification)
    {
        il::support::printDiag(verification.error(), std::cerr, &sm);
        return 1;
    }

    if (!config.shared.stdinPath.empty())
    {
        if (!freopen(config.shared.stdinPath.c_str(), "r", stdin))
        {
            std::cerr << "unable to open stdin file\n";
            return 1;
        }
    }

    // Use standard VM for debugging (when --debug-vm or --trace is specified)
    bool useStandardVm = config.debugVm || config.shared.trace.enabled();

    if (useStandardVm)
    {
        vm::TraceConfig traceCfg = config.shared.trace;
        traceCfg.sm = &sm;

        vm::RunConfig runCfg;
        runCfg.trace = traceCfg;
        runCfg.maxSteps = config.shared.maxSteps;
        runCfg.programArgs = config.programArgs;

        vm::Runner runner(module, std::move(runCfg));
        int rc = static_cast<int>(runner.run());

        const auto trapMessage = runner.lastTrapMessage();
        if (trapMessage)
        {
            if (config.shared.dumpTrap && !trapMessage->empty())
            {
                std::cerr << *trapMessage;
                if (trapMessage->back() != '\n')
                {
                    std::cerr << '\n';
                }
            }
            if (rc == 0)
            {
                rc = 1;
            }
        }
        return rc;
    }

    // Default: use fast bytecode VM with threaded dispatch
    il::tools::common::VMExecutorConfig vmConfig;
    vmConfig.outputTrapMessage = config.shared.dumpTrap;

    auto result = il::tools::common::executeBytecodeVM(module, vmConfig);
    return result.exitCode;
}

} // namespace

/// @brief Handle Pascal front-end subcommands with an externally managed source manager.
int cmdFrontPascalWithSourceManager(int argc, char **argv, il::support::SourceManager &sm)
{
    auto parsed = parseFrontPascalArgs(argc, argv);
    if (!parsed)
    {
        const auto &diag = parsed.error();
        il::support::printDiag(diag, std::cerr, &sm);
        usage();
        return 1;
    }

    FrontPascalConfig config = std::move(parsed.value());

    // Load all source files
    std::vector<il::tools::common::LoadedSource> sources;
    for (const auto &path : config.sourcePaths)
    {
        auto source = il::tools::common::loadSourceBuffer(path, sm);
        if (!source)
        {
            const auto &diag = source.error();
            il::support::printDiag(diag, std::cerr, &sm);
            return 1;
        }
        sources.push_back(std::move(source.value()));
    }

    return runFrontPascal(config, sources, sm);
}

/// @brief Top-level Pascal frontend command invoked by main().
int cmdFrontPascal(int argc, char **argv)
{
    SourceManager sm;
    return cmdFrontPascalWithSourceManager(argc, argv, sm);
}
