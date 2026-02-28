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
#include "tools/common/native_compiler.hpp"
#include "tools/common/project_loader.hpp"
#include "tools/common/source_loader.hpp"
#include "tools/common/vm_executor.hpp"
#include "viper/il/IO.hpp"
#include "viper/il/Verify.hpp"
#include "viper/vm/VM.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
inline int setenv(const char *name, const char *value, int)
{
    return _putenv_s(name, value);
}
#endif

using namespace il;
using namespace il::support;
using namespace il::tools::common;

namespace
{

enum class RunMode
{
    Run,
    Build
};

struct RunBuildConfig
{
    RunMode mode{RunMode::Run};
    std::string target{"."};
    std::string outputPath;
    ilc::SharedCliOptions shared;
    std::vector<std::string> programArgs;
    bool debugVm{false};
    bool noRuntimeNamespaces{false};

    // CLI overrides (take precedence over manifest)
    std::optional<std::string> optimizeLevelOverride;
    std::optional<viper::tools::TargetArch> archOverride;
};

il::support::Expected<RunBuildConfig> parseRunBuildArgs(RunMode mode, int argc, char **argv)
{
    RunBuildConfig config;
    config.mode = mode;

    bool hasTarget = false;

    for (int i = 0; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        if (arg == "--")
        {
            for (int j = i + 1; j < argc; ++j)
                config.programArgs.emplace_back(argv[j]);
            break;
        }
        else if (arg == "-o")
        {
            if (mode != RunMode::Build)
            {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "-o is only valid with 'build'", {}, {}});
            }
            if (i + 1 >= argc)
            {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "missing output path after -o", {}, {}});
            }
            config.outputPath = argv[++i];
        }
        else if (arg == "-O0" || arg == "-O1" || arg == "-O2")
        {
            config.optimizeLevelOverride = std::string(arg.substr(1));
        }
        else if (arg == "--debug-vm")
        {
            config.debugVm = true;
        }
        else if (arg == "--no-runtime-namespaces")
        {
            config.noRuntimeNamespaces = true;
        }
        else if (arg == "--arch")
        {
            if (i + 1 >= argc)
            {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "--arch requires arm64 or x64", {}, {}});
            }
            std::string_view val = argv[++i];
            if (val == "arm64")
                config.archOverride = viper::tools::TargetArch::ARM64;
            else if (val == "x64")
                config.archOverride = viper::tools::TargetArch::X64;
            else
            {
                return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                    il::support::Severity::Error, "--arch must be 'arm64' or 'x64'", {}, {}});
            }
        }
        else
        {
            switch (ilc::parseSharedOption(i, argc, argv, config.shared))
            {
                case ilc::SharedOptionParseResult::Parsed:
                    continue;
                case ilc::SharedOptionParseResult::Error:
                    return il::support::Expected<RunBuildConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, "failed to parse shared option", {}, {}});
                case ilc::SharedOptionParseResult::NotMatched:
                    if (!arg.empty() && arg[0] != '-' && !hasTarget)
                    {
                        config.target = std::string(arg);
                        hasTarget = true;
                    }
                    else
                    {
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
                     il::support::SourceManager &sm)
{
    auto verification = il::verify::Verifier::verify(module);
    if (!verification)
    {
        il::support::printDiag(verification.error(), std::cerr, &sm);
        return 1;
    }

    if (!shared.stdinPath.empty())
    {
        if (!freopen(shared.stdinPath.c_str(), "r", stdin))
        {
            std::cerr << "unable to open stdin file\n";
            return 1;
        }
    }

    bool useStandardVm = debugVm || shared.trace.enabled();

    if (useStandardVm)
    {
        vm::TraceConfig traceCfg = shared.trace;
        traceCfg.sm = &sm;

        vm::RunConfig runCfg;
        runCfg.trace = traceCfg;
        runCfg.maxSteps = shared.maxSteps;
        runCfg.programArgs = programArgs;

        vm::Runner runner(module, std::move(runCfg));
        int rc = static_cast<int>(runner.run());
        const auto trapMessage = runner.lastTrapMessage();
        if (trapMessage)
        {
            if (shared.dumpTrap && !trapMessage->empty())
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

    il::tools::common::VMExecutorConfig vmConfig;
    vmConfig.programArgs = programArgs;
    vmConfig.outputTrapMessage = true;
    vmConfig.flushStdout = true;

    auto vmResult = il::tools::common::executeBytecodeVM(module, vmConfig);
    return vmResult.exitCode;
}

/// @brief Compile a Zia project and return the module.
il::support::Expected<il::core::Module> compileZiaProject(const ProjectConfig &project,
                                                          const ilc::SharedCliOptions &shared,
                                                          il::support::SourceManager &sm)
{
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

    // Warning policy from CLI flags
    opts.warningPolicy.enableAll = shared.wall;
    opts.warningPolicy.warningsAsErrors = shared.werror;
    for (const auto &w : shared.disabledWarnings)
    {
        if (auto code = il::frontends::zia::parseWarningCode(w))
            opts.warningPolicy.disabled.insert(*code);
    }

    if (project.optimizeLevel == "O0")
        opts.optLevel = il::frontends::zia::OptLevel::O0;
    else if (project.optimizeLevel == "O1")
        opts.optLevel = il::frontends::zia::OptLevel::O1;
    else if (project.optimizeLevel == "O2")
        opts.optLevel = il::frontends::zia::OptLevel::O2;

    auto result = il::frontends::zia::compileFile(project.entryFile, opts, sm);
    if (!result.succeeded())
    {
        result.diagnostics.printAll(std::cerr, &sm);
        return il::support::Expected<il::core::Module>(
            il::support::Diagnostic{il::support::Severity::Error, "compilation failed", {}, {}});
    }

    return std::move(result.module);
}

/// @brief Compile a BASIC project and return the module.
il::support::Expected<il::core::Module> compileBasicProject(const ProjectConfig &project,
                                                            bool noRuntimeNamespaces,
                                                            const ilc::SharedCliOptions &shared,
                                                            il::support::SourceManager &sm)
{
    auto source = loadSourceBuffer(project.entryFile, sm);
    if (!source)
    {
        il::support::printDiag(source.error(), std::cerr, &sm);
        return il::support::Expected<il::core::Module>(
            il::support::Diagnostic{il::support::Severity::Error, "failed to load source", {}, {}});
    }

    if (noRuntimeNamespaces)
        setenv("VIPER_NO_RUNTIME_NAMESPACES", "1", 1);

    il::frontends::basic::BasicCompilerOptions opts;
    opts.boundsChecks = project.boundsChecks;
    opts.dumpTokens = shared.dumpTokens;
    opts.dumpAst = shared.dumpAst;
    opts.dumpIL = shared.dumpIL;
    opts.dumpILOpt = shared.dumpILOpt;
    opts.dumpILPasses = shared.dumpILPasses;

    il::frontends::basic::BasicCompilerInput input{source.value().buffer, project.entryFile};
    input.fileId = source.value().fileId;

    auto result = il::frontends::basic::compileBasic(input, opts, sm);
    if (!result.succeeded())
    {
        if (result.emitter)
            result.emitter->printAll(std::cerr);
        return il::support::Expected<il::core::Module>(
            il::support::Diagnostic{il::support::Severity::Error, "compilation failed", {}, {}});
    }

    // Apply the canonical IL optimizer pipeline based on the project's opt level.
    {
        il::transform::PassManager pm;
        pm.setVerifyBetweenPasses(false);

        // Enable per-pass IL dumps when requested.
        if (shared.dumpILPasses)
        {
            pm.setPrintBeforeEach(true);
            pm.setPrintAfterEach(true);
            pm.setInstrumentationStream(std::cerr);
        }

        if (project.optimizeLevel == "O2")
            pm.runPipeline(result.module, "O2");
        else if (project.optimizeLevel == "O1")
            pm.runPipeline(result.module, "O1");
        else
            pm.runPipeline(result.module, "O0");

        // Dump IL after the full optimization pipeline.
        if (shared.dumpILOpt)
        {
            std::cerr << "=== IL after optimization (" << project.optimizeLevel << ") ===\n";
            io::Serializer::write(result.module, std::cerr);
            std::cerr << "=== End IL ===\n";
        }
    }

    return std::move(result.module);
}

/// @brief Compile a mixed-language project (Zia + BASIC) and link the modules.
il::support::Expected<il::core::Module> compileMixedProject(const ProjectConfig &project,
                                                            bool noRuntimeNamespaces,
                                                            const ilc::SharedCliOptions &shared,
                                                            il::support::SourceManager &sm)
{
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
    il::support::Expected<il::core::Module> entryResult =
        entryIsZia ? compileZiaProject(entryProject, shared, sm)
                   : compileBasicProject(entryProject, noRuntimeNamespaces, shared, sm);
    if (!entryResult)
        return entryResult;

    // Compile the library module (the other language).
    // Use the first library file as the entry file for the library project.
    if (libProject.sourceFiles.empty())
        return entryResult; // No library files, just return the entry module.

    libProject.entryFile = libProject.sourceFiles[0];
    il::support::Expected<il::core::Module> libResult =
        entryIsZia ? compileBasicProject(libProject, noRuntimeNamespaces, shared, sm)
                   : compileZiaProject(libProject, shared, sm);
    if (!libResult)
        return libResult;

    // Generate boolean interop thunks.
    auto thunks = il::link::generateBooleanThunks(entryResult.value(), libResult.value());
    for (auto &thunk : thunks)
        entryResult.value().functions.push_back(std::move(thunk.thunk));

    // Link the two modules.
    std::vector<il::core::Module> modules;
    modules.push_back(std::move(entryResult.value()));
    modules.push_back(std::move(libResult.value()));

    auto linkResult = il::link::linkModules(std::move(modules));
    if (!linkResult.succeeded())
    {
        std::string errMsg = "link errors:";
        for (const auto &e : linkResult.errors)
            errMsg += "\n  " + e;
        return il::support::Expected<il::core::Module>(
            il::support::Diagnostic{il::support::Severity::Error, errMsg, {}, {}});
    }

    return std::move(linkResult.module);
}

/// @brief Common implementation for both run and build commands.
int runOrBuild(RunMode mode, int argc, char **argv)
{
    auto parsed = parseRunBuildArgs(mode, argc, argv);
    if (!parsed)
    {
        const auto &diag = parsed.error();
        SourceManager sm;
        il::support::printDiag(diag, std::cerr, &sm);
        usage();
        return 1;
    }

    RunBuildConfig config = std::move(parsed.value());

    // Resolve the project
    auto project = resolveProject(config.target);
    if (!project)
    {
        SourceManager sm;
        il::support::printDiag(project.error(), std::cerr, &sm);
        return 1;
    }

    ProjectConfig &proj = project.value();

    // Apply CLI overrides
    if (config.shared.boundsChecks)
        proj.boundsChecks = true;
    if (config.optimizeLevelOverride)
        proj.optimizeLevel = *config.optimizeLevelOverride;

    // Compile
    SourceManager sm;
    il::support::Expected<il::core::Module> moduleResult =
        (proj.lang == ProjectLang::Mixed)
            ? compileMixedProject(proj, config.noRuntimeNamespaces, config.shared, sm)
        : (proj.lang == ProjectLang::Zia)
            ? compileZiaProject(proj, config.shared, sm)
            : compileBasicProject(proj, config.noRuntimeNamespaces, config.shared, sm);

    if (!moduleResult)
        return 1; // diagnostics already printed

    il::core::Module module = std::move(moduleResult.value());

    // Build mode: emit IL or compile to native binary
    if (mode == RunMode::Build)
    {
        // Verify before emitting
        auto verification = il::verify::Verifier::verify(module);
        if (!verification)
        {
            il::support::printDiag(verification.error(), std::cerr, &sm);
            return 1;
        }

        // No -o: emit IL to stdout (backwards compat)
        if (config.outputPath.empty())
        {
            io::Serializer::write(module, std::cout);
            return 0;
        }

        // -o path ends in .il: emit IL text (backwards compat)
        if (!viper::tools::isNativeOutputPath(config.outputPath))
        {
            std::ofstream outFile(config.outputPath);
            if (!outFile.is_open())
            {
                std::cerr << "error: cannot open output file: " << config.outputPath << "\n";
                return 1;
            }
            io::Serializer::write(module, outFile);
            return 0;
        }

        // Native binary output: serialize IL to temp file, then codegen
        auto arch = config.archOverride.value_or(viper::tools::detectHostArch());
        std::string tempIlPath = viper::tools::generateTempIlPath();
        {
            std::ofstream tempFile(tempIlPath);
            if (!tempFile.is_open())
            {
                std::cerr << "error: cannot create temporary file for IL serialization\n";
                return 1;
            }
            io::Serializer::write(module, tempFile);
        }
        int rc = viper::tools::compileToNative(tempIlPath, config.outputPath, arch);
        std::error_code ec;
        std::filesystem::remove(tempIlPath, ec);
        return rc;
    }

    // Run mode: verify and execute
    return verifyAndExecute(module, config.shared, config.programArgs, config.debugVm, sm);
}

} // namespace

int cmdRun(int argc, char **argv)
{
    return runOrBuild(RunMode::Run, argc, argv);
}

int cmdBuild(int argc, char **argv)
{
    return runOrBuild(RunMode::Build, argc, argv);
}
