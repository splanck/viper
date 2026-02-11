//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/LinkerSupport.hpp
// Purpose: Shared linker utilities used by both x86_64 and AArch64 backends.
// Key invariants: Archive paths are validated via fileExists() before use;
//                 missing archives trigger cmake rebuild before link failure.
// Ownership/Lifetime: All functions are stateless utilities except
//                     prepareLinkContext() which populates a LinkContext by ref.
// Links: codegen/x86_64/CodegenPipeline.hpp, codegen/aarch64/CodegenPipeline.hpp
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

/// @brief Check if a file exists at the given path, suppressing filesystem exceptions.
/// @param path The filesystem path to check.
/// @return True if the file exists and is accessible, false otherwise.
bool fileExists(const std::filesystem::path &path);

/// @brief Read the entire contents of a file into a string.
/// @param path The filesystem path to read from.
/// @param dst Output string to receive the file contents.
/// @return True if the file was read successfully, false on error.
bool readFileToString(const std::filesystem::path &path, std::string &dst);

/// @brief Search for the CMake build directory by walking parent directories.
/// @details Starts from the current working directory and walks upward looking
///          for a directory containing `CMakeCache.txt`. Returns the first match.
/// @return The path to the build directory, or std::nullopt if not found.
std::optional<std::filesystem::path> findBuildDir();

/// @brief Scan assembly text for referenced runtime symbols (rt_* / _rt_*).
/// @details Parses the assembly source for call/reference instructions targeting
///          symbols matching the Viper runtime naming convention.
/// @param text The assembly source text to scan.
/// @return A set of unique runtime symbol names found in the text.
std::unordered_set<std::string> parseRuntimeSymbols(std::string_view text);

/// @brief Compute the filesystem path to a runtime library archive.
/// @param buildDir The CMake build directory containing compiled libraries.
/// @param libBaseName Base name of the library (e.g., "viper_rt_core").
/// @return The full path to the archive file (.a or .lib).
std::filesystem::path runtimeArchivePath(const std::filesystem::path &buildDir,
                                         std::string_view libBaseName);

// =========================================================================
// Link context — shared linker preamble
// =========================================================================

/// @brief Holds resolved linker state after symbol scanning and archive discovery.
/// @details Populated by prepareLinkContext(). Contains the build directory,
///          the set of required runtime components, and the resolved paths to
///          their archive files.
struct LinkContext
{
    std::filesystem::path buildDir;              ///< Resolved CMake build directory.
    std::vector<RtComponent> requiredComponents; ///< Runtime components needed by the program.
    std::vector<std::pair<std::string, std::filesystem::path>>
        requiredArchives; ///< (lib name, archive path) pairs.
};

/// @brief Check if a specific runtime component is required by the link context.
/// @param ctx The link context to query.
/// @param c The runtime component to check for.
/// @return True if @p c is in the required components list.
bool hasComponent(const LinkContext &ctx, RtComponent c);

/// @brief Prepare a complete link context by scanning assembly for runtime symbols.
/// @details Reads the assembly file at @p asmPath, scans for runtime symbols,
///          resolves them to runtime components, locates the build directory,
///          computes archive paths, and triggers cmake rebuilds for any missing
///          library targets.
/// @param asmPath Path to the assembly source file to scan for symbols.
/// @param ctx Output link context to populate with resolved state.
/// @param out Standard output stream for progress messages.
/// @param err Standard error stream for error messages.
/// @return 0 on success, non-zero on failure (missing build dir, build failure, etc.).
int prepareLinkContext(const std::string &asmPath,
                       LinkContext &ctx,
                       std::ostream &out,
                       std::ostream &err);

/// @brief Append required archive paths to a linker command in reverse dependency order.
/// @param ctx The link context containing resolved archive paths.
/// @param cmd The command-line vector to append archive paths to.
void appendArchives(const LinkContext &ctx, std::vector<std::string> &cmd);

/// @brief Append graphics library flags and platform frameworks to the link command.
/// @details Only appends if the link context requires the graphics runtime component.
/// @param ctx The link context to check for graphics dependency.
/// @param cmd The command-line vector to append flags to.
/// @param frameworks Platform-specific framework names (e.g., "-framework Cocoa").
void appendGraphicsLibs(const LinkContext &ctx,
                        std::vector<std::string> &cmd,
                        const std::vector<std::string> &frameworks);

// =========================================================================
// Tool invocation
// =========================================================================

/// @brief Invoke the system assembler to compile an assembly file to an object file.
/// @param ccArgs Base compiler command and flags (e.g., {"cc", "-arch", "arm64"}).
/// @param asmPath Path to the input assembly source file.
/// @param objPath Path for the output object file.
/// @param out Standard output stream for assembler messages.
/// @param err Standard error stream for assembler error messages.
/// @return The assembler's exit code (0 on success).
int invokeAssembler(const std::vector<std::string> &ccArgs,
                    const std::string &asmPath,
                    const std::string &objPath,
                    std::ostream &out,
                    std::ostream &err);

/// @brief Execute a linked native binary and forward its stdout/stderr.
/// @param exePath Path to the executable to run.
/// @param out Standard output stream to forward the executable's stdout to.
/// @param err Standard error stream to forward the executable's stderr to.
/// @return The executable's exit code.
int runExecutable(const std::string &exePath, std::ostream &out, std::ostream &err);

} // namespace viper::codegen::common
