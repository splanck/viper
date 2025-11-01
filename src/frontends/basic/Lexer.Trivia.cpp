//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lexer.Trivia.cpp
// Purpose: Implement trivia-skipping helpers (whitespace and comments) for the
//          BASIC lexer to keep the main driver loop concise.
// Key invariants: Comment skipping never consumes newline terminators so
//                 statement boundaries remain observable by the caller.
// Ownership/Lifetime: Helpers operate directly on the lexer's borrowed buffer.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lexer.hpp"

#include <cctype>

namespace il::frontends::basic
{

/// @brief Skip spaces, tabs, and carriage returns but stop at newlines.
///
/// @details Whitespace between statements is ignored by BASIC except for
///          newline boundaries that influence statement grouping.  This helper
///          advances the cursor past horizontal whitespace while keeping
///          newlines in the stream for later tokenisation.
void Lexer::skipWhitespaceExceptNewline()
{
    while (!eof())
    {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r')
        {
            get();
        }
        else
        {
            break;
        }
    }
}

/// @brief Skip whitespace and BASIC comments starting with <tt>'</tt> or REM.
///
/// @details BASIC treats apostrophe-prefixed and "REM" tokens as
///          rest-of-line comments.  The helper repeatedly removes whitespace and
///          comment bodies so the next significant token begins at the current
///          cursor.  The newline terminating a comment is preserved so callers
///          can emit @ref TokenKind::EndOfLine.
void Lexer::skipWhitespaceAndComments()
{
    while (true)
    {
        skipWhitespaceExceptNewline();

        if (peek() == '\'')
        {
            while (!eof() && peek() != '\n')
                get();
            continue;
        }

        if (std::toupper(static_cast<unsigned char>(peek())) == 'R' && pos_ + 2 < src_.size() &&
            std::toupper(static_cast<unsigned char>(src_[pos_ + 1])) == 'E' &&
            std::toupper(static_cast<unsigned char>(src_[pos_ + 2])) == 'M')
        {
            char after = (pos_ + 3 < src_.size()) ? src_[pos_ + 3] : '\0';
            if (!std::isalnum(static_cast<unsigned char>(after)) && after != '$' && after != '#' &&
                after != '!' && after != '%' && after != '&')
            {
                get();
                get();
                get();
                while (!eof() && peek() != '\n')
                    get();
                continue;
            }
        }

        break;
    }
}

} // namespace il::frontends::basic
