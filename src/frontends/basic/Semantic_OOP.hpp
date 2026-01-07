//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/Semantic_OOP.hpp
// Purpose: Declares the OOP index builder for BASIC AST processing.
// Key invariants: Builder walks the AST once to populate the OopIndex.
// Ownership/Lifetime: Builder does not own the AST or the OopIndex.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/OopIndex.hpp"
#include "frontends/basic/ast/NodeFwd.hpp"

namespace il::frontends::basic
{

class DiagnosticEmitter;

/// @brief Populate @p index with class metadata extracted from @p program.
/// @details This is the main entry point that builds the OopIndex from a parsed
///          BASIC program. It walks the AST, extracts class/interface declarations,
///          and populates the index with metadata for use by later compiler phases.
/// @param program Parsed BASIC program supplying class declarations.
/// @param index Index instance that receives the reconstructed metadata.
/// @param emitter Optional diagnostics interface for reporting errors and warnings.
void buildOopIndex(const Program &program, OopIndex &index, DiagnosticEmitter *emitter);

} // namespace il::frontends::basic