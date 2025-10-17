//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the lightweight parser that translates runtime signature
// specifications (for example, "i64(i32, f64)") into structured
// RuntimeSignature objects.  The helpers in this file intentionally avoid
// allocating new strings; instead they operate on string_view slices supplied
// by generated metadata tables.  Keeping the logic here ensures the runtime
// descriptor registry can rely on a single, well-documented implementation of
// the parsing rules described in docs/il-guide.md#reference.
//
//===----------------------------------------------------------------------===//

#include "il/runtime/RuntimeSignatureParser.hpp"

#include <cctype>

namespace il::runtime
{
namespace
{

/// @brief Classify whether the provided character should be treated as
///        whitespace by the signature grammar.
///
/// Runtime signature strings are generated programmatically, but the parser
/// still accepts the conventional ASCII whitespace characters so that hand
/// written specs (used in tests) behave the same way.  The helper exists as a
/// constexpr predicate so it can be used in other constexpr utilities within
/// this translation unit.
///
/// @param ch Character to classify.
/// @return True when @p ch is one of space, tab, carriage return, or newline.
constexpr bool isWhitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

/// @brief Convert a textual type token into its @ref il::core::Type::Kind
///        counterpart.
///
/// The parser accepts the abbreviated spellings used in the generated runtime
/// metadata.  The helper trims surrounding whitespace before comparing the
/// token against the supported variants.  Unknown tokens produce
/// @ref il::core::Type::Kind::Error so callers can detect malformed
/// descriptors.
///
/// @param token Potentially whitespace padded type token.
/// @return Matching type kind enumerator or Kind::Error when unrecognised.
il::core::Type::Kind parseKindToken(std::string_view token)
{
    using Kind = il::core::Type::Kind;
    token = trim(token);
    if (token == "void")
        return Kind::Void;
    if (token == "i1" || token == "bool")
        return Kind::I1;
    if (token == "i16")
        return Kind::I16;
    if (token == "i32")
        return Kind::I32;
    if (token == "i64")
        return Kind::I64;
    if (token == "f64")
        return Kind::F64;
    if (token == "str" || token == "string")
        return Kind::Str;
    if (token.rfind("ptr", 0) == 0)
        return Kind::Ptr;
    if (token == "resume" || token == "resume_tok")
        return Kind::ResumeTok;
    return Kind::Error;
}

} // namespace

/// @brief Remove leading and trailing ASCII whitespace from a signature slice.
///
/// The trimming routine is deliberately minimal: it only strips the handful of
/// characters considered whitespace by @ref isWhitespace.  This mirrors the
/// behaviour of the runtime metadata generator and avoids locale dependent
/// classification.
///
/// @param text Textual slice to trim.
/// @return View with leading and trailing whitespace removed.
std::string_view trim(std::string_view text)
{
    while (!text.empty() && isWhitespace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isWhitespace(text.back()))
        text.remove_suffix(1);
    return text;
}

/// @brief Split a comma separated parameter list while respecting nested
///        parentheses.
///
/// Runtime signatures encode pointer qualifiers using a parenthesised suffix
/// (for example, "ptr(i8)"), so the split routine tracks parentheses depth and
/// only treats commas at depth zero as separators.  Empty tokens are filtered
/// out so callers receive a clean list of meaningful parameter slices.
///
/// @param text Portion of the signature between the outer parentheses.
/// @return Ordered list of parameter tokens with surrounding whitespace removed.
std::vector<std::string_view> splitTypeList(std::string_view text)
{
    std::vector<std::string_view> tokens;
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (ch == '(')
        {
            ++depth;
        }
        else if (ch == ')')
        {
            if (depth > 0)
                --depth;
        }
        else if (ch == ',' && depth == 0)
        {
            tokens.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
    }

    if (start < text.size())
    {
        tokens.push_back(trim(text.substr(start)));
    }
    else if (!text.empty())
    {
        tokens.emplace_back();
    }

    std::vector<std::string_view> filtered;
    filtered.reserve(tokens.size());
    for (auto token : tokens)
    {
        if (!token.empty())
            filtered.push_back(token);
    }
    return filtered;
}

/// @brief Parse a runtime signature specification string into a structured
///        @ref RuntimeSignature object.
///
/// The parser accepts the compact notation emitted by
/// data::kRtSigSpecs—"ret(param, ... )"—and performs the following steps:
///   1. Trim surrounding whitespace and locate the outer parentheses.
///   2. Convert the prefix token into the return type kind.
///   3. Split the parameter list with @ref splitTypeList and translate each
///      token into a type kind.
/// Missing parentheses or unknown type tokens produce a signature populated
/// with Kind::Error entries so downstream consumers can report a diagnostic.
///
/// @param spec String representation of the signature.
/// @return Parsed signature ready for insertion into the runtime registry.
RuntimeSignature parseSignatureSpec(std::string_view spec)
{
    RuntimeSignature signature;
    spec = trim(spec);
    const auto open = spec.find('(');
    const auto close = spec.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
        return signature;

    const auto retToken = trim(spec.substr(0, open));
    signature.retType = il::core::Type(parseKindToken(retToken));

    const auto paramsSlice = spec.substr(open + 1, close - open - 1);
    if (!paramsSlice.empty())
    {
        const auto tokens = splitTypeList(paramsSlice);
        signature.paramTypes.reserve(tokens.size());
        for (auto token : tokens)
            signature.paramTypes.emplace_back(parseKindToken(token));
    }

    return signature;
}

} // namespace il::runtime
