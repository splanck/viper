//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Compiler.hpp
// Purpose: ViperLang compiler driver.
// Key invariants: Coordinates lexer, parser, sema, and lowerer.
// Ownership/Lifetime: Produces IL Module; borrows source text.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include "il/core/Module.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace il::frontends::viperlang
{

/// @brief Options controlling ViperLang compilation behavior.
struct CompilerOptions
{
    /// @brief Enable runtime bounds checks for arrays.
    bool boundsChecks{true};

    /// @brief Enable overflow checks for arithmetic.
    bool overflowChecks{true};

    /// @brief Enable null checks for optional access.
    bool nullChecks{true};

    /// @brief Dump AST after parsing (for debugging).
    bool dumpAst{false};

    /// @brief Dump IL after lowering (for debugging).
    bool dumpIL{false};
};

/// @brief Input parameters describing the source to compile.
struct CompilerInput
{
    /// @brief ViperLang source code to compile.
    std::string_view source;

    /// @brief Path used for diagnostics; defaults to "<input>" when empty.
    std::string_view path{"<input>"};

    /// @brief Existing file id within the supplied source manager, if any.
    std::optional<uint32_t> fileId{};
};

/// @brief Aggregated result of compiling ViperLang source.
struct CompilerResult
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

/// @brief Compile ViperLang source text into IL.
/// @param input Source information describing the buffer to compile.
/// @param options Front-end options controlling compilation behavior.
/// @param sm Source manager used for diagnostics and tracing.
/// @return Module and diagnostics emitted during compilation.
CompilerResult compile(const CompilerInput &input,
                       const CompilerOptions &options,
                       il::support::SourceManager &sm);

/// @brief Compile ViperLang source from a file path.
/// @param path Path to the .viper source file.
/// @param options Compiler options.
/// @param sm Source manager.
/// @return Compilation result.
CompilerResult compileFile(const std::string &path,
                           const CompilerOptions &options,
                           il::support::SourceManager &sm);

} // namespace il::frontends::viperlang
