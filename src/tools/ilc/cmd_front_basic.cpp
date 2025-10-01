// File: src/tools/ilc/cmd_front_basic.cpp
// License: MIT License (see LICENSE).
// Purpose: BASIC front-end driver for ilc.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/codemap.md

#include "vm/Trace.hpp"
#include "cli.hpp"
#include "frontends/basic/BasicCompiler.hpp"
#include "il/io/Serializer.hpp"
#include "il/api/expected_api.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"
#include "vm/VM.hpp"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <optional>

using namespace il;
using namespace il::frontends::basic;
using namespace il::support;

namespace
{

struct FrontBasicConfig
{
    bool emitIl{false};
    bool run{false};
    std::string sourcePath;
    ilc::SharedCliOptions shared;
    std::optional<uint32_t> sourceFileId{};
};

struct LoadedSource
{
    std::string buffer;
    uint32_t fileId{0};
};

il::support::Expected<FrontBasicConfig> parseFrontBasicArgs(int argc, char **argv)
{
    FrontBasicConfig config{};
    for (int i = 0; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-emit-il")
        {
            if (i + 1 >= argc)
            {
                usage();
                return il::support::Expected<FrontBasicConfig>(
                    il::support::Diagnostic{il::support::Severity::Error,
                                             "missing BASIC source path", {}});
            }
            config.emitIl = true;
            config.sourcePath = argv[++i];
        }
        else if (arg == "-run")
        {
            if (i + 1 >= argc)
            {
                usage();
                return il::support::Expected<FrontBasicConfig>(
                    il::support::Diagnostic{il::support::Severity::Error,
                                             "missing BASIC source path", {}});
            }
            config.run = true;
            config.sourcePath = argv[++i];
        }
        else
        {
            switch (ilc::parseSharedOption(i, argc, argv, config.shared))
            {
            case ilc::SharedOptionParseResult::Parsed:
                continue;
            case ilc::SharedOptionParseResult::Error:
                usage();
                return il::support::Expected<FrontBasicConfig>(
                    il::support::Diagnostic{il::support::Severity::Error,
                                             "failed to parse shared option", {}});
            case ilc::SharedOptionParseResult::NotMatched:
                usage();
                return il::support::Expected<FrontBasicConfig>(
                    il::support::Diagnostic{il::support::Severity::Error,
                                             "unknown flag", {}});
            }
        }
    }

    if ((config.emitIl == config.run) || config.sourcePath.empty())
    {
        usage();
        return il::support::Expected<FrontBasicConfig>(
            il::support::Diagnostic{il::support::Severity::Error,
                                     "specify exactly one of -emit-il or -run", {}});
    }

    return il::support::Expected<FrontBasicConfig>(std::move(config));
}

il::support::Expected<LoadedSource> loadSourceBuffer(const std::string &path,
                                                     il::support::SourceManager &sm)
{
    std::ifstream in(path);
    if (!in)
    {
        return il::support::Expected<LoadedSource>(
            il::support::Diagnostic{il::support::Severity::Error,
                                     "unable to open " + path, {}});
    }

    std::ostringstream ss;
    ss << in.rdbuf();

    LoadedSource source{};
    source.buffer = ss.str();
    source.fileId = sm.addFile(path);
    return il::support::Expected<LoadedSource>(std::move(source));
}

int runFrontBasic(const FrontBasicConfig &config, const std::string &source,
                  il::support::SourceManager &sm)
{
    BasicCompilerOptions compilerOpts{};
    compilerOpts.boundsChecks = config.shared.boundsChecks;

    BasicCompilerInput compilerInput{source, config.sourcePath};
    compilerInput.fileId = config.sourceFileId;

    auto result = compileBasic(compilerInput, compilerOpts, sm);
    if (!result.succeeded())
    {
        if (result.emitter)
        {
            result.emitter->printAll(std::cerr);
        }
        return 1;
    }

    core::Module module = std::move(result.module);

    if (config.emitIl)
    {
        io::Serializer::write(module, std::cout);
        return 0;
    }

    auto verification = il::verify::Verifier::verify(module);
    if (!verification)
    {
        il::support::printDiag(verification.error(), std::cerr);
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
    vm::VM vm(module, traceCfg, config.shared.maxSteps);
    return static_cast<int>(vm.run());
}

} // namespace

/**
 * @brief Handle BASIC front-end subcommands.
 *
 * The driver now factors argument parsing, source loading, and execution into
 * dedicated helpers to make future reuse easier while preserving the
 * externally observable CLI behaviour.
 */
int cmdFrontBasic(int argc, char **argv)
{
    auto parsed = parseFrontBasicArgs(argc, argv);
    if (!parsed)
    {
        return 1;
    }

    FrontBasicConfig config = std::move(parsed.value());

    SourceManager sm;
    auto source = loadSourceBuffer(config.sourcePath, sm);
    if (!source)
    {
        std::cerr << source.error().message << "\n";
        return 1;
    }

    config.sourceFileId = source.value().fileId;
    return runFrontBasic(config, source.value().buffer, sm);
}
