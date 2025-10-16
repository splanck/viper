// File: src/il/runtime/RuntimeSignatureParser.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements helpers for parsing runtime signature specifications.
// Key invariants: Parsing logic must remain consistent with runtime signature metadata.
// Ownership/Lifetime: Stateless utilities operating on caller-provided data.
// Links: docs/il-guide.md#reference

#include "il/runtime/RuntimeSignatureParser.hpp"

#include <cctype>

namespace il::runtime
{
namespace
{

constexpr bool isWhitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

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

std::string_view trim(std::string_view text)
{
    while (!text.empty() && isWhitespace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isWhitespace(text.back()))
        text.remove_suffix(1);
    return text;
}

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
