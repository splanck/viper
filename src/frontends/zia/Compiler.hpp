//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Compiler.hpp
/// @brief Zia compiler driver - orchestrates the complete compilation pipeline.
///
/// @details This file provides the main entry point for compiling Zia source
/// code to Viper Intermediate Language (IL). The compiler driver coordinates all
/// phases of compilation:
///
/// ## Compilation Pipeline
///
/// 1. **Lexing** - Tokenize source text (Lexer)
/// 2. **Parsing** - Build AST from tokens (Parser)
/// 3. **Import Resolution** - Load and merge imported modules
/// 4. **Semantic Analysis** - Type checking and name resolution (Sema)
/// 5. **IL Generation** - Lower AST to IL instructions (Lowerer)
///
/// ## Usage
///
/// The primary API consists of two functions:
///
/// **compile()** - Compile from a source string:
/// ```cpp
/// SourceManager sm;
/// CompilerInput input{.source = sourceCode, .path = "main.zia"};
/// CompilerOptions options{};
/// CompilerResult result = compile(input, options, sm);
///
/// if (result.succeeded()) {
///     // Use result.module
/// } else {
///     // Check result.diagnostics
/// }
/// ```
///
/// **compileFile()** - Compile from a file path:
/// ```cpp
/// SourceManager sm;
/// CompilerOptions options{};
/// CompilerResult result = compileFile("main.zia", options, sm);
/// ```
///
/// ## Import Resolution
///
/// The compiler automatically resolves and merges imported modules:
/// - Relative imports: `import ./utils;` or `import ../lib/helper;`
/// - Simple imports: `import foo;` (looks in same directory)
/// - Circular imports are allowed (skipped without error)
/// - Maximum import depth of 50 levels
/// - Maximum of 100 imported files
///
/// ## Error Handling
///
/// Errors at any compilation phase are accumulated in the CompilerResult's
/// diagnostics field. Use `result.succeeded()` to check for errors, and
/// iterate `result.diagnostics` for detailed error information.
///
/// @invariant All compilation phases are executed in order.
/// @invariant Circular imports are safely skipped (depth limit prevents runaway).
/// @invariant Result module is valid only if succeeded() returns true.
///
/// @see Lexer.hpp - Tokenization
/// @see Parser.hpp - AST construction
/// @see Sema.hpp - Semantic analysis
/// @see Lowerer.hpp - IL generation
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/zia/Options.hpp"
#include "il/core/Module.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace il::frontends::zia
{

/// @brief Input parameters describing the source to compile.
struct CompilerInput
{
    /// @brief Zia source code to compile.
    std::string_view source;

    /// @brief Path used for diagnostics; defaults to "<input>" when empty.
    std::string_view path{"<input>"};

    /// @brief Existing file id within the supplied source manager, if any.
    std::optional<uint32_t> fileId{};
};

/// @brief Aggregated result of compiling Zia source.
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

/// @brief Compile Zia source text into IL.
/// @param input Source information describing the buffer to compile.
/// @param options Front-end options controlling compilation behavior.
/// @param sm Source manager used for diagnostics and tracing.
/// @return Module and diagnostics emitted during compilation.
CompilerResult compile(const CompilerInput &input,
                       const CompilerOptions &options,
                       il::support::SourceManager &sm);

/// @brief Compile Zia source from a file path.
/// @param path Path to the .zia source file.
/// @param options Compiler options.
/// @param sm Source manager.
/// @return Compilation result.
CompilerResult compileFile(const std::string &path,
                           const CompilerOptions &options,
                           il::support::SourceManager &sm);

} // namespace il::frontends::zia
