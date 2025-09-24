// File: src/il/io/ParserUtil.hpp
// Purpose: Declares lexical helpers shared by IL parser components.
// Key invariants: None.
// Ownership/Lifetime: Stateless utility routines operate on caller-provided data.
// Links: docs/il-guide.md#reference
#pragma once

#include <sstream>
#include <string>

namespace il::io
{

/// @brief Remove leading and trailing whitespace from the supplied text.
/// @param text Input string that may contain surrounding whitespace.
/// @return Substring view with surrounding whitespace stripped.
std::string trim(const std::string &text);

/// @brief Extract the next comma or whitespace delimited token from a stream.
/// @param stream Source stream backed by an instruction tail segment.
/// @return Token without any trailing comma delimiter.
std::string readToken(std::istringstream &stream);

/// @brief Attempt to parse an integer literal token.
/// @param token Textual representation of a signed integer.
/// @param value Destination receiving the parsed value on success.
/// @return True if the entire token was consumed as an integer.
bool parseIntegerLiteral(const std::string &token, long long &value);

/// @brief Attempt to parse a floating-point literal token.
/// @param token Textual representation of a floating value.
/// @param value Destination receiving the parsed value on success.
/// @return True if the entire token was consumed as a floating literal.
bool parseFloatLiteral(const std::string &token, double &value);

} // namespace il::io
