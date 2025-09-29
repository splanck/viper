// File: src/il/io/TypeParser.cpp
// Purpose: Implements parsing for IL textual type specifiers.
// Key invariants: Supported types mirror docs/il-guide.md#reference definitions.
// Ownership/Lifetime: Stateless utilities returning value objects.
// License: MIT (see LICENSE for details).
// Links: docs/il-guide.md#reference

#include "il/io/TypeParser.hpp"

namespace il::io
{

/// @brief Resolve a primitive IL type token to a concrete Type value.
/// @param token Lowercase identifier naming a supported primitive type (e.g., "i64").
/// @param ok Optional pointer that receives true when parsing succeeds and false on failure.
/// @return Matching il::core::Type on success or default constructed when the token is unsupported.
/// @note When @p ok is null the caller is opting out of explicit success signalling; failure is
///       observable via the returned default-constructed Type.
il::core::Type parseType(const std::string &token, bool *ok)
{
    auto makeType = [ok](il::core::Type::Kind kind)
    {
        if (ok)
            *ok = true;
        return il::core::Type(kind);
    };

    if (token == "i16")
        return makeType(il::core::Type::Kind::I16);
    if (token == "i32")
        return makeType(il::core::Type::Kind::I32);
    if (token == "i64")
        return makeType(il::core::Type::Kind::I64);
    if (token == "i1")
        return makeType(il::core::Type::Kind::I1);
    if (token == "f64")
        return makeType(il::core::Type::Kind::F64);
    if (token == "ptr")
        return makeType(il::core::Type::Kind::Ptr);
    if (token == "str")
        return makeType(il::core::Type::Kind::Str);
    if (token == "error" || token == "Error")
        return makeType(il::core::Type::Kind::Error);
    if (token == "resume_tok" || token == "ResumeTok")
        return makeType(il::core::Type::Kind::ResumeTok);
    if (token == "void")
        return makeType(il::core::Type::Kind::Void);

    if (ok)
        *ok = false;
    return il::core::Type();
}

} // namespace il::io
