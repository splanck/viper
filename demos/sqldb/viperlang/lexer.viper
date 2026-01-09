// lexer.viper - SQL Lexer
// Part of SQLite Clone - ViperLang Implementation

module lexer;

import "./token";

//=============================================================================
// LEXER ENTITY
//=============================================================================

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

    expose func isAtEnd() -> Boolean {
        return pos >= length;
    }

    hide func peek() -> String {
        if (isAtEnd()) { return ""; }
        return Viper.String.Substring(source, pos, 1);
    }

    hide func peekNext() -> String {
        if (pos + 1 >= length) { return ""; }
        return Viper.String.Substring(source, pos + 1, 1);
    }

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

    hide func skipWhitespace() {
        while (!isAtEnd()) {
            var ch = peek();
            if (ch == " " || ch == "\t" || ch == "\r" || ch == "\n") {
                advance();
            } else if (ch == "-" && peekNext() == "-") {
                while (!isAtEnd() && peek() != "\n") { advance(); }
            } else if (ch == "/" && peekNext() == "*") {
                advance(); advance();
                while (!isAtEnd()) {
                    if (peek() == "*" && peekNext() == "/") {
                        advance(); advance(); break;
                    }
                    advance();
                }
            } else {
                break;
            }
        }
    }

    hide func isDigit(ch: String) -> Boolean {
        var code = Viper.String.Asc(ch);
        return code >= 48 && code <= 57;  // '0' = 48, '9' = 57
    }

    hide func isAlpha(ch: String) -> Boolean {
        var code = Viper.String.Asc(ch);
        if (code >= 65 && code <= 90) { return true; }   // 'A' = 65, 'Z' = 90
        if (code >= 97 && code <= 122) { return true; }  // 'a' = 97, 'z' = 122
        return ch == "_";
    }

    hide func isAlphaNum(ch: String) -> Boolean {
        return isAlpha(ch) || isDigit(ch);
    }

    hide func readNumber() -> Token {
        var startLine = line;
        var startCol = column;
        var startPos = pos;

        while (!isAtEnd() && isDigit(peek())) { advance(); }

        if (peek() == "." && isDigit(peekNext())) {
            advance();
            while (!isAtEnd() && isDigit(peek())) { advance(); }
            var text = Viper.String.Substring(source, startPos, pos - startPos);
            var tok = new Token(TK_NUMBER, text, startLine, startCol);
            return tok;
        }

        var text = Viper.String.Substring(source, startPos, pos - startPos);
        var tok = new Token(TK_INTEGER, text, startLine, startCol);
        return tok;
    }

    hide func readString() -> Token {
        var startLine = line;
        var startCol = column;
        var quote = advance();
        var startPos = pos;

        while (!isAtEnd() && peek() != quote) {
            if (peek() == "\\") {
                advance();
                if (!isAtEnd()) { advance(); }
            } else {
                advance();
            }
        }

        var text = Viper.String.Substring(source, startPos, pos - startPos);
        if (!isAtEnd()) { advance(); }

        var tok = new Token(TK_STRING, text, startLine, startCol);
        return tok;
    }

    hide func readIdentifier() -> Token {
        var startLine = line;
        var startCol = column;
        var startPos = pos;

        while (!isAtEnd() && isAlphaNum(peek())) { advance(); }

        var len = pos - startPos;
        var text = Viper.String.Substring(source, startPos, len);
        var upper = Viper.String.ToUpper(text);
        var kind = lookupKeyword(upper);

        var tok = new Token(kind, text, startLine, startCol);
        return tok;
    }

    hide func lookupKeyword(word: String) -> Integer {
        if (word == "CREATE") { return TK_CREATE; }
        if (word == "TABLE") { return TK_TABLE; }
        if (word == "DROP") { return TK_DROP; }
        if (word == "ALTER") { return TK_ALTER; }
        if (word == "INDEX") { return TK_INDEX; }
        if (word == "SELECT") { return TK_SELECT; }
        if (word == "INSERT") { return TK_INSERT; }
        if (word == "UPDATE") { return TK_UPDATE; }
        if (word == "DELETE") { return TK_DELETE; }
        if (word == "INTO") { return TK_INTO; }
        if (word == "FROM") { return TK_FROM; }
        if (word == "WHERE") { return TK_WHERE; }
        if (word == "SET") { return TK_SET; }
        if (word == "VALUES") { return TK_VALUES; }
        if (word == "ORDER") { return TK_ORDER; }
        if (word == "BY") { return TK_BY; }
        if (word == "ASC") { return TK_ASC; }
        if (word == "DESC") { return TK_DESC; }
        if (word == "LIMIT") { return TK_LIMIT; }
        if (word == "OFFSET") { return TK_OFFSET; }
        if (word == "GROUP") { return TK_GROUP; }
        if (word == "HAVING") { return TK_HAVING; }
        if (word == "DISTINCT") { return TK_DISTINCT; }
        if (word == "JOIN") { return TK_JOIN; }
        if (word == "INNER") { return TK_INNER; }
        if (word == "LEFT") { return TK_LEFT; }
        if (word == "RIGHT") { return TK_RIGHT; }
        if (word == "FULL") { return TK_FULL; }
        if (word == "OUTER") { return TK_OUTER; }
        if (word == "CROSS") { return TK_CROSS; }
        if (word == "ON") { return TK_ON; }
        if (word == "AND") { return TK_AND; }
        if (word == "OR") { return TK_OR; }
        if (word == "NOT") { return TK_NOT; }
        if (word == "IN") { return TK_IN; }
        if (word == "IS") { return TK_IS; }
        if (word == "LIKE") { return TK_LIKE; }
        if (word == "BETWEEN") { return TK_BETWEEN; }
        if (word == "EXISTS") { return TK_EXISTS; }
        if (word == "NULL") { return TK_NULL; }
        if (word == "TRUE") { return TK_TRUE; }
        if (word == "FALSE") { return TK_FALSE; }
        if (word == "DEFAULT") { return TK_DEFAULT; }
        if (word == "PRIMARY") { return TK_PRIMARY; }
        if (word == "FOREIGN") { return TK_FOREIGN; }
        if (word == "KEY") { return TK_KEY; }
        if (word == "REFERENCES") { return TK_REFERENCES; }
        if (word == "UNIQUE") { return TK_UNIQUE; }
        if (word == "CHECK") { return TK_CHECK; }
        if (word == "CONSTRAINT") { return TK_CONSTRAINT; }
        if (word == "AUTOINCREMENT") { return TK_AUTOINCREMENT; }
        if (word == "INT") { return TK_INT; }
        if (word == "INTEGER") { return TK_INTEGER_TYPE; }
        if (word == "REAL") { return TK_REAL; }
        if (word == "TEXT") { return TK_TEXT; }
        if (word == "BLOB") { return TK_BLOB; }
        if (word == "BOOLEAN") { return TK_BOOLEAN; }
        if (word == "VARCHAR") { return TK_VARCHAR; }
        if (word == "BEGIN") { return TK_BEGIN; }
        if (word == "COMMIT") { return TK_COMMIT; }
        if (word == "ROLLBACK") { return TK_ROLLBACK; }
        if (word == "TRANSACTION") { return TK_TRANSACTION; }
        if (word == "SAVEPOINT") { return TK_SAVEPOINT; }
        if (word == "RELEASE") { return TK_RELEASE; }
        if (word == "AS") { return TK_AS; }
        if (word == "CASE") { return TK_CASE; }
        if (word == "WHEN") { return TK_WHEN; }
        if (word == "THEN") { return TK_THEN; }
        if (word == "ELSE") { return TK_ELSE; }
        if (word == "END") { return TK_END; }
        if (word == "UNION") { return TK_UNION; }
        if (word == "ALL") { return TK_ALL; }
        if (word == "CAST") { return TK_CAST; }
        if (word == "SHOW") { return TK_SHOW; }
        if (word == "DESCRIBE") { return TK_DESCRIBE; }
        if (word == "EXPLAIN") { return TK_EXPLAIN; }
        if (word == "VACUUM") { return TK_VACUUM; }
        if (word == "SAVE") { return TK_SAVE; }
        if (word == "OPEN") { return TK_OPEN; }
        if (word == "EXPORT") { return TK_EXPORT; }
        if (word == "IMPORT") { return TK_IMPORT; }
        if (word == "HELP") { return TK_HELP; }
        if (word == "TO") { return TK_TO; }
        if (word == "ADD") { return TK_ADD; }
        if (word == "COLUMN") { return TK_COLUMN; }
        if (word == "RENAME") { return TK_RENAME; }
        if (word == "IF") { return TK_IF; }
        return TK_IDENTIFIER;
    }

    expose func nextToken() -> Token {
        skipWhitespace();

        if (isAtEnd()) {
            var tok = new Token(TK_EOF, "", line, column);
            return tok;
        }

        var startLine = line;
        var startCol = column;
        var ch = peek();

        if (isDigit(ch)) { return readNumber(); }
        if (isAlpha(ch)) { return readIdentifier(); }
        if (ch == "'" || ch == "\"") { return readString(); }

        advance();

        // Two-character operators
        if (ch == "<" && peek() == "=") { advance(); return new Token(TK_LE, "<=", startLine, startCol); }
        if (ch == ">" && peek() == "=") { advance(); return new Token(TK_GE, ">=", startLine, startCol); }
        if (ch == "<" && peek() == ">") { advance(); return new Token(TK_NE, "<>", startLine, startCol); }
        if (ch == "!" && peek() == "=") { advance(); return new Token(TK_NE, "!=", startLine, startCol); }
        if (ch == "|" && peek() == "|") { advance(); return new Token(TK_CONCAT, "||", startLine, startCol); }

        // Single-character operators
        if (ch == "+") { return new Token(TK_PLUS, "+", startLine, startCol); }
        if (ch == "-") { return new Token(TK_MINUS, "-", startLine, startCol); }
        if (ch == "*") { return new Token(TK_STAR, "*", startLine, startCol); }
        if (ch == "/") { return new Token(TK_SLASH, "/", startLine, startCol); }
        if (ch == "%") { return new Token(TK_PERCENT, "%", startLine, startCol); }
        if (ch == "=") { return new Token(TK_EQ, "=", startLine, startCol); }
        if (ch == "<") { return new Token(TK_LT, "<", startLine, startCol); }
        if (ch == ">") { return new Token(TK_GT, ">", startLine, startCol); }

        // Punctuation
        if (ch == "(") { return new Token(TK_LPAREN, "(", startLine, startCol); }
        if (ch == ")") { return new Token(TK_RPAREN, ")", startLine, startCol); }
        if (ch == ",") { return new Token(TK_COMMA, ",", startLine, startCol); }
        if (ch == ";") { return new Token(TK_SEMICOLON, ";", startLine, startCol); }
        if (ch == ".") { return new Token(TK_DOT, ".", startLine, startCol); }

        return new Token(TK_ERROR, ch, startLine, startCol);
    }
}
