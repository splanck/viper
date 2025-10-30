//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the `ilc front basic` subcommand. The driver parses BASIC source,
// optionally emits IL, or executes the compiled program inside the VM. Argument
// parsing, source loading, compilation, verification, and execution are staged
// into helpers so other tools can reuse the same behaviour.
//
//===----------------------------------------------------------------------===//

#include "cli.hpp"
#include "frontends/basic/BasicCompiler.hpp"
#include "il/api/expected_api.hpp"
#include "il/io/Serializer.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"
#include "vm/Trace.hpp"
#include "vm/VM.hpp"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

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

bool isSourceManagerOverflowDiag(const il::support::Diag &diag)
{
    return diag.message == il::support::kSourceManagerFileIdOverflowMessage;
}

/// @brief Parse CLI arguments for the BASIC frontend subcommand.
///
/// The routine accepts either `-emit-il <file>` or `-run <file>` plus shared
/// options reused by other ilc subcommands. The workflow iterates the argument
/// list, toggling configuration flags and delegating shared option parsing to
/// @ref ilc::parseSharedOption. Invalid combinations—such as specifying both
/// modes or omitting the source path—return an error diagnostic packaged inside
/// @ref il::support::Expected so callers can present consistent messaging.
///
/// @param argc Number of subcommand arguments (excluding `front basic`).
/// @param argv Argument vector pointing at UTF-8 encoded strings.
/// @return Populated configuration on success; otherwise a diagnostic describing
///         the parse failure.
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
                return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "missing BASIC source path", {}});
            }
            config.emitIl = true;
            config.sourcePath = argv[++i];
        }
        else if (arg == "-run")
        {
            if (i + 1 >= argc)
            {
                usage();
                return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "missing BASIC source path", {}});
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
                    return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, "failed to parse shared option", {}});
                case ilc::SharedOptionParseResult::NotMatched:
                    usage();
                    return il::support::Expected<FrontBasicConfig>(
                        il::support::Diagnostic{il::support::Severity::Error, "unknown flag", {}});
            }
        }
    }

    if ((config.emitIl == config.run) || config.sourcePath.empty())
    {
        usage();
        return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
            il::support::Severity::Error, "specify exactly one of -emit-il or -run", {}});
    }

    return il::support::Expected<FrontBasicConfig>(std::move(config));
}

/// @brief Load BASIC source text and register it with the source manager.
///
/// Opens @p path, reads the entire file into memory, stores the contents in a
/// @ref LoadedSource buffer, and registers the path with @p sm so diagnostics
/// can resolve the location later. Errors propagate as diagnostics inside an
/// @ref il::support::Expected value.
///
/// @param path Filesystem path to the BASIC source file.
/// @param sm Source manager tracking file identifiers for diagnostics.
/// @return Loaded source buffer on success; otherwise a diagnostic describing
///         the I/O failure.
il::support::Expected<LoadedSource> loadSourceBuffer(const std::string &path,
                                                     il::support::SourceManager &sm)
{
    std::ifstream in(path);
    if (!in)
    {
        return il::support::Expected<LoadedSource>(
            il::support::Diagnostic{il::support::Severity::Error, "unable to open " + path, {}});
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

/// @brief Compile (and optionally execute) BASIC source according to @p config.
///
/// Steps performed:
///   1. Configure the BASIC compiler options (bounds checks, source file ID).
///   2. Invoke @ref compileBasic and print diagnostics when compilation fails.
///   3. Either serialize the resulting IL (@c -emit-il) or verify and run it
///      inside the VM (@c -run), respecting shared CLI options like stdin
///      redirection, tracing, trap dumps, and maximum step counts.
///   4. When running the VM, translate trap messages into exit codes following
///      ilc conventions.
///
/// @param config Parsed command-line configuration.
/// @param source BASIC source buffer to compile.
/// @param sm Source manager for diagnostic printing and trace source lookup.
/// @return Zero on success; non-zero when compilation, verification, or
///         execution fails.
int runFrontBasic(const FrontBasicConfig &config,
                  const std::string &source,
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

    std::optional<il::support::Expected<void>> cachedVerification{};
    if (config.run)
    {
        auto initialVerify = il::verify::Verifier::verify(module);
        if (!initialVerify)
        {
            il::transform::SimplifyCFG simplifyCfg;
            for (auto &function : module.functions)
            {
                simplifyCfg.run(function);
            }

            cachedVerification = il::verify::Verifier::verify(module);
        }
        else
        {
            cachedVerification = std::move(initialVerify);
        }
    }

    if (config.emitIl)
    {
        io::Serializer::write(module, std::cout);
        return 0;
    }

    auto verification =
        cachedVerification ? std::move(*cachedVerification) : il::verify::Verifier::verify(module);
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
    vm::VM vm(module, traceCfg, config.shared.maxSteps);
    int rc = static_cast<int>(vm.run());
    const auto trapMessage = vm.lastTrapMessage();
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

/// @brief Handle BASIC front-end subcommands.
///
/// Coordinates the high-level workflow:
///   1. Parse the command-line arguments into a @ref FrontBasicConfig.
///   2. Load the requested BASIC source and register it with the source manager.
///   3. Delegate to @ref runFrontBasic to either emit IL or execute the program.
/// Any failure at these stages results in a non-zero exit status with diagnostics
/// already printed to stderr, matching the behaviour expected by ilc callers.
int cmdFrontBasicWithSourceManager(int argc, char **argv, il::support::SourceManager &sm)
{
    auto parsed = parseFrontBasicArgs(argc, argv);
    if (!parsed)
    {
        return 1;
    }

    FrontBasicConfig config = std::move(parsed.value());

    auto source = loadSourceBuffer(config.sourcePath, sm);
    if (!source)
    {
        const auto &diag = source.error();
        if (!isSourceManagerOverflowDiag(diag))
        {
            il::support::printDiag(diag, std::cerr, &sm);
        }
        return 1;
    }

    config.sourceFileId = source.value().fileId;
    return runFrontBasic(config, source.value().buffer, sm);
}

int cmdFrontBasic(int argc, char **argv)
{
    SourceManager sm;
    return cmdFrontBasicWithSourceManager(argc, argv, sm);
}
