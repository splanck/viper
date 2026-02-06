//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/LinkerSupport.hpp
// Purpose: Shared linker utilities used by both x86_64 and AArch64 backends.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/RuntimeComponents.hpp"

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace viper::codegen::common
{

// =========================================================================
// Pure utility functions
// =========================================================================

/// @brief Check if a file exists (suppresses exceptions).
bool fileExists(const std::filesystem::path &path);

/// @brief Read an entire file into a string.
bool readFileToString(const std::filesystem::path &path, std::string &dst);

/// @brief Search for the CMake build directory by walking parent directories.
std::optional<std::filesystem::path> findBuildDir();

/// @brief Scan assembly text for referenced runtime symbols (rt_* / _rt_*).
std::unordered_set<std::string> parseRuntimeSymbols(std::string_view text);

/// @brief Compute the archive path for a runtime library.
std::filesystem::path runtimeArchivePath(const std::filesystem::path &buildDir,
                                         std::string_view libBaseName);

// =========================================================================
// Link context — shared linker preamble
// =========================================================================

/// @brief Holds resolved linker state after symbol scanning and archive discovery.
struct LinkContext
{
    std::filesystem::path buildDir;
    std::vector<RtComponent> requiredComponents;
    std::vector<std::pair<std::string, std::filesystem::path>> requiredArchives;
};

/// @brief Check if a specific runtime component is required.
bool hasComponent(const LinkContext &ctx, RtComponent c);

/// @brief Scan assembly for runtime symbols, resolve components, find build dir,
///        compute archive paths, and trigger cmake builds for missing targets.
/// @return 0 on success, non-zero on failure.
int prepareLinkContext(const std::string &asmPath,
                       LinkContext &ctx,
                       std::ostream &out,
                       std::ostream &err);

/// @brief Append required archive paths (in reverse dependency order) to a link command.
void appendArchives(const LinkContext &ctx, std::vector<std::string> &cmd);

/// @brief Append the graphics library and frameworks if graphics is required.
void appendGraphicsLibs(const LinkContext &ctx,
                        std::vector<std::string> &cmd,
                        const std::vector<std::string> &frameworks);

// =========================================================================
// Tool invocation
// =========================================================================

/// @brief Invoke the system assembler.
/// @param ccArgs Base command and flags, e.g. {"cc", "-arch", "arm64"}.
int invokeAssembler(const std::vector<std::string> &ccArgs,
                    const std::string &asmPath,
                    const std::string &objPath,
                    std::ostream &out,
                    std::ostream &err);

/// @brief Execute a linked native binary and forward its output.
int runExecutable(const std::string &exePath, std::ostream &out, std::ostream &err);

} // namespace viper::codegen::common
