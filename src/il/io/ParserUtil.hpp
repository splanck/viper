// File: src/il/io/ParserUtil.hpp
// Purpose: Declares lexical helpers shared by IL parser components.
// Key invariants: None.
// Ownership/Lifetime: Stateless utility routines operate on caller-provided data.
// Links: docs/il-guide.md#reference
#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <string_view>

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

/// @brief Format a diagnostic string including the line number prefix used by the parser.
/// @param lineNo Line number associated with the diagnostic message.
/// @param message Human-readable diagnostic message body.
/// @return Formatted diagnostic string, e.g. "Line 3: malformed foo".
std::string formatLineDiag(unsigned lineNo, std::string_view message);

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

/// @brief Attempt to parse a trap kind identifier token.
/// @param token Candidate identifier, e.g. "DivideByZero".
/// @param value Destination receiving the enumerated integral value on success.
/// @return True when @p token matches a known trap kind identifier.
bool parseTrapKindToken(const std::string &token, long long &value);

/// @brief Map a trap kind integral value back to its identifier spelling.
/// @param value Enumerated integral trap kind value.
/// @return Identifier string when known; std::nullopt otherwise.
std::optional<std::string_view> trapKindTokenFromValue(long long value);

} // namespace il::io
