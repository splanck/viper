// File: src/frontends/basic/Diag.hpp
// Purpose: Centralized diagnostics helpers for BASIC frontend.
// Key invariants: Messages are actionable and consistent across callers.
// Ownership/Lifetime: Helpers emit through a caller-provided DiagnosticEmitter.

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

} // namespace il::frontends::basic::diagx
