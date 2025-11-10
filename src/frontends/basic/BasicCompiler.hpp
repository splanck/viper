// File: src/frontends/basic/BasicCompiler.hpp
// Purpose: Declares the BASIC front-end driver that compiles source text into IL.
// Key invariants: Diagnostics are captured before emitting the final module.
// Ownership/Lifetime: Result owns diagnostics; borrows SourceManager for source mapping.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include "viper/il/Module.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace il::frontends::basic
{

/// @brief Options controlling BASIC compilation behavior.
/// @invariant Bounds check flag only affects lowering.
struct BasicCompilerOptions
{
    /// @brief Enable debug bounds checks when lowering arrays.
    bool boundsChecks{false};
};

/// @brief Input parameters describing the source to compile.
/// @invariant When @ref fileId is set, @ref path may be empty.
struct BasicCompilerInput
{
    /// @brief BASIC source code to compile.
    std::string_view source;
    /// @brief Path used for diagnostics; defaults to "<input>" when empty.
    std::string_view path{"<input>"};
    /// @brief Existing file id within the supplied source manager, if any.
    std::optional<uint32_t> fileId{};
};

/// @brief Aggregated result of compiling BASIC source.
/// @ownership Owns diagnostics engine and emitter; module returned by value.
struct BasicCompilerResult
{
    /// @brief Diagnostics accumulated during compilation.
    il::support::DiagnosticEngine diagnostics{};
    /// @brief Formatter for diagnostics bound to the provided source manager.
    std::unique_ptr<DiagnosticEmitter> emitter{};
    /// @brief File identifier used for the compiled source.
    uint32_t fileId{0};
    /// @brief Lowered IL module.
    il::core::Module module{};

    /// @brief Helper indicating whether compilation succeeded without errors.
    [[nodiscard]] bool succeeded() const;
};

/// @brief Compile BASIC source text into IL.
/// @param input Source information describing the buffer to compile.
/// @param options Front-end options controlling lowering behavior.
/// @param sm Source manager used for diagnostics and tracing.
/// @return Module and diagnostics emitted during compilation.
BasicCompilerResult compileBasic(const BasicCompilerInput &input,
                                 const BasicCompilerOptions &options,
                                 il::support::SourceManager &sm);

} // namespace il::frontends::basic
