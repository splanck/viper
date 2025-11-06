//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the light-weight parser that recognises IL textual type tokens.
// The translation unit remains intentionally tiny so the parser can be used
// freely in tools and unit tests without pulling in the full front-end stack.
//
// Invariants:
//   * Only lowercase mnemonic spellings are accepted for primitive types,
//     matching the canonical IL textual format.
//   * Error and resume-token synonyms are tolerated to make diagnostics more
//     forgiving when called from user-facing tools.
// Ownership/Lifetime: Parsing functions operate purely on caller-provided
//                     strings and return value objects by copy.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Helpers for converting textual IL type names into @ref il::core::Type.
/// @details The parsing functions accept lower-case mnemonic tokens and translate
///          them into the strongly typed representation used by the rest of the
///          compiler.  The helpers are pure and do not retain references to
///          caller data.

#include "il/internal/io/TypeParser.hpp"

namespace il::io
{

/// @brief Resolve a primitive IL type token to a concrete Type value.
/// @details Accepts canonical textual spellings (``i1``, ``i16``, ``i32``, ``i64``,
///          ``f64``, ``ptr``, ``str``) along with ``void`` and the error/resume
///          token mnemonics.  When @p ok is supplied the helper writes a boolean
///          indicating whether the token mapped to a known primitive; otherwise
///          callers detect failure by observing a default-constructed @ref Type.
///          The parser intentionally performs no trimming so higher-level
///          components remain responsible for whitespace handling.
/// @param token Identifier naming a supported primitive type (e.g., ``"i64"``).
/// @param ok Optional pointer that receives true on success and false on
///        failure.  Unmodified when the caller passes @c nullptr.
/// @return Concrete @ref il::core::Type describing @p token, or default
///         constructed when unsupported.
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
