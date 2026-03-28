//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/BasicAnalysis.hpp
// Purpose: Partial-compilation API for BASIC IDE tooling (completion, hover, etc.).
// Key invariants:
//   - Runs parse → CollectProcedures → foldConstants → SemanticAnalyzer
//   - Stops before lowering (no IL generation)
//   - Result is heap-allocated for stable DiagnosticEngine address
// Ownership/Lifetime:
//   - AnalysisResult owns diagnostics, emitter, AST, and sema
//   - Destruction order: sema → ast → emitter → diagnostics (reverse declaration)
// Links: frontends/basic/BasicCompiler.hpp, frontends/basic/SemanticAnalyzer.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/BasicCompiler.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"

#include <memory>

namespace il::frontends::basic {

// Forward declarations
struct Program;

/// @brief Result of a partial BASIC compilation run (parse + sema only).
///
/// Provides access to the resolved symbol tables even when the source has
/// errors. Callers should query `sema` for completion/hover information and
/// inspect `diagnostics` for error details.
///
/// Heap-allocated to ensure stable DiagnosticEngine address (SemanticAnalyzer
/// holds a reference to the emitter which references diagnostics).
struct BasicAnalysisResult {
    /// @brief Diagnostics accumulated during parsing and semantic analysis.
    /// @note Declared first so it is destroyed last.
    il::support::DiagnosticEngine diagnostics{};

    /// @brief Formatter for diagnostics.
    std::unique_ptr<DiagnosticEmitter> emitter;

    /// @brief The parsed AST (owned).
    /// @details May be nullptr if the parser cannot produce any output.
    std::unique_ptr<Program> ast;

    /// @brief The semantic analyzer after analysis (owned).
    /// @details Non-null whenever `ast` is non-null.
    std::unique_ptr<SemanticAnalyzer> sema;

    /// @brief File identifier for the analyzed source.
    uint32_t fileId{0};

    /// @brief True if any errors were reported.
    [[nodiscard]] bool hasErrors() const {
        return diagnostics.errorCount() > 0;
    }
};

/// @brief Run the BASIC pipeline through semantic analysis only.
///
/// Executes Lexer → Parser → CollectProcedures → foldConstants → SemanticAnalyzer,
/// stopping before lowering. Returns a heap-allocated result containing the
/// analyzed AST and a live SemanticAnalyzer whose symbol tables can be queried
/// for IDE features.
///
/// @param input  Source information (code text + path + optional file id).
/// @param sm     Source manager for file registration and diagnostics.
/// @return       Heap-allocated result with AST, sema, and diagnostics.
std::unique_ptr<BasicAnalysisResult> parseAndAnalyzeBasic(const BasicCompilerInput &input,
                                                          il::support::SourceManager &sm);

} // namespace il::frontends::basic
