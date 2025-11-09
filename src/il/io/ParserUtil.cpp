//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements lexical helper functions used by the IL parser.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Token-level utilities shared by the textual IL parser.
/// @details Supplies trimming, tokenisation, literal parsing, and trap-kind
///          mapping helpers that keep the main parser logic concise.

#include "il/internal/io/ParserUtil.hpp"

#include <array>
#include <cctype>
#include <exception>
#include <optional>
#include <sstream>
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

/// @brief Format a diagnostic string that mirrors the "Line N:" prefix style.
/// @param lineNo Input line number associated with the diagnostic.
/// @param message Human-readable message body.
/// @return Combined diagnostic string.
std::string formatLineDiag(unsigned lineNo, std::string_view message)
{
    std::ostringstream oss;
    oss << "Line " << lineNo << ": " << message;
    return oss.str();
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
    // Recognise optional sign
    size_t pos = 0;
    bool negative = false;
    if (pos < token.size() && (token[pos] == '+' || token[pos] == '-'))
    {
        negative = (token[pos] == '-');
        ++pos;
    }

    // Handle 0b/0B binary prefix explicitly for portability.
    if (pos + 2 <= token.size() && token[pos] == '0' && (token[pos + 1] == 'b' || token[pos + 1] == 'B'))
    {
        pos += 2;
        if (pos >= token.size())
            return false;
        long long acc = 0;
        for (; pos < token.size(); ++pos)
        {
            char ch = token[pos];
            if (ch == '_')
                continue; // allow visual separators in the future (ignored)
            if (ch != '0' && ch != '1')
                return false;
            int bit = (ch == '1') ? 1 : 0;
            // Basic overflow-safe shift-add for signed range
            if (acc > (std::numeric_limits<long long>::max() >> 1))
                return false;
            acc = (acc << 1) | bit;
        }
        value = negative ? -acc : acc;
        return true;
    }

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
    // Handle well-known spellings for special values explicitly so behaviour is
    // consistent across libstdc++/libc++ and locales.
    if (!token.empty())
    {
        std::string lower;
        lower.reserve(token.size());
        for (unsigned char ch : token)
        {
            lower.push_back(static_cast<char>(std::tolower(ch)));
        }
        if (lower == "nan")
        {
            value = std::numeric_limits<double>::quiet_NaN();
            return true;
        }
        if (lower == "inf" || lower == "+inf")
        {
            value = std::numeric_limits<double>::infinity();
            return true;
        }
        if (lower == "-inf")
        {
            value = -std::numeric_limits<double>::infinity();
            return true;
        }
    }

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

/// @brief Parse a trap-kind mnemonic into its numeric representation.
/// @param token Trap kind name such as "DivideByZero".
/// @param value Output receiving the numeric trap code on success.
/// @return True when @p token matches a known trap name.
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

/// @brief Map a numeric trap code back to its mnemonic name.
/// @param value Numeric trap kind identifier.
/// @return Name of the trap kind when recognised; otherwise empty optional.
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
