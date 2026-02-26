//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/CodegenPipeline.cpp
// Purpose: Implement the reusable IL-to-x86-64 compilation pipeline used by CLI front ends.
// Key invariants: Passes execute sequentially with early exits on failure, ensuring diagnostics
//                 are recorded deterministically and no partial artefacts leak on error.
// Ownership/Lifetime: The pipeline borrows IL modules and writes assembly/binaries to caller-
//                     specified locations without assuming ownership of external resources.
// Links: docs/codemap.md, src/tools/common/module_loader.hpp
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implementation of the high-level IL-to-native compilation pipeline.
/// @details Coordinates module loading, verification, backend pass execution,
///          and optional linking/execution so command-line tools can rely on a
///          single entry point for x86-64 code generation.

#include "codegen/x86_64/CodegenPipeline.hpp"

#include "codegen/common/LinkerSupport.hpp"
#include "codegen/x86_64/passes/EmitPass.hpp"
#include "codegen/x86_64/passes/LegalizePass.hpp"
#include "codegen/x86_64/passes/LoweringPass.hpp"
#include "codegen/x86_64/passes/PassManager.hpp"
#include "codegen/x86_64/passes/RegAllocPass.hpp"
#include "common/RunProcess.hpp"
#include "il/transform/PassManager.hpp"
#include "tools/common/module_loader.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace viper::codegen::x64
{
namespace
{

/// @brief Platform-specific C compiler command.
/// @details On Windows, `cc` isn't available, so we use `clang` instead.
///          On Unix-like systems, `cc` is typically a symlink to the default compiler.
#if defined(_WIN32)
constexpr const char *kCcCommand = "clang";
#else
constexpr const char *kCcCommand = "cc";
#endif

/// @brief Convert platform-specific process status codes to POSIX-style exits.
/// @details Handles negative launch failures, Windows return values, and
///          Unix wait statuses so pipeline users receive consistent exit
///          codes irrespective of platform.
/// @param status Raw status returned by @ref run_process or waitpid.
/// @return Normalised exit code suitable for user display.
int normaliseStatus(int status)
{
    if (status == -1)
    {
        return -1;
    }
#if defined(_WIN32)
    return status;
#else
    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status))
    {
        return 128 + WTERMSIG(status);
    }
    return status;
#endif
}

/// @brief Compute the output assembly path from pipeline options.
/// @details Falls back to sensible defaults when the input path is empty
///          or refers to a directory, mirroring traditional compiler
///          behaviour.
/// @param opts Pipeline configuration specifying the IL input path.
/// @return Filesystem location for the generated assembly file.
std::filesystem::path deriveAssemblyPath(const CodegenPipeline::Options &opts)
{
    std::filesystem::path assembly = std::filesystem::path(opts.input_il_path);
    if (assembly.empty())
    {
        return std::filesystem::path("out.s");
    }
    assembly.replace_extension(".s");
    if (assembly.filename().empty())
    {
        assembly = assembly.parent_path() / "out.s";
    }
    return assembly;
}

/// @brief Determine the executable output path based on user input.
/// @details Strips the IL extension when present and ensures the result has
///          a filename component so the linker output is predictable.
///          On Windows, adds the .exe extension.
/// @param opts Pipeline configuration describing the IL input.
/// @return Filesystem path for the linked executable.
std::filesystem::path deriveExecutablePath(const CodegenPipeline::Options &opts)
{
    std::filesystem::path exe = std::filesystem::path(opts.input_il_path);
    if (exe.empty())
    {
#if defined(_WIN32)
        return std::filesystem::path("a.exe");
#else
        return std::filesystem::path("a.out");
#endif
    }
    exe.replace_extension("");
    if (exe.filename().empty() || exe.filename() == ".")
    {
#if defined(_WIN32)
        return exe.parent_path() / "a.exe";
#else
        return exe.parent_path() / "a.out";
#endif
    }
#if defined(_WIN32)
    exe.replace_extension(".exe");
#endif
    return exe;
}

/// @brief Persist generated assembly to disk.
/// @details Writes @p text to @p path, reporting any failures to the
///          provided error stream. The helper returns @c false when I/O
///          errors occur so the pipeline can stop before invoking the linker.
/// @param path Destination path for the assembly file.
/// @param text Assembly text to write.
/// @param err  Stream receiving human-readable error messages.
/// @return @c true when the file was written successfully.
bool writeAssemblyFile(const std::filesystem::path &path,
                       const std::string &text,
                       std::ostream &err)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        err << "error: unable to open '" << path.string() << "' for writing\n";
        return false;
    }
    out << text;
    if (!out)
    {
        err << "error: failed to write assembly to '" << path.string() << "'\n";
        return false;
    }
    return true;
}

/// @brief Convert a path to use native separators on the current platform.
/// @details On Windows, forward slashes in paths can confuse cmd.exe when
///          passed through run_process. This helper ensures backslashes are used.
/// @param path Original path to normalize.
/// @return String with platform-native path separators.
std::string toNativePath(const std::filesystem::path &path)
{
    std::filesystem::path native = path;
    native.make_preferred();
    return native.string();
}

/// @brief Assemble emitted assembly into an object file.
/// @details Invokes the system C compiler with the `-c` flag so the pipeline
///          can stop after producing a relocatable object when no executable is
///          required.
/// @param asmPath Path to the assembly file to assemble.
/// @param objPath Destination path for the object file.
/// @param out     Stream receiving the assembler's standard output.
/// @param err     Stream receiving the assembler's standard error.
/// @return Normalised assembler exit code (-1 when the command could not start).
int invokeAssembler(const std::filesystem::path &asmPath,
                    const std::filesystem::path &objPath,
                    std::ostream &out,
                    std::ostream &err)
{
    const RunResult assemble =
        run_process({kCcCommand, "-c", toNativePath(asmPath), "-o", toNativePath(objPath)});
    if (assemble.exit_code == -1)
    {
        err << "error: failed to launch system assembler command\n";
        return -1;
    }

    if (!assemble.out.empty())
    {
        out << assemble.out;
    }
#if defined(_WIN32)
    if (!assemble.err.empty())
    {
        err << assemble.err;
    }
#endif

    const int exitCode = normaliseStatus(assemble.exit_code);
    if (exitCode != 0)
    {
        err << "error: " << kCcCommand << " (assemble) exited with status " << exitCode << "\n";
    }
    return exitCode;
}

/// @brief Link the emitted assembly into an executable.
/// @details Invokes the system C compiler, forwarding stdout/stderr to the
///          provided streams and normalising the resulting exit code.
/// @param asmPath Path to the assembly file to assemble and link.
/// @param exePath Desired output executable path.
/// @param out     Stream receiving the linker's standard output.
/// @param err     Stream receiving the linker's standard error.
/// @return Normalised linker exit code (-1 when the command could not start).
int invokeLinker(const std::filesystem::path &asmPath,
                 const std::filesystem::path &exePath,
                 std::size_t stackSize,
                 std::ostream &out,
                 std::ostream &err)
{
    // Use common runtime component classification
    using RtComponent = viper::codegen::RtComponent;

#if defined(_WIN32)
    // Windows: Simple approach - find build dir and link all runtime libraries
    auto fileExists = [](const std::filesystem::path &path) -> bool
    {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    };

    auto findBuildDir = [&]() -> std::optional<std::filesystem::path>
    {
        std::error_code ec;
        std::filesystem::path cur = std::filesystem::current_path(ec);
        if (ec)
            return std::nullopt;
        const std::size_t maxDepth = 10;
        for (std::size_t i = 0; i < maxDepth && !cur.empty(); ++i)
        {
            const std::filesystem::path cmake_cache = cur / "build" / "CMakeCache.txt";
            if (std::filesystem::exists(cmake_cache, ec))
                return cur / "build";
            const std::filesystem::path parent = cur.parent_path();
            if (parent == cur)
                break;
            cur = parent;
        }
        return std::nullopt;
    };

    const std::optional<std::filesystem::path> buildDirOpt = findBuildDir();
    const std::filesystem::path buildDir = buildDirOpt.value_or(std::filesystem::path{});

    // Try multiple paths for runtime libraries: Release, Debug, and direct path
    // MSVC multi-config builds put outputs in Release/ or Debug/ subdirectories
    bool foundDebugRtLib = false; // BUG-018: track whether libs are Debug-built
    auto findRuntimeArchive =
        [&](std::string_view libBaseName) -> std::optional<std::filesystem::path>
    {
        const std::string libFile = std::string(libBaseName) + ".lib";
        if (!buildDir.empty())
        {
            // Try Release first, then Debug, then direct path
            const std::filesystem::path releasePath = buildDir / "src/runtime/Release" / libFile;
            if (fileExists(releasePath))
                return releasePath;
            const std::filesystem::path debugPath = buildDir / "src/runtime/Debug" / libFile;
            if (fileExists(debugPath))
            {
                foundDebugRtLib = true;
                return debugPath;
            }
            const std::filesystem::path directPath = buildDir / "src/runtime" / libFile;
            if (fileExists(directPath))
                return directPath;
        }
        // Fallback: try relative paths
        const std::filesystem::path relPath = std::filesystem::path("src/runtime") / libFile;
        if (fileExists(relPath))
            return relPath;
        return std::nullopt;
    };

    std::vector<std::string> cmd = {kCcCommand, toNativePath(asmPath)};

    // Link all runtime libraries that exist (simpler than symbol detection)
    const std::vector<std::string_view> rtLibs = {"viper_rt_graphics",
                                                  "viper_rt_network",
                                                  "viper_rt_exec",
                                                  "viper_rt_io_fs",
                                                  "viper_rt_text",
                                                  "viper_rt_collections",
                                                  "viper_rt_arrays",
                                                  "viper_rt_threads",
                                                  "viper_rt_oop",
                                                  "viper_rt_base"};
    for (const auto &lib : rtLibs)
    {
        const auto pathOpt = findRuntimeArchive(lib);
        if (pathOpt.has_value())
            cmd.push_back(toNativePath(*pathOpt));
    }

    // Find and link vipergfx and viperaud libraries (in lib/ instead of src/runtime/)
    auto findLibArchive = [&](std::string_view libBaseName) -> std::optional<std::filesystem::path>
    {
        const std::string libFile = std::string(libBaseName) + ".lib";
        if (!buildDir.empty())
        {
            // Try Release first, then Debug, then direct path
            const std::filesystem::path releasePath = buildDir / "lib/Release" / libFile;
            if (fileExists(releasePath))
                return releasePath;
            const std::filesystem::path debugPath = buildDir / "lib/Debug" / libFile;
            if (fileExists(debugPath))
                return debugPath;
            const std::filesystem::path directPath = buildDir / "lib" / libFile;
            if (fileExists(directPath))
                return directPath;
        }
        return std::nullopt;
    };

    // Link graphics and audio backend libraries
    const std::vector<std::string_view> backendLibs = {"vipergfx", "viperaud"};
    for (const auto &lib : backendLibs)
    {
        const auto pathOpt = findLibArchive(lib);
        if (pathOpt.has_value())
            cmd.push_back(toNativePath(*pathOpt));
    }

    // Add Windows CRT and system libraries
    // BUG-018: Match CRT variant (Debug vs Release) to how runtime libs were built
    if (foundDebugRtLib)
    {
        cmd.push_back("-lmsvcrtd");
        cmd.push_back("-lucrtd");
        cmd.push_back("-lvcruntimed");
    }
    else
    {
        cmd.push_back("-lmsvcrt");
        cmd.push_back("-lucrt");
        cmd.push_back("-lvcruntime");
    }

    // Add Windows system libraries needed for graphics and input
    cmd.push_back("-lgdi32");
    cmd.push_back("-luser32");
    cmd.push_back("-lxinput");

    // Set stack size (default 8MB for better recursion support)
    const std::size_t effectiveStackSize = (stackSize > 0) ? stackSize : (8 * 1024 * 1024);
    cmd.push_back("-Wl,/STACK:" + std::to_string(effectiveStackSize));

    cmd.push_back("-o");
    cmd.push_back(toNativePath(exePath));

    const RunResult link = run_process(cmd);
#else
    using namespace viper::codegen::common;

    LinkContext ctx;
    if (const int rc = prepareLinkContext(asmPath.string(), ctx, out, err); rc != 0)
        return rc;

    std::vector<std::string> cmd = {kCcCommand, asmPath.string()};
    appendArchives(ctx, cmd);
    {
        std::vector<std::string> frameworks;
#if defined(__APPLE__)
        frameworks.push_back("Cocoa");
        frameworks.push_back("IOKit");
        frameworks.push_back("CoreFoundation");
        frameworks.push_back("UniformTypeIdentifiers");
#endif
        appendGraphicsLibs(ctx, cmd, frameworks);
    }

#if defined(__APPLE__)
    cmd.push_back("-Wl,-dead_strip");
    const std::size_t effStack = (stackSize > 0) ? stackSize : (8 * 1024 * 1024);
    std::ostringstream stackArg;
    stackArg << "-Wl,-stack_size,0x" << std::hex << effStack;
    cmd.push_back(stackArg.str());
#else
    cmd.push_back("-Wl,--gc-sections");
    if (hasComponent(ctx, RtComponent::Threads))
        cmd.push_back("-pthread");
    cmd.push_back("-lm");
    const std::size_t effStack = (stackSize > 0) ? stackSize : (8 * 1024 * 1024);
    cmd.push_back("-Wl,-z,stack-size=" + std::to_string(effStack));
#endif

    cmd.push_back("-o");
    cmd.push_back(exePath.string());

    const RunResult link = run_process(cmd);
#endif
    if (link.exit_code == -1)
    {
        err << "error: failed to launch system linker command\n";
        return -1;
    }

    if (!link.out.empty())
    {
        out << link.out;
    }
#if defined(_WIN32)
    if (!link.err.empty())
    {
        err << link.err;
    }
#endif

    const int exitCode = normaliseStatus(link.exit_code);
    if (exitCode != 0)
    {
        err << "error: " << kCcCommand << " exited with status " << exitCode << "\n";
    }
    return exitCode;
}

/// @brief Execute a freshly linked binary and capture its output.
/// @details Launches the executable using @ref run_process and forwards its
///          standard streams to the provided sinks, normalising the exit
///          code for consistency across platforms.
/// @param exePath Path to the executable to run.
/// @param out     Stream receiving program stdout.
/// @param err     Stream receiving program stderr.
/// @return Normalised process exit code (-1 when the process could not be started).
int runExecutable(const std::filesystem::path &exePath, std::ostream &out, std::ostream &err)
{
    const RunResult run = run_process({toNativePath(exePath)});
    if (run.exit_code == -1)
    {
        err << "error: failed to execute '" << exePath.string() << "'\n";
        return -1;
    }
    if (!run.out.empty())
    {
        out << run.out;
    }
#if defined(_WIN32)
    if (!run.err.empty())
    {
        err << run.err;
    }
#endif
    return normaliseStatus(run.exit_code);
}

} // namespace

/// @brief Construct a pipeline with the given configuration options.
/// @details Copies the option struct so the pipeline retains a stable
///          configuration even if the caller mutates their original instance.
/// @param opts Pipeline configuration (input path, action flags, etc.).
CodegenPipeline::CodegenPipeline(Options opts) : opts_(std::move(opts)) {}

/// @brief Run the configured pipeline from IL loading to optional execution.
/// @details Loads and verifies the IL module, executes the backend pass
///          manager, writes assembly files, optionally links, and optionally
///          runs the resulting executable. All diagnostics are aggregated into
///          the returned @ref PipelineResult.
/// @return Struct summarising exit code, stdout, and stderr output.
PipelineResult CodegenPipeline::run()
{
    PipelineResult result{0, "", ""};
    std::ostringstream out;
    std::ostringstream err;

    il::core::Module module;
    const auto loadResult = il::tools::common::loadModuleFromFile(opts_.input_il_path, module, err);
    if (!loadResult.succeeded())
    {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }
    if (!il::tools::common::verifyModule(module, err))
    {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    // Run IL optimizations before lowering to MIR.
    // Codegen-safe pipelines omit LICM and check-opt (known correctness
    // issues).  SCCP and inlining are safe and enabled.
    if (opts_.optimize >= 2)
    {
        il::transform::PassManager ilpm;
        if (opts_.optimize >= 3)
        {
            ilpm.registerPipeline("codegen-O2",
                                  {"simplify-cfg",
                                   "mem2reg",
                                   "simplify-cfg",
                                   "sccp",
                                   "dce",
                                   "simplify-cfg",
                                   "inline",
                                   "simplify-cfg",
                                   "dce",
                                   "sccp",
                                   "gvn",
                                   "earlycse",
                                   "dse",
                                   "peephole",
                                   "dce",
                                   "late-cleanup"});
            ilpm.runPipeline(module, "codegen-O2");
        }
        else
        {
            ilpm.registerPipeline("codegen-O1",
                                  {"simplify-cfg",
                                   "mem2reg",
                                   "simplify-cfg",
                                   "sccp",
                                   "dce",
                                   "simplify-cfg",
                                   "peephole",
                                   "dce"});
            ilpm.runPipeline(module, "codegen-O1");
        }
    }

    passes::Module pipelineModule{};
    pipelineModule.il = std::move(module);

    passes::Diagnostics diagnostics{};
    passes::PassManager manager{};
    manager.addPass(std::make_unique<passes::LoweringPass>());
    manager.addPass(std::make_unique<passes::LegalizePass>());
    manager.addPass(std::make_unique<passes::RegAllocPass>());

    CodegenOptions codegenOpts{};
    codegenOpts.optimizeLevel = opts_.optimize;
    manager.addPass(std::make_unique<passes::EmitPass>(codegenOpts));

    if (!manager.run(pipelineModule, diagnostics))
    {
        diagnostics.flush(err);
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    diagnostics.flush(err);

    if (!pipelineModule.codegenResult)
    {
        err << "error: emit pass did not produce assembly output\n";
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    const std::string &asmText = pipelineModule.codegenResult->asmText;

    const std::filesystem::path asmPath = opts_.output_asm_path.empty()
                                              ? deriveAssemblyPath(opts_)
                                              : std::filesystem::path(opts_.output_asm_path);
    if (!writeAssemblyFile(asmPath, asmText, err))
    {
        result.exit_code = 1;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    // If user requested assembly output via -S with a specific path, stop here.
    // Don't try to assemble or link - just emit the assembly file.
    if (opts_.emit_asm && !opts_.output_asm_path.empty())
    {
        result.exit_code = 0;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    // Check if -o path looks like an executable (ends with .exe or has no extension)
    // vs an object file (ends with .o or .obj)
    auto looksLikeObjectFile = [](const std::string &path) -> bool
    {
        const std::size_t dotPos = path.rfind('.');
        if (dotPos == std::string::npos)
            return false; // No extension - treat as executable
        const std::string ext = path.substr(dotPos);
        return ext == ".o" || ext == ".obj";
    };

    const bool wantsObjectOnly = !opts_.output_obj_path.empty() && !opts_.run_native &&
                                 looksLikeObjectFile(opts_.output_obj_path);
    if (wantsObjectOnly)
    {
        const std::filesystem::path objPath(opts_.output_obj_path);
        const int assembleExit = invokeAssembler(asmPath, objPath, out, err);
        if (assembleExit != 0)
        {
            result.exit_code = assembleExit == -1 ? 1 : assembleExit;
        }
        else
        {
            result.exit_code = 0;
            // Clean up intermediate assembly after successful object file creation.
            if (!opts_.emit_asm)
            {
                std::error_code ec;
                std::filesystem::remove(asmPath, ec);
            }
        }
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    // Link to executable if: running native, no output path specified, or output looks like .exe
    const bool needsExecutable = opts_.run_native || opts_.output_obj_path.empty() ||
                                 !looksLikeObjectFile(opts_.output_obj_path);
    if (!needsExecutable)
    {
        result.exit_code = 0;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    const std::filesystem::path exePath = opts_.output_obj_path.empty()
                                              ? deriveExecutablePath(opts_)
                                              : std::filesystem::path(opts_.output_obj_path);
    const int linkExit = invokeLinker(asmPath, exePath, opts_.stack_size, out, err);
    if (linkExit != 0)
    {
        result.exit_code = linkExit == -1 ? 1 : linkExit;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    // Clean up the intermediate assembly file after successful linking,
    // unless the user explicitly requested assembly output via -S.
    if (!opts_.emit_asm)
    {
        std::error_code ec;
        std::filesystem::remove(asmPath, ec);
    }

    if (!opts_.run_native)
    {
        result.exit_code = 0;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    const int runExit = runExecutable(exePath, out, err);
    if (runExit == -1)
    {
        result.exit_code = 1;
    }
    else
    {
        result.exit_code = runExit;
    }
    result.stdout_text = out.str();
    result.stderr_text = err.str();
    return result;
}

} // namespace viper::codegen::x64
