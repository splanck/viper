//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file provides centralized diagnostic helper functions for the BASIC
// frontend, ensuring consistent and actionable error messages across all
// compilation stages.
//
// Diagnostic Helpers:
// These functions provide high-level diagnostic emission for common error
// scenarios throughout the BASIC frontend:
// - Duplicate procedure definitions
// - Type mismatches in expressions and assignments
// - Invalid control flow constructs
// - Undefined symbol references
// - Array dimension mismatches
//
// Key Benefits:
// - Consistency: Standardized error message formatting across the frontend
// - Actionability: Messages include context and suggestions for fixes
// - Reusability: Common diagnostic patterns are centralized rather than
//   duplicated throughout parser, semantic analyzer, and lowerer code
// - Maintainability: Error message updates can be made in one place
//
// Integration:
// - Used by: Parser, SemanticAnalyzer, Lowerer
// - Emits through: DiagnosticEmitter (passed as parameter)
// - Message format: Consistent with BASIC frontend diagnostic conventions
//
// Design Notes:
// - Helpers are stateless functions that emit through caller-provided emitter
// - Each function encapsulates a specific diagnostic scenario with appropriate
//   severity level and message text
// - Functions may format multi-part diagnostics (e.g., showing both definition
//   locations for duplicate symbols)
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "support/source_location.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace il::frontends::basic::diagx
{

/// @brief Emit duplicate procedure error with both definition locations.
/// @details Formats a single actionable error message:
///          "duplicate procedure '<qname>' first defined at X:line, again at Y:line"
///          and places the caret at the second occurrence location.
void ErrorDuplicateProc(DiagnosticEmitter &emitter,
                        std::string_view qname,
                        il::support::SourceLoc first,
                        il::support::SourceLoc second);

/// @brief Emit unknown unqualified procedure with attempted candidates listed.
/// @details Canonicalizes identifier to lowercase in the headline while preserving
///          the canonical, fully-qualified candidates in the tried list.
void ErrorUnknownProc(DiagnosticEmitter &emitter,
                      il::support::SourceLoc loc,
                      std::string_view ident,
                      const std::vector<std::string> &tried);

/// @brief Emit unknown qualified procedure error.
/// @details Caller should provide a canonicalized qualified name if available.
void ErrorUnknownProcQualified(DiagnosticEmitter &emitter,
                               il::support::SourceLoc loc,
                               std::string_view qname);

/// @brief Emit unknown unqualified procedure with a possibly long tried list (truncated).
void ErrorUnknownProcWithTries(DiagnosticEmitter &emitter,
                               il::support::SourceLoc loc,
                               std::string_view ident,
                               const std::vector<std::string> &tried);

/// @brief Emit ambiguous procedure diagnostic with sorted matches.
void ErrorAmbiguousProc(DiagnosticEmitter &emitter,
                        il::support::SourceLoc loc,
                        std::string_view ident,
                        std::vector<std::string> matches);

/// @brief Emit unknown type with tried list (truncated when long).
void ErrorUnknownTypeWithTries(DiagnosticEmitter &emitter,
                               il::support::SourceLoc loc,
                               std::string_view ident,
                               const std::vector<std::string> &tried);

/// @brief Emit a note showing that an alias expanded to a qualified namespace.
void NoteAliasExpansion(DiagnosticEmitter &emitter,
                        std::string_view alias,
                        std::string_view targetQn);

/// @brief Emit error for attempts to declare a procedure that shadows a builtin extern.
/// @details Used when a user-defined procedure collides with a seeded Viper.* runtime helper.
void ErrorBuiltinShadow(DiagnosticEmitter &emitter,
                        std::string_view qname,
                        il::support::SourceLoc loc);

} // namespace il::frontends::basic::diagx
