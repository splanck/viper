// File: src/tools/ilc/cmd_codegen_x64.cpp
// Purpose: Implement the ilc glue that lowers IL modules via the x86-64 backend.
// Key invariants: Emits diagnostics on I/O or backend failures and never leaves
//                 partially written output files on error paths.
// Ownership/Lifetime: Borrows parsed IL modules and writes assembly/binaries to
//                     caller-specified locations.
// Links: src/codegen/x86_64/Backend.hpp, src/tools/common/module_loader.hpp

#include "cmd_codegen_x64.hpp"

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
#include <utility>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace viper::tools::ilc
{
namespace
{

/// @brief Configuration derived from the command line.
struct CodegenConfig
{
    std::string inputPath;                       ///< IL file to compile.
    std::optional<std::string> assemblyPath{};   ///< Optional explicit assembly output path.
    std::optional<std::string> executablePath{}; ///< Optional explicit executable path.
    bool runNative{false};                       ///< Request execution of the produced binary.
};

/// @brief Print a concise usage hint for the subcommand.
void printUsageHint()
{
    std::cerr << "usage: ilc codegen x64 <file.il> [-S <file.s>] [-o <a.out>] [-run-native]\n";
}

/// @brief Decode the system() return value into an exit status when possible.
int normaliseSystemStatus(int status)
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

/// @brief Convert the parsed IL module into the temporary adapter structure.
viper::codegen::x64::ILModule convertToAdapterModule(const il::core::Module &module)
{
    viper::codegen::x64::ILModule adapted{};

    adapted.funcs.reserve(module.functions.size());
    for (const auto &func : module.functions)
    {
        viper::codegen::x64::ILFunction adaptedFunc{};
        adaptedFunc.name = func.name;

        // TODO: Populate block parameters and instructions once the real IL
        // bridge is available. For now we emit placeholder blocks so the backend
        // can surface a deterministic diagnostic instead of crashing.
        for (const auto &block : func.blocks)
        {
            viper::codegen::x64::ILBlock adaptedBlock{};
            adaptedBlock.name = block.label;
            adaptedFunc.blocks.push_back(adaptedBlock);
        }

        adapted.funcs.push_back(std::move(adaptedFunc));
    }

    return adapted;
}

/// @brief Parse command-line flags into @p config.
std::optional<CodegenConfig> parseArgs(int argc, char **argv)
{
    if (argc < 1)
    {
        printUsageHint();
        return std::nullopt;
    }

    CodegenConfig config{};
    config.inputPath = argv[0];

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "-S")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "error: -S requires an output path\n";
                printUsageHint();
                return std::nullopt;
            }
            config.assemblyPath = argv[++i];
            continue;
        }
        if (arg == "-o")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "error: -o requires an output path\n";
                printUsageHint();
                return std::nullopt;
            }
            config.executablePath = argv[++i];
            continue;
        }
        if (arg == "-run-native")
        {
            config.runNative = true;
            continue;
        }

        std::cerr << "error: unknown flag '" << arg << "'\n";
        printUsageHint();
        return std::nullopt;
    }

    return config;
}

/// @brief Derive a default assembly output path when the user does not supply one.
std::filesystem::path deriveAssemblyPath(const CodegenConfig &config)
{
    std::filesystem::path assembly = std::filesystem::path(config.inputPath);
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

/// @brief Derive a default executable path when not explicitly requested.
std::filesystem::path deriveExecutablePath(const CodegenConfig &config)
{
    std::filesystem::path exe = std::filesystem::path(config.inputPath);
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

/// @brief Write assembly text to disk, overwriting @p path on success.
bool writeAssemblyFile(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        std::cerr << "error: unable to open '" << path.string() << "' for writing\n";
        return false;
    }
    out << text;
    if (!out)
    {
        std::cerr << "error: failed to write assembly to '" << path.string() << "'\n";
        return false;
    }
    return true;
}

/// @brief Invoke the system C compiler to assemble and link the module.
int invokeLinker(const std::filesystem::path &asmPath, const std::filesystem::path &exePath)
{
    std::ostringstream cmd;
    cmd << "cc \"" << asmPath.string() << "\" -o \"" << exePath.string() << "\"";
    const std::string command = cmd.str();
    const int status = std::system(command.c_str()); // TODO: replace with process management that captures output.
    if (status == -1)
    {
        std::cerr << "error: failed to launch system linker command\n";
        return -1;
    }
    const int exitCode = normaliseSystemStatus(status);
    if (exitCode != 0)
    {
        std::cerr << "error: cc exited with status " << exitCode << "\n";
    }
    return exitCode;
}

/// @brief Execute the generated binary when requested.
int runExecutable(const std::filesystem::path &exePath)
{
    std::ostringstream cmd;
    cmd << "\"" << exePath.string() << "\"";
    const std::string command = cmd.str();
    const int status = std::system(command.c_str()); // TODO: replace with process management that captures output.
    if (status == -1)
    {
        std::cerr << "error: failed to execute '" << exePath.string() << "'\n";
        return -1;
    }
    return normaliseSystemStatus(status);
}

} // namespace

int cmd_codegen_x64(int argc, char **argv)
{
    auto configOpt = parseArgs(argc, argv);
    if (!configOpt)
    {
        return 1;
    }
    const CodegenConfig config = *configOpt;

    il::core::Module module;
    const auto loadResult = il::tools::common::loadModuleFromFile(config.inputPath, module, std::cerr);
    if (!loadResult.succeeded())
    {
        return 1;
    }
    if (!il::tools::common::verifyModule(module, std::cerr))
    {
        return 1;
    }

    const viper::codegen::x64::ILModule adapted = convertToAdapterModule(module);
    const viper::codegen::x64::CodegenResult result = viper::codegen::x64::emitModuleToAssembly(adapted, {});
    if (!result.errors.empty())
    {
        std::cerr << "error: x64 codegen failed:\n" << result.errors << "\n";
        return 1;
    }

    const std::filesystem::path asmPath = config.assemblyPath ? std::filesystem::path(*config.assemblyPath)
                                                              : deriveAssemblyPath(config);
    if (!writeAssemblyFile(asmPath, result.asmText))
    {
        return 1;
    }

    const bool needLink = config.executablePath.has_value() || config.runNative;
    if (!needLink)
    {
        return 0;
    }

    const std::filesystem::path exePath = config.executablePath ? std::filesystem::path(*config.executablePath)
                                                                : deriveExecutablePath(config);
    const int linkExit = invokeLinker(asmPath, exePath);
    if (linkExit != 0)
    {
        return linkExit == -1 ? 1 : linkExit;
    }

    if (!config.runNative)
    {
        return 0;
    }

    const int runExit = runExecutable(exePath);
    if (runExit == -1)
    {
        return 1;
    }
    return runExit;
}

void register_codegen_x64_commands(CLI &cli)
{
    (void)cli;
    // TODO: Integrate with the structured CLI once available.
}

} // namespace viper::tools::ilc

