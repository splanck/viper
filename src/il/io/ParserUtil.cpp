// File: src/il/io/ParserUtil.cpp
// Purpose: Implements lexical helpers used by the IL parser.
// Key invariants: None.
// Ownership/Lifetime: Stateless functions operate on caller-provided buffers.
// Links: docs/il-spec.md

#include "il/io/ParserUtil.hpp"

#include <cctype>
#include <exception>

namespace il::io
{

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

std::string readToken(std::istringstream &stream)
{
    std::string token;
    stream >> token;
    if (!token.empty() && token.back() == ',')
        token.pop_back();
    return token;
}

bool parseIntegerLiteral(const std::string &token, long long &value)
{
    try
    {
        size_t idx = 0;
        long long parsed = std::stoll(token, &idx);
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

} // namespace il::io
