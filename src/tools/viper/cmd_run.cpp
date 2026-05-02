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
#include "tools/common/asset/AssetCompiler.hpp"
#include "tools/common/native_compiler.hpp"
#include "tools/common/project_loader.hpp"
#include "tools/common/source_loader.hpp"
#include "tools/common/vm_executor.hpp"
#include "viper/il/IO.hpp"
#include "viper/il/Verify.hpp"
#include "viper/vm/VM.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
inline int setenv(const char *name, const char *value, int) {
    return _putenv_s(name, value);
}
#endif

using namespace il;
using namespace il::support;
using namespace il::tools::common;

namespace {

enum class RunMode { Run, Build };

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

struct RunBuildConfig {
    RunMode mode{RunMode::Run};
    std::string target{"."};
    std::string outputPath;
    ilc::SharedCliOptions shared;
    std::vector<std::string> programArgs;
    bool debugVm{false};
    bool noRuntimeNamespaces{false};

    // CLI overrides (take precedence over manifest)
    std::optional<std::string> optimizeLevelOverride;
    std::optional<std::string> buildProfileOverride;
    std::optional<viper::tools::TargetArch> archOverride;
};

struct CompiledProjectModule {
    il::core::Module module;
    bool verified{false};
};

std::optional<std::string> optimizeForBuildProfile(std::string_view profile) {
    if (profile == "debug")
        return std::string("O0");
    if (profile == "balanced")
        return std::string("O1");
    if (profile == "release")
        return std::string("O2");
    return std::nullopt;
}

std::optional<int> optimizeLevelNumber(std::string_view level) {
    if (level == "O0")
        return 0;
    if (level == "O1")
        return 1;
    if (level == "O2")
        return 2;
    return std::nullopt;
}

void printCompileTime(const ilc::SharedCliOptions &shared,
                      std::string_view phase,
                      std::chrono::steady_clock::time_point start) {
    if (!shared.timeCompile)
        return;
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start);
    std::cerr << "[time-compile] " << phase << " " << elapsed.count() << "ms\n";
}

bool shouldEnableParallelFunctionPasses(const ilc::SharedCliOptions &shared) {
    return !shared.verifyEachPass && !shared.dumpILPasses && !shared.dumpIL &&
           !shared.dumpILOpt && !shared.dumpAst && !shared.dumpSemaAst && !shared.dumpTokens;
}

il::support::Expected<RunBuildConfig> parseRunBuildArgs(RunMode mode, int argc, char **argv) {
    RunBuildConfig config;
    config.mode = mode;

    bool hasTarget = false;

    for (int i = 0; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--") {
            for (int j = i + 1; j < argc; ++j)
                config.programArgs.emplace_back(argv[j]);
            break;
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
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error,
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
            config.debugVm = true;
        } else if (arg == "--no-runtime-namespaces") {
            config.noRuntimeNamespaces = true;
        } else if (arg == "--arch") {
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
                        il::support::Severity::Error, "failed to parse shared option", {}, {}});
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
                     bool moduleAlreadyVerified,
                     il::support::SourceManager &sm) {
    if (!moduleAlreadyVerified &&
        !reportVerifierDiagnostics(
            module, std::cerr, sm, shared.diagnosticFormat, shared.showWarnings)) {
        return 1;
    }

    if (!shared.stdinPath.empty()) {
        if (!freopen(shared.stdinPath.c_str(), "r", stdin)) {
            std::cerr << "unable to open stdin file\n";
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

        int rc = static_cast<int>(runner.run());

        std::chrono::steady_clock::time_point endTime;
        if (shared.profile)
            endTime = std::chrono::steady_clock::now();

        const auto trapMessage = runner.lastTrapMessage();
        if (trapMessage) {
            if (shared.dumpTrap && !trapMessage->empty()) {
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
    opts.dumpTokens = shared.dumpTokens;
    opts.dumpAst = shared.dumpAst;
    opts.dumpSemaAst = shared.dumpSemaAst;
    opts.dumpIL = shared.dumpIL;
    opts.dumpILOpt = shared.dumpILOpt;
    opts.dumpILPasses = shared.dumpILPasses;
    opts.verifyEachPass = shared.verifyEachPass;
    opts.passStats = shared.timeCompile;
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

    auto result = il::frontends::zia::compileFile(project.entryFile, opts, sm);
    if (!result.succeeded() ||
        (shared.showWarnings && result.diagnostics.warningCount() != 0)) {
        ilc::printDiagnosticEngine(result.diagnostics, std::cerr, &sm, shared.diagnosticFormat);
    }
    if (!result.succeeded()) {
        return il::support::Expected<CompiledProjectModule>(
            il::support::Diagnostic{il::support::Severity::Error, "compilation failed", {}, {}});
    }

    return CompiledProjectModule{std::move(result.module), result.moduleVerified};
}

/// @brief Compile a BASIC project and return the module.
il::support::Expected<CompiledProjectModule> compileBasicProject(const ProjectConfig &project,
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

    if (noRuntimeNamespaces)
        setenv("VIPER_NO_RUNTIME_NAMESPACES", "1", 1);

    il::frontends::basic::BasicCompilerOptions opts;
    opts.boundsChecks = project.boundsChecks;
    opts.dumpTokens = shared.dumpTokens;
    opts.dumpAst = shared.dumpAst;
    opts.dumpIL = shared.dumpIL;
    opts.dumpILOpt = shared.dumpILOpt;
    opts.dumpILPasses = shared.dumpILPasses;
    opts.timeCompile = shared.timeCompile;

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
        pm.setReportPassStatistics(shared.timeCompile);
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
            return il::support::Expected<CompiledProjectModule>(il::support::Diagnostic{
                il::support::Severity::Error,
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
il::support::Expected<CompiledProjectModule> compileMixedProject(const ProjectConfig &project,
                                                                 bool noRuntimeNamespaces,
                                                                 const ilc::SharedCliOptions &shared,
                                                                 il::support::SourceManager &sm) {
    // Determine entry language from file extension.
    std::string entryExt;
    if (project.entryFile.size() >= 4)
        entryExt = project.entryFile.substr(project.entryFile.size() - 4);

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

    libProject.entryFile = libProject.sourceFiles[0];
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
        pm.setReportPassStatistics(shared.timeCompile);
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
        usage();
        return 1;
    }

    RunBuildConfig config = std::move(parsed.value());

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
        if (!moduleVerified &&
            !reportVerifierDiagnostics(
                module, std::cerr, sm, config.shared.diagnosticFormat, config.shared.showWarnings)) {
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
            std::ofstream outFile(config.outputPath);
            if (!outFile.is_open()) {
                std::cerr << "error: cannot open output file: " << config.outputPath << "\n";
                return 1;
            }
            io::Serializer::write(module, outFile);
            if (!outFile) {
                std::cerr << "error: failed to write IL to " << config.outputPath << "\n";
                return 1;
            }
            return 0;
        }

        // Native binary output: hand the verified module directly to codegen.
        auto arch = config.archOverride.value_or(viper::tools::detectHostArch());

        // Compile assets (embed → blob, pack → .vpa files)
        std::string assetBlobPath;
        std::string assetObjPath;
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

            // Write asset blob to .o using Viper's own object file writer
            if (!bundle->embeddedBlob.empty()) {
                assetBlobPath = viper::tools::generateTempAssetPath();
                assetObjPath = assetBlobPath + ".o";
                if (!viper::asset::writeAssetBlobObject(
                        bundle->embeddedBlob, assetObjPath, assetErr)) {
                    std::cerr << "error: " << assetErr << "\n";
                    return 1;
                }
            }
            printCompileTime(config.shared, "assets", assetStart);
        }

        const int backendOptimizeLevel = optimizeLevelNumber(proj.optimizeLevel).value_or(1);
        const auto nativeStart = std::chrono::steady_clock::now();
        int rc = viper::tools::compileModuleToNative(std::move(module),
                                                     proj.entryFile,
                                                     config.outputPath,
                                                     arch,
                                                     assetBlobPath,
                                                     assetObjPath,
                                                     backendOptimizeLevel,
                                                     true,
                                                     moduleVerified,
                                                     config.shared.timeCompile);
        printCompileTime(config.shared, "native-codegen-link", nativeStart);
        std::error_code ec;
        if (!assetBlobPath.empty())
            std::filesystem::remove(assetBlobPath, ec);
        if (!assetObjPath.empty())
            std::filesystem::remove(assetObjPath, ec);
        return rc;
    }

    // Run mode: verify and execute
    return verifyAndExecute(
        module, config.shared, config.programArgs, config.debugVm, moduleVerified, sm);
}

} // namespace

int cmdRun(int argc, char **argv) {
    return runOrBuild(RunMode::Run, argc, argv);
}

int cmdBuild(int argc, char **argv) {
    return runOrBuild(RunMode::Build, argc, argv);
}
