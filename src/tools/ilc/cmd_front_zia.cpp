//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the `ilc front zia` subcommand.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief CLI implementation for the `ilc front zia` subcommand.
/// @details Handles argument parsing, compilation to IL, verification, and
///          optional execution using the VM for the Zia frontend.

#include "cli.hpp"
#include "frontends/zia/Compiler.hpp"
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
using namespace il::frontends::zia;
using namespace il::support;

namespace
{

/// @brief Parsed configuration for the Zia frontend subcommand.
/// @details Captures whether the user requested IL emission or execution, plus
///          shared CLI options and any extra program arguments.
struct FrontZiaConfig
{
    bool emitIl{false};                   ///< True when `-emit-il` is requested.
    bool run{false};                      ///< True when `-run` is requested.
    std::string sourcePath;               ///< Path to the input `.zia` source.
    ilc::SharedCliOptions shared;         ///< Shared CLI settings (trace, steps, IO).
    std::vector<std::string> programArgs; ///< Extra arguments forwarded to the program.
};

/// @brief Parse CLI arguments for the Zia frontend subcommand.
/// @details Recognizes `-emit-il` and `-run`, delegates shared flags to
///          @ref ilc::parseSharedOption, and collects any program arguments
///          after `--`. When parsing fails, a diagnostic is returned with a
///          precise message such as "unknown flag: X" or
///          "specify exactly one of -emit-il or -run, followed by source file".
/// @param argc Number of arguments in @p argv.
/// @param argv Argument vector for the subcommand (excluding `ilc` itself).
/// @return Expected configuration on success; diagnostic on failure.
il::support::Expected<FrontZiaConfig> parseFrontZiaArgs(int argc, char **argv)
{
    FrontZiaConfig config{};

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
                    return il::support::Expected<FrontZiaConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, "failed to parse shared option", {}, {}});
                case ilc::SharedOptionParseResult::NotMatched:
                    if (!arg.empty() && arg[0] != '-')
                    {
                        config.sourcePath = arg;
                    }
                    else
                    {
                        return il::support::Expected<FrontZiaConfig>(il::support::Diagnostic{
                            il::support::Severity::Error, "unknown flag: " + arg, {}, {}});
                    }
                    break;
            }
        }
    }

    if ((config.emitIl == config.run) || config.sourcePath.empty())
    {
        return il::support::Expected<FrontZiaConfig>(il::support::Diagnostic{
            il::support::Severity::Error,
            "specify exactly one of -emit-il or -run, followed by source file",
            {},
            {}});
    }

    return il::support::Expected<FrontZiaConfig>(std::move(config));
}

/// @brief Compile and optionally execute a Zia program.
/// @details Compiles the provided source into IL, emits IL when requested, or
///          otherwise verifies the IL and executes it via the VM. The runtime
///          configuration respects shared CLI options (trace, max steps, stdin,
///          and program arguments). Traps are reported to stderr, and the
///          return code is coerced to non-zero when a trap occurs.
/// @param config Parsed CLI configuration.
/// @param source Full source text to compile.
/// @param sm Source manager used for diagnostics and trace reporting.
/// @return Zero on success; non-zero on compile, verify, IO, or runtime errors.
int runFrontZia(const FrontZiaConfig &config,
                const std::string &source,
                il::support::SourceManager &sm)
{
    CompilerInput compilerInput{source, config.sourcePath};
    CompilerOptions compilerOpts{};
    compilerOpts.boundsChecks = config.shared.boundsChecks;

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

/// @brief Entry point for the `ilc front zia` subcommand.
/// @details Parses command-line flags, loads the source file, and delegates to
///          @ref runFrontZia for compilation and execution.
/// @param argc Number of arguments in @p argv.
/// @param argv Argument vector for the subcommand.
/// @return Zero on success; non-zero on parsing, IO, or runtime failure.
int cmdFrontZia(int argc, char **argv)
{
    SourceManager sm;

    auto parsed = parseFrontZiaArgs(argc, argv);
    if (!parsed)
    {
        const auto &diag = parsed.error();
        il::support::printDiag(diag, std::cerr, &sm);
        usage();
        return 1;
    }

    FrontZiaConfig config = std::move(parsed.value());

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

    return runFrontZia(config, source, sm);
}
