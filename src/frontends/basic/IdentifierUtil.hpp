//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file provides utilities for canonicalizing BASIC identifiers and
// qualified names according to BASIC's case-insensitive language semantics.
//
// BASIC Identifier Rules:
// BASIC is a case-insensitive language where identifiers like "Counter",
// "COUNTER", and "counter" all refer to the same variable. The frontend
// canonicalizes all identifiers to lowercase for consistent symbol table
// lookups and IL name generation.
//
// Key Utilities:
// - canonicalizeIdentifier: Converts a single identifier to lowercase,
//   validating that it contains only [A-Za-z0-9_]
// - qualifiedName: Joins namespace/module segments with '.' separator
// - validateIdentifier: Checks identifier validity without conversion
//
// Canonicalization:
// All identifiers are canonicalized to ASCII lowercase for:
// - Symbol table lookups (case-insensitive matching)
// - IL name generation (deterministic output)
// - Namespace resolution (consistent qualified names)
//
// Validation:
// Identifiers must contain only [A-Za-z0-9_] characters. Type suffixes
// (%, &, !, #, $) are handled separately and not part of the base identifier.
//
// Qualified Names:
// For namespace support (NAMESPACE...END NAMESPACE), qualified names use
// dot notation:
//   MyNamespace.MyModule.MyFunction
//
// The canonicalization preserves namespace hierarchy while applying lowercase
// conversion to each segment.
//
// Error Handling:
// Validation is conservative: functions return empty string on failure,
// leaving detailed error reporting to callers (parser, semantic analyzer).
//
// Integration:
// - Used by: Parser for identifier processing
// - Used by: SemanticAnalyzer for symbol table operations
// - Used by: Lowerer for IL name generation
//
// Design Notes:
// - Header-only implementation for zero overhead
// - No dynamic state; all functions are pure
// - Validation failures return empty string for caller handling
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace il::frontends::basic
{

/// @brief Canonicalize a single identifier to lowercase ASCII.
/// @details Validates characters against [A-Za-z0-9_]. On validation failure,
///          returns an empty string to signal invalid input to the caller.
/// @param ident Input identifier text.
/// @return Lowercased identifier or empty on invalid characters.
inline std::string CanonicalizeIdent(std::string_view ident)
{
    std::string out;
    out.reserve(ident.size());
    for (unsigned char ch : ident)
    {
        if (std::isalnum(ch) || ch == '_')
        {
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
        else
        {
            return std::string{}; // invalid
        }
    }
    return out;
}

/// @brief Canonicalize a single identifier (alias of CanonicalizeIdent).
inline std::string Canon(std::string_view ident)
{
    return CanonicalizeIdent(ident);
}

/// @brief Join qualified name segments with '.' separators.
/// @param parts Qualified path segments in declaration order.
/// @return Dot-joined name; empty when @p parts is empty.
inline std::string JoinQualified(const std::vector<std::string> &parts)
{
    if (parts.empty())
    {
        return std::string{};
    }
    size_t total = 0;
    for (const auto &p : parts)
        total += p.size();
    std::string out;
    out.reserve(total + (parts.size() - 1));
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i)
            out.push_back('.');
        out.append(parts[i]);
    }
    return out;
}

/// @brief Join qualified name segments with '.' (alias of JoinQualified).
inline std::string JoinDots(const std::vector<std::string> &parts)
{
    return JoinQualified(parts);
}

/// @brief Canonicalize each segment then join as a fully-qualified name.
/// @details Each segment is validated via @ref CanonicalizeIdent. If any
///          segment is invalid, returns an empty string.
/// @param parts Qualified path segments in declaration order.
/// @return Lowercased, dot-joined name or empty when validation fails.
inline std::string CanonicalizeQualified(const std::vector<std::string> &parts)
{
    std::vector<std::string> canon;
    canon.reserve(parts.size());
    for (const auto &p : parts)
    {
        std::string c = CanonicalizeIdent(p);
        if (c.empty() && !p.empty())
        {
            return std::string{};
        }
        canon.emplace_back(std::move(c));
    }
    return JoinQualified(canon);
}

/// @brief Canonicalize each segment and join with '.' separators.
/// @details Equivalent to CanonicalizeQualified.
inline std::string CanonJoin(const std::vector<std::string> &parts)
{
    return CanonicalizeQualified(parts);
}

/// @brief Split a dot-joined string into segments (empties ignored).
/// @param dotted Qualified path like "A.B.C".
/// @return Vector of non-empty segments in order.
inline std::vector<std::string> SplitDots(std::string_view dotted)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : dotted)
    {
        if (ch == '.')
        {
            if (!cur.empty())
            {
                out.emplace_back(std::move(cur));
                cur.clear();
            }
        }
        else
        {
            cur.push_back(ch);
        }
    }
    if (!cur.empty())
        out.emplace_back(std::move(cur));
    return out;
}

/// @brief Strip BASIC type suffix from an identifier.
/// @details Removes trailing %, &, !, #, or $ if present.
///          These suffixes denote types in BASIC (Integer, Long, Single, Double, String).
/// @param ident Input identifier with possible type suffix.
/// @return Identifier without the type suffix.
inline std::string StripTypeSuffix(std::string_view ident)
{
    if (ident.empty())
        return std::string{};
    char last = ident.back();
    if (last == '$' || last == '%' || last == '#' || last == '!' || last == '&')
        return std::string{ident.substr(0, ident.size() - 1)};
    return std::string{ident};
}

} // namespace il::frontends::basic
