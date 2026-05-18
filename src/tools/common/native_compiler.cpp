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

#include "codegen/aarch64/CodegenPipeline.hpp"
#include "codegen/x86_64/CodegenPipeline.hpp"
#include "support/source_manager.hpp"

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
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
                    const std::string &assetObjPath,
                    int backendOptimizeLevel,
                    bool skipIlOptimization,
                    bool timePasses,
                    bool fastLink,
                    std::optional<bool> windowsDebugRuntime) {
    if (arch == TargetArch::ARM64) {
        viper::codegen::aarch64::CodegenPipeline::Options opts;
        opts.input_il_path = ilPath;
        opts.output_obj_path = outputPath;
        opts.optimize = backendOptimizeLevel;
        opts.skip_il_optimization = skipIlOptimization;
        opts.asset_blob_path = assetBlobPath;
        opts.time_passes = timePasses;
        opts.fast_link = fastLink;
        opts.windows_debug_runtime = windowsDebugRuntime;
        if (!assetObjPath.empty())
            opts.extra_objects.push_back(assetObjPath);

        viper::codegen::aarch64::CodegenPipeline pipeline(opts);
        auto result = pipeline.run();
        if (!result.stdout_text.empty())
            std::cout << result.stdout_text;
        if (!result.stderr_text.empty())
            std::cerr << result.stderr_text;
        return result.exit_code;
    }

    // X64: use CodegenPipeline directly
    viper::codegen::x64::CodegenPipeline::Options opts;
    opts.input_il_path = ilPath;
    opts.output_obj_path = outputPath;
    opts.optimize = backendOptimizeLevel;
    opts.skip_il_optimization = skipIlOptimization;
    opts.asset_blob_path = assetBlobPath;
    opts.time_passes = timePasses;
    opts.fast_link = fastLink;
    opts.windows_debug_runtime = windowsDebugRuntime;
#if defined(_WIN32)
    opts.target_abi = viper::codegen::x64::CodegenOptions::TargetABI::Win64;
    opts.target_platform = viper::codegen::x64::CodegenOptions::TargetPlatform::Windows;
#elif defined(__APPLE__)
    opts.target_abi = viper::codegen::x64::CodegenOptions::TargetABI::SysV;
    opts.target_platform = viper::codegen::x64::CodegenOptions::TargetPlatform::Darwin;
#else
    opts.target_abi = viper::codegen::x64::CodegenOptions::TargetABI::SysV;
    opts.target_platform = viper::codegen::x64::CodegenOptions::TargetPlatform::Linux;
#endif
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

int compileModuleToNative(il::core::Module module,
                          const std::string &debugSourcePath,
                          const std::string &outputPath,
                          TargetArch arch,
                          const std::string &assetBlobPath,
                          const std::string &assetObjPath,
                          int backendOptimizeLevel,
                          bool skipIlOptimization,
                          bool moduleAlreadyVerified,
                          bool timePasses,
                          bool fastLink,
                          std::optional<bool> windowsDebugRuntime) {
    const std::string syntheticInputPath = generateTempIlPath();

    if (arch == TargetArch::ARM64) {
        viper::codegen::aarch64::CodegenPipeline::Options opts;
        opts.input_il_path = syntheticInputPath;
        opts.output_obj_path = outputPath;
        opts.optimize = backendOptimizeLevel;
        opts.skip_il_optimization = skipIlOptimization;
        opts.asset_blob_path = assetBlobPath;
        opts.time_passes = timePasses;
        opts.fast_link = fastLink;
        opts.windows_debug_runtime = windowsDebugRuntime;
        if (!assetObjPath.empty())
            opts.extra_objects.push_back(assetObjPath);

        viper::codegen::aarch64::CodegenPipeline pipeline(opts);
        auto result =
            pipeline.runWithModule(std::move(module), debugSourcePath, moduleAlreadyVerified);
        if (!result.stdout_text.empty())
            std::cout << result.stdout_text;
        if (!result.stderr_text.empty())
            std::cerr << result.stderr_text;
        return result.exit_code;
    }

    viper::codegen::x64::CodegenPipeline::Options opts;
    opts.input_il_path = syntheticInputPath;
    opts.output_obj_path = outputPath;
    opts.optimize = backendOptimizeLevel;
    opts.skip_il_optimization = skipIlOptimization;
    opts.asset_blob_path = assetBlobPath;
    opts.time_passes = timePasses;
    opts.fast_link = fastLink;
    opts.windows_debug_runtime = windowsDebugRuntime;
#if defined(_WIN32)
    opts.target_abi = viper::codegen::x64::CodegenOptions::TargetABI::Win64;
    opts.target_platform = viper::codegen::x64::CodegenOptions::TargetPlatform::Windows;
#elif defined(__APPLE__)
    opts.target_abi = viper::codegen::x64::CodegenOptions::TargetABI::SysV;
    opts.target_platform = viper::codegen::x64::CodegenOptions::TargetPlatform::Darwin;
#else
    opts.target_abi = viper::codegen::x64::CodegenOptions::TargetABI::SysV;
    opts.target_platform = viper::codegen::x64::CodegenOptions::TargetPlatform::Linux;
#endif
    if (!assetObjPath.empty())
        opts.extra_objects.push_back(assetObjPath);

    viper::codegen::x64::CodegenPipeline pipeline(opts);
    PipelineResult result;
    try {
        result = pipeline.runWithModule(std::move(module), debugSourcePath, moduleAlreadyVerified);
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
