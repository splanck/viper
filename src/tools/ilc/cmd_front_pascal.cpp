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
#include "viper/il/IO.hpp"
#include "viper/il/Verify.hpp"
#include "viper/vm/VM.hpp"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
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
    std::string sourcePath;
    ilc::SharedCliOptions shared;
    std::optional<uint32_t> sourceFileId{};
    std::vector<std::string> programArgs;
};

struct LoadedSource
{
    std::string buffer;
    uint32_t fileId{0};
};

/// @brief Parse CLI arguments for the Pascal frontend subcommand.
il::support::Expected<FrontPascalConfig> parseFrontPascalArgs(int argc, char **argv)
{
    FrontPascalConfig config{};
    for (int i = 0; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-emit-il")
        {
            if (i + 1 >= argc)
            {
                return il::support::Expected<FrontPascalConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "missing Pascal source path", {}, {}});
            }
            config.emitIl = true;
            config.sourcePath = argv[++i];
        }
        else if (arg == "-run")
        {
            if (i + 1 >= argc)
            {
                return il::support::Expected<FrontPascalConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "missing Pascal source path", {}, {}});
            }
            config.run = true;
            config.sourcePath = argv[++i];
        }
        else if (arg == "--")
        {
            for (int j = i + 1; j < argc; ++j)
                config.programArgs.emplace_back(argv[j]);
            break;
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
                    return il::support::Expected<FrontPascalConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, "unknown flag", {}, {}});
            }
        }
    }

    if ((config.emitIl == config.run) || config.sourcePath.empty())
    {
        return il::support::Expected<FrontPascalConfig>(il::support::Diagnostic{
            il::support::Severity::Error, "specify exactly one of -emit-il or -run", {}, {}});
    }

    return il::support::Expected<FrontPascalConfig>(std::move(config));
}

/// @brief Load Pascal source text and register it with the source manager.
il::support::Expected<LoadedSource> loadSourceBuffer(const std::string &path,
                                                     il::support::SourceManager &sm)
{
    std::ifstream in(path);
    if (!in)
    {
        return il::support::Expected<LoadedSource>(il::support::Diagnostic{
            il::support::Severity::Error, "unable to open " + path, {}, {}});
    }

    std::ostringstream ss;
    ss << in.rdbuf();

    std::string contents = ss.str();

    const uint32_t fileId = sm.addFile(path);
    if (fileId == 0)
    {
        return il::support::Expected<LoadedSource>(il::support::makeError(
            {}, std::string{il::support::kSourceManagerFileIdOverflowMessage}));
    }

    LoadedSource source{};
    source.buffer = std::move(contents);
    source.fileId = fileId;
    return il::support::Expected<LoadedSource>(std::move(source));
}

/// @brief Compile (and optionally execute) Pascal source according to config.
int runFrontPascal(const FrontPascalConfig &config,
                   const std::string &source,
                   il::support::SourceManager &sm)
{
    PascalCompilerOptions compilerOpts{};
    compilerOpts.boundsChecks = config.shared.boundsChecks;

    PascalCompilerInput compilerInput{source, config.sourcePath};
    compilerInput.fileId = config.sourceFileId;

    auto result = compilePascal(compilerInput, compilerOpts, sm);
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

    auto source = loadSourceBuffer(config.sourcePath, sm);
    if (!source)
    {
        const auto &diag = source.error();
        il::support::printDiag(diag, std::cerr, &sm);
        return 1;
    }

    config.sourceFileId = source.value().fileId;
    return runFrontPascal(config, source.value().buffer, sm);
}

/// @brief Top-level Pascal frontend command invoked by main().
int cmdFrontPascal(int argc, char **argv)
{
    SourceManager sm;
    return cmdFrontPascalWithSourceManager(argc, argv, sm);
}
