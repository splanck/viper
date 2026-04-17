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
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace viper::tools {
namespace {

std::string generateUniqueTempPath(const char *prefix, const char *extension) {
    static std::atomic<uint64_t> counter{0};

    const auto dir = std::filesystem::temp_directory_path();
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const auto pid = static_cast<uint64_t>(_getpid());
#else
    const auto pid = static_cast<uint64_t>(getpid());
#endif
    const auto uniqueId = counter.fetch_add(1, std::memory_order_relaxed);
    return (dir / (std::string(prefix) + "_" + std::to_string(pid) + "_" + std::to_string(tick) +
                   "_" + std::to_string(uniqueId) + extension))
        .string();
}

} // namespace

/// @brief Is native output path.
bool isNativeOutputPath(const std::string &path) {
    return std::filesystem::path(path).extension() != ".il";
}

/// @brief Generate temp il path.
std::string generateTempIlPath() {
    return generateUniqueTempPath("viper_build", ".il");
}

/// @brief Compile to native.
int compileToNative(const std::string &ilPath,
                    const std::string &outputPath,
                    TargetArch arch,
                    const std::string &assetBlobPath,
                    const std::string &assetObjPath) {
    if (arch == TargetArch::ARM64) {
        // The frontend already emitted the final IL. Do not re-run an IL
        // optimization pipeline here; that can double-optimize build output
        // and introduce correctness regressions.
        std::vector<std::string> storage = {ilPath, "-o", outputPath, "-O0"};
        if (!assetBlobPath.empty()) {
            storage.push_back("--asset-blob");
            storage.push_back(assetBlobPath);
        }
        if (!assetObjPath.empty()) {
            storage.push_back("--extra-obj");
            storage.push_back(assetObjPath);
        }
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
    opts.optimize = 0;
    opts.asset_blob_path = assetBlobPath;
    if (!assetObjPath.empty())
        opts.extra_objects.push_back(assetObjPath);

    viper::codegen::x64::CodegenPipeline pipeline(opts);
    PipelineResult result;
    try {
        result = pipeline.run();
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }

    if (!result.stdout_text.empty())
        std::cout << result.stdout_text;
    if (!result.stderr_text.empty())
        std::cerr << result.stderr_text;

    return result.exit_code;
}

/// @brief Generate temp asset path.
std::string generateTempAssetPath() {
    return generateUniqueTempPath("viper_assets", ".vpa");
}

} // namespace viper::tools
