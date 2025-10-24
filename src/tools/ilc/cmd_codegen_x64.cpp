//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/ilc/cmd_codegen_x64.cpp
// Purpose: Implement the `ilc codegen x64` subcommand glue.
// Key invariants: Backend invocations surface diagnostics without throwing.
// Ownership/Lifetime: Functions borrow CLI arguments and allocate no global state.
// Links: docs/codemap.md, src/codegen/x86_64/Backend.cpp
//
//===----------------------------------------------------------------------===//

#include "cmd_codegen_x64.hpp"

#include "cli.hpp"
#include "codegen/x86_64/Backend.hpp"
#include "il/core/Module.hpp"
#include "tools/common/module_loader.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace viper::tools::ilc
{
namespace
{
struct CodegenConfig
{
    std::string inputPath{};
    bool emitAssembly{false};
    bool linkExecutable{false};
    bool runNative{false};
    std::optional<std::filesystem::path> explicitExePath{};
    std::filesystem::path assemblyPath{};
};

[[nodiscard]] std::filesystem::path defaultAssemblyPath(const std::string &input)
{
    std::filesystem::path path(input);
    if (path.extension() == ".il")
    {
        path.replace_extension(".s");
        return path;
    }
    return std::filesystem::path(path.string() + ".s");
}

[[nodiscard]] std::filesystem::path defaultExecutablePath(const std::string &input)
{
    std::filesystem::path path(input);
    if (!path.extension().empty())
    {
        path.replace_extension();
    }
    return path;
}

[[nodiscard]] viper::codegen::x64::ILModule adaptModule(const il::core::Module &module)
{
    viper::codegen::x64::ILModule shim{};
    (void)module;
    // TODO: Bridge real IL once the backend adapters land.
    return shim;
}

bool writeAssemblyFile(const std::filesystem::path &path, std::string_view text)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        std::cerr << "unable to open " << path << " for writing\n";
        return false;
    }
    out << text;
    if (!out)
    {
        std::cerr << "failed to write assembly to " << path << "\n";
        return false;
    }
    return true;
}

[[nodiscard]] int decodeSystemStatus(int status)
{
    if (status == -1)
    {
        return -1;
    }
#ifdef _WIN32
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

} // namespace

int cmdCodegenX64(int argc, char **argv)
{
    if (argc < 1)
    {
        usage();
        return 1;
    }

    CodegenConfig cfg;
    cfg.inputPath = argv[0];
    cfg.assemblyPath = defaultAssemblyPath(cfg.inputPath);

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "-S")
        {
            cfg.emitAssembly = true;
            continue;
        }
        if (arg == "-o")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "missing operand for -o\n";
                usage();
                return 1;
            }
            cfg.linkExecutable = true;
            cfg.explicitExePath = std::filesystem::path(argv[++i]);
            continue;
        }
        if (arg == "-run-native")
        {
            cfg.runNative = true;
            cfg.linkExecutable = true;
            continue;
        }
        std::cerr << "unknown argument '" << arg << "'\n";
        usage();
        return 1;
    }

    const std::filesystem::path exePath = cfg.explicitExePath.value_or(defaultExecutablePath(cfg.inputPath));

    il::core::Module ilModule;
    const auto loadResult = il::tools::common::loadModuleFromFile(cfg.inputPath, ilModule, std::cerr);
    if (!loadResult.succeeded())
    {
        return 1;
    }
    if (!il::tools::common::verifyModule(ilModule, std::cerr))
    {
        return 1;
    }

    const viper::codegen::x64::ILModule adapterModule = adaptModule(ilModule);
    const viper::codegen::x64::CodegenOptions options{};
    const auto cgResult = viper::codegen::x64::emitModuleToAssembly(adapterModule, options);
    if (!cgResult.errors.empty())
    {
        std::cerr << cgResult.errors << '\n';
        return 1;
    }

    if (!cfg.emitAssembly && !cfg.linkExecutable)
    {
        std::cout << cgResult.asmText;
        return 0;
    }

    if (!writeAssemblyFile(cfg.assemblyPath, cgResult.asmText))
    {
        return 1;
    }

    if (!cfg.linkExecutable)
    {
        return 0;
    }

    std::ostringstream linkCmd;
    linkCmd << "cc \"" << cfg.assemblyPath.string() << "\" -o \"" << exePath.string() << "\"";
    const int linkStatus = std::system(linkCmd.str().c_str());
    const int linkExitCode = decodeSystemStatus(linkStatus);
    if (linkExitCode != 0)
    {
        std::cerr << "cc failed with exit code " << linkExitCode << "\n";
        return linkExitCode == -1 ? 1 : linkExitCode;
    }

    if (!cfg.runNative)
    {
        return 0;
    }

    std::ostringstream runCmd;
    runCmd << '"' << exePath.string() << '"';
    const int runStatus = std::system(runCmd.str().c_str()); // TODO: replace with spawned process capturing stdout/stderr.
    const int runExitCode = decodeSystemStatus(runStatus);
    if (runExitCode == -1)
    {
        std::cerr << "failed to execute native binary\n";
        return 1;
    }
    return runExitCode;
}

void register_codegen_x64_commands(CLI &cli)
{
    // TODO: Integrate with structured CLI infrastructure when available.
    (void)cli;
}

} // namespace viper::tools::ilc

//===----------------------------------------------------------------------===//
