//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the shared native compilation utility. Dispatches to the ARM64
// or x64 codegen backend based on the requested target architecture.
//
//===----------------------------------------------------------------------===//

#include "tools/common/native_compiler.hpp"

#include "codegen/x86_64/CodegenPipeline.hpp"
#include "tools/viper/cmd_codegen_arm64.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace viper::tools
{

bool isNativeOutputPath(const std::string &path)
{
    return std::filesystem::path(path).extension() != ".il";
}

std::string generateTempIlPath()
{
    auto dir = std::filesystem::temp_directory_path();
#ifdef _WIN32
    auto pid = _getpid();
#else
    auto pid = getpid();
#endif
    return (dir / ("viper_build_" + std::to_string(pid) + ".il")).string();
}

int compileToNative(const std::string &ilPath, const std::string &outputPath, TargetArch arch)
{
    if (arch == TargetArch::ARM64)
    {
        // Build argv for cmd_codegen_arm64: [file.il, -o, output]
        std::vector<std::string> storage = {ilPath, "-o", outputPath};
        std::vector<char *> argv;
        argv.reserve(storage.size());
        for (auto &s : storage)
            argv.push_back(s.data());
        return viper::tools::ilc::cmd_codegen_arm64(static_cast<int>(argv.size()), argv.data());
    }

    // X64: use CodegenPipeline directly
    viper::codegen::x64::CodegenPipeline::Options opts;
    opts.input_il_path = ilPath;
    opts.output_obj_path = outputPath;
    opts.optimize = 1;

    viper::codegen::x64::CodegenPipeline pipeline(opts);
    PipelineResult result = pipeline.run();

    if (!result.stdout_text.empty())
        std::cout << result.stdout_text;
    if (!result.stderr_text.empty())
        std::cerr << result.stderr_text;

    return result.exit_code;
}

} // namespace viper::tools
