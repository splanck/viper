// File: src/frontends/basic/Lexer.hpp
// Purpose: Declares BASIC token lexer.
// Key invariants: Current position always within source buffer.
// Ownership/Lifetime: Lexer does not own the source buffer.
// Links: docs/class-catalog.md
#pragma once
#include "frontends/basic/Token.hpp"
#include <string_view>

namespace il::frontends::basic
{

class Lexer
{
  public:
    Lexer(std::string_view src, uint32_t file_id);
    Token next();

  private:
    char peek() const;
    char get();
    bool eof() const;
    void skipWhitespaceExceptNewline();
    Token lexNumber();
    Token lexIdentifierOrKeyword();
    Token lexString();

    std::string_view src_;
    size_t pos_ = 0;
    uint32_t file_id_;
    uint32_t line_ = 1;
    uint32_t column_ = 1;
};

} // namespace il::frontends::basic
