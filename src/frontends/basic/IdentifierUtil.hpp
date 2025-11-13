// File: src/frontends/basic/IdentifierUtil.hpp
// Purpose: Common helpers for canonicalizing identifiers and qualified names
//          in the BASIC front end (case-insensitive language semantics).
// Invariants: Canonical forms are ASCII lowercase; segments are validated to
//             contain only [A-Za-z0-9_]. Joining uses '.' between segments.
// Ownership/Lifetime: Header-only utilities; no dynamic state.
// Notes: Validation is conservative and returns an empty string on failure,
//        leaving error handling to callers in semantic or parsing layers.
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

} // namespace il::frontends::basic
