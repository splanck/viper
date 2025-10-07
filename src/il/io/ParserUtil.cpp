// File: src/il/io/ParserUtil.cpp
// Purpose: Implements lexical helpers used by the IL parser.
// Key invariants: None.
// Ownership/Lifetime: Stateless functions operate on caller-provided buffers.
// License: MIT (see LICENSE).
// Links: docs/il-guide.md#reference

#include "il/io/ParserUtil.hpp"

#include <array>
#include <cctype>
#include <exception>
#include <optional>
#include <string_view>

namespace
{
struct TrapKindSymbol
{
    const char *name;
    long long value;
};

constexpr std::array<TrapKindSymbol, 10> kTrapKindSymbols = {{
    {"DivideByZero", 0},
    {"Overflow", 1},
    {"InvalidCast", 2},
    {"DomainError", 3},
    {"Bounds", 4},
    {"FileNotFound", 5},
    {"EOF", 6},
    {"IOError", 7},
    {"InvalidOperation", 8},
    {"RuntimeError", 9},
}};
} // namespace

namespace il::io
{

/// @brief Strip leading and trailing ASCII whitespace from the supplied text.
///
/// Uses std::isspace to scan from both ends of the buffer and returns a
/// substring containing the original content without surrounding whitespace.
/// Interior characters are preserved verbatim, so callers can safely trim
/// instruction tokens or directive fields before further parsing.
///
/// @param text Input text that may contain leading or trailing whitespace.
/// @return Copy of @p text without surrounding whitespace characters.
std::string trim(const std::string &text)
{
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(begin, end - begin);
}

/// @brief Read the next token from a comma-delimited instruction tail segment.
///
/// Extraction relies on operator>> to skip leading whitespace and gather
/// characters until the next whitespace boundary. A trailing comma—common in
/// operand lists—is removed so the returned token can be matched without
/// additional sanitisation.
///
/// @param stream Backing stream positioned at the next token to extract.
/// @return Token read from @p stream with any trailing comma stripped.
std::string readToken(std::istringstream &stream)
{
    std::string token;
    stream >> token;
    if (!token.empty() && token.back() == ',')
        token.pop_back();
    return token;
}

/// @brief Parse a token as a signed integer literal.
///
/// Internally forwards to std::stoll and verifies that the entire token is
/// consumed by the conversion. On success @p value receives the parsed result;
/// otherwise the function returns false and leaves @p value unmodified.
///
/// @param token Candidate integer literal, e.g. "-42".
/// @param value Output location for the parsed integer when parsing succeeds.
/// @return True when @p token represents a valid signed integer literal.
bool parseIntegerLiteral(const std::string &token, long long &value)
{
    try
    {
        size_t idx = 0;
        long long parsed = std::stoll(token, &idx, 0);
        if (idx != token.size())
            return false;
        value = parsed;
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

/// @brief Parse a token as a floating-point literal.
///
/// Uses std::stod to recognise decimal or scientific-notation values and checks
/// that the conversion consumed the full token. Successful parses store the
/// resulting double in @p value; failures report false without mutating it.
///
/// @param token Candidate floating literal, e.g. "3.14" or "1e-3".
/// @param value Output location for the parsed floating-point value.
/// @return True when @p token is a valid floating-point literal.
bool parseFloatLiteral(const std::string &token, double &value)
{
    try
    {
        size_t idx = 0;
        double parsed = std::stod(token, &idx);
        if (idx != token.size())
            return false;
        value = parsed;
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

bool parseTrapKindToken(const std::string &token, long long &value)
{
    for (const auto &entry : kTrapKindSymbols)
    {
        if (token == entry.name)
        {
            value = entry.value;
            return true;
        }
    }
    return false;
}

std::optional<std::string_view> trapKindTokenFromValue(long long value)
{
    for (const auto &entry : kTrapKindSymbols)
    {
        if (entry.value == value)
            return entry.name;
    }
    return std::nullopt;
}

} // namespace il::io
