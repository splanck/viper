// File: src/il/io/Lexer.cpp
// Purpose: Implements lexical helper utilities for IL text parsing.
// Key invariants: Operates on UTF-8/ASCII compatible strings.
// Ownership/Lifetime: Functions allocate new std::string instances as needed.
// Links: docs/il-spec.md

#include "il/io/Lexer.hpp"

#include <cctype>

namespace il::io
{

std::string Lexer::trim(std::string_view text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return std::string{text.substr(begin, end - begin)};
}

std::string Lexer::nextToken(std::istringstream &stream)
{
    std::string token;
    stream >> token;
    if (!token.empty() && token.back() == ',')
        token.pop_back();
    return token;
}

std::vector<std::string> Lexer::splitCommaSeparated(std::string_view text)
{
    std::vector<std::string> tokens;
    std::stringstream ss(std::string{text});
    std::string piece;
    while (std::getline(ss, piece, ','))
    {
        tokens.push_back(trim(piece));
    }
    return tokens;
}

} // namespace il::io
