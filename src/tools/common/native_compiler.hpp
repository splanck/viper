//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/common/native_compiler.hpp
// Purpose: Shared utility for compiling IL to native binaries via codegen backends.
// Key invariants: detectHostArch() is constexpr and determined at compile time.
//                 compileToNative() dispatches to ARM64 or x64 backends based on arch.
// Ownership/Lifetime: Callers retain ownership of all paths.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace viper::tools
{

/// @brief Target architecture for native code generation.
enum class TargetArch
{
    ARM64,
    X64
};

/// @brief Detect the host architecture at compile time.
/// @return ARM64 on Apple Silicon / AArch64, X64 otherwise.
constexpr TargetArch detectHostArch()
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return TargetArch::ARM64;
#else
    return TargetArch::X64;
#endif
}

/// @brief Check if an output path implies native binary output.
/// @param path The output path to inspect.
/// @return true if the path does NOT end in ".il" (i.e., native output requested).
bool isNativeOutputPath(const std::string &path);

/// @brief Compile an IL file on disk to a native binary.
///
/// For ARM64, delegates to the existing cmd_codegen_arm64 entry point.
/// For X64, uses the CodegenPipeline directly.
///
/// @param ilPath Path to the IL text file on disk.
/// @param outputPath Path for the output native binary.
/// @param arch Target architecture (defaults to host architecture).
/// @return 0 on success, non-zero on failure.
int compileToNative(const std::string &ilPath,
                    const std::string &outputPath,
                    TargetArch arch = detectHostArch());

/// @brief Generate a unique temporary file path for IL serialization.
/// @return A path in the system temp directory with a .il extension.
std::string generateTempIlPath();

} // namespace viper::tools
