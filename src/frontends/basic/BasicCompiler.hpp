//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the BasicCompiler driver class, which orchestrates the
// complete BASIC-to-IL compilation pipeline.
//
// The BasicCompiler provides the top-level entry point for compiling BASIC
// source code into Viper Intermediate Language (IL) modules, coordinating all
// compilation stages:
//   Lexer → Parser → AST → Semantic → Lowerer → IL
//
// Key Responsibilities:
// - Pipeline orchestration: Coordinates the lexer, parser, semantic analyzer,
//   and lowerer to produce a valid IL module from BASIC source code
// - Diagnostic management: Collects and reports errors/warnings from all
//   compilation stages via a unified DiagnosticEngine
// - Source management: Integrates with the SourceManager for file tracking
//   and location-based error reporting
// - Configuration: Applies compilation options (bounds checking, optimization
//   settings) to control lowering behavior
// - Result packaging: Returns a structured result containing the IL module,
//   diagnostics, and compilation metadata
//
// Compilation Flow:
// 1. Lexical Analysis: Tokenize BASIC source text
// 2. Syntax Analysis: Parse tokens into AST
// 3. Semantic Analysis: Validate AST, resolve symbols, check types
// 4. IL Generation: Lower AST to IL instructions
// 5. Module Finalization: Return IL module with diagnostics
//
// Compilation Options:
// - Bounds checking: Enable/disable runtime array bounds checks (default: off)
// - Debug info: Control source location preservation in IL (future)
// - Optimization level: Configure lowering optimizations (future)
//
// Input Specification:
// - Source code: BASIC program text as string_view
// - Path: Source file path for diagnostic messages (optional)
// - File ID: Existing file identifier in SourceManager (optional)
//
// Output Structure:
// The compiler returns a BasicCompilerResult containing:
// - IL module: The lowered program ready for VM execution or codegen
// - Diagnostics: All errors, warnings, and notes from compilation
// - Emitter: Configured DiagnosticEmitter for formatting messages
// - File ID: Source file identifier for cross-referencing
//
// Error Handling:
// - Compilation may produce diagnostics at any stage
// - The IL module may be null if parsing or semantic analysis fails
// - Diagnostics are accumulated and available even on failure
// - The result always includes the DiagnosticEngine for error reporting
//
// Design Notes:
// - The compiler owns the DiagnosticEngine and DiagnosticEmitter
// - Source management is borrowed via SourceManager reference
// - The returned IL module transfers ownership to the caller
// - Multiple compilations can share a single SourceManager
//
// Usage:
//   support::SourceManager srcMgr;
//   BasicCompilerInput input{
//     .source = sourceCode,
//     .path = "program.bas"
//   };
//   BasicCompilerOptions opts{
//     .boundsChecks = true
//   };
//   auto result = compileBasicProgram(input, srcMgr, opts);
//   if (result.module) {
//     // Compilation succeeded, use IL module
//   } else {
//     // Report diagnostics
//     result.emitter->report(result.diagnostics);
//   }
//
//===----------------------------------------------------------------------===//
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

    /// @brief Dump the raw token stream from the lexer.
    bool dumpTokens{false};

    /// @brief Dump AST after parsing.
    bool dumpAst{false};

    /// @brief Dump IL after lowering, before optimization.
    bool dumpIL{false};

    /// @brief Dump IL after the full optimization pipeline.
    bool dumpILOpt{false};

    /// @brief Dump IL before and after each optimization pass.
    bool dumpILPasses{false};
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
