//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/diag/BasicDiag.hpp
// Purpose: Generated diagnostic descriptors for the BASIC frontend.
// Key invariants: Enum values are stable across regeneration; do not reorder.
// Ownership/Lifetime: All returned string_views point to static storage.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//
//
// NOTE: Generated file -- do not edit manually.
//

#pragma once

#include "support/diagnostics.hpp"
#include <initializer_list>
#include <string>
#include <string_view>

namespace il::frontends::basic::diag
{

/// @brief Enumeration of all BASIC frontend diagnostics.
/// @details Each enumerator maps to a unique diagnostic id, error code,
///          severity level, and human-readable format string. Used by the
///          BASIC lowering passes to report semantic errors and warnings.
enum class BasicDiag
{
    UnknownVariable,        ///< Reference to an undeclared variable.
    UnknownArray,           ///< Reference to an undeclared array.
    NotAnArray,             ///< Subscript applied to a non-array identifier.
    UnknownProcedure,       ///< Call to an undeclared procedure.
    DuplicateParameter,     ///< Parameter name repeated in the same signature.
    ArrayParamType,         ///< Array used where a scalar parameter is expected.
    DuplicateProcedure,     ///< Procedure name declared more than once.
    UnknownStatement,       ///< Unrecognized statement keyword.
    UnexpectedLineNumber,   ///< Line number appears where it is not allowed.
    UnknownLineLabel,       ///< GOTO/GOSUB targets a non-existent line label.
    IfaceDupMethod,         ///< Duplicate method declared in an interface.
    ClassMissesIfaceMethod, ///< Class fails to implement a required interface method.
    NsUnknownNamespace,     ///< Reference to an undefined namespace.
    NsTypeNotInNs,          ///< Type not found within the specified namespace.
    NsAmbiguousType,        ///< Ambiguous type reference across imported namespaces.
    NsDuplicateAlias,       ///< Namespace alias already defined.
    NsUsingAfterDecl,       ///< Using directive appears after declarations.
    NsTypeNotFound,         ///< Named type not found in any visible scope.
    NsAliasShadowsNs,       ///< Alias name shadows an existing namespace.
    NsUsingNotFileScope,    ///< Using directive used outside file scope.
    NsReservedViper,        ///< Identifier collides with reserved Viper namespace.
    ReservedRootDecl        ///< Declaration name collides with a root-level reserved name.
};

/// @brief A key/value pair used for placeholder substitution in diagnostic messages.
struct Replacement
{
    std::string_view key;   ///< Placeholder name (without delimiters).
    std::string_view value; ///< Replacement text to substitute.
};

/// @brief Static metadata record for a single BASIC diagnostic.
/// @details Holds the identifier string, numeric code, severity level, and
///          message format template for one diagnostic kind.
struct BasicDiagInfo
{
    std::string_view id;            ///< Unique diagnostic identifier string.
    std::string_view code;          ///< Numeric diagnostic code for programmatic use.
    il::support::Severity severity; ///< Severity level (error, warning, etc.).
    std::string_view format;        ///< Message format string with placeholders.
};

/// @brief Retrieve the full metadata record for a diagnostic.
/// @param diag The diagnostic enumerator.
/// @return Reference to the static BasicDiagInfo for @p diag.
[[nodiscard]] const BasicDiagInfo &getInfo(BasicDiag diag);

/// @brief Retrieve the identifier string for a diagnostic.
/// @param diag The diagnostic enumerator.
/// @return View of the diagnostic's unique identifier.
[[nodiscard]] std::string_view getId(BasicDiag diag);

/// @brief Retrieve the numeric code string for a diagnostic.
/// @param diag The diagnostic enumerator.
/// @return View of the diagnostic's numeric code.
[[nodiscard]] std::string_view getCode(BasicDiag diag);

/// @brief Retrieve the severity level for a diagnostic.
/// @param diag The diagnostic enumerator.
/// @return The diagnostic's severity (error, warning, etc.).
[[nodiscard]] il::support::Severity getSeverity(BasicDiag diag);

/// @brief Retrieve the raw format string for a diagnostic.
/// @param diag The diagnostic enumerator.
/// @return View of the format template with unexpanded placeholders.
[[nodiscard]] std::string_view getFormat(BasicDiag diag);

/// @brief Format a diagnostic message with placeholder substitution.
/// @param diag The diagnostic enumerator.
/// @param replacements Key/value pairs to substitute into the format string.
/// @return The fully formatted diagnostic message string.
[[nodiscard]] std::string formatMessage(BasicDiag diag,
                                        std::initializer_list<Replacement> replacements = {});

} // namespace il::frontends::basic::diag
