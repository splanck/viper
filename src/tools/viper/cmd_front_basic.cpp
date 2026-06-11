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
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"
#include "tools/common/ScopedProcess.hpp"
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

using namespace il;
using namespace il::frontends::basic;
using namespace il::support;

namespace {

/// @brief Run the IL verifier on @p module and print any diagnostics.
/// @details Collects up to 50 diagnostics; prints them (errors always, warnings
///          only when @p showWarnings) using the requested format.
/// @return true when the module has no verifier errors.
bool reportVerifierDiagnostics(il::core::Module &module,
                               std::ostream &err,
                               il::support::SourceManager &sm,
                               ilc::DiagnosticFormat format,
                               bool showWarnings) {
    il::support::DiagnosticEngine diagnostics;
    for (auto diag : il::verify::Verifier::verifyAll(module, 50))
        diagnostics.report(std::move(diag));
    if (diagnostics.errorCount() != 0 || (showWarnings && diagnostics.warningCount() != 0))
        ilc::printDiagnosticEngine(diagnostics, err, &sm, format);
    return diagnostics.errorCount() == 0;
}

/// @brief Parsed configuration for the `viper front basic` subcommand.
struct FrontBasicConfig {
    bool emitIl{false};                   ///< True when `-emit-il` is requested.
    bool run{false};                      ///< True when `-run` is requested.
    bool debugVm{false};                  ///< True to use standard VM for debugging.
    bool helpRequested{false};            ///< True when help was requested.
    std::string sourcePath;               ///< Path to the input `.bas` source.
    ilc::SharedCliOptions shared;         ///< Shared CLI settings (trace, steps, IO).
    std::optional<uint32_t> sourceFileId{}; ///< Source-manager file id, once registered.
    std::vector<std::string> programArgs; ///< Arguments to pass to BASIC program after '--'.
    bool noRuntimeNamespaces{false};      ///< Disable runtime namespace binding.
    std::string optLevel{"O0"}; ///< Optimization level: "O0", "O1", or "O2"; default = O0.
};

/// @brief Print usage for the `viper front basic` subcommand to stderr.
void frontBasicUsage() {
    std::cerr
        << "Usage: viper front basic (-emit-il|-run) <file.bas> [options] [-- program-args...]\n"
        << "\n"
        << "Options:\n"
        << "  -emit-il <file.bas>            Emit IL to stdout\n"
        << "  -run <file.bas>                Compile and execute\n"
        << "  -O0|-O1|-O2                    Select optimization level for normal runs\n"
        << "  --debug-vm                     Use the standard VM for debugging\n"
        << "  --trace[=il|src]               Enable execution tracing\n"
        << "  --stdin-from FILE              Redirect stdin from file\n"
        << "  --max-steps N                  Limit VM execution steps\n"
        << "  --dump-trap                    Show detailed trap diagnostics\n"
        << "  --bounds-checks                Enable generated bounds checks\n"
        << "  --no-bounds-checks             Disable generated bounds checks\n"
        << "  -h, --help                     Show this help\n";
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
il::support::Expected<FrontBasicConfig> parseFrontBasicArgs(int argc, char **argv) {
    FrontBasicConfig config{};
    for (int i = 0; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-emit-il") {
            if (i + 1 >= argc) {
                return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "missing BASIC source path", {}, {}});
            }
            config.emitIl = true;
            config.sourcePath = argv[++i];
        } else if (arg == "-run") {
            if (i + 1 >= argc) {
                return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "missing BASIC source path", {}, {}});
            }
            config.run = true;
            config.sourcePath = argv[++i];
        } else if (arg == "--") {
            // Remaining tokens are program arguments
            for (int j = i + 1; j < argc; ++j)
                config.programArgs.emplace_back(argv[j]);
            break;
        } else if (arg == "-O0") {
            config.optLevel = "O0";
        } else if (arg == "-O1" || arg == "-O2") {
            config.optLevel = std::string(arg.substr(1));
        } else if (arg == "--no-runtime-namespaces") {
            config.noRuntimeNamespaces = true;
        } else if (arg == "--debug-vm") {
            config.debugVm = true;
        } else if (arg == "--help" || arg == "-h") {
            config.helpRequested = true;
            return il::support::Expected<FrontBasicConfig>(std::move(config));
        } else {
            switch (ilc::parseSharedOption(i, argc, argv, config.shared)) {
                case ilc::SharedOptionParseResult::Parsed:
                    continue;
                case ilc::SharedOptionParseResult::Error:
                    return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, ilc::lastSharedOptionError(), {}, {}});
                case ilc::SharedOptionParseResult::NotMatched:
                    return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, "unknown flag", {}, {}});
            }
        }
    }

    if (!config.helpRequested && ((config.emitIl == config.run) || config.sourcePath.empty())) {
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
                  il::support::SourceManager &sm) {
    BasicCompilerOptions compilerOpts{};
    compilerOpts.boundsChecks = config.shared.boundsChecks;
    compilerOpts.dumpTokens = config.shared.dumpTokens;
    compilerOpts.dumpAst = config.shared.dumpAst;
    compilerOpts.dumpIL = config.shared.dumpIL;
    compilerOpts.dumpILOpt = config.shared.dumpILOpt;
    compilerOpts.dumpILPasses = config.shared.dumpILPasses;
    compilerOpts.allowUnsafePointers = config.shared.allowUnsafePointers;

    BasicCompilerInput compilerInput{source, config.sourcePath};
    compilerInput.fileId = config.sourceFileId;

    std::optional<viper::tools::ScopedEnvVar> noRuntimeNamespacesEnv;
    if (config.noRuntimeNamespaces) {
        noRuntimeNamespacesEnv.emplace("VIPER_NO_RUNTIME_NAMESPACES", "1");
        if (!noRuntimeNamespacesEnv->ok()) {
            std::cerr << "error: " << noRuntimeNamespacesEnv->errorMessage() << "\n";
            return 1;
        }
    }

    auto result = compileBasic(compilerInput, compilerOpts, sm);
    const bool shouldPrintDiagnostics =
        !result.succeeded() ||
        (config.shared.showWarnings && result.diagnostics.warningCount() != 0);
    if (shouldPrintDiagnostics) {
        if (result.emitter) {
            if (config.shared.diagnosticFormat == ilc::DiagnosticFormat::Json) {
                ilc::printDiagnosticEngine(
                    result.diagnostics, std::cerr, &sm, config.shared.diagnosticFormat);
            } else {
                result.emitter->printAll(std::cerr);
            }
        }
    }
    if (!result.succeeded()) {
        return 1;
    }

    core::Module module = std::move(result.module);

    if (config.emitIl) {
        if (!reportVerifierDiagnostics(
                module, std::cerr, sm, config.shared.diagnosticFormat, false)) {
            return 1;
        }
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
    // when the module is already ill-formed so the verifier can emit the full
    // diagnostic set with proper file locations.
    if (!useStandardVm && !config.optLevel.empty()) {
        if (reportVerifierDiagnostics(
                module, std::cerr, sm, config.shared.diagnosticFormat, false)) {
            il::transform::PassManager pm;
            pm.setVerifyBetweenPasses(config.shared.verifyEachPass);
            pm.setReportPassStatistics(config.shared.passStats);
            pm.setInstrumentationStream(std::cerr);

            // Enable per-pass IL dumps when requested.
            if (config.shared.dumpILPasses) {
                pm.setPrintBeforeEach(true);
                pm.setPrintAfterEach(true);
            }

            if (!pm.runPipeline(module, config.optLevel)) {
                ilc::printDiagnostic(il::support::Diag{il::support::Severity::Error,
                                                       "IL optimization pipeline '" +
                                                           config.optLevel +
                                                           "' failed verification",
                                                       {},
                                                       "V-OPT-PIPELINE"},
                                     std::cerr,
                                     &sm,
                                     config.shared.diagnosticFormat);
                return 1;
            }

            // Dump IL after the full optimization pipeline.
            if (config.shared.dumpILOpt) {
                std::cerr << "=== IL after optimization (" << config.optLevel << ") ===\n";
                io::Serializer::write(module, std::cerr);
                std::cerr << "=== End IL ===\n";
            }
        } else {
            return 1;
        }
    }

    if (!reportVerifierDiagnostics(
            module, std::cerr, sm, config.shared.diagnosticFormat, config.shared.showWarnings)) {
        return 1;
    }

    std::optional<viper::tools::ScopedStdinRedirect> stdinRedirect;
    if (!config.shared.stdinPath.empty()) {
        stdinRedirect.emplace(config.shared.stdinPath);
        if (!stdinRedirect->ok()) {
            std::cerr << "unable to open stdin file: " << stdinRedirect->errorMessage() << "\n";
            return 1;
        }
    }

    if (useStandardVm) {
        vm::TraceConfig traceCfg = config.shared.trace;
        traceCfg.sm = &sm;

        vm::RunConfig runCfg;
        runCfg.trace = traceCfg;
        runCfg.maxSteps = config.shared.maxSteps;
        runCfg.programArgs = config.programArgs;

        vm::Runner runner(module, std::move(runCfg));
        int rc = static_cast<int>(runner.run());
        const auto trapMessage = runner.lastTrapMessage();
        if (trapMessage) {
            if (!trapMessage->empty()) {
                std::cerr << *trapMessage;
                if (trapMessage->back() != '\n') {
                    std::cerr << '\n';
                }
            }
            if (rc == 0) {
                rc = 1;
            }
        }
        return rc;
    }

    // Default: use fast bytecode VM with threaded dispatch
    il::tools::common::VMExecutorConfig vmConfig;
    vmConfig.programArgs = config.programArgs;
    vmConfig.outputTrapMessage = true;
    vmConfig.sourceManager = &sm;
    vmConfig.maxSteps = config.shared.maxSteps;

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
int cmdFrontBasicWithSourceManager(int argc, char **argv, il::support::SourceManager &sm) {
    auto parsed = parseFrontBasicArgs(argc, argv);
    if (!parsed) {
        const auto &diag = parsed.error();
        il::support::printDiag(diag, std::cerr, &sm);
        frontBasicUsage();
        return 1;
    }

    FrontBasicConfig config = std::move(parsed.value());
    if (config.helpRequested) {
        frontBasicUsage();
        return 0;
    }

    auto source = il::tools::common::loadSourceBuffer(config.sourcePath, sm);
    if (!source) {
        const auto &diag = source.error();
        ilc::printDiagnostic(diag, std::cerr, &sm, config.shared.diagnosticFormat);
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
int cmdFrontBasic(int argc, char **argv) {
    SourceManager sm;
    return cmdFrontBasicWithSourceManager(argc, argv, sm);
}
