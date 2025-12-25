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

#include "codegen/x86_64/passes/EmitPass.hpp"
#include "codegen/x86_64/passes/LegalizePass.hpp"
#include "codegen/x86_64/passes/LoweringPass.hpp"
#include "codegen/x86_64/passes/PassManager.hpp"
#include "codegen/x86_64/passes/RegAllocPass.hpp"
#include "common/RunProcess.hpp"
#include "tools/common/module_loader.hpp"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_set>

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
                 std::ostream &out,
                 std::ostream &err)
{
    // Common enum for both Windows and Unix
    enum class RtComponent
    {
        Base,
        Arrays,
        Oop,
        Collections,
        Text,
        IoFs,
        Exec,
        Threads,
        Graphics,
    };

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

    auto runtimeArchivePath = [&](std::string_view libBaseName) -> std::filesystem::path
    {
        if (!buildDir.empty())
            return buildDir / "src/runtime" / (std::string(libBaseName) + ".lib");
        return std::filesystem::path("src/runtime") / (std::string(libBaseName) + ".lib");
    };

    std::vector<std::string> cmd = {kCcCommand, toNativePath(asmPath)};

    // Link all runtime libraries that exist (simpler than symbol detection)
    const std::vector<std::string_view> rtLibs = {
        "viper_rt_graphics", "viper_rt_exec", "viper_rt_io_fs",
        "viper_rt_text", "viper_rt_collections", "viper_rt_arrays",
        "viper_rt_threads", "viper_rt_oop", "viper_rt_base"
    };
    for (const auto &lib : rtLibs)
    {
        const std::filesystem::path path = runtimeArchivePath(lib);
        if (fileExists(path))
            cmd.push_back(toNativePath(path));
    }

    // Add Windows CRT and system libraries
    cmd.push_back("-lmsvcrt");
    cmd.push_back("-lucrt");
    cmd.push_back("-lvcruntime");

    cmd.push_back("-o");
    cmd.push_back(toNativePath(exePath));

    const RunResult link = run_process(cmd);
#else
    auto fileExists = [](const std::filesystem::path &path) -> bool
    {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    };

    auto readFile = [&](const std::filesystem::path &path, std::string &dst) -> bool
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        dst = ss.str();
        return true;
    };

    auto findBuildDir = [&]() -> std::optional<std::filesystem::path>
    {
        std::error_code ec;
        std::filesystem::path cur = std::filesystem::current_path(ec);
        if (!ec)
        {
            for (int depth = 0; depth < 8; ++depth)
            {
                if (fileExists(cur / "CMakeCache.txt"))
                    return cur;
                if (!cur.has_parent_path())
                    break;
                cur = cur.parent_path();
            }
        }

        const std::filesystem::path defaultBuild = std::filesystem::path("build");
        if (fileExists(defaultBuild / "CMakeCache.txt"))
            return defaultBuild;

        return std::nullopt;
    };

    auto parseRuntimeSymbols = [](std::string_view text) -> std::unordered_set<std::string>
    {
        auto isIdent = [](unsigned char c) -> bool { return std::isalnum(c) || c == '_'; };

        std::unordered_set<std::string> symbols;
        for (std::size_t i = 0; i + 3 < text.size(); ++i)
        {
            std::size_t start = std::string_view::npos;
            std::size_t boundary = std::string_view::npos;
            if (text[i] == 'r' && text[i + 1] == 't' && text[i + 2] == '_')
            {
                start = i;
                boundary = (start == 0) ? std::string_view::npos : (start - 1);
            }
            else if (text[i] == '_' && text[i + 1] == 'r' && text[i + 2] == 't' &&
                     text[i + 3] == '_')
            {
                start = i + 1;
                boundary = (i == 0) ? std::string_view::npos : (i - 1);
            }

            if (start == std::string_view::npos)
                continue;
            if (boundary != std::string_view::npos &&
                isIdent(static_cast<unsigned char>(text[boundary])))
                continue;

            std::size_t j = start;
            while (j < text.size() && isIdent(static_cast<unsigned char>(text[j])))
                ++j;

            if (j > start)
                symbols.emplace(text.substr(start, j - start));
            i = j;
        }
        return symbols;
    };

    auto needsComponentForSymbol = [](std::string_view sym) -> std::optional<RtComponent>
    {
        auto starts = [&](std::string_view p) -> bool { return sym.rfind(p, 0) == 0; };

        if (starts("rt_arr_"))
            return RtComponent::Arrays;

        if (starts("rt_obj_") || starts("rt_type_") || starts("rt_cast_") || starts("rt_ns_") ||
            sym == "rt_bind_interface")
            return RtComponent::Oop;

        if (starts("rt_list_") || starts("rt_map_") || starts("rt_treemap_") || starts("rt_bag_") ||
            starts("rt_queue_") || starts("rt_ring_") || starts("rt_seq_") || starts("rt_stack_") ||
            starts("rt_bytes_"))
            return RtComponent::Collections;

        if (starts("rt_codec_") || starts("rt_csv_") || starts("rt_guid_") || starts("rt_hash_") ||
            starts("rt_parse_"))
            return RtComponent::Text;

        if (starts("rt_file_") || starts("rt_dir_") || starts("rt_path_") ||
            starts("rt_binfile_") || starts("rt_linereader_") || starts("rt_linewriter_") ||
            starts("rt_io_file_") || sym == "rt_eof_ch" || sym == "rt_lof_ch" ||
            sym == "rt_loc_ch" || sym == "rt_close_err" || sym == "rt_seek_ch_err" ||
            sym == "rt_write_ch_err" || sym == "rt_println_ch_err" ||
            sym == "rt_line_input_ch_err" || sym == "rt_open_err_vstr")
            return RtComponent::IoFs;

        if (starts("rt_exec_") || starts("rt_machine_"))
            return RtComponent::Exec;

        if (starts("rt_monitor_") || starts("rt_thread_") || starts("rt_safe_"))
            return RtComponent::Threads;

        if (starts("rt_canvas_") || starts("rt_color_") || starts("rt_vec2_") ||
            starts("rt_vec3_") || starts("rt_pixels_"))
            return RtComponent::Graphics;

        return std::nullopt;
    };

    std::string asmText;
    if (!readFile(asmPath, asmText))
    {
        err << "error: unable to read '" << asmPath.string() << "' for runtime library selection\n";
        return 1;
    }

    const std::unordered_set<std::string> symbols = parseRuntimeSymbols(asmText);

    bool needArrays = false;
    bool needOop = false;
    bool needCollections = false;
    bool needText = false;
    bool needIoFs = false;
    bool needExec = false;
    bool needThreads = false;
    bool needGraphics = false;

    for (const auto &sym : symbols)
    {
        const auto comp = needsComponentForSymbol(sym);
        if (!comp)
            continue;
        switch (*comp)
        {
            case RtComponent::Arrays:
                needArrays = true;
                break;
            case RtComponent::Oop:
                needOop = true;
                break;
            case RtComponent::Collections:
                needCollections = true;
                break;
            case RtComponent::Text:
                needText = true;
                break;
            case RtComponent::IoFs:
                needIoFs = true;
                break;
            case RtComponent::Exec:
                needExec = true;
                break;
            case RtComponent::Threads:
                needThreads = true;
                break;
            case RtComponent::Graphics:
                needGraphics = true;
                break;
            case RtComponent::Base:
                break;
        }
    }

    if (needText || needIoFs || needExec)
        needCollections = true;
    if (needCollections || needArrays || needGraphics || needThreads)
        needOop = true;

    const std::optional<std::filesystem::path> buildDirOpt = findBuildDir();
    const std::filesystem::path buildDir = buildDirOpt.value_or(std::filesystem::path{});

    auto runtimeArchivePath = [&](std::string_view libBaseName) -> std::filesystem::path
    {
        if (!buildDir.empty())
            return buildDir / "src/runtime" /
                   (std::string("lib") + std::string(libBaseName) + ".a");
        return std::filesystem::path("src/runtime") /
               (std::string("lib") + std::string(libBaseName) + ".a");
    };

    std::vector<std::pair<std::string, std::filesystem::path>> requiredArchives;
    requiredArchives.emplace_back("viper_rt_base", runtimeArchivePath("viper_rt_base"));
    if (needOop)
        requiredArchives.emplace_back("viper_rt_oop", runtimeArchivePath("viper_rt_oop"));
    if (needArrays)
        requiredArchives.emplace_back("viper_rt_arrays", runtimeArchivePath("viper_rt_arrays"));
    if (needCollections)
        requiredArchives.emplace_back("viper_rt_collections",
                                      runtimeArchivePath("viper_rt_collections"));
    if (needText)
        requiredArchives.emplace_back("viper_rt_text", runtimeArchivePath("viper_rt_text"));
    if (needIoFs)
        requiredArchives.emplace_back("viper_rt_io_fs", runtimeArchivePath("viper_rt_io_fs"));
    if (needExec)
        requiredArchives.emplace_back("viper_rt_exec", runtimeArchivePath("viper_rt_exec"));
    if (needThreads)
        requiredArchives.emplace_back("viper_rt_threads", runtimeArchivePath("viper_rt_threads"));
    if (needGraphics)
        requiredArchives.emplace_back("viper_rt_graphics", runtimeArchivePath("viper_rt_graphics"));

    std::vector<std::string> missingTargets;
    if (!buildDir.empty())
    {
        for (const auto &[tgt, path] : requiredArchives)
        {
            if (!fileExists(path))
                missingTargets.push_back(tgt);
        }
        if (needGraphics)
        {
            const std::filesystem::path gfxLib = buildDir / "lib" / "libvipergfx.a";
            if (!fileExists(gfxLib))
                missingTargets.push_back("vipergfx");
        }
        if (!missingTargets.empty())
        {
            std::vector<std::string> cmd = {"cmake", "--build", buildDir.string(), "--target"};
            cmd.insert(cmd.end(), missingTargets.begin(), missingTargets.end());
            const RunResult build = run_process(cmd);
            if (!build.out.empty())
                out << build.out;
#if defined(_WIN32)
            if (!build.err.empty())
                err << build.err;
#endif
            if (build.exit_code != 0)
            {
                err << "error: failed to build required runtime libraries in '" << buildDir.string()
                    << "'\n";
                return 1;
            }
        }
    }

    std::vector<std::string> cmd = {kCcCommand, asmPath.string()};
    auto appendArchiveIf = [&](std::string_view name)
    {
        const std::filesystem::path path = runtimeArchivePath(name);
        if (fileExists(path))
            cmd.push_back(path.string());
    };

    if (needGraphics)
        appendArchiveIf("viper_rt_graphics");
    if (needExec)
        appendArchiveIf("viper_rt_exec");
    if (needIoFs)
        appendArchiveIf("viper_rt_io_fs");
    if (needText)
        appendArchiveIf("viper_rt_text");
    if (needCollections)
        appendArchiveIf("viper_rt_collections");
    if (needArrays)
        appendArchiveIf("viper_rt_arrays");
    if (needThreads)
        appendArchiveIf("viper_rt_threads");
    if (needOop)
        appendArchiveIf("viper_rt_oop");
    appendArchiveIf("viper_rt_base");

    if (needGraphics)
    {
        std::filesystem::path gfxLib;
        if (!buildDir.empty())
            gfxLib = buildDir / "lib" / "libvipergfx.a";
        else
            gfxLib = std::filesystem::path("lib") / "libvipergfx.a";
        if (fileExists(gfxLib))
            cmd.push_back(gfxLib.string());
#if defined(__APPLE__)
        cmd.push_back("-framework");
        cmd.push_back("Cocoa");
#endif
    }

#if defined(__APPLE__)
    cmd.push_back("-Wl,-dead_strip");
#else
    cmd.push_back("-Wl,--gc-sections");
    if (needThreads)
        cmd.push_back("-pthread");
    cmd.push_back("-lm");
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

    if (!opts_.emit_asm)
    {
        // Maintain parity with previous behaviour by always keeping the file around for linking.
    }

    const bool wantsObjectOnly = !opts_.output_obj_path.empty() && !opts_.run_native;
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
        }
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
    }

    const bool needsExecutable = opts_.run_native || opts_.output_obj_path.empty();
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
    const int linkExit = invokeLinker(asmPath, exePath, out, err);
    if (linkExit != 0)
    {
        result.exit_code = linkExit == -1 ? 1 : linkExit;
        result.stdout_text = out.str();
        result.stderr_text = err.str();
        return result;
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
