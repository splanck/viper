// File: src/il/io/StringEscape.cpp
// Purpose: Implement helpers for encoding and decoding escaped string literals.
// Key invariants: Decoding rejects malformed escapes and reports descriptive
//                 messages; encoding emits canonical escape sequences for
//                 non-printable characters.
// Ownership/Lifetime: Stateless utility functions.
// License: MIT (see LICENSE).
// Links: docs/il-guide.md#reference

#include "il/io/StringEscape.hpp"

#include <cctype>
#include <cstdio>

namespace il::io
{
namespace
{
unsigned hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f')
        return static_cast<unsigned>(10 + (c - 'a'));
    if (c >= 'A' && c <= 'F')
        return static_cast<unsigned>(10 + (c - 'A'));
    return 0u;
}

bool isHex(char c)
{
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

} // namespace

bool decodeEscapedString(std::string_view input, std::string &output, std::string *error)
{
    output.clear();
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        char c = input[i];
        if (c != '\\')
        {
            output.push_back(c);
            continue;
        }
        if (i + 1 >= input.size())
        {
            if (error)
                *error = "unterminated escape sequence";
            return false;
        }
        char next = input[++i];
        switch (next)
        {
            case '\\':
                output.push_back('\\');
                break;
            case '\"':
                output.push_back('\"');
                break;
            case 'n':
                output.push_back('\n');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 't':
                output.push_back('\t');
                break;
            case '0':
                output.push_back('\0');
                break;
            case 'x':
            {
                if (i + 2 >= input.size() || !isHex(input[i + 1]) || !isHex(input[i + 2]))
                {
                    if (error)
                        *error = "invalid hex escape";
                    return false;
                }
                unsigned hi = hexValue(input[i + 1]);
                unsigned lo = hexValue(input[i + 2]);
                i += 2;
                char value = static_cast<char>((hi << 4) | lo);
                output.push_back(value);
                break;
            }
            default:
                if (error)
                {
                    char buf[64];
                    if (std::snprintf(buf, sizeof(buf), "unknown escape sequence \\%c", next) < 0)
                        *error = "unknown escape sequence";
                    else
                        *error = buf;
                }
                return false;
        }
    }
    return true;
}

} // namespace il::io

