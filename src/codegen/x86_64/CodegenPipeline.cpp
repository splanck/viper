//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>

#if !defined(_WIN32)
#    include <sys/wait.h>
#endif

namespace viper::codegen::x64
{
namespace
{

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
    /// @param opts Pipeline configuration describing the IL input.
    /// @return Filesystem path for the linked executable.
    std::filesystem::path deriveExecutablePath(const CodegenPipeline::Options &opts)
{
    std::filesystem::path exe = std::filesystem::path(opts.input_il_path);
    if (exe.empty())
    {
        return std::filesystem::path("a.out");
    }
    exe.replace_extension("");
    if (exe.filename().empty() || exe.filename() == ".")
    {
        return exe.parent_path() / "a.out";
    }
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
    const RunResult link = run_process({"cc", asmPath.string(), "-o", exePath.string()});
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
        err << "error: cc exited with status " << exitCode << "\n";
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
    int runExecutable(const std::filesystem::path &exePath,
                      std::ostream &out,
                      std::ostream &err)
{
    const RunResult run = run_process({exePath.string()});
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
    const auto loadResult =
        il::tools::common::loadModuleFromFile(opts_.input_il_path, module, err);
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
    manager.addPass(std::make_unique<passes::EmitPass>(CodegenOptions{}));

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

    const bool needLink = !opts_.output_obj_path.empty() || opts_.run_native;
    if (!needLink)
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
