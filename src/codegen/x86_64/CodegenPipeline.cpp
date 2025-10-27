// File: src/codegen/x86_64/CodegenPipeline.cpp
// Purpose: Implement the reusable IL-to-x86-64 compilation pipeline used by CLI front ends.
// Key invariants: Passes execute sequentially with early exits on failure, ensuring diagnostics
//                 are recorded deterministically and no partial artefacts leak on error.
// Ownership/Lifetime: The pipeline borrows IL modules and writes assembly/binaries to caller-
//                     specified locations without assuming ownership of external resources.
// Links: docs/codemap.md, src/tools/common/module_loader.hpp

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

CodegenPipeline::CodegenPipeline(Options opts) : opts_(std::move(opts)) {}

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
