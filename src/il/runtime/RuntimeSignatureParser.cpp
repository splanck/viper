//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the lightweight parser that converts textual runtime signature
// specifications into structured @ref RuntimeSignature objects.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Parsing helpers for runtime signature specifications.
/// @details Translates the textual signature format emitted by the runtime data
///          generator into structured return/parameter type lists for use by the
///          runtime bridge.

#include "il/runtime/RuntimeSignatureParser.hpp"

#include <cctype>

namespace il::runtime
{
namespace
{

/// @brief Determine whether a character is considered whitespace by the parser.
/// @param ch Character to inspect.
/// @return True when @p ch is a space, tab, carriage return, or newline.
constexpr bool isWhitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

/// @brief Map a textual token to an @ref il::core::Type::Kind.
/// @details Accepts the mnemonics emitted by the runtime generator and performs
///          a case-insensitive match for select aliases (e.g. `bool`).
/// @param token Token to translate.
/// @return Parsed kind or @ref il::core::Type::Kind::Error when unknown.
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
    if (token == "f32")
        return Kind::F32;
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

/// @brief Strip leading and trailing ASCII whitespace from a string view.
/// @param text View to trim.
/// @return View referencing the trimmed range of @p text.
std::string_view trim(std::string_view text)
{
    while (!text.empty() && isWhitespace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isWhitespace(text.back()))
        text.remove_suffix(1);
    return text;
}

/// @brief Split a comma-delimited parameter list while respecting parentheses.
/// @details Handles nested parentheses so pointer types are not split apart and
///          removes empty tokens created by redundant commas.
/// @param text Slice containing the comma-separated list between parentheses.
/// @return Sequence of trimmed tokens.
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

/// @brief Parse a runtime signature specification string.
/// @details Extracts the return type token, parses parameter tokens using
///          @ref splitTypeList, and populates a @ref RuntimeSignature.  Invalid
///          layouts yield a default-constructed signature with empty parameter
///          lists.
/// @param spec Textual specification such as "i32(i32,i32)".
/// @return Structured signature describing the runtime call ABI.
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
