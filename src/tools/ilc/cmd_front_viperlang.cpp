//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the `ilc front viperlang` subcommand.
//
//===----------------------------------------------------------------------===//

#include "cli.hpp"
#include "frontends/viperlang/Compiler.hpp"
#include "il/api/expected_api.hpp"
#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"
#include "viper/il/IO.hpp"
#include "viper/il/Verify.hpp"
#include "viper/vm/VM.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace il;
using namespace il::frontends::viperlang;
using namespace il::support;

namespace
{

struct FrontViperlangConfig
{
    bool emitIl{false};
    bool run{false};
    std::string sourcePath;
    ilc::SharedCliOptions shared;
    std::vector<std::string> programArgs;
};

il::support::Expected<FrontViperlangConfig> parseFrontViperlangArgs(int argc, char **argv)
{
    FrontViperlangConfig config{};

    for (int i = 0; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-emit-il")
        {
            config.emitIl = true;
        }
        else if (arg == "-run")
        {
            config.run = true;
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
                    return il::support::Expected<FrontViperlangConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, "failed to parse shared option", {}, {}});
                case ilc::SharedOptionParseResult::NotMatched:
                    if (!arg.empty() && arg[0] != '-')
                    {
                        config.sourcePath = arg;
                    }
                    else
                    {
                        return il::support::Expected<FrontViperlangConfig>(il::support::Diagnostic{
                            il::support::Severity::Error, "unknown flag: " + arg, {}, {}});
                    }
                    break;
            }
        }
    }

    if ((config.emitIl == config.run) || config.sourcePath.empty())
    {
        return il::support::Expected<FrontViperlangConfig>(il::support::Diagnostic{
            il::support::Severity::Error,
            "specify exactly one of -emit-il or -run, followed by source file",
            {},
            {}});
    }

    return il::support::Expected<FrontViperlangConfig>(std::move(config));
}

int runFrontViperlang(const FrontViperlangConfig &config,
                      const std::string &source,
                      il::support::SourceManager &sm)
{
    CompilerInput compilerInput{source, config.sourcePath};
    CompilerOptions compilerOpts{};

    auto result = compile(compilerInput, compilerOpts, sm);

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
                std::cerr << '\n';
        }
        if (rc == 0)
            rc = 1;
    }
    return rc;
}

} // namespace

int cmdFrontViperlang(int argc, char **argv)
{
    SourceManager sm;

    auto parsed = parseFrontViperlangArgs(argc, argv);
    if (!parsed)
    {
        const auto &diag = parsed.error();
        il::support::printDiag(diag, std::cerr, &sm);
        usage();
        return 1;
    }

    FrontViperlangConfig config = std::move(parsed.value());

    std::ifstream in(config.sourcePath);
    if (!in)
    {
        std::cerr << "error: unable to open " << config.sourcePath << "\n";
        return 1;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();

    sm.addFile(config.sourcePath);

    return runFrontViperlang(config, source, sm);
}
