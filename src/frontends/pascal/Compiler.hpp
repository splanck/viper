//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the PascalCompiler driver, which orchestrates the
// complete Pascal-to-IL compilation pipeline.
//
// The PascalCompiler provides the top-level entry point for compiling Pascal
// source code into Viper Intermediate Language (IL) modules, coordinating all
// compilation stages:
//   Lexer -> Parser -> AST -> Semantic -> Lowerer -> IL
//
// Reference: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
// Reference: docs/frontend-howto.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include "viper/il/Module.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace il::frontends::pascal
{

/// @brief Options controlling Pascal compilation behavior.
struct PascalCompilerOptions
{
    /// @brief Enable runtime bounds checks for arrays.
    bool boundsChecks{false};
};

/// @brief Input parameters describing the source to compile.
struct PascalCompilerInput
{
    /// @brief Pascal source code to compile.
    std::string_view source;
    /// @brief Path used for diagnostics; defaults to "<input>" when empty.
    std::string_view path{"<input>"};
    /// @brief Existing file id within the supplied source manager, if any.
    std::optional<uint32_t> fileId{};
};

/// @brief Aggregated result of compiling Pascal source.
struct PascalCompilerResult
{
    /// @brief Diagnostics accumulated during compilation.
    il::support::DiagnosticEngine diagnostics{};
    /// @brief File identifier used for the compiled source.
    uint32_t fileId{0};
    /// @brief Lowered IL module.
    il::core::Module module{};

    /// @brief Helper indicating whether compilation succeeded without errors.
    [[nodiscard]] bool succeeded() const;
};

/// @brief Compile Pascal source text into IL.
/// @param input Source information describing the buffer to compile.
/// @param options Front-end options controlling lowering behavior.
/// @param sm Source manager used for diagnostics and tracing.
/// @return Module and diagnostics emitted during compilation.
PascalCompilerResult compilePascal(const PascalCompilerInput &input,
                                   const PascalCompilerOptions &options,
                                   il::support::SourceManager &sm);

/// @brief Multi-file compilation input.
struct PascalMultiFileInput
{
    /// @brief List of unit sources (in dependency order).
    std::vector<PascalCompilerInput> units;
    /// @brief Main program source.
    PascalCompilerInput program;
};

/// @brief Compile multiple Pascal files into a single IL module.
/// @details Units are analyzed first (in order), then the main program.
///          All functions are lowered into a single Module.
/// @param input Multi-file input specification.
/// @param options Front-end options.
/// @param sm Source manager.
/// @return Combined module and diagnostics.
PascalCompilerResult compilePascalMultiFile(const PascalMultiFileInput &input,
                                            const PascalCompilerOptions &options,
                                            il::support::SourceManager &sm);

} // namespace il::frontends::pascal
