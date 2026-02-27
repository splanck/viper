//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the `viper front basic` subcommand. The driver parses BASIC source,
// optionally emits IL, or executes the compiled program inside the VM. Argument
// parsing, source loading, compilation, verification, and execution are staged
// into helpers so other tools can reuse the same behaviour.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Command-line entry point for the BASIC frontend of `viper`.
/// @details Documents the supporting helpers used to parse arguments, load
///          BASIC source files, and either emit IL or execute the program
///          through the virtual machine.  The implementation keeps side effects
///          (like diagnostic printing and VM execution) in well-contained
///          functions so higher-level tooling can reuse them.

#include "cli.hpp"
#include "frontends/basic/BasicCompiler.hpp"
#include "il/api/expected_api.hpp"
#include "il/transform/PassManager.hpp"
#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"
#include "tools/common/source_loader.hpp"
#include "tools/common/vm_executor.hpp"
#include "viper/il/IO.hpp"
#include "viper/il/Verify.hpp"
#include "viper/vm/VM.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
// Windows doesn't have setenv, use _putenv_s instead
inline int setenv(const char *name, const char *value, int /*overwrite*/)
{
    return _putenv_s(name, value);
}
#endif

using namespace il;
using namespace il::frontends::basic;
using namespace il::support;

namespace
{

struct FrontBasicConfig
{
    bool emitIl{false};
    bool run{false};
    bool debugVm{false}; ///< True to use standard VM for debugging.
    std::string sourcePath;
    ilc::SharedCliOptions shared;
    std::optional<uint32_t> sourceFileId{};
    std::vector<std::string> programArgs; ///< Arguments to pass to BASIC program after '--'.
    bool noRuntimeNamespaces{false};
    std::string optLevel{"O0"}; ///< Optimization level: "O0", "O1", or "O2"; default = O0.
};

/// @brief Identify diagnostics that reflect SourceManager identifier overflow.
/// @details The BASIC driver must suppress SourceManager overflow diagnostics
///          because the error itself already gets surfaced during file loading.
///          This helper compares the diagnostic message against the canonical
///          overflow text so callers can detect and elide the redundant report.
/// @param diag Diagnostic produced by helper routines while loading sources.
/// @return @c true when the diagnostic indicates SourceManager overflow.
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
        std::string_view arg = argv[i];
        if (arg == "-emit-il")
        {
            if (i + 1 >= argc)
            {
                return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "missing BASIC source path", {}, {}});
            }
            config.emitIl = true;
            config.sourcePath = argv[++i];
        }
        else if (arg == "-run")
        {
            if (i + 1 >= argc)
            {
                return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "missing BASIC source path", {}, {}});
            }
            config.run = true;
            config.sourcePath = argv[++i];
        }
        else if (arg == "--")
        {
            // Remaining tokens are program arguments
            for (int j = i + 1; j < argc; ++j)
                config.programArgs.emplace_back(argv[j]);
            break;
        }
        else if (arg == "-O0")
        {
            config.optLevel = "O0";
        }
        else if (arg == "-O1" || arg == "-O2")
        {
            config.optLevel = std::string(arg.substr(1));
        }
        else if (arg == "--no-runtime-namespaces")
        {
            config.noRuntimeNamespaces = true;
        }
        else if (arg == "--debug-vm")
        {
            config.debugVm = true;
        }
        else
        {
            switch (ilc::parseSharedOption(i, argc, argv, config.shared))
            {
                case ilc::SharedOptionParseResult::Parsed:
                    continue;
                case ilc::SharedOptionParseResult::Error:
                    return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, "failed to parse shared option", {}, {}});
                case ilc::SharedOptionParseResult::NotMatched:
                    return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, "unknown flag", {}, {}});
            }
        }
    }

    if ((config.emitIl == config.run) || config.sourcePath.empty())
    {
        return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
            il::support::Severity::Error, "specify exactly one of -emit-il or -run", {}, {}});
    }

    return il::support::Expected<FrontBasicConfig>(std::move(config));
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
    compilerOpts.dumpTokens = config.shared.dumpTokens;
    compilerOpts.dumpAst = config.shared.dumpAst;
    compilerOpts.dumpIL = config.shared.dumpIL;
    compilerOpts.dumpILOpt = config.shared.dumpILOpt;
    compilerOpts.dumpILPasses = config.shared.dumpILPasses;

    BasicCompilerInput compilerInput{source, config.sourcePath};
    compilerInput.fileId = config.sourceFileId;

    // Feature control: default ON; allow disabling via CLI for debugging/tests via env var.
    if (config.noRuntimeNamespaces)
    {
        setenv("VIPER_NO_RUNTIME_NAMESPACES", "1", 1);
    }

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

    if (config.emitIl)
    {
        io::Serializer::write(module, std::cout);
        return 0;
    }

    // Debugging and source-trace modes preserve the original IL structure so that
    // IP-to-source-location mappings remain accurate.  Optimization is skipped
    // for those paths; it is applied unconditionally for normal (fast-VM) runs.
    bool useStandardVm = config.debugVm || config.shared.trace.enabled();

    // Apply the IL optimizer pipeline for normal (non-debug) runs.  Debug and
    // trace modes skip optimization because block merging / elimination would
    // invalidate the IP-to-source-location table, breaking trace output.
    //
    // Verify the module BEFORE handing it to the optimizer: in debug builds the
    // optimizer asserts on invalid IL via verifyPreconditions, which would crash
    // instead of producing a nice source-annotated diagnostic.  Skip optimization
    // (and cache the failure) when the module is already ill-formed so that the
    // reporter below can emit the verifier error with proper file locations.
    if (!useStandardVm && !config.optLevel.empty())
    {
        auto preOptVerify = il::verify::Verifier::verify(module);
        if (preOptVerify)
        {
            il::transform::PassManager pm;
            pm.setVerifyBetweenPasses(false);

            // Enable per-pass IL dumps when requested.
            if (config.shared.dumpILPasses)
            {
                pm.setPrintBeforeEach(true);
                pm.setPrintAfterEach(true);
                pm.setInstrumentationStream(std::cerr);
            }

            pm.runPipeline(module, config.optLevel);

            // Dump IL after the full optimization pipeline.
            if (config.shared.dumpILOpt)
            {
                std::cerr << "=== IL after optimization (" << config.optLevel << ") ===\n";
                io::Serializer::write(module, std::cerr);
                std::cerr << "=== End IL ===\n";
            }
        }
        else
        {
            cachedVerification = std::move(preOptVerify);
        }
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
    vmConfig.programArgs = config.programArgs;
    vmConfig.outputTrapMessage = true;

    auto vmResult = il::tools::common::executeBytecodeVM(module, vmConfig);
    return vmResult.exitCode;
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
        const auto &diag = parsed.error();
        il::support::printDiag(diag, std::cerr, &sm);
        usage();
        return 1;
    }

    FrontBasicConfig config = std::move(parsed.value());

    auto source = il::tools::common::loadSourceBuffer(config.sourcePath, sm);
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

/// @brief Top-level BASIC frontend command invoked by main().
/// @details Instantiates a fresh @ref il::support::SourceManager so diagnostics
///          can resolve file paths and then forwards to
///          @ref cmdFrontBasicWithSourceManager for the actual workflow.
///          Keeping this entry point tiny allows tests to exercise the driver
///          with injected source managers while production builds use the
///          default implementation.
int cmdFrontBasic(int argc, char **argv)
{
    SourceManager sm;
    return cmdFrontBasicWithSourceManager(argc, argv, sm);
}
