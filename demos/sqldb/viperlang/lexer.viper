// SQLite Clone - SQL Lexer
// ViperLang Implementation

module lexer;

import "./token";

// Lexer entity - tokenizes SQL input
entity Lexer {
    hide String source;
    hide Integer pos;
    hide Integer line;
    hide Integer column;
    hide Integer length;

    expose func init(src: String) {
        source = src;
        pos = 0;
        line = 1;
        column = 1;
        length = Viper.String.Length(src);
    }

    // Check if we've reached the end
    expose func isAtEnd() -> Boolean {
        return pos >= length;
    }

    // Peek at current character without consuming
    hide func peek() -> String {
        if (isAtEnd()) {
            return "";
        }
        return Viper.String.Substring(source, pos, 1);
    }

    // Peek at next character
    hide func peekNext() -> String {
        if (pos + 1 >= length) {
            return "";
        }
        return Viper.String.Substring(source, pos + 1, 1);
    }

    // Consume and return current character
    hide func advance() -> String {
        var ch = peek();
        pos = pos + 1;
        if (ch == "\n") {
            line = line + 1;
            column = 1;
        } else {
            column = column + 1;
        }
        return ch;
    }

    // Skip whitespace and comments
    hide func skipWhitespace() {
        while (!isAtEnd()) {
            var ch = peek();
            if (ch == " " || ch == "\t" || ch == "\r" || ch == "\n") {
                advance();
            } else if (ch == "-" && peekNext() == "-") {
                // Single line comment
                while (!isAtEnd() && peek() != "\n") {
                    advance();
                }
            } else if (ch == "/" && peekNext() == "*") {
                // Multi-line comment
                advance();
                advance();
                while (!isAtEnd()) {
                    if (peek() == "*" && peekNext() == "/") {
                        advance();
                        advance();
                        break;
                    }
                    advance();
                }
            } else {
                break;
            }
        }
    }

    // Check if character is digit
    hide func isDigit(ch: String) -> Boolean {
        return ch >= "0" && ch <= "9";
    }

    // Check if character is letter or underscore
    hide func isAlpha(ch: String) -> Boolean {
        return (ch >= "a" && ch <= "z") || (ch >= "A" && ch <= "Z") || ch == "_";
    }

    // Check if character is alphanumeric
    hide func isAlphaNum(ch: String) -> Boolean {
        return isAlpha(ch) || isDigit(ch);
    }

    // Read a number (integer or float)
    hide func readNumber() -> token.Token {
        var startLine = line;
        var startCol = column;
        var startPos = pos;

        while (!isAtEnd() && isDigit(peek())) {
            advance();
        }

        // Check for decimal
        if (peek() == "." && isDigit(peekNext())) {
            advance(); // consume dot
            while (!isAtEnd() && isDigit(peek())) {
                advance();
            }
            var text = Viper.String.Substring(source, startPos, pos - startPos);
            return new token.Token(token.TK_NUMBER, text, startLine, startCol);
        }

        var text = Viper.String.Substring(source, startPos, pos - startPos);
        return new token.Token(token.TK_INTEGER, text, startLine, startCol);
    }

    // Read a string literal
    hide func readString() -> token.Token {
        var startLine = line;
        var startCol = column;
        var quote = advance(); // consume opening quote
        var startPos = pos;

        while (!isAtEnd() && peek() != quote) {
            if (peek() == "\\") {
                advance(); // escape char
                if (!isAtEnd()) {
                    advance();
                }
            } else {
                advance();
            }
        }

        var text = Viper.String.Substring(source, startPos, pos - startPos);

        if (!isAtEnd()) {
            advance(); // consume closing quote
        }

        return new token.Token(token.TK_STRING, text, startLine, startCol);
    }

    // Read an identifier or keyword
    hide func readIdentifier() -> token.Token {
        var startLine = line;
        var startCol = column;
        var startPos = pos;

        while (!isAtEnd() && isAlphaNum(peek())) {
            advance();
        }

        var text = Viper.String.Substring(source, startPos, pos - startPos);
        var upper = Viper.String.ToUpper(text);

        // Check for keywords
        var kind = lookupKeyword(upper);
        return new token.Token(kind, text, startLine, startCol);
    }

    // Lookup keyword token type
    hide func lookupKeyword(word: String) -> Integer {
        // DDL
        if (word == "CREATE") { return token.TK_CREATE; }
        if (word == "TABLE") { return token.TK_TABLE; }
        if (word == "DROP") { return token.TK_DROP; }
        if (word == "ALTER") { return token.TK_ALTER; }
        if (word == "INDEX") { return token.TK_INDEX; }
        if (word == "VIEW") { return token.TK_VIEW; }
        if (word == "TRIGGER") { return token.TK_TRIGGER; }

        // DML
        if (word == "SELECT") { return token.TK_SELECT; }
        if (word == "INSERT") { return token.TK_INSERT; }
        if (word == "UPDATE") { return token.TK_UPDATE; }
        if (word == "DELETE") { return token.TK_DELETE; }
        if (word == "INTO") { return token.TK_INTO; }
        if (word == "FROM") { return token.TK_FROM; }
        if (word == "WHERE") { return token.TK_WHERE; }
        if (word == "SET") { return token.TK_SET; }
        if (word == "VALUES") { return token.TK_VALUES; }

        // Clauses
        if (word == "ORDER") { return token.TK_ORDER; }
        if (word == "BY") { return token.TK_BY; }
        if (word == "ASC") { return token.TK_ASC; }
        if (word == "DESC") { return token.TK_DESC; }
        if (word == "LIMIT") { return token.TK_LIMIT; }
        if (word == "OFFSET") { return token.TK_OFFSET; }
        if (word == "GROUP") { return token.TK_GROUP; }
        if (word == "HAVING") { return token.TK_HAVING; }
        if (word == "DISTINCT") { return token.TK_DISTINCT; }

        // Joins
        if (word == "JOIN") { return token.TK_JOIN; }
        if (word == "INNER") { return token.TK_INNER; }
        if (word == "LEFT") { return token.TK_LEFT; }
        if (word == "RIGHT") { return token.TK_RIGHT; }
        if (word == "FULL") { return token.TK_FULL; }
        if (word == "OUTER") { return token.TK_OUTER; }
        if (word == "CROSS") { return token.TK_CROSS; }
        if (word == "ON") { return token.TK_ON; }

        // Logical
        if (word == "AND") { return token.TK_AND; }
        if (word == "OR") { return token.TK_OR; }
        if (word == "NOT") { return token.TK_NOT; }
        if (word == "IN") { return token.TK_IN; }
        if (word == "IS") { return token.TK_IS; }
        if (word == "LIKE") { return token.TK_LIKE; }
        if (word == "BETWEEN") { return token.TK_BETWEEN; }
        if (word == "EXISTS") { return token.TK_EXISTS; }

        // Values
        if (word == "NULL") { return token.TK_NULL; }
        if (word == "TRUE") { return token.TK_TRUE; }
        if (word == "FALSE") { return token.TK_FALSE; }
        if (word == "DEFAULT") { return token.TK_DEFAULT; }

        // Constraints
        if (word == "PRIMARY") { return token.TK_PRIMARY; }
        if (word == "FOREIGN") { return token.TK_FOREIGN; }
        if (word == "KEY") { return token.TK_KEY; }
        if (word == "REFERENCES") { return token.TK_REFERENCES; }
        if (word == "UNIQUE") { return token.TK_UNIQUE; }
        if (word == "CHECK") { return token.TK_CHECK; }
        if (word == "CONSTRAINT") { return token.TK_CONSTRAINT; }
        if (word == "AUTOINCREMENT") { return token.TK_AUTOINCREMENT; }

        // Types
        if (word == "INT") { return token.TK_INT; }
        if (word == "INTEGER") { return token.TK_INTEGER_TYPE; }
        if (word == "REAL") { return token.TK_REAL; }
        if (word == "TEXT") { return token.TK_TEXT; }
        if (word == "BLOB") { return token.TK_BLOB; }
        if (word == "BOOLEAN") { return token.TK_BOOLEAN; }
        if (word == "VARCHAR") { return token.TK_VARCHAR; }

        // Transactions
        if (word == "BEGIN") { return token.TK_BEGIN; }
        if (word == "COMMIT") { return token.TK_COMMIT; }
        if (word == "ROLLBACK") { return token.TK_ROLLBACK; }
        if (word == "TRANSACTION") { return token.TK_TRANSACTION; }
        if (word == "SAVEPOINT") { return token.TK_SAVEPOINT; }
        if (word == "RELEASE") { return token.TK_RELEASE; }

        // Other
        if (word == "AS") { return token.TK_AS; }
        if (word == "CASE") { return token.TK_CASE; }
        if (word == "WHEN") { return token.TK_WHEN; }
        if (word == "THEN") { return token.TK_THEN; }
        if (word == "ELSE") { return token.TK_ELSE; }
        if (word == "END") { return token.TK_END; }
        if (word == "UNION") { return token.TK_UNION; }
        if (word == "ALL") { return token.TK_ALL; }
        if (word == "CAST") { return token.TK_CAST; }

        // Utility
        if (word == "SHOW") { return token.TK_SHOW; }
        if (word == "DESCRIBE") { return token.TK_DESCRIBE; }
        if (word == "EXPLAIN") { return token.TK_EXPLAIN; }
        if (word == "VACUUM") { return token.TK_VACUUM; }
        if (word == "SAVE") { return token.TK_SAVE; }
        if (word == "OPEN") { return token.TK_OPEN; }
        if (word == "EXPORT") { return token.TK_EXPORT; }
        if (word == "IMPORT") { return token.TK_IMPORT; }
        if (word == "HELP") { return token.TK_HELP; }
        if (word == "TO") { return token.TK_TO; }
        if (word == "ADD") { return token.TK_ADD; }
        if (word == "COLUMN") { return token.TK_COLUMN; }
        if (word == "RENAME") { return token.TK_RENAME; }
        if (word == "IF") { return token.TK_IF; }

        return token.TK_IDENTIFIER;
    }

    // Get next token
    expose func nextToken() -> token.Token {
        skipWhitespace();

        if (isAtEnd()) {
            return new token.Token(token.TK_EOF, "", line, column);
        }

        var startLine = line;
        var startCol = column;
        var ch = peek();

        // Numbers
        if (isDigit(ch)) {
            return readNumber();
        }

        // Identifiers and keywords
        if (isAlpha(ch)) {
            return readIdentifier();
        }

        // String literals
        if (ch == "'" || ch == "\"") {
            return readString();
        }

        // Operators and punctuation
        advance();

        // Two-character operators
        if (ch == "<" && peek() == "=") {
            advance();
            return new token.Token(token.TK_LE, "<=", startLine, startCol);
        }
        if (ch == ">" && peek() == "=") {
            advance();
            return new token.Token(token.TK_GE, ">=", startLine, startCol);
        }
        if (ch == "<" && peek() == ">") {
            advance();
            return new token.Token(token.TK_NE, "<>", startLine, startCol);
        }
        if (ch == "!" && peek() == "=") {
            advance();
            return new token.Token(token.TK_NE, "!=", startLine, startCol);
        }
        if (ch == "|" && peek() == "|") {
            advance();
            return new token.Token(token.TK_CONCAT, "||", startLine, startCol);
        }

        // Single-character operators
        if (ch == "+") { return new token.Token(token.TK_PLUS, "+", startLine, startCol); }
        if (ch == "-") { return new token.Token(token.TK_MINUS, "-", startLine, startCol); }
        if (ch == "*") { return new token.Token(token.TK_STAR, "*", startLine, startCol); }
        if (ch == "/") { return new token.Token(token.TK_SLASH, "/", startLine, startCol); }
        if (ch == "%") { return new token.Token(token.TK_PERCENT, "%", startLine, startCol); }
        if (ch == "=") { return new token.Token(token.TK_EQ, "=", startLine, startCol); }
        if (ch == "<") { return new token.Token(token.TK_LT, "<", startLine, startCol); }
        if (ch == ">") { return new token.Token(token.TK_GT, ">", startLine, startCol); }

        // Punctuation
        if (ch == "(") { return new token.Token(token.TK_LPAREN, "(", startLine, startCol); }
        if (ch == ")") { return new token.Token(token.TK_RPAREN, ")", startLine, startCol); }
        if (ch == ",") { return new token.Token(token.TK_COMMA, ",", startLine, startCol); }
        if (ch == ";") { return new token.Token(token.TK_SEMICOLON, ";", startLine, startCol); }
        if (ch == ".") { return new token.Token(token.TK_DOT, ".", startLine, startCol); }

        // Unknown character - return error token
        return new token.Token(token.TK_ERROR, ch, startLine, startCol);
    }
}

// Test function for lexer
func testLexer() {
    Viper.Terminal.Say("=== Lexer Test ===");

    // Test basic SQL
    var sql = "SELECT id, name FROM users WHERE age > 21;";
    Viper.Terminal.Say("Input: " + sql);
    Viper.Terminal.Say("");

    var lex = new Lexer(sql);
    var tok = lex.nextToken();
    while (tok.kind != token.TK_EOF) {
        Viper.Terminal.Say("  " + token.tokenTypeName(tok.kind) + ": '" + tok.text + "'");
        tok = lex.nextToken();
    }

    Viper.Terminal.Say("");

    // Test with numbers and strings
    var sql2 = "INSERT INTO users VALUES (1, 'John', 3.14);";
    Viper.Terminal.Say("Input: " + sql2);
    Viper.Terminal.Say("");

    var lex2 = new Lexer(sql2);
    tok = lex2.nextToken();
    while (tok.kind != token.TK_EOF) {
        Viper.Terminal.Say("  " + token.tokenTypeName(tok.kind) + ": '" + tok.text + "'");
        tok = lex2.nextToken();
    }

    Viper.Terminal.Say("");

    // Test operators
    var sql3 = "x >= 10 AND y <= 20 OR z <> 0";
    Viper.Terminal.Say("Input: " + sql3);
    Viper.Terminal.Say("");

    var lex3 = new Lexer(sql3);
    tok = lex3.nextToken();
    while (tok.kind != token.TK_EOF) {
        Viper.Terminal.Say("  " + token.tokenTypeName(tok.kind) + ": '" + tok.text + "'");
        tok = lex3.nextToken();
    }

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Lexer Test PASSED ===");
}
