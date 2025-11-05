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

/// @file
/// @brief Command-line entry point for the BASIC frontend of `ilc`.
/// @details Documents the supporting helpers used to parse arguments, load
///          BASIC source files, and either emit IL or execute the program
///          through the virtual machine.  The implementation keeps side effects
///          (like diagnostic printing and VM execution) in well-contained
///          functions so higher-level tooling can reuse them.

#include "cli.hpp"
#include "codegen/x86_64/CodegenPipeline.hpp"
#include "frontends/basic/BasicCompiler.hpp"
#include "il/api/expected_api.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "viper/il/IO.hpp"
#include "viper/il/Verify.hpp"
#include "support/diag_expected.hpp"
#include "support/source_manager.hpp"
#include "viper/vm/VM.hpp"
#include "vm/VMConfig.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

using namespace il;
using namespace il::frontends::basic;
using namespace il::support;

#ifndef VIPER_CLI_HAS_NATIVE_RUN
#define VIPER_CLI_HAS_NATIVE_RUN 0
#endif

namespace
{

/// @brief RAII helper that temporarily overrides an environment variable.
/// @details Mirrors the helper used by the run command so BASIC execution can select
///          specific dispatch strategies without leaking state across invocations.
class ScopedEnvOverride
{
  public:
    ScopedEnvOverride() = default;

    ScopedEnvOverride(const char *name, const char *value)
        : name_(name)
    {
        if (name_ == nullptr)
        {
            return;
        }
        const char *existing = std::getenv(name_);
        if (existing != nullptr)
        {
            previous_ = std::string(existing);
        }
#ifdef _WIN32
        _putenv_s(name_, value);
#else
        setenv(name_, value, 1);
#endif
        active_ = true;
    }

    ScopedEnvOverride(const ScopedEnvOverride &) = delete;
    ScopedEnvOverride &operator=(const ScopedEnvOverride &) = delete;

    ScopedEnvOverride(ScopedEnvOverride &&other) noexcept
        : name_(other.name_), previous_(std::move(other.previous_)), active_(other.active_)
    {
        other.name_ = nullptr;
        other.active_ = false;
    }

    ScopedEnvOverride &operator=(ScopedEnvOverride &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }
        restore();
        name_ = other.name_;
        previous_ = std::move(other.previous_);
        active_ = other.active_;
        other.name_ = nullptr;
        other.active_ = false;
        return *this;
    }

    ~ScopedEnvOverride()
    {
        restore();
    }

    void reset(const char *name, const char *value)
    {
        restore();
        name_ = name;
        previous_.reset();
        active_ = false;
        if (name_ == nullptr)
        {
            return;
        }
        const char *existing = std::getenv(name_);
        if (existing != nullptr)
        {
            previous_ = std::string(existing);
        }
#ifdef _WIN32
        _putenv_s(name_, value);
#else
        setenv(name_, value, 1);
#endif
        active_ = true;
    }

  private:
    void restore()
    {
        if (!active_ || name_ == nullptr)
        {
            return;
        }
        if (previous_.has_value())
        {
#ifdef _WIN32
            _putenv_s(name_, previous_->c_str());
#else
            setenv(name_, previous_->c_str(), 1);
#endif
        }
        else
        {
#ifdef _WIN32
            _putenv_s(name_, "");
#else
            unsetenv(name_);
#endif
        }
        active_ = false;
    }

    const char *name_ = nullptr;
    std::optional<std::string> previous_{};
    bool active_ = false;
};

/// @brief Determine the effective engine selection from CLI and environment inputs.
/// @param opts Shared CLI options for the current invocation.
/// @return Selected engine kind; defaults to @c Auto when unspecified.
ilc::SharedCliOptions::EngineKind resolveEngine(const ilc::SharedCliOptions &opts)
{
    if (opts.engineExplicit)
    {
        return opts.engine;
    }
    if (const char *env = std::getenv("VIPER_ENGINE"))
    {
        if (auto parsed = ilc::parseEngineName(env))
        {
            return *parsed;
        }
    }
    return opts.engine;
}

/// @brief Materialise the compiled module into a temporary IL file and run native codegen.
/// @param module Lowered BASIC module ready for execution.
/// @return Exit status reported by the native runner or 1 on infrastructure failures.
int runModuleNative(const core::Module &module)
{
#if VIPER_CLI_HAS_NATIVE_RUN
    /// @brief Remove a filesystem path recursively, ignoring failures.
    auto removeAllQuiet = [](const std::filesystem::path &path) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        (void)ec;
    };

    std::error_code ec;
    const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
    if (ec)
    {
        std::cerr << "error: unable to resolve temporary directory: " << ec.message() << "\n";
        return 1;
    }

    const std::filesystem::path workDir = base / std::filesystem::unique_path("viper_basic_native-%%%%-%%%%");
    std::filesystem::create_directories(workDir, ec);
    if (ec)
    {
        std::cerr << "error: unable to create temporary directory: " << ec.message() << "\n";
        return 1;
    }

    const std::filesystem::path ilPath = workDir / "module.il";
    {
        std::ofstream out(ilPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            std::cerr << "error: unable to open temporary IL file for native execution\n";
            removeAllQuiet(workDir);
            return 1;
        }
        io::Serializer::write(module, out, io::Serializer::Mode::Canonical);
        if (!out)
        {
            std::cerr << "error: failed to write temporary IL module for native execution\n";
            removeAllQuiet(workDir);
            return 1;
        }
    }

    viper::codegen::x64::CodegenPipeline::Options opts{};
    opts.input_il_path = ilPath.string();
    opts.run_native = true;
    viper::codegen::x64::CodegenPipeline pipeline(opts);
    const PipelineResult result = pipeline.run();
    if (!result.stdout_text.empty())
    {
        std::cout << result.stdout_text;
    }
    if (!result.stderr_text.empty())
    {
        std::cerr << result.stderr_text;
    }

    removeAllQuiet(workDir);
    return result.exit_code;
#else
    (void)module;
    std::cerr << "error: native engine requested but IL_ENABLE_X64_NATIVE_RUN=OFF during the build\n";
    return 1;
#endif
}

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
        std::string arg = argv[i];
        if (arg == "-emit-il")
        {
            if (i + 1 >= argc)
            {
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
                    return il::support::Expected<FrontBasicConfig>(il::support::Diagnostic{
                        il::support::Severity::Error, "failed to parse shared option", {}});
                case ilc::SharedOptionParseResult::NotMatched:
                    return il::support::Expected<FrontBasicConfig>(
                        il::support::Diagnostic{il::support::Severity::Error, "unknown flag", {}});
            }
        }
    }

    if ((config.emitIl == config.run) || config.sourcePath.empty())
    {
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
    const auto engine = resolveEngine(config.shared);

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

    if (engine == ilc::SharedCliOptions::EngineKind::Native)
    {
        if (config.shared.trace.mode != vm::TraceConfig::Off)
        {
            std::cerr << "error: --trace is not supported with the native engine\n";
            return 1;
        }
        if (config.shared.maxSteps != 0)
        {
            std::cerr << "error: --max-steps is not supported with the native engine\n";
            return 1;
        }
        if (!config.shared.stdinPath.empty())
        {
            std::cerr << "error: --stdin-from is not supported with the native engine\n";
            return 1;
        }

        return runModuleNative(module);
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

    ScopedEnvOverride dispatchOverride;
    switch (engine)
    {
        case ilc::SharedCliOptions::EngineKind::Auto:
            break;
        case ilc::SharedCliOptions::EngineKind::VmSwitch:
            dispatchOverride.reset("VIPER_DISPATCH", "switch");
            break;
        case ilc::SharedCliOptions::EngineKind::VmTable:
            dispatchOverride.reset("VIPER_DISPATCH", "table");
            break;
        case ilc::SharedCliOptions::EngineKind::VmThreaded:
#if VIPER_THREADING_SUPPORTED
            dispatchOverride.reset("VIPER_DISPATCH", "threaded");
            break;
#else
            std::cerr << "error: threaded dispatch requested but the interpreter was built without "
                      << "VIPER_VM_THREADED support\n";
            return 1;
#endif
        case ilc::SharedCliOptions::EngineKind::Native:
            // Already handled above.
            break;
    }

    vm::RunConfig runCfg;
    runCfg.trace = traceCfg;
    runCfg.maxSteps = config.shared.maxSteps;

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
