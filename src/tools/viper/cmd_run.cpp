//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the `viper run` and `viper build` subcommands. These provide a
// unified, frontend-agnostic interface for compiling and executing Viper
// projects. The commands delegate to the appropriate frontend (Zia or BASIC)
// based on language detection by the project loader.
//
//===----------------------------------------------------------------------===//

#include "cli.hpp"
#include "frontends/basic/BasicCompiler.hpp"
#include "frontends/zia/Compiler.hpp"
#include "frontends/zia/Warnings.hpp"
#include "il/api/expected_api.hpp"
#include "il/link/InteropThunks.hpp"
#include "il/link/ModuleLinker.hpp"
#include "il/transform/PassManager.hpp"
#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"
#include "tools/common/ScopedProcess.hpp"
#include "tools/common/asset/AssetCompiler.hpp"
#include "tools/common/native_compiler.hpp"
#include "tools/common/packaging/PkgUtils.hpp"
#include "tools/common/project_loader.hpp"
#include "tools/common/source_loader.hpp"
#include "tools/common/vm_executor.hpp"
#include "tools/viper/DebugAdapter.hpp"
#include "viper/il/IO.hpp"
#include "viper/il/Verify.hpp"
#include "viper/vm/VM.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

using namespace il;
using namespace il::support;
using namespace il::tools::common;

namespace {

enum class RunMode { Run, Build };

/// @brief Return an ASCII-lowercased copy of @p value.
/// @details Project entry extension checks are command-line syntax, so ASCII folding is enough and
///          avoids locale-sensitive surprises when users write uppercase `.ZIA` or `.BAS` paths.
std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

/// @brief Pick a deterministic entry source for the non-entry side of a mixed project.
/// @details Mixed-language manifests have one true executable entry. The other language may still
///          need a file to seed frontend compilation. This helper keeps the historical behavior of
///          compiling that side while avoiding dependence on insertion order by sorting and then
///          preferring conventional `main` filenames.
std::string selectMixedLibraryEntry(std::vector<std::string> files, ProjectLang lang) {
    if (files.empty())
        return {};
    std::sort(files.begin(), files.end());
    const char *mainName = lang == ProjectLang::Zia ? "main.zia" : "main.bas";
    const auto it = std::find_if(files.begin(), files.end(), [&](const std::string &path) {
        return std::filesystem::path(path).filename() == mainName;
    });
    return it != files.end() ? *it : files.front();
}

/// @brief Removes a temporary file on scope exit unless no path was assigned.
/// @details Native builds create temporary asset blobs for linker input. This guard ensures those
/// blobs are deleted on success and on every early-return failure path.
class ScopedTempPath {
  public:
    ScopedTempPath() = default;

    explicit ScopedTempPath(std::string path) : path_(std::move(path)) {}

    ~ScopedTempPath() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
    }

    ScopedTempPath(const ScopedTempPath &) = delete;
    ScopedTempPath &operator=(const ScopedTempPath &) = delete;

    /// @brief Replace the guarded path, deleting the previous temp file first if one existed.
    void reset(std::string path) {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
        path_ = std::move(path);
    }

    /// @brief Return the guarded temp path as a string.
    const std::string &path() const {
        return path_;
    }

  private:
    std::string path_;
};

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

/// @brief Parsed configuration shared by the `run` and `build` subcommands.
/// @details CLI override fields (optimize level, build profile, arch, link mode,
///          Windows runtime) take precedence over the project manifest when set.
struct RunBuildConfig {
    RunMode mode{RunMode::Run};           ///< Whether this is a run or build invocation.
    std::string target{"."};              ///< Target file/dir/manifest (default: cwd).
    std::string outputPath;               ///< Output path for build (-o), empty for run.
    ilc::SharedCliOptions shared;         ///< Shared CLI settings (trace, dumps, etc.).
    std::vector<std::string> programArgs; ///< Args forwarded to the program after '--'.
    bool debugVm{false};                  ///< Use the standard VM for debugging (run only).
    bool debugAdapter{false};             ///< Run as an interactive debug adapter (run only).
    bool helpRequested{false};            ///< True when help was requested.
    bool noRuntimeNamespaces{false};      ///< Disable runtime namespace binding.

    // CLI overrides (take precedence over manifest)
    std::optional<std::string> optimizeLevelOverride;     ///< -O0/-O1/-O2 override.
    std::optional<std::string> buildProfileOverride;      ///< --build-profile override.
    std::optional<viper::tools::TargetArch> archOverride; ///< --arch override.
    std::optional<bool> fastLinkOverride;                 ///< --fast-link/--no-fast-link.
    std::optional<bool> windowsDebugRuntimeOverride;      ///< Windows debug/release runtime.
};

/// @brief Print usage for the `viper run` or `viper build` subcommand to stderr.
void printRunBuildUsage(RunMode mode) {
    if (mode == RunMode::Run) {
        std::cerr << "Usage: viper run [target] [options] [-- program-args...]\n"
                  << "\n"
                  << "Run a .zia file, .bas file, project directory, or viper.project.\n"
                  << "\n"
                  << "Run options:\n"
                  << "  --debug-vm                    Use the standard VM for debugging\n"
                  << "  --stdin-from FILE             Redirect stdin from file\n"
                  << "  --max-steps N                 Limit VM execution steps\n"
                  << "  --dump-trap                   Show detailed trap diagnostics\n"
                  << "  --trace[=il|src]              Enable execution tracing\n"
                  << "  --bounds-checks               Enable generated bounds checks\n"
                  << "  --no-bounds-checks            Disable generated bounds checks\n"
                  << "  --build-profile debug|balanced|release\n"
                  << "  -O0|-O1|-O2                   Override optimization level\n"
                  << "  -h, --help                    Show this help\n";
        return;
    }

    std::cerr << "Usage: viper build [target] [-o output] [options]\n"
              << "\n"
              << "Build IL or a native binary from a .zia file, .bas file, project directory, or "
                 "viper.project.\n"
              << "\n"
              << "Build options:\n"
              << "  -o PATH                       Output .il or native binary path\n"
              << "  --arch arm64|x64              Override native target architecture\n"
              << "  --fast-link | --no-fast-link  Override linker mode\n"
              << "  --windows-debug-runtime       Link Windows debug runtime\n"
              << "  --windows-release-runtime     Link Windows release runtime\n"
              << "  --build-profile debug|balanced|release\n"
              << "  -O0|-O1|-O2                   Override optimization level\n"
              << "  --bounds-checks               Enable generated bounds checks\n"
              << "  --no-bounds-checks            Disable generated bounds checks\n"
              << "  -h, --help                    Show this help\n";
}

/// @brief A compiled project module plus whether it has already been verified.
struct CompiledProjectModule {
    il::core::Module module; ///< The lowered IL module.
    bool verified{false};    ///< True if the module already passed verification.
};

/// @brief Map a build profile name to its default optimization level string.
/// @return "O0"/"O1"/"O2" for debug/balanced/release, or nullopt if unrecognized.
std::optional<std::string> optimizeForBuildProfile(std::string_view profile) {
    if (profile == "debug")
        return std::string("O0");
    if (profile == "balanced")
        return std::string("O1");
    if (profile == "release")
        return std::string("O2");
    return std::nullopt;
}

/// @brief Map an optimization level string to its numeric value.
/// @return 0/1/2 for "O0"/"O1"/"O2", or nullopt if unrecognized.
std::optional<int> optimizeLevelNumber(std::string_view level) {
    if (level == "O0")
        return 0;
    if (level == "O1")
        return 1;
    if (level == "O2")
        return 2;
    return std::nullopt;
}

/// @brief Print elapsed time for a compile @p phase when --time-compile is set.
/// @param shared Shared options (checked for the timeCompile flag).
/// @param phase Human-readable phase label.
/// @param start Phase start timestamp; elapsed is measured against now.
void printCompileTime(const ilc::SharedCliOptions &shared,
                      std::string_view phase,
                      std::chrono::steady_clock::time_point start) {
    if (!shared.timeCompile)
        return;
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
    std::cerr << "[time-compile] " << phase << " " << elapsed.count() << "ms\n";
}

/// @brief Decide whether function-level optimizer passes may run in parallel.
/// @details Parallelism is disabled when any per-pass verification or dump option
///          is active, since those require deterministic, observable ordering.
bool shouldEnableParallelFunctionPasses(const ilc::SharedCliOptions &shared) {
    return !shared.verifyEachPass && !shared.dumpILPasses && !shared.dumpIL && !shared.dumpILOpt &&
           !shared.dumpAst && !shared.dumpSemaAst && !shared.dumpTokens;
}

/// @brief Parse the arguments for `viper run`/`viper build` into a RunBuildConfig.
/// @details Recognises the target, output path, shared options, optimization/profile
///          and architecture/link overrides, and program arguments after `--`.
/// @param mode Whether parsing a run or build invocation (affects accepted flags).
/// @param argc Argument count.
/// @param argv Argument vector.
/// @return The parsed config, or a diagnostic on malformed arguments.
il::support::Expected<RunBuildConfig> parseRunBuildArgs(RunMode mode, int argc, char **argv) {
    RunBuildConfig config;
    config.mode = mode;

    bool hasTarget = false;

    for (int i = 0; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--") {
            if (mode == RunMode::Build) {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error,
                    "program arguments after -- are only valid with 'run'",
                    {},
                    {}});
            }
            for (int j = i + 1; j < argc; ++j)
                config.programArgs.emplace_back(argv[j]);
            break;
        } else if (arg == "--help" || arg == "-h") {
            config.helpRequested = true;
            return il::support::Expected<RunBuildConfig>(std::move(config));
        } else if (arg == "-o") {
            if (mode != RunMode::Build) {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "-o is only valid with 'build'", {}, {}});
            }
            if (i + 1 >= argc) {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "missing output path after -o", {}, {}});
            }
            config.outputPath = argv[++i];
        } else if (arg == "-O0" || arg == "-O1" || arg == "-O2") {
            config.optimizeLevelOverride = std::string(arg.substr(1));
        } else if (arg == "--build-profile") {
            if (i + 1 >= argc) {
                return il::support::Expected<RunBuildConfig>(
                    il::support::Diagnostic{il::support::Severity::Error,
                                            "--build-profile requires debug, balanced, or release",
                                            {},
                                            {}});
            }
            std::string_view value = argv[++i];
            if (!optimizeForBuildProfile(value)) {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error,
                    "--build-profile must be 'debug', 'balanced', or 'release'",
                    {},
                    {}});
            }
            config.buildProfileOverride = std::string(value);
        } else if (arg.substr(0, 16) == "--build-profile=") {
            std::string_view value = arg.substr(16);
            if (!optimizeForBuildProfile(value)) {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error,
                    "--build-profile must be 'debug', 'balanced', or 'release'",
                    {},
                    {}});
            }
            config.buildProfileOverride = std::string(value);
        } else if (arg == "--debug-vm") {
            if (mode != RunMode::Run) {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "--debug-vm is only valid with 'run'", {}, {}});
            }
            config.debugVm = true;
        } else if (arg == "--debug-adapter") {
            if (mode != RunMode::Run) {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error,
                    "--debug-adapter is only valid with 'run'",
                    {},
                    {}});
            }
            config.debugAdapter = true;
            // Debug unoptimized code so every source line and local survives for
            // breakpoints and variable inspection (unless the user overrode -O).
            if (!config.optimizeLevelOverride)
                config.optimizeLevelOverride = std::string("O0");
        } else if (arg == "--fast-link") {
            if (mode != RunMode::Build) {
                return il::support::Expected<RunBuildConfig>(
                    il::support::Diagnostic{il::support::Severity::Error,
                                            "--fast-link is only valid with 'build'",
                                            {},
                                            {}});
            }
            config.fastLinkOverride = true;
        } else if (arg == "--no-fast-link") {
            if (mode != RunMode::Build) {
                return il::support::Expected<RunBuildConfig>(
                    il::support::Diagnostic{il::support::Severity::Error,
                                            "--no-fast-link is only valid with 'build'",
                                            {},
                                            {}});
            }
            config.fastLinkOverride = false;
        } else if (arg == "--windows-debug-runtime") {
            if (mode != RunMode::Build) {
                return il::support::Expected<RunBuildConfig>(
                    il::support::Diagnostic{il::support::Severity::Error,
                                            "--windows-debug-runtime is only valid with 'build'",
                                            {},
                                            {}});
            }
            config.windowsDebugRuntimeOverride = true;
        } else if (arg == "--windows-release-runtime") {
            if (mode != RunMode::Build) {
                return il::support::Expected<RunBuildConfig>(
                    il::support::Diagnostic{il::support::Severity::Error,
                                            "--windows-release-runtime is only valid with 'build'",
                                            {},
                                            {}});
            }
            config.windowsDebugRuntimeOverride = false;
        } else if (arg == "--no-runtime-namespaces") {
            config.noRuntimeNamespaces = true;
        } else if (arg == "--arch") {
            if (mode != RunMode::Build) {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "--arch is only valid with 'build'", {}, {}});
            }
            if (i + 1 >= argc) {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "--arch requires arm64 or x64", {}, {}});
            }
            std::string_view val = argv[++i];
            if (val == "arm64")
                config.archOverride = viper::tools::TargetArch::ARM64;
            else if (val == "x64")
                config.archOverride = viper::tools::TargetArch::X64;
            else {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "--arch must be 'arm64' or 'x64'", {}, {}});
            }
        } else {
            switch (ilc::parseSharedOption(i, argc, argv, config.shared)) {
                case ilc::SharedOptionParseResult::Parsed:
                    continue;
                case ilc::SharedOptionParseResult::Error:
                    return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, ilc::lastSharedOptionError(), {}, {}});
                case ilc::SharedOptionParseResult::NotMatched:
                    if (!arg.empty() && arg[0] != '-' && !hasTarget) {
                        config.target = std::string(arg);
                        hasTarget = true;
                    } else {
                        return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                            il::support::Severity::Error,
                            std::string("unknown flag: ") + std::string(arg),
                            {},
                            {}});
                    }
                    break;
            }
        }
    }

    return il::support::Expected<RunBuildConfig>(std::move(config));
}

/// @brief Verify and execute an IL module.
int verifyAndExecute(il::core::Module &module,
                     const ilc::SharedCliOptions &shared,
                     const std::vector<std::string> &programArgs,
                     bool debugVm,
                     bool debugAdapter,
                     bool moduleAlreadyVerified,
                     il::support::SourceManager &sm) {
    if (!moduleAlreadyVerified &&
        !reportVerifierDiagnostics(
            module, std::cerr, sm, shared.diagnosticFormat, shared.showWarnings)) {
        return 1;
    }

    if (debugAdapter)
        return il::tools::debug::runDebugAdapter(module, programArgs, shared.maxSteps, sm);

    std::optional<viper::tools::ScopedStdinRedirect> stdinRedirect;
    if (!shared.stdinPath.empty()) {
        stdinRedirect.emplace(shared.stdinPath);
        if (!stdinRedirect->ok()) {
            std::cerr << "unable to open stdin file: " << stdinRedirect->errorMessage() << "\n";
            return 1;
        }
    }

    bool useStandardVm = debugVm || shared.trace.enabled();

    if (useStandardVm || shared.profile) {
        vm::TraceConfig traceCfg = shared.trace;
        traceCfg.sm = &sm;

        vm::RunConfig runCfg;
        runCfg.trace = traceCfg;
        runCfg.maxSteps = shared.maxSteps;
        runCfg.programArgs = programArgs;

        vm::Runner runner(module, std::move(runCfg));

        std::chrono::steady_clock::time_point startTime;
        if (shared.profile)
            startTime = std::chrono::steady_clock::now();

        const int64_t runResult = runner.run();
        int rc = 0;
        const auto intMin = static_cast<int64_t>(std::numeric_limits<int>::min());
        const auto intMax = static_cast<int64_t>(std::numeric_limits<int>::max());
        if (runResult < intMin || runResult > intMax) {
            std::cerr << "program return value " << runResult << " outside host int range ["
                      << intMin << ", " << intMax << "]\n";
            rc = 1;
        } else {
            rc = static_cast<int>(runResult);
        }

        std::chrono::steady_clock::time_point endTime;
        if (shared.profile)
            endTime = std::chrono::steady_clock::now();

        const auto trapMessage = runner.lastTrapMessage();
        if (trapMessage) {
            if (!trapMessage->empty()) {
                std::cerr << *trapMessage;
                if (trapMessage->back() != '\n')
                    std::cerr << '\n';
            }
            if (rc == 0)
                rc = 1;
        }

        if (shared.profile) {
            double ms = std::chrono::duration<double, std::milli>(endTime - startTime).count();
            std::cerr << "[SUMMARY] instr=" << runner.instructionCount() << " time_ms=" << ms
                      << "\n";
        }

        return rc;
    }

    il::tools::common::VMExecutorConfig vmConfig;
    vmConfig.programArgs = programArgs;
    vmConfig.outputTrapMessage = true;
    vmConfig.flushStdout = true;
    vmConfig.sourceManager = &sm;
    vmConfig.maxSteps = shared.maxSteps;

    auto vmResult = il::tools::common::executeBytecodeVM(module, vmConfig);
    return vmResult.exitCode;
}

/// @brief Compile a Zia project and return the module.
il::support::Expected<CompiledProjectModule> compileZiaProject(const ProjectConfig &project,
                                                               const ilc::SharedCliOptions &shared,
                                                               il::support::SourceManager &sm,
                                                               bool optimizeModule = true) {
    il::frontends::zia::CompilerOptions opts;
    opts.boundsChecks = project.boundsChecks;
    opts.overflowChecks = project.overflowChecks;
    opts.nullChecks = project.nullChecks;
    opts.allowUnsafePointers = shared.allowUnsafePointers;
    opts.dumpTokens = shared.dumpTokens;
    opts.dumpAst = shared.dumpAst;
    opts.dumpSemaAst = shared.dumpSemaAst;
    opts.dumpIL = shared.dumpIL;
    opts.dumpILOpt = shared.dumpILOpt;
    opts.dumpILPasses = shared.dumpILPasses;
    opts.verifyEachPass = shared.verifyEachPass;
    opts.passStats = shared.passStats;
    opts.timeCompile = shared.timeCompile;
    opts.parallelFunctionPasses = shouldEnableParallelFunctionPasses(shared);

    // Warning policy from CLI flags
    opts.warningPolicy.enableAll = shared.wall;
    opts.warningPolicy.warningsAsErrors = shared.werror;
    opts.warningPolicy.strictSafetyWarnings = shared.strictDiagnostics;
    for (const auto &w : shared.disabledWarnings) {
        if (auto code = il::frontends::zia::parseWarningCode(w))
            opts.warningPolicy.disabled.insert(*code);
    }

    if (!optimizeModule || project.optimizeLevel == "O0")
        opts.optLevel = il::frontends::zia::OptLevel::O0;
    else if (project.optimizeLevel == "O1")
        opts.optLevel = il::frontends::zia::OptLevel::O1;
    else if (project.optimizeLevel == "O2")
        opts.optLevel = il::frontends::zia::OptLevel::O2;

    const bool optimized = opts.optLevel != il::frontends::zia::OptLevel::O0;
    const bool needsLowerVerify = !optimized || shared.paranoidVerify || shared.verifyEachPass ||
                                  shared.dumpIL || shared.dumpILPasses;
    opts.verifyAfterLowering = needsLowerVerify;
    opts.verifyAfterOptimization = optimized || shared.paranoidVerify;

    auto result = il::frontends::zia::compileFile(project.entryFile, opts, sm);
    if (!result.succeeded() || (shared.showWarnings && result.diagnostics.warningCount() != 0)) {
        ilc::printDiagnosticEngine(result.diagnostics, std::cerr, &sm, shared.diagnosticFormat);
    }
    if (!result.succeeded()) {
        return il::support::Expected<CompiledProjectModule>(
            il::support::Diagnostic{il::support::Severity::Error, "compilation failed", {}, {}});
    }

    return CompiledProjectModule{std::move(result.module), result.moduleVerified};
}

/// @brief Compile a BASIC project and return the module.
il::support::Expected<CompiledProjectModule> compileBasicProject(
    const ProjectConfig &project,
    bool noRuntimeNamespaces,
    const ilc::SharedCliOptions &shared,
    il::support::SourceManager &sm,
    bool optimizeModule = true) {
    const auto readStart = std::chrono::steady_clock::now();
    auto source = loadSourceBuffer(project.entryFile, sm);
    if (!source) {
        ilc::printDiagnostic(source.error(), std::cerr, &sm, shared.diagnosticFormat);
        return il::support::Expected<CompiledProjectModule>(
            il::support::Diagnostic{il::support::Severity::Error, "failed to load source", {}, {}});
    }
    printCompileTime(shared, "basic.read", readStart);

    std::optional<viper::tools::ScopedEnvVar> noRuntimeNamespacesEnv;
    if (noRuntimeNamespaces) {
        noRuntimeNamespacesEnv.emplace("VIPER_NO_RUNTIME_NAMESPACES", "1");
        if (!noRuntimeNamespacesEnv->ok()) {
            return il::support::Expected<CompiledProjectModule>(il::support::Diagnostic{
                il::support::Severity::Error,
                noRuntimeNamespacesEnv->errorMessage(),
                {},
                {}});
        }
    }

    il::frontends::basic::BasicCompilerOptions opts;
    opts.boundsChecks = project.boundsChecks;
    opts.dumpTokens = shared.dumpTokens;
    opts.dumpAst = shared.dumpAst;
    opts.dumpIL = shared.dumpIL;
    opts.dumpILOpt = shared.dumpILOpt;
    opts.dumpILPasses = shared.dumpILPasses;
    opts.timeCompile = shared.timeCompile;
    opts.allowUnsafePointers = shared.allowUnsafePointers;

    il::frontends::basic::BasicCompilerInput input{source.value().buffer, project.entryFile};
    input.fileId = source.value().fileId;

    auto result = il::frontends::basic::compileBasic(input, opts, sm);
    const bool shouldPrintDiagnostics =
        !result.succeeded() || (shared.showWarnings && result.diagnostics.warningCount() != 0);
    if (shouldPrintDiagnostics && result.emitter) {
        if (shared.diagnosticFormat == ilc::DiagnosticFormat::Json) {
            ilc::printDiagnosticEngine(result.diagnostics, std::cerr, &sm, shared.diagnosticFormat);
        } else {
            result.emitter->printAll(std::cerr);
        }
    }
    if (!result.succeeded()) {
        return il::support::Expected<CompiledProjectModule>(
            il::support::Diagnostic{il::support::Severity::Error, "compilation failed", {}, {}});
    }

    // Apply the canonical IL optimizer pipeline based on the project's opt level.
    if (optimizeModule && project.optimizeLevel != "O0") {
        il::transform::PassManager pm;
        pm.setVerifyBetweenPasses(shared.verifyEachPass);
        pm.setReportPassStatistics(shared.passStats);
        pm.setInstrumentationStream(std::cerr);
        pm.enableParallelFunctionPasses(shouldEnableParallelFunctionPasses(shared));

        // Enable per-pass IL dumps when requested.
        if (shared.dumpILPasses) {
            pm.setPrintBeforeEach(true);
            pm.setPrintAfterEach(true);
        }

        const std::string pipelineId = (project.optimizeLevel == "O2") ? "O2" : "O1";
        result.moduleVerified = false;
        if (!pm.runPipeline(result.module, pipelineId)) {
            il::support::Diag diag{il::support::Severity::Error,
                                   "IL optimization pipeline '" + pipelineId +
                                       "' failed verification",
                                   {},
                                   "V-OPT-PIPELINE"};
            ilc::printDiagnostic(diag, std::cerr, &sm, shared.diagnosticFormat);
            return il::support::Expected<CompiledProjectModule>(il::support::Diagnostic{
                il::support::Severity::Error, "optimization failed", {}, "V-OPT-PIPELINE"});
        }

        if (!reportVerifierDiagnostics(
                result.module, std::cerr, sm, shared.diagnosticFormat, shared.showWarnings)) {
            return il::support::Expected<CompiledProjectModule>(
                il::support::Diagnostic{il::support::Severity::Error,
                                        "optimized BASIC IL failed verification",
                                        {},
                                        "V-OPT-VERIFY"});
        }
        result.moduleVerified = true;

        // Dump IL after the full optimization pipeline.
        if (shared.dumpILOpt) {
            std::cerr << "=== IL after optimization (" << project.optimizeLevel << ") ===\n";
            io::Serializer::write(result.module, std::cerr);
            std::cerr << "=== End IL ===\n";
        }
    }

    return CompiledProjectModule{std::move(result.module), result.moduleVerified};
}

/// @brief Compile a mixed-language project (Zia + BASIC) and link the modules.
il::support::Expected<CompiledProjectModule> compileMixedProject(
    const ProjectConfig &project,
    bool noRuntimeNamespaces,
    const ilc::SharedCliOptions &shared,
    il::support::SourceManager &sm) {
    // Determine entry language from file extension.
    std::string entryExt;
    if (project.entryFile.size() >= 4)
        entryExt = lowerAscii(project.entryFile.substr(project.entryFile.size() - 4));

    bool entryIsZia = (entryExt == ".zia");

    // Build a single-language project config for the entry module.
    ProjectConfig entryProject = project;
    entryProject.lang = entryIsZia ? ProjectLang::Zia : ProjectLang::Basic;
    entryProject.sourceFiles = entryIsZia ? project.ziaFiles : project.basicFiles;

    // Build a single-language project config for the library module.
    ProjectConfig libProject = project;
    libProject.lang = entryIsZia ? ProjectLang::Basic : ProjectLang::Zia;
    libProject.sourceFiles = entryIsZia ? project.basicFiles : project.ziaFiles;

    // Compile the entry module.
    il::support::Expected<CompiledProjectModule> entryResult =
        entryIsZia ? compileZiaProject(entryProject, shared, sm, false)
                   : compileBasicProject(entryProject, noRuntimeNamespaces, shared, sm, false);
    if (!entryResult)
        return entryResult;

    // Compile the library module (the other language).
    // Use the first library file as the entry file for the library project.
    if (libProject.sourceFiles.empty())
        return entryResult; // No library files, just return the entry module.

    libProject.entryFile = selectMixedLibraryEntry(libProject.sourceFiles, libProject.lang);
    if (libProject.entryFile.empty())
        return entryResult;
    il::support::Expected<CompiledProjectModule> libResult =
        entryIsZia ? compileBasicProject(libProject, noRuntimeNamespaces, shared, sm, false)
                   : compileZiaProject(libProject, shared, sm, false);
    if (!libResult)
        return libResult;

    // Generate boolean interop thunks.
    auto thunks =
        il::link::generateBooleanThunks(entryResult.value().module, libResult.value().module);
    for (auto &thunk : thunks)
        entryResult.value().module.functions.push_back(std::move(thunk.thunk));

    // Link the two modules.
    std::vector<il::core::Module> modules;
    modules.push_back(std::move(entryResult.value().module));
    modules.push_back(std::move(libResult.value().module));

    auto linkResult = il::link::linkModules(std::move(modules));
    if (!linkResult.succeeded()) {
        std::string errMsg = "link errors:";
        for (const auto &e : linkResult.errors)
            errMsg += "\n  " + e;
        return il::support::Expected<CompiledProjectModule>(
            il::support::Diagnostic{il::support::Severity::Error, errMsg, {}, {}});
    }

    CompiledProjectModule compiled{std::move(linkResult.module), false};
    if (project.optimizeLevel != "O0") {
        il::transform::PassManager pm;
        pm.setVerifyBetweenPasses(shared.verifyEachPass);
        pm.setReportPassStatistics(shared.passStats);
        pm.setInstrumentationStream(std::cerr);
        pm.enableParallelFunctionPasses(shouldEnableParallelFunctionPasses(shared));
        const std::string pipelineId = (project.optimizeLevel == "O2") ? "O2" : "O1";
        if (!pm.runPipeline(compiled.module, pipelineId)) {
            return il::support::Expected<CompiledProjectModule>(il::support::Diagnostic{
                il::support::Severity::Error, "linked mixed-module optimization failed", {}, {}});
        }
    }

    if (!reportVerifierDiagnostics(
            compiled.module, std::cerr, sm, shared.diagnosticFormat, shared.showWarnings)) {
        return il::support::Expected<CompiledProjectModule>(il::support::Diagnostic{
            il::support::Severity::Error, "linked mixed-module verification failed", {}, {}});
    }
    compiled.verified = true;
    return compiled;
}

/// @brief Common implementation for both run and build commands.
int runOrBuild(RunMode mode, int argc, char **argv) {
    auto parsed = parseRunBuildArgs(mode, argc, argv);
    if (!parsed) {
        const auto &diag = parsed.error();
        SourceManager sm;
        il::support::printDiag(diag, std::cerr, &sm);
        printRunBuildUsage(mode);
        return 1;
    }

    RunBuildConfig config = std::move(parsed.value());
    if (config.helpRequested) {
        printRunBuildUsage(mode);
        return 0;
    }

    // Resolve the project
    const auto projectStart = std::chrono::steady_clock::now();
    auto project = resolveProject(config.target);
    if (!project) {
        SourceManager sm;
        il::support::printDiag(project.error(), std::cerr, &sm);
        return 1;
    }

    ProjectConfig &proj = project.value();
    printCompileTime(config.shared, "project-resolve", projectStart);

    // Apply CLI overrides
    if (config.shared.boundsChecksSpecified)
        proj.boundsChecks = config.shared.boundsChecks;
    if (config.buildProfileOverride) {
        proj.buildProfile = *config.buildProfileOverride;
        proj.optimizeLevel = *optimizeForBuildProfile(*config.buildProfileOverride);
    }
    if (config.optimizeLevelOverride)
        proj.optimizeLevel = *config.optimizeLevelOverride;
    if (mode == RunMode::Run && !config.buildProfileOverride && !config.optimizeLevelOverride &&
        !proj.buildProfileExplicit && !proj.optimizeLevelExplicit) {
        proj.buildProfile = "debug";
        proj.optimizeLevel = "O0";
    }

    // Compile
    SourceManager sm;
    const auto compileStart = std::chrono::steady_clock::now();
    il::support::Expected<CompiledProjectModule> moduleResult =
        (proj.lang == ProjectLang::Mixed)
            ? compileMixedProject(proj, config.noRuntimeNamespaces, config.shared, sm)
        : (proj.lang == ProjectLang::Zia)
            ? compileZiaProject(proj, config.shared, sm)
            : compileBasicProject(proj, config.noRuntimeNamespaces, config.shared, sm);
    printCompileTime(config.shared, "source-to-il", compileStart);

    if (!moduleResult)
        return 1; // diagnostics already printed

    CompiledProjectModule compiled = std::move(moduleResult.value());
    il::core::Module module = std::move(compiled.module);
    bool moduleVerified = compiled.verified;

    // Build mode: emit IL or compile to native binary
    if (mode == RunMode::Build) {
        // Verify before emitting
        const auto verifyStart = std::chrono::steady_clock::now();
        if (!moduleVerified && !reportVerifierDiagnostics(module,
                                                          std::cerr,
                                                          sm,
                                                          config.shared.diagnosticFormat,
                                                          config.shared.showWarnings)) {
            return 1;
        }
        moduleVerified = true;
        printCompileTime(config.shared, "final-verify", verifyStart);

        // No -o: emit IL to stdout (backwards compat)
        if (config.outputPath.empty()) {
            io::Serializer::write(module, std::cout);
            return 0;
        }

        // -o path ends in .il: emit IL text (backwards compat)
        if (!viper::tools::isNativeOutputPath(config.outputPath)) {
            const auto outputParent = std::filesystem::path(config.outputPath).parent_path();
            if (!outputParent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(outputParent, ec);
                if (ec) {
                    std::cerr << "error: cannot create output directory: " << outputParent.string()
                              << ": " << ec.message() << "\n";
                    return 1;
                }
            }
            std::ostringstream ilText;
            io::Serializer::write(module, ilText);
            if (!ilText) {
                std::cerr << "error: failed to serialize IL for " << config.outputPath << "\n";
                return 1;
            }
            try {
                viper::pkg::writeTextFileAtomic(config.outputPath, ilText.str());
            } catch (const std::exception &ex) {
                std::cerr << "error: failed to write IL to " << config.outputPath << ": "
                          << ex.what() << "\n";
                return 1;
            }
            return 0;
        }

        // Native binary output: hand the verified module directly to codegen.
        auto arch = config.archOverride.value_or(viper::tools::detectHostArch());

        // Compile assets (embed → blob, pack → .vpa files)
        ScopedTempPath assetBlobTemp;
        std::string assetBlobPath;
        if (!proj.embedAssets.empty() || !proj.packGroups.empty()) {
            const auto assetStart = std::chrono::steady_clock::now();
            std::string outputDir = std::filesystem::path(config.outputPath).parent_path().string();
            if (outputDir.empty())
                outputDir = ".";

            std::string assetErr;
            auto bundle = viper::asset::compileAssets(proj, outputDir, assetErr);
            if (!bundle) {
                std::cerr << "error: asset compilation failed: " << assetErr << "\n";
                return 1;
            }

            // Write the VPA blob to a temp file; codegen injects it into .rodata
            // (the same path as `viper codegen --asset-blob`). We deliberately do
            // NOT also emit/link a separate asset .o here: doing both defines the
            // `viper_asset_blob` symbol twice and fails linking with
            // "multiply defined symbol 'viper_asset_blob'". assetObjPath stays
            // empty so compileModuleToNative skips the redundant extra object.
            if (!bundle->embeddedBlob.empty()) {
                assetBlobTemp.reset(viper::tools::generateTempAssetPath());
                assetBlobPath = assetBlobTemp.path();
                try {
                    viper::pkg::writeFileAtomic(assetBlobPath, bundle->embeddedBlob);
                } catch (const std::exception &ex) {
                    std::cerr << "error: failed to write temporary asset blob: " << assetBlobPath
                              << ": " << ex.what() << "\n";
                    return 1;
                }
            }
            printCompileTime(config.shared, "assets", assetStart);
        }

        const int backendOptimizeLevel = optimizeLevelNumber(proj.optimizeLevel).value_or(1);
        const bool fastLink = config.fastLinkOverride.value_or(backendOptimizeLevel == 0);
        const auto nativeStart = std::chrono::steady_clock::now();
        int rc = viper::tools::compileModuleToNative(std::move(module),
                                                     proj.entryFile,
                                                     config.outputPath,
                                                     arch,
                                                     assetBlobPath,
                                                     std::string{},
                                                     backendOptimizeLevel,
                                                     true,
                                                     moduleVerified,
                                                     config.shared.timeCompile,
                                                     fastLink,
                                                     config.windowsDebugRuntimeOverride);
        printCompileTime(config.shared, "native-codegen-link", nativeStart);
        return rc;
    }

    // Run mode: verify and execute
    return verifyAndExecute(module,
                            config.shared,
                            config.programArgs,
                            config.debugVm,
                            config.debugAdapter,
                            moduleVerified,
                            sm);
}

} // namespace

int cmdRun(int argc, char **argv) {
    return runOrBuild(RunMode::Run, argc, argv);
}

int cmdBuild(int argc, char **argv) {
    return runOrBuild(RunMode::Build, argc, argv);
}
