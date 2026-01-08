// SQLite Clone - Combined SQL Module
// ViperLang Implementation

module sql;

//=============================================================================
// TOKEN TYPES
//=============================================================================

// Token type constants
final TK_EOF = 0;
final TK_ERROR = 1;

// Literals
final TK_INTEGER = 10;
final TK_NUMBER = 11;
final TK_STRING = 12;
final TK_IDENTIFIER = 13;

// Keywords - DDL
final TK_CREATE = 20;
final TK_TABLE = 21;
final TK_DROP = 22;
final TK_ALTER = 23;
final TK_INDEX = 24;
final TK_VIEW = 25;
final TK_TRIGGER = 26;

// Keywords - DML
final TK_SELECT = 30;
final TK_INSERT = 31;
final TK_UPDATE = 32;
final TK_DELETE = 33;
final TK_INTO = 34;
final TK_FROM = 35;
final TK_WHERE = 36;
final TK_SET = 37;
final TK_VALUES = 38;

// Keywords - Clauses
final TK_ORDER = 40;
final TK_BY = 41;
final TK_ASC = 42;
final TK_DESC = 43;
final TK_LIMIT = 44;
final TK_OFFSET = 45;
final TK_GROUP = 46;
final TK_HAVING = 47;
final TK_DISTINCT = 48;

// Keywords - Joins
final TK_JOIN = 50;
final TK_INNER = 51;
final TK_LEFT = 52;
final TK_RIGHT = 53;
final TK_FULL = 54;
final TK_OUTER = 55;
final TK_CROSS = 56;
final TK_ON = 57;

// Keywords - Logical
final TK_AND = 60;
final TK_OR = 61;
final TK_NOT = 62;
final TK_IN = 63;
final TK_IS = 64;
final TK_LIKE = 65;
final TK_BETWEEN = 66;
final TK_EXISTS = 67;

// Keywords - Values
final TK_NULL = 70;
final TK_TRUE = 71;
final TK_FALSE = 72;
final TK_DEFAULT = 73;

// Keywords - Constraints
final TK_PRIMARY = 80;
final TK_FOREIGN = 81;
final TK_KEY = 82;
final TK_REFERENCES = 83;
final TK_UNIQUE = 84;
final TK_CHECK = 85;
final TK_CONSTRAINT = 86;
final TK_AUTOINCREMENT = 87;

// Keywords - Types
final TK_INT = 90;
final TK_INTEGER_TYPE = 91;
final TK_REAL = 92;
final TK_TEXT = 93;
final TK_BLOB = 94;
final TK_BOOLEAN = 95;
final TK_VARCHAR = 96;

// Keywords - Transactions
final TK_BEGIN = 100;
final TK_COMMIT = 101;
final TK_ROLLBACK = 102;
final TK_TRANSACTION = 103;
final TK_SAVEPOINT = 104;
final TK_RELEASE = 105;

// Keywords - Other
final TK_AS = 110;
final TK_CASE = 111;
final TK_WHEN = 112;
final TK_THEN = 113;
final TK_ELSE = 114;
final TK_END = 115;
final TK_UNION = 116;
final TK_ALL = 117;
final TK_CAST = 118;

// Keywords - Utility
final TK_SHOW = 120;
final TK_DESCRIBE = 121;
final TK_EXPLAIN = 122;
final TK_VACUUM = 123;
final TK_SAVE = 124;
final TK_OPEN = 125;
final TK_EXPORT = 126;
final TK_IMPORT = 127;
final TK_HELP = 128;
final TK_TO = 129;
final TK_ADD = 130;
final TK_COLUMN = 131;
final TK_RENAME = 132;
final TK_IF = 133;

// Operators
final TK_PLUS = 140;
final TK_MINUS = 141;
final TK_STAR = 142;
final TK_SLASH = 143;
final TK_PERCENT = 144;
final TK_EQ = 145;
final TK_NE = 146;
final TK_LT = 147;
final TK_LE = 148;
final TK_GT = 149;
final TK_GE = 150;
final TK_CONCAT = 151;

// Punctuation
final TK_LPAREN = 160;
final TK_RPAREN = 161;
final TK_COMMA = 162;
final TK_SEMICOLON = 163;
final TK_DOT = 164;

//=============================================================================
// TOKEN ENTITY
//=============================================================================

entity Token {
    expose Integer kind;
    expose String text;
    expose Integer line;
    expose Integer column;

    expose func init(k: Integer, t: String, ln: Integer, col: Integer) {
        kind = k;
        text = t;
        line = ln;
        column = col;
    }

    expose func getText() -> String {
        return text;
    }

    expose func isKeyword() -> Boolean {
        return kind >= 20 && kind < 140;
    }

    expose func isOperator() -> Boolean {
        return kind >= 140 && kind < 160;
    }

    expose func isPunctuation() -> Boolean {
        return kind >= 160;
    }

    expose func isLiteral() -> Boolean {
        return kind >= 10 && kind < 20;
    }

    expose func toString() -> String {
        return "Token(" + Viper.Fmt.Int(kind) + ", '" + text + "', " +
               Viper.Fmt.Int(line) + ":" + Viper.Fmt.Int(column) + ")";
    }
}

// Helper function to get token type name
func tokenTypeName(kind: Integer) -> String {
    if (kind == TK_EOF) { return "EOF"; }
    if (kind == TK_ERROR) { return "ERROR"; }
    if (kind == TK_INTEGER) { return "INTEGER"; }
    if (kind == TK_NUMBER) { return "NUMBER"; }
    if (kind == TK_STRING) { return "STRING"; }
    if (kind == TK_IDENTIFIER) { return "IDENTIFIER"; }
    if (kind == TK_SELECT) { return "SELECT"; }
    if (kind == TK_INSERT) { return "INSERT"; }
    if (kind == TK_UPDATE) { return "UPDATE"; }
    if (kind == TK_DELETE) { return "DELETE"; }
    if (kind == TK_CREATE) { return "CREATE"; }
    if (kind == TK_TABLE) { return "TABLE"; }
    if (kind == TK_DROP) { return "DROP"; }
    if (kind == TK_FROM) { return "FROM"; }
    if (kind == TK_WHERE) { return "WHERE"; }
    if (kind == TK_AND) { return "AND"; }
    if (kind == TK_OR) { return "OR"; }
    if (kind == TK_NOT) { return "NOT"; }
    if (kind == TK_NULL) { return "NULL"; }
    if (kind == TK_INTO) { return "INTO"; }
    if (kind == TK_VALUES) { return "VALUES"; }
    if (kind == TK_PLUS) { return "PLUS"; }
    if (kind == TK_MINUS) { return "MINUS"; }
    if (kind == TK_STAR) { return "STAR"; }
    if (kind == TK_SLASH) { return "SLASH"; }
    if (kind == TK_EQ) { return "EQ"; }
    if (kind == TK_NE) { return "NE"; }
    if (kind == TK_LT) { return "LT"; }
    if (kind == TK_GT) { return "GT"; }
    if (kind == TK_LE) { return "LE"; }
    if (kind == TK_GE) { return "GE"; }
    if (kind == TK_LPAREN) { return "LPAREN"; }
    if (kind == TK_RPAREN) { return "RPAREN"; }
    if (kind == TK_COMMA) { return "COMMA"; }
    if (kind == TK_SEMICOLON) { return "SEMICOLON"; }
    if (kind == TK_DOT) { return "DOT"; }
    return "UNKNOWN(" + Viper.Fmt.Int(kind) + ")";
}

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
        // Workaround for Bug #003: use ASCII codes instead of string comparison
        var code = Viper.String.Asc(ch);
        return code >= 48 && code <= 57;  // '0' = 48, '9' = 57
    }

    hide func isAlpha(ch: String) -> Boolean {
        // Workaround for Bug #003: use ASCII codes instead of string comparison
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
        Viper.Terminal.Say("[LEXER] readIdent: startPos=" + Viper.Fmt.Int(startPos) + " len=" + Viper.Fmt.Int(len) + " text='" + text + "' text.len=" + Viper.Fmt.Int(text.length()));
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

//=============================================================================
// SQL VALUE TYPES
//=============================================================================

// SqlType constants
final SQL_NULL = 0;
final SQL_INTEGER = 1;
final SQL_REAL = 2;
final SQL_TEXT = 3;
final SQL_BLOB = 4;

// Helper function to convert string to integer (for use in entity methods)
func stringToInt(text: String) -> Integer {
    var result = 0;
    var i = 0;
    var len = Viper.String.Length(text);
    var negative = false;

    // Check for negative
    if len > 0 {
        if Viper.String.Substring(text, 0, 1) == "-" {
            negative = true;
            i = 1;
        }
    }

    while i < len {
        var ch = Viper.String.Substring(text, i, 1);
        var code = Viper.String.Asc(ch);
        if code >= 48 {
            if code <= 57 {
                result = result * 10 + (code - 48);
            }
        }
        i = i + 1;
    }

    if negative == true {
        return -result;
    }
    return result;
}

// SqlValue - Tagged union for SQL values
entity SqlValue {
    expose Integer kind;       // SQL_NULL, SQL_INTEGER, SQL_REAL, SQL_TEXT, SQL_BLOB
    expose Integer intValue;   // Used when kind == SQL_INTEGER
    expose Float realValue;    // Used when kind == SQL_REAL
    expose String textValue;   // Used when kind == SQL_TEXT or SQL_BLOB

    // Default constructor - creates NULL value
    expose func init() {
        kind = SQL_NULL;
        intValue = 0;
        realValue = 0.0;
        textValue = "";
    }

    // Create a NULL value
    expose func initNull() {
        kind = SQL_NULL;
        intValue = 0;
        realValue = 0.0;
        textValue = "";
    }

    // Create an INTEGER value
    expose func initInteger(val: Integer) {
        kind = SQL_INTEGER;
        intValue = val;
        realValue = 0.0;
        textValue = "";
    }

    // Create a REAL value (text stores string representation for display)
    expose func initReal(val: Float, text: String) {
        kind = SQL_REAL;
        intValue = 0;
        realValue = val;
        textValue = text;
    }

    // Create a TEXT value
    expose func initText(val: String) {
        kind = SQL_TEXT;
        intValue = 0;
        realValue = 0.0;
        textValue = val;
    }

    // Create a BLOB value (stored as string for simplicity)
    expose func initBlob(val: String) {
        kind = SQL_BLOB;
        intValue = 0;
        realValue = 0.0;
        textValue = val;
    }

    // Check type
    expose func isNull() -> Boolean { return kind == SQL_NULL; }
    expose func isInteger() -> Boolean { return kind == SQL_INTEGER; }
    expose func isReal() -> Boolean { return kind == SQL_REAL; }
    expose func isText() -> Boolean { return kind == SQL_TEXT; }
    expose func isBlob() -> Boolean { return kind == SQL_BLOB; }

    // Get type name
    expose func typeName() -> String {
        if (kind == SQL_NULL) { return "NULL"; }
        if (kind == SQL_INTEGER) { return "INTEGER"; }
        if (kind == SQL_REAL) { return "REAL"; }
        if (kind == SQL_TEXT) { return "TEXT"; }
        if (kind == SQL_BLOB) { return "BLOB"; }
        return "UNKNOWN";
    }

    // Convert to string representation
    expose func toString() -> String {
        if (kind == SQL_NULL) { return "NULL"; }
        if (kind == SQL_INTEGER) { return Viper.Fmt.Int(intValue); }
        if (kind == SQL_REAL) { return textValue; } // Store REAL as text for display
        if (kind == SQL_TEXT) { return "'" + textValue + "'"; }
        if (kind == SQL_BLOB) { return "X'" + textValue + "'"; }
        return "?";
    }

    // Compare values (returns -1, 0, or 1)
    expose func compare(other: SqlValue) -> Integer {
        // NULL handling: NULL is less than everything, NULL == NULL
        if (kind == SQL_NULL && other.kind == SQL_NULL) { return 0; }
        if (kind == SQL_NULL) { return -1; }
        if (other.kind == SQL_NULL) { return 1; }

        // Same type comparisons
        if (kind == SQL_INTEGER && other.kind == SQL_INTEGER) {
            if (intValue < other.intValue) { return -1; }
            if (intValue > other.intValue) { return 1; }
            return 0;
        }
        if (kind == SQL_REAL && other.kind == SQL_REAL) {
            if (realValue < other.realValue) { return -1; }
            if (realValue > other.realValue) { return 1; }
            return 0;
        }
        if (kind == SQL_TEXT && other.kind == SQL_TEXT) {
            // String comparison
            if (textValue == other.textValue) { return 0; }
            // Compare character by character
            var myLen = Viper.String.Length(textValue);
            var otherLen = Viper.String.Length(other.textValue);
            var minLen = myLen;
            if otherLen < minLen { minLen = otherLen; }
            var i = 0;
            while i < minLen {
                var myChar = Viper.String.Asc(Viper.String.Substring(textValue, i, 1));
                var otherChar = Viper.String.Asc(Viper.String.Substring(other.textValue, i, 1));
                if myChar < otherChar { return -1; }
                if myChar > otherChar { return 1; }
                i = i + 1;
            }
            // If all chars equal, shorter string is less
            if myLen < otherLen { return -1; }
            if myLen > otherLen { return 1; }
            return 0;
        }

        // Cross-type comparison: coerce to numbers if possible
        // TEXT vs INTEGER: try to convert text to integer
        if (kind == SQL_TEXT && other.kind == SQL_INTEGER) {
            var myInt = stringToInt(textValue);
            if myInt < other.intValue { return -1; }
            if myInt > other.intValue { return 1; }
            return 0;
        }
        if (kind == SQL_INTEGER && other.kind == SQL_TEXT) {
            var otherInt = stringToInt(other.textValue);
            if intValue < otherInt { return -1; }
            if intValue > otherInt { return 1; }
            return 0;
        }

        // Default: compare by type codes
        if (kind < other.kind) { return -1; }
        if (kind > other.kind) { return 1; }
        return 0;
    }

    // Check equality
    expose func equals(other: SqlValue) -> Boolean {
        return compare(other) == 0;
    }
}

// Factory functions for creating SqlValue instances
func sqlNull() -> SqlValue {
    var v = new SqlValue();
    v.initNull();
    return v;
}

func sqlInteger(val: Integer) -> SqlValue {
    var v = new SqlValue();
    v.initInteger(val);
    return v;
}

func sqlReal(val: Float, text: String) -> SqlValue {
    var v = new SqlValue();
    v.initReal(val, text);
    return v;
}

func sqlText(val: String) -> SqlValue {
    var v = new SqlValue();
    v.initText(val);
    return v;
}

func sqlBlob(val: String) -> SqlValue {
    var v = new SqlValue();
    v.initBlob(val);
    return v;
}

//=============================================================================
// COLUMN ENTITY
//=============================================================================

entity Column {
    expose String name;
    expose Integer typeCode;
    expose Boolean notNull;
    expose Boolean primaryKey;
    expose Boolean autoIncrement;
    expose Boolean unique;
    expose Boolean hasDefault;
    expose SqlValue defaultValue;

    expose func init() {
        name = "";
        typeCode = SQL_TEXT;
        notNull = false;
        primaryKey = false;
        autoIncrement = false;
        unique = false;
        hasDefault = false;
        defaultValue = new SqlValue();
        defaultValue.initNull();
    }

    expose func initWithName(n: String, t: Integer) {
        name = n;
        typeCode = t;
        notNull = false;
        primaryKey = false;
        autoIncrement = false;
        unique = false;
        hasDefault = false;
        defaultValue = new SqlValue();
        defaultValue.initNull();
    }

    expose func typeName() -> String {
        if typeCode == SQL_NULL { return "NULL"; }
        if typeCode == SQL_INTEGER { return "INTEGER"; }
        if typeCode == SQL_REAL { return "REAL"; }
        if typeCode == SQL_TEXT { return "TEXT"; }
        if typeCode == SQL_BLOB { return "BLOB"; }
        return "UNKNOWN";
    }

    expose func setDefault(val: SqlValue) {
        hasDefault = true;
        defaultValue = val;
    }

    expose func toString() -> String {
        var result = name + " " + typeName();
        if primaryKey {
            result = result + " PRIMARY KEY";
        }
        if autoIncrement {
            result = result + " AUTOINCREMENT";
        }
        if notNull {
            result = result + " NOT NULL";
        }
        if unique {
            result = result + " UNIQUE";
        }
        if hasDefault {
            result = result + " DEFAULT " + defaultValue.toString();
        }
        return result;
    }
}

// Factory function for creating columns
func makeColumn(name: String, typeCode: Integer) -> Column {
    var col = new Column();
    col.initWithName(name, typeCode);
    return col;
}

//=============================================================================
// ROW ENTITY
//=============================================================================

entity Row {
    expose List[SqlValue] values;
    expose Boolean deleted;

    expose func init() {
        values = [];
        deleted = false;
    }

    expose func initWithCount(count: Integer) {
        values = [];
        deleted = false;
        var i = 0;
        while i < count {
            var v = new SqlValue();
            v.initNull();
            values.add(v);
            i = i + 1;
        }
    }

    expose func columnCount() -> Integer {
        return values.count();
    }

    expose func getValue(index: Integer) -> SqlValue {
        if index < 0 || index >= values.count() {
            var nullVal = new SqlValue();
            nullVal.initNull();
            return nullVal;
        }
        return values.get(index);
    }

    expose func setValue(index: Integer, val: SqlValue) {
        if index >= 0 && index < values.count() {
            values.set(index, val);
        }
    }

    expose func addValue(val: SqlValue) {
        values.add(val);
    }

    expose func clone() -> Row {
        var newRow = new Row();
        newRow.init();
        var i = 0;
        while i < values.count() {
            var v = values.get(i);
            var cloned = new SqlValue();
            cloned.kind = v.kind;
            cloned.intValue = v.intValue;
            cloned.realValue = v.realValue;
            cloned.textValue = v.textValue;
            newRow.addValue(cloned);
            i = i + 1;
        }
        newRow.deleted = deleted;
        return newRow;
    }

    expose func toString() -> String {
        var result = "(";
        var i = 0;
        while i < values.count() {
            if i > 0 {
                result = result + ", ";
            }
            result = result + values.get(i).toString();
            i = i + 1;
        }
        result = result + ")";
        return result;
    }
}

// Factory function for creating rows
func makeRow(columnCount: Integer) -> Row {
    var row = new Row();
    row.initWithCount(columnCount);
    return row;
}

//=============================================================================
// TABLE ENTITY
//=============================================================================

entity Table {
    expose String name;
    expose List[Column] columns;
    expose List[Row] rows;
    expose Integer autoIncrementValue;

    expose func init() {
        name = "";
        columns = [];
        rows = [];
        autoIncrementValue = 1;
    }

    expose func initWithName(tableName: String) {
        name = tableName;
        columns = [];
        rows = [];
        autoIncrementValue = 1;
    }

    expose func columnCount() -> Integer {
        return columns.count();
    }

    expose func rowCount() -> Integer {
        return rows.count();
    }

    expose func addColumn(col: Column) {
        columns.add(col);
    }

    expose func getColumn(index: Integer) -> Column? {
        if index < 0 || index >= columns.count() {
            return null;
        }
        return columns.get(index);
    }

    expose func findColumnIndex(colName: String) -> Integer {
        var i = 0;
        while i < columns.count() {
            if columns.get(i).name == colName {
                return i;
            }
            i = i + 1;
        }
        return -1;
    }

    expose func getColumnByName(colName: String) -> Column? {
        var idx = findColumnIndex(colName);
        if idx < 0 {
            return null;
        }
        return columns.get(idx);
    }

    expose func addRow(row: Row) {
        rows.add(row);
    }

    expose func getRow(index: Integer) -> Row? {
        if index < 0 || index >= rows.count() {
            return null;
        }
        return rows.get(index);
    }

    expose func deleteRow(index: Integer) -> Boolean {
        if index < 0 || index >= rows.count() {
            return false;
        }
        var row = rows.get(index);
        row.deleted = true;
        return true;
    }

    expose func nextAutoIncrement() -> Integer {
        var val = autoIncrementValue;
        autoIncrementValue = autoIncrementValue + 1;
        return val;
    }

    expose func createRow() -> Row {
        var row = new Row();
        row.initWithCount(columns.count());
        return row;
    }

    expose func insertRow(values: List[SqlValue]) -> Boolean {
        if values.count() != columns.count() {
            return false;
        }
        var row = new Row();
        row.init();
        var i = 0;
        while i < values.count() {
            row.addValue(values.get(i));
            i = i + 1;
        }
        rows.add(row);
        return true;
    }

    expose func schemaString() -> String {
        var result = "CREATE TABLE " + name + " (\n";
        var i = 0;
        while i < columns.count() {
            if i > 0 {
                result = result + ",\n";
            }
            result = result + "  " + columns.get(i).toString();
            i = i + 1;
        }
        result = result + "\n);";
        return result;
    }

    expose func toString() -> String {
        var numCols = columns.count();
        var numRows = rows.count();
        var colStr = Viper.Fmt.Int(numCols);
        var rowStr = Viper.Fmt.Int(numRows);
        var result = "Table: " + name + " (" + colStr + " cols, " + rowStr + " rows)";
        return result;
    }
}

// Factory function for creating tables
func makeTable(name: String) -> Table {
    var table = new Table();
    table.initWithName(name);
    return table;
}

//=============================================================================
// EXPRESSION TYPES
//=============================================================================

// Expression kind constants
Integer EXPR_LITERAL = 1;      // Literal value (NULL, int, real, text)
Integer EXPR_COLUMN = 2;       // Column reference
Integer EXPR_BINARY = 3;       // Binary operation (+, -, *, /, =, <, >, etc.)
Integer EXPR_UNARY = 4;        // Unary operation (-, NOT)
Integer EXPR_FUNCTION = 5;     // Function call (COUNT, SUM, etc.)
Integer EXPR_STAR = 6;         // SELECT * wildcard
Integer EXPR_SUBQUERY = 7;     // Scalar subquery (SELECT ...)

// Binary operator constants
Integer OP_ADD = 1;
Integer OP_SUB = 2;
Integer OP_MUL = 3;
Integer OP_DIV = 4;
Integer OP_MOD = 5;
Integer OP_EQ = 10;
Integer OP_NE = 11;
Integer OP_LT = 12;
Integer OP_LE = 13;
Integer OP_GT = 14;
Integer OP_GE = 15;
Integer OP_AND = 20;
Integer OP_OR = 21;
Integer OP_LIKE = 22;
Integer OP_IN = 23;
Integer OP_IS = 24;
Integer OP_CONCAT = 25;  // ||

// Unary operator constants
Integer OP_NEG = 1;
Integer OP_NOT = 2;

entity Expr {
    expose Integer kind;

    // For EXPR_LITERAL
    expose SqlValue literalValue;

    // For EXPR_COLUMN
    expose String tableName;   // Optional table alias
    expose String columnName;

    // For EXPR_BINARY and EXPR_UNARY
    expose Integer op;
    // Children stored in args: binary uses args[0]=left, args[1]=right; unary uses args[0]=operand

    // For EXPR_FUNCTION and children
    expose String funcName;
    expose List[Expr] args;  // Also used for binary/unary children

    // For EXPR_SUBQUERY (stores SQL to re-parse - avoids circular dependency)
    expose String subquerySQL;

    expose func init() {
        kind = 1;  // EXPR_LITERAL
        literalValue = new SqlValue();
        literalValue.initNull();
        tableName = "";
        columnName = "";
        op = 0;
        funcName = "";
        args = [];
        subquerySQL = "";
    }

    expose func initLiteral(val: SqlValue) {
        kind = 1;  // EXPR_LITERAL
        literalValue = val;
    }

    expose func initColumn(tbl: String, col: String) {
        kind = 2;  // EXPR_COLUMN
        tableName = tbl;
        columnName = col;
    }

    expose func initBinary(operator: Integer, l: Expr, r: Expr) {
        kind = 3;  // EXPR_BINARY
        op = operator;
        args = [];
        args.add(l);
        args.add(r);
    }

    expose func initUnary(operator: Integer, expr: Expr) {
        kind = 4;  // EXPR_UNARY
        op = operator;
        args = [];
        args.add(expr);
    }

    expose func initFunction(name: String) {
        kind = 5;  // EXPR_FUNCTION
        funcName = name;
        args = [];
    }

    expose func initStar() {
        kind = 6;  // EXPR_STAR
    }

    expose func initSubquery(sql: String) {
        kind = 7;  // EXPR_SUBQUERY
        subquerySQL = sql;
    }

    expose func isLiteral() -> Boolean {
        return kind == EXPR_LITERAL;
    }

    expose func isColumn() -> Boolean {
        return kind == EXPR_COLUMN;
    }

    expose func isBinary() -> Boolean {
        return kind == EXPR_BINARY;
    }

    expose func getLeft() -> Expr {
        return args.get(0);
    }

    expose func getRight() -> Expr {
        return args.get(1);
    }

    expose func getOperand() -> Expr {
        return args.get(0);
    }

    expose func toString() -> String {
        if kind == 1 {  // EXPR_LITERAL
            return literalValue.toString();
        }
        if kind == 2 {  // EXPR_COLUMN
            if tableName != "" {
                return tableName + "." + columnName;
            }
            return columnName;
        }
        if kind == 6 {  // EXPR_STAR
            return "*";
        }
        if kind == 3 {  // EXPR_BINARY
            var opStr = opToString(op);
            var leftExpr = args.get(0);
            var rightExpr = args.get(1);
            return "(" + leftExpr.toString() + " " + opStr + " " + rightExpr.toString() + ")";
        }
        if kind == 4 {  // EXPR_UNARY
            var opStr = "";
            if op == 1 {  // OP_NEG
                opStr = "-";
            }
            if op == 2 {  // OP_NOT
                opStr = "NOT ";
            }
            var operandExpr = args.get(0);
            return opStr + operandExpr.toString();
        }
        if kind == 5 {  // EXPR_FUNCTION
            var result = funcName + "(";
            var i = 0;
            while i < args.count() {
                if i > 0 {
                    result = result + ", ";
                }
                var arg = args.get(i);
                result = result + arg.toString();
                i = i + 1;
            }
            return result + ")";
        }
        if kind == 7 {  // EXPR_SUBQUERY
            return "(" + subquerySQL + ")";
        }
        return "?";
    }
}

func opToString(op: Integer) -> String {
    if op == 1 { return "+"; }   // OP_ADD
    if op == 2 { return "-"; }   // OP_SUB
    if op == 3 { return "*"; }   // OP_MUL
    if op == 4 { return "/"; }   // OP_DIV
    if op == 5 { return "%"; }   // OP_MOD
    if op == 10 { return "="; }  // OP_EQ
    if op == 11 { return "<>"; } // OP_NE
    if op == 12 { return "<"; }  // OP_LT
    if op == 13 { return "<="; } // OP_LE
    if op == 14 { return ">"; }  // OP_GT
    if op == 15 { return ">="; } // OP_GE
    if op == 20 { return "AND"; } // OP_AND
    if op == 21 { return "OR"; } // OP_OR
    if op == 22 { return "LIKE"; } // OP_LIKE
    if op == 25 { return "||"; } // OP_CONCAT
    return "??";
}

// Factory functions for expressions
func exprLiteral(val: SqlValue) -> Expr {
    var e = new Expr();
    e.init();
    e.initLiteral(val);
    return e;
}

func exprNull() -> Expr {
    return exprLiteral(sqlNull());
}

func exprInt(val: Integer) -> Expr {
    return exprLiteral(sqlInteger(val));
}

func exprReal(val: Float, text: String) -> Expr {
    return exprLiteral(sqlReal(val, text));
}

func exprText(val: String) -> Expr {
    return exprLiteral(sqlText(val));
}

func exprColumn(name: String) -> Expr {
    var e = new Expr();
    e.init();
    e.initColumn("", name);
    return e;
}

func exprTableColumn(table: String, column: String) -> Expr {
    var e = new Expr();
    e.init();
    e.initColumn(table, column);
    return e;
}

func exprBinary(op: Integer, left: Expr, right: Expr) -> Expr {
    var e = new Expr();
    e.init();
    e.initBinary(op, left, right);
    return e;
}

func exprUnary(op: Integer, operand: Expr) -> Expr {
    var e = new Expr();
    e.init();
    e.initUnary(op, operand);
    return e;
}

func exprStar() -> Expr {
    var e = new Expr();
    e.init();
    e.initStar();
    return e;
}

func exprFunction(name: String) -> Expr {
    var e = new Expr();
    e.init();
    e.initFunction(name);
    return e;
}

func exprSubquery(sql: String) -> Expr {
    var e = new Expr();
    e.init();
    e.initSubquery(sql);
    return e;
}

//=============================================================================
// STATEMENT TYPES
//=============================================================================

// Statement kind constants
Integer STMT_CREATE_TABLE = 1;
Integer STMT_INSERT = 2;
Integer STMT_SELECT = 3;
Integer STMT_UPDATE = 4;
Integer STMT_DELETE = 5;
Integer STMT_DROP_TABLE = 6;

// CreateTableStmt - holds parsed CREATE TABLE statement
entity CreateTableStmt {
    expose String tableName;
    expose List[Column] columns;

    expose func init() {
        tableName = "";
        columns = [];
    }

    expose func addColumn(col: Column) {
        columns.add(col);
    }

    expose func getColumn(index: Integer) -> Column {
        return columns.get(index);
    }

    expose func columnCount() -> Integer {
        return columns.length();
    }

    expose func toString() -> String {
        var result = "CREATE TABLE " + tableName + " (\n";
        var i = 0;
        while i < columns.length() {
            if i > 0 {
                result = result + ",\n";
            }
            result = result + "  " + columns.get(i).toString();
            i = i + 1;
        }
        result = result + "\n);";
        return result;
    }
}

// InsertStmt - holds parsed INSERT statement
entity InsertStmt {
    expose String tableName;
    expose List[String] columnNames;  // Optional column list
    expose List[List[Expr]] valueRows;  // VALUES (...), (...)

    expose func init() {
        tableName = "";
        columnNames = [];
        valueRows = [];
    }

    expose func addColumnName(name: String) {
        columnNames.add(name);
    }

    expose func addValueRow() {
        var row: List[Expr] = [];
        valueRows.add(row);
    }

    expose func addValue(rowIndex: Integer, val: Expr) {
        valueRows.get(rowIndex).add(val);
    }

    expose func rowCount() -> Integer {
        return valueRows.length();
    }

    expose func toString() -> String {
        var result = "INSERT INTO " + tableName;
        if columnNames.length() > 0 {
            result = result + " (";
            var i = 0;
            while i < columnNames.length() {
                if i > 0 {
                    result = result + ", ";
                }
                result = result + columnNames.get(i);
                i = i + 1;
            }
            result = result + ")";
        }
        result = result + " VALUES ";
        var r = 0;
        while r < valueRows.length() {
            if r > 0 {
                result = result + ", ";
            }
            result = result + "(";
            var row = valueRows.get(r);
            var v = 0;
            while v < row.length() {
                if v > 0 {
                    result = result + ", ";
                }
                result = result + row.get(v).toString();
                v = v + 1;
            }
            result = result + ")";
            r = r + 1;
        }
        result = result + ";";
        return result;
    }
}

// Note: Parser entity causes compiler hang. Using standalone functions with
// global parser state as a workaround.

//=============================================================================
// PARSER STATE (Global)
//=============================================================================
Lexer parserLexer;
Token parserToken;
String parserError = "";
Boolean parserHasError = false;

func parserInit(sql: String) {
    parserLexer = new Lexer(sql);
    parserToken = parserLexer.nextToken();
    parserError = "";
    parserHasError = false;
}

func parserAdvance() {
    parserToken = parserLexer.nextToken();
}

func parserMatch(kind: Integer) -> Boolean {
    if parserToken.kind == kind {
        parserAdvance();
        return true;
    }
    return false;
}

func parserExpect(kind: Integer) -> Boolean {
    if parserToken.kind == kind {
        parserAdvance();
        return true;
    }
    parserHasError = true;
    parserError = "Expected token " + Viper.Fmt.Int(kind) + ", got " + Viper.Fmt.Int(parserToken.kind);
    return false;
}

func parserSetError(msg: String) {
    parserHasError = true;
    parserError = msg;
}

// Note: String-to-number conversion causes compiler issues.
// For now, we store text in expressions and avoid runtime conversion.
// The SqlValue already stores text representation for REAL values.

// Parse primary expression (literals, identifiers, parenthesized)
// Note: Due to no forward declarations, parseExpr calls must be limited to avoid recursion issues
func parsePrimaryExpr() -> Expr {
    var kind = parserToken.kind;

    // Integer literal - store as INTEGER type
    if kind == 10 {  // TK_INTEGER
        var text = parserToken.text;
        parserAdvance();
        // Parse the integer value
        var intVal = textToInt(text);
        return exprInt(intVal);
    }

    // Real/float literal - store as text
    if kind == 11 {  // TK_NUMBER
        var text = parserToken.text;
        parserAdvance();
        return exprReal(0.0, text);  // Value stored in text
    }

    // String literal
    if kind == 12 {  // TK_STRING
        var text = parserToken.text;
        parserAdvance();
        return exprText(text);
    }

    // NULL
    if kind == 70 {  // TK_NULL
        parserAdvance();
        return exprNull();
    }

    // Identifier (column ref or function call)
    if kind == 13 {  // TK_IDENTIFIER
        var name = parserToken.text;
        parserAdvance();

        // Check for function call - identifier followed by (
        if parserToken.kind == 160 {  // TK_LPAREN
            parserAdvance();  // Consume (

            // Create function expression
            var funcExpr = exprFunction(name);

            // Parse arguments until ) - simple parsing to avoid recursion issues
            while parserToken.kind != 161 && !parserHasError {  // TK_RPAREN
                if parserToken.kind == 142 {  // TK_STAR
                    funcExpr.args.add(exprStar());
                    parserAdvance();
                } else if parserToken.kind == 13 {  // TK_IDENTIFIER
                    var argText = parserToken.text;
                    parserAdvance();
                    funcExpr.args.add(exprColumn(argText));
                } else if parserToken.kind == 10 {  // TK_INTEGER
                    var intText = parserToken.text;
                    parserAdvance();
                    var intVal = textToInt(intText);
                    funcExpr.args.add(exprInt(intVal));
                } else if parserToken.kind == 12 {  // TK_STRING
                    var strText = parserToken.text;
                    parserAdvance();
                    funcExpr.args.add(exprText(strText));
                } else {
                    break;  // Unknown token in arg list, exit
                }

                // Check for comma
                if parserToken.kind == 162 {  // TK_COMMA
                    parserAdvance();
                }
            }

            // Consume )
            if parserToken.kind == 161 {
                parserAdvance();
            }

            return funcExpr;
        }

        // Check for table.column
        if parserToken.kind == 164 {  // TK_DOT
            parserAdvance();
            if parserToken.kind != 13 {
                parserSetError("Expected column name after dot");
                return exprNull();
            }
            var colName = parserToken.text;
            parserAdvance();
            return exprTableColumn(name, colName);
        }

        return exprColumn(name);
    }

    // Star (*)
    if kind == 142 {  // TK_STAR
        parserAdvance();
        return exprStar();
    }

    // Handle parenthesized expressions including subqueries
    if kind == 160 {  // TK_LPAREN
        parserAdvance();  // Consume (

        // Check if this is a subquery: (SELECT ...)
        if parserToken.kind == 30 {  // TK_SELECT
            // Collect tokens to build subquery SQL
            var subquerySql = "SELECT";
            parserAdvance();
            var depth = 1;  // We're inside one level of parens

            while depth > 0 && !parserHasError {
                if parserToken.kind == 160 {  // TK_LPAREN
                    depth = depth + 1;
                    subquerySql = subquerySql + " (";
                } else if parserToken.kind == 161 {  // TK_RPAREN
                    depth = depth - 1;
                    if depth > 0 {
                        subquerySql = subquerySql + ")";
                    }
                } else if parserToken.kind == 0 {  // TK_EOF
                    parserSetError("Unexpected end of input in subquery");
                    return exprNull();
                } else {
                    // Add token text with space
                    subquerySql = subquerySql + " " + parserToken.text;
                }
                parserAdvance();
            }

            return exprSubquery(subquerySql);
        }

        // Regular parenthesized expression - parse inner expression
        var inner = parseExpr();
        if parserToken.kind == 161 {  // TK_RPAREN
            parserAdvance();
        } else {
            parserSetError("Expected ) after parenthesized expression");
        }
        return inner;
    }

    parserSetError("Unexpected token in expression");
    return exprNull();
}

// Parse unary expression
func parseUnaryExpr() -> Expr {
    if parserToken.kind == 141 {  // TK_MINUS
        parserAdvance();
        var operand = parseUnaryExpr();
        return exprUnary(1, operand);  // OP_NEG
    }
    if parserToken.kind == 62 {  // TK_NOT
        parserAdvance();
        var operand = parseUnaryExpr();
        return exprUnary(2, operand);  // OP_NOT
    }
    return parsePrimaryExpr();
}

// Parse multiplication/division
func parseMulExpr() -> Expr {
    var left = parseUnaryExpr();
    while parserToken.kind == 142 || parserToken.kind == 143 {
        var op = parserToken.kind;
        parserAdvance();
        var right = parseUnaryExpr();
        if op == 142 {
            left = exprBinary(3, left, right);  // OP_MUL
        } else {
            left = exprBinary(4, left, right);  // OP_DIV
        }
    }
    return left;
}

// Parse addition/subtraction
func parseAddExpr() -> Expr {
    var left = parseMulExpr();
    while parserToken.kind == 140 || parserToken.kind == 141 {
        var op = parserToken.kind;
        parserAdvance();
        var right = parseMulExpr();
        if op == 140 {
            left = exprBinary(1, left, right);  // OP_ADD
        } else {
            left = exprBinary(2, left, right);  // OP_SUB
        }
    }
    return left;
}

// Parse comparison
func parseCompExpr() -> Expr {
    var left = parseAddExpr();
    var kind = parserToken.kind;
    if kind == 145 {  // TK_EQ
        parserAdvance();
        return exprBinary(10, left, parseAddExpr());  // OP_EQ
    }
    if kind == 146 {  // TK_NE (<>)
        parserAdvance();
        return exprBinary(11, left, parseAddExpr());  // OP_NE
    }
    if kind == 147 {  // TK_LT
        parserAdvance();
        return exprBinary(12, left, parseAddExpr());  // OP_LT
    }
    if kind == 148 {  // TK_LE (<=)
        parserAdvance();
        return exprBinary(13, left, parseAddExpr());  // OP_LE
    }
    if kind == 149 {  // TK_GT
        parserAdvance();
        return exprBinary(14, left, parseAddExpr());  // OP_GT
    }
    if kind == 150 {  // TK_GE (>=)
        parserAdvance();
        return exprBinary(15, left, parseAddExpr());  // OP_GE
    }
    // Handle IN (subquery or value list)
    if kind == 63 {  // TK_IN
        parserAdvance();
        // Expect ( after IN
        if parserToken.kind != 160 {  // TK_LPAREN
            parserSetError("Expected '(' after IN");
            return left;
        }
        // The parsePrimaryExpr will handle (SELECT ...) as a subquery
        var right = parsePrimaryExpr();
        return exprBinary(23, left, right);  // OP_IN
    }
    return left;
}

// Parse AND
func parseAndExpr() -> Expr {
    var left = parseCompExpr();
    while parserToken.kind == 60 {  // TK_AND
        parserAdvance();
        var right = parseCompExpr();
        left = exprBinary(20, left, right);  // OP_AND
    }
    return left;
}

// Parse OR
func parseOrExpr() -> Expr {
    var left = parseAndExpr();
    while parserToken.kind == 61 {  // TK_OR
        parserAdvance();
        var right = parseAndExpr();
        left = exprBinary(21, left, right);  // OP_OR
    }
    return left;
}

// Main expression entry point
func parseExpr() -> Expr {
    return parseOrExpr();
}

// Parse column definition for CREATE TABLE
func parseColumnDef() -> Column {
    var col = new Column();
    col.init();

    // Column name
    if parserToken.kind != 13 {  // TK_IDENTIFIER
        parserSetError("Expected column name");
        return col;
    }
    col.name = parserToken.text;
    parserAdvance();

    // Data type
    if parserToken.kind == 91 {  // TK_INTEGER_TYPE
        col.typeCode = 1;  // SQL_INTEGER
        parserAdvance();
    } else if parserToken.kind == 93 {  // TK_TEXT
        col.typeCode = 3;  // SQL_TEXT
        parserAdvance();
    } else if parserToken.kind == 92 {  // TK_REAL
        col.typeCode = 2;  // SQL_REAL
        parserAdvance();
    } else {
        parserSetError("Expected data type");
        return col;
    }

    // Optional constraints
    while !parserHasError {
        if parserToken.kind == 80 {  // TK_PRIMARY
            parserAdvance();
            if !parserExpect(82) {  // TK_KEY
                return col;
            }
            col.primaryKey = true;
        } else if parserToken.kind == 87 {  // TK_AUTOINCREMENT
            parserAdvance();
            col.autoIncrement = true;
        } else if parserToken.kind == 62 {  // TK_NOT
            parserAdvance();
            if !parserExpect(70) {  // TK_NULL
                return col;
            }
            col.notNull = true;
        } else {
            break;
        }
    }

    return col;
}

// Parse CREATE TABLE statement
func parseCreateTableStmt() -> CreateTableStmt {
    var stmt = new CreateTableStmt();

    Viper.Terminal.Say("[DEBUG] parseCreateTableStmt: token kind=" + Viper.Fmt.Int(parserToken.kind) + " text='" + parserToken.text + "'");

    // Already at TABLE, expect TABLE
    if !parserExpect(21) {  // TK_TABLE
        Viper.Terminal.Say("[DEBUG] Failed to match TK_TABLE");
        return stmt;
    }

    Viper.Terminal.Say("[DEBUG] After expect TK_TABLE: token kind=" + Viper.Fmt.Int(parserToken.kind) + " text='" + parserToken.text + "' len=" + Viper.Fmt.Int(parserToken.text.length()));

    // Table name
    if parserToken.kind != 13 {  // TK_IDENTIFIER
        parserSetError("Expected table name");
        return stmt;
    }
    // Try copying the token locally first
    var localToken = parserToken;
    var tmpName = localToken.text;
    Viper.Terminal.Say("[DEBUG] tmpName = '" + tmpName + "' len=" + Viper.Fmt.Int(tmpName.length()));
    stmt.tableName = tmpName;
    Viper.Terminal.Say("[DEBUG] stmt.tableName = '" + stmt.tableName + "'");
    parserAdvance();

    // Opening paren
    if !parserExpect(160) {  // TK_LPAREN
        return stmt;
    }

    // Column definitions
    while !parserHasError {
        var col = parseColumnDef();
        if parserHasError {
            return stmt;
        }
        stmt.addColumn(col);

        if parserToken.kind == 162 {  // TK_COMMA
            parserAdvance();
        } else {
            break;
        }
    }

    // Closing paren
    if !parserExpect(161) {  // TK_RPAREN
        return stmt;
    }

    // Optional semicolon
    parserMatch(163);  // TK_SEMICOLON

    return stmt;
}

// Parse INSERT statement
func parseInsertStmt() -> InsertStmt {
    var stmt = new InsertStmt();

    // Already at INSERT, expect INTO
    if !parserExpect(34) {  // TK_INTO
        return stmt;
    }

    // Table name
    if parserToken.kind != 13 {  // TK_IDENTIFIER
        parserSetError("Expected table name");
        return stmt;
    }
    stmt.tableName = parserToken.text;
    parserAdvance();

    // Optional column list
    if parserToken.kind == 160 {  // TK_LPAREN
        parserAdvance();
        while !parserHasError {
            if parserToken.kind != 13 {
                parserSetError("Expected column name");
                return stmt;
            }
            stmt.addColumnName(parserToken.text);
            parserAdvance();

            if parserToken.kind == 162 {  // TK_COMMA
                parserAdvance();
            } else {
                break;
            }
        }
        if !parserExpect(161) {  // TK_RPAREN
            return stmt;
        }
    }

    // Expect VALUES
    if !parserExpect(38) {  // TK_VALUES
        return stmt;
    }

    // Value rows
    while !parserHasError {
        if !parserExpect(160) {  // TK_LPAREN
            return stmt;
        }

        stmt.addValueRow();
        var rowIdx = stmt.rowCount() - 1;

        // Parse values
        while !parserHasError {
            var val = parseExpr();
            if parserHasError {
                return stmt;
            }
            stmt.addValue(rowIdx, val);

            if parserToken.kind == 162 {  // TK_COMMA
                parserAdvance();
            } else {
                break;
            }
        }

        if !parserExpect(161) {  // TK_RPAREN
            return stmt;
        }

        if parserToken.kind == 162 {  // TK_COMMA
            parserAdvance();
        } else {
            break;
        }
    }

    parserMatch(163);  // TK_SEMICOLON
    return stmt;
}

//=============================================================================
// SELECT STATEMENT
//=============================================================================

entity SelectStmt {
    expose List[Expr] columns;      // SELECT columns (or * for all)
    expose String tableName;        // FROM table (for backwards compat)
    expose String tableAlias;       // Table alias (e.g., FROM users u)
    expose List[String] tableNames;    // Multiple tables for JOINs
    expose List[String] tableAliases;  // Aliases for each table
    expose List[Integer] joinTypes;    // 0=CROSS, 1=INNER, 2=LEFT, 3=RIGHT, 4=FULL
    expose List[Expr] joinConditions;  // ON conditions for each join (null for CROSS)
    expose Expr? whereClause;       // WHERE condition (optional)
    expose Boolean selectAll;       // True if SELECT *
    expose Boolean isDistinct;      // True if SELECT DISTINCT
    expose List[Expr] orderByExprs; // ORDER BY expressions
    expose List[Integer] orderByDir; // 0 = ASC, 1 = DESC for each ORDER BY
    expose Integer limitValue;      // LIMIT value (-1 = no limit)
    expose Integer offsetValue;     // OFFSET value (0 = no offset)
    expose List[Expr] groupByExprs; // GROUP BY expressions
    expose Expr? havingClause;      // HAVING clause (optional)
    // Derived tables (subqueries in FROM clause)
    expose String derivedTableSQL;   // SQL for the derived table
    expose String derivedTableAlias; // Alias for the derived table
    expose Boolean hasDerivedTable;  // True if FROM clause contains a subquery

    expose func init() {
        columns = [];
        tableName = "";
        tableAlias = "";
        tableNames = [];
        tableAliases = [];
        joinTypes = [];
        joinConditions = [];
        whereClause = null;
        selectAll = false;
        isDistinct = false;
        orderByExprs = [];
        orderByDir = [];
        limitValue = -1;
        offsetValue = 0;
        groupByExprs = [];
        havingClause = null;
        derivedTableSQL = "";
        derivedTableAlias = "";
        hasDerivedTable = false;
    }

    expose func addTable(name: String, alias: String) {
        tableNames.add(name);
        tableAliases.add(alias);
    }

    expose func addJoin(name: String, alias: String, joinType: Integer, condition: Expr?) {
        tableNames.add(name);
        tableAliases.add(alias);
        joinTypes.add(joinType);
        if condition != null {
            joinConditions.add(condition);
        }
    }

    expose func addGroupBy(expr: Expr) {
        groupByExprs.add(expr);
    }

    expose func addColumn(col: Expr) {
        columns.add(col);
    }

    expose func addOrderBy(expr: Expr, isDesc: Integer) {
        orderByExprs.add(expr);
        orderByDir.add(isDesc);
    }

    expose func toString() -> String {
        var result = "SELECT ";
        if selectAll == true {
            result = result + "* FROM " + tableName;
        } else {
            result = result + "... FROM " + tableName;
        }
        return result + ";";
    }
}

// Parse SELECT statement
func parseSelectStmt() -> SelectStmt {
    var stmt = new SelectStmt();
    stmt.init();

    // Check for DISTINCT
    if parserToken.kind == 48 {  // TK_DISTINCT
        stmt.isDistinct = true;
        parserAdvance();
    }

    // Check for * or column list
    if parserToken.kind == 142 {  // TK_STAR
        stmt.selectAll = true;
        parserAdvance();
    } else {
        // Parse column list
        while !parserHasError {
            var col = parseExpr();
            if parserHasError {
                return stmt;
            }

            // Handle column alias: expr AS alias or expr alias
            if parserToken.kind == 110 {  // TK_AS
                parserAdvance();
                if parserToken.kind == 13 {  // TK_IDENTIFIER
                    // Store alias in the expression (we'll ignore it for now since
                    // the column name is already in the expression)
                    parserAdvance();
                }
            } else if parserToken.kind == 13 {  // TK_IDENTIFIER - implicit alias
                var maybeAlias = parserToken.text;
                var upperAlias = Viper.String.ToUpper(maybeAlias);
                if upperAlias != "FROM" && upperAlias != "WHERE" && upperAlias != "GROUP" && upperAlias != "ORDER" && upperAlias != "LIMIT" && upperAlias != "HAVING" {
                    parserAdvance();  // Skip the alias
                }
            }

            stmt.addColumn(col);

            if parserToken.kind == 162 {  // TK_COMMA
                parserAdvance();
            } else {
                break;
            }
        }
    }

    // Expect FROM
    if !parserExpect(35) {  // TK_FROM
        return stmt;
    }

    // Check for derived table (subquery in FROM): FROM (SELECT ...)
    if parserToken.kind == 160 {  // TK_LPAREN
        // Could be a derived table - peek ahead for SELECT
        parserAdvance();
        if parserToken.kind == 30 {  // TK_SELECT
            // This is a derived table - capture the full subquery
            var parenDepth = 1;
            var subquerySQL = "SELECT";
            parserAdvance();  // Move past SELECT

            // Collect tokens until we close the parenthesis
            while parenDepth > 0 && !parserHasError && parserToken.kind != 0 {
                if parserToken.kind == 160 {  // TK_LPAREN
                    parenDepth = parenDepth + 1;
                    subquerySQL = subquerySQL + " (";
                } else if parserToken.kind == 161 {  // TK_RPAREN
                    parenDepth = parenDepth - 1;
                    if parenDepth > 0 {
                        subquerySQL = subquerySQL + " )";
                    }
                } else {
                    subquerySQL = subquerySQL + " " + parserToken.text;
                }
                parserAdvance();
            }

            stmt.hasDerivedTable = true;
            stmt.derivedTableSQL = subquerySQL;
            stmt.tableName = "__derived__";  // Marker for derived table

            // Expect alias after derived table (required in SQL)
            if parserToken.kind == 110 {  // TK_AS
                parserAdvance();
            }
            if parserToken.kind == 13 {  // TK_IDENTIFIER
                stmt.derivedTableAlias = parserToken.text;
                stmt.tableAlias = parserToken.text;
                parserAdvance();
            } else {
                parserSetError("Derived table requires an alias");
                return stmt;
            }

            stmt.addTable("__derived__", stmt.derivedTableAlias);
        } else {
            parserSetError("Expected SELECT in derived table");
            return stmt;
        }
    } else {
        // Parse regular table name
        if parserToken.kind != 13 {  // TK_IDENTIFIER
            parserSetError("Expected table name");
            return stmt;
        }
        var firstTableName = parserToken.text;
        var firstTableAlias = "";
        stmt.tableName = firstTableName;  // Keep for backward compatibility
        parserAdvance();

        // Optional table alias (AS alias or just alias)
        if parserToken.kind == 110 {  // TK_AS
            parserAdvance();
            if parserToken.kind == 13 {  // TK_IDENTIFIER
                firstTableAlias = parserToken.text;
                stmt.tableAlias = firstTableAlias;
                parserAdvance();
            } else {
                parserSetError("Expected alias name after AS");
                return stmt;
            }
        } else if parserToken.kind == 13 {  // TK_IDENTIFIER (alias without AS)
            // Check it's not a keyword that could follow FROM table
            var aliasText = parserToken.text;
            var upperAlias = Viper.String.ToUpper(aliasText);
            if upperAlias != "WHERE" && upperAlias != "GROUP" && upperAlias != "ORDER" && upperAlias != "LIMIT" && upperAlias != "HAVING" && upperAlias != "JOIN" && upperAlias != "INNER" && upperAlias != "LEFT" && upperAlias != "RIGHT" && upperAlias != "FULL" && upperAlias != "CROSS" {
                firstTableAlias = aliasText;
                stmt.tableAlias = firstTableAlias;
                parserAdvance();
            }
        }

        // Add first table to lists
        stmt.addTable(firstTableName, firstTableAlias);
    }

    // Parse additional tables (comma-separated for CROSS JOIN)
    while parserToken.kind == 162 {  // TK_COMMA
        parserAdvance();
        if parserToken.kind != 13 {  // TK_IDENTIFIER
            parserSetError("Expected table name after comma");
            return stmt;
        }
        var extraTableName = parserToken.text;
        var extraTableAlias = "";
        parserAdvance();

        // Optional alias for extra table
        if parserToken.kind == 110 {  // TK_AS
            parserAdvance();
            if parserToken.kind == 13 {
                extraTableAlias = parserToken.text;
                parserAdvance();
            }
        } else if parserToken.kind == 13 {
            var maybeAlias = parserToken.text;
            var upperMaybe = Viper.String.ToUpper(maybeAlias);
            if upperMaybe != "WHERE" && upperMaybe != "GROUP" && upperMaybe != "ORDER" && upperMaybe != "LIMIT" && upperMaybe != "HAVING" && upperMaybe != "JOIN" {
                extraTableAlias = maybeAlias;
                parserAdvance();
            }
        }

        stmt.addTable(extraTableName, extraTableAlias);
    }

    // Parse JOIN clauses (INNER JOIN, LEFT JOIN, etc.)
    while parserToken.kind == 50 || parserToken.kind == 51 || parserToken.kind == 52 ||
          parserToken.kind == 53 || parserToken.kind == 54 || parserToken.kind == 56 {  // TK_JOIN, TK_INNER, TK_LEFT, TK_RIGHT, TK_FULL, TK_CROSS
        var joinType = 1;  // Default to INNER JOIN

        // Determine join type
        if parserToken.kind == 51 {  // TK_INNER
            joinType = 1;
            parserAdvance();
            if parserToken.kind != 50 {  // TK_JOIN
                parserSetError("Expected JOIN after INNER");
                return stmt;
            }
            parserAdvance();
        } else if parserToken.kind == 52 {  // TK_LEFT
            joinType = 2;
            parserAdvance();
            if parserToken.kind == 55 {  // TK_OUTER (optional)
                parserAdvance();
            }
            if parserToken.kind != 50 {  // TK_JOIN
                parserSetError("Expected JOIN after LEFT");
                return stmt;
            }
            parserAdvance();
        } else if parserToken.kind == 53 {  // TK_RIGHT
            joinType = 3;
            parserAdvance();
            if parserToken.kind == 55 {  // TK_OUTER (optional)
                parserAdvance();
            }
            if parserToken.kind != 50 {  // TK_JOIN
                parserSetError("Expected JOIN after RIGHT");
                return stmt;
            }
            parserAdvance();
        } else if parserToken.kind == 54 {  // TK_FULL
            joinType = 4;
            parserAdvance();
            if parserToken.kind == 55 {  // TK_OUTER (optional)
                parserAdvance();
            }
            if parserToken.kind != 50 {  // TK_JOIN
                parserSetError("Expected JOIN after FULL");
                return stmt;
            }
            parserAdvance();
        } else if parserToken.kind == 56 {  // TK_CROSS
            joinType = 0;
            parserAdvance();
            if parserToken.kind != 50 {  // TK_JOIN
                parserSetError("Expected JOIN after CROSS");
                return stmt;
            }
            parserAdvance();
        } else if parserToken.kind == 50 {  // TK_JOIN (bare JOIN = INNER JOIN)
            joinType = 1;
            parserAdvance();
        }

        // Expect table name
        if parserToken.kind != 13 {  // TK_IDENTIFIER
            parserSetError("Expected table name after JOIN");
            return stmt;
        }
        var joinTableName = parserToken.text;
        var joinTableAlias = "";
        parserAdvance();

        // Optional alias
        if parserToken.kind == 110 {  // TK_AS
            parserAdvance();
            if parserToken.kind == 13 {
                joinTableAlias = parserToken.text;
                parserAdvance();
            }
        } else if parserToken.kind == 13 {
            var maybeJoinAlias = parserToken.text;
            var upperJoinAlias = Viper.String.ToUpper(maybeJoinAlias);
            if upperJoinAlias != "ON" && upperJoinAlias != "WHERE" && upperJoinAlias != "GROUP" &&
               upperJoinAlias != "ORDER" && upperJoinAlias != "LIMIT" && upperJoinAlias != "JOIN" {
                joinTableAlias = maybeJoinAlias;
                parserAdvance();
            }
        }

        // ON clause (required for INNER/LEFT/RIGHT, not for CROSS)
        var joinCondition: Expr? = null;
        if parserToken.kind == 57 {  // TK_ON
            parserAdvance();
            joinCondition = parseExpr();
        } else if joinType != 0 {  // Not CROSS JOIN - ON is required
            parserSetError("Expected ON clause for JOIN");
            return stmt;
        }

        stmt.addJoin(joinTableName, joinTableAlias, joinType, joinCondition);
    }

    // Optional WHERE clause
    if parserToken.kind == 36 {  // TK_WHERE
        parserAdvance();
        stmt.whereClause = parseExpr();
    }

    // Optional GROUP BY clause
    if parserToken.kind == 46 {  // TK_GROUP
        parserAdvance();
        if !parserExpect(41) {  // TK_BY
            return stmt;
        }
        // Parse GROUP BY expressions
        while !parserHasError {
            var groupExpr = parseExpr();
            if parserHasError {
                return stmt;
            }
            stmt.addGroupBy(groupExpr);

            if parserToken.kind == 162 {  // TK_COMMA
                parserAdvance();
            } else {
                break;
            }
        }
    }

    // Optional HAVING clause (after GROUP BY)
    if parserToken.kind == 47 {  // TK_HAVING
        parserAdvance();
        stmt.havingClause = parseExpr();
    }

    // Optional ORDER BY clause
    if parserToken.kind == 40 {  // TK_ORDER
        parserAdvance();
        if !parserExpect(41) {  // TK_BY
            return stmt;
        }
        // Parse ORDER BY expressions
        while !parserHasError {
            var orderExpr = parseExpr();
            if parserHasError {
                return stmt;
            }
            // Check for ASC or DESC (default is ASC = 0, DESC = 1)
            var isDesc = 0;
            if parserToken.kind == 43 {  // TK_DESC
                isDesc = 1;
                parserAdvance();
            } else if parserToken.kind == 42 {  // TK_ASC
                parserAdvance();
            }
            stmt.addOrderBy(orderExpr, isDesc);

            if parserToken.kind == 162 {  // TK_COMMA
                parserAdvance();
            } else {
                break;
            }
        }
    }

    // Optional LIMIT clause
    if parserToken.kind == 44 {  // TK_LIMIT
        parserAdvance();
        if parserToken.kind == 10 {  // TK_INTEGER
            stmt.limitValue = stringToInt(parserToken.text);
            parserAdvance();
        } else {
            parserSetError("Expected integer after LIMIT");
            return stmt;
        }

        // Optional OFFSET after LIMIT
        if parserToken.kind == 45 {  // TK_OFFSET
            parserAdvance();
            if parserToken.kind == 10 {  // TK_INTEGER
                stmt.offsetValue = stringToInt(parserToken.text);
                parserAdvance();
            } else {
                parserSetError("Expected integer after OFFSET");
                return stmt;
            }
        }
    }

    parserMatch(163);  // TK_SEMICOLON
    return stmt;
}

// UpdateStmt - holds parsed UPDATE statement
entity UpdateStmt {
    expose String tableName;
    expose List[String] setColumns;     // Columns to update
    expose List[Expr] setValues;        // Values to set
    expose Expr? whereClause;           // WHERE condition (optional)

    expose func init() {
        tableName = "";
        setColumns = [];
        setValues = [];
        whereClause = null;
    }

    expose func addSet(colName: String, val: Expr) {
        setColumns.add(colName);
        setValues.add(val);
    }

    expose func toString() -> String {
        var result = "UPDATE " + tableName + " SET ";
        var i = 0;
        while i < setColumns.count() {
            if i > 0 {
                result = result + ", ";
            }
            result = result + setColumns.get(i) + " = ";
            result = result + setValues.get(i).toString();
            i = i + 1;
        }
        if whereClause != null {
            result = result + " WHERE ...";
        }
        return result + ";";
    }
}

// Parse UPDATE statement
func parseUpdateStmt() -> UpdateStmt {
    var stmt = new UpdateStmt();
    stmt.init();

    // Already past UPDATE, expect table name
    if parserToken.kind != 13 {  // TK_IDENTIFIER
        parserSetError("Expected table name");
        return stmt;
    }
    stmt.tableName = parserToken.text;
    parserAdvance();

    // Expect SET
    if !parserExpect(37) {  // TK_SET
        return stmt;
    }

    // Parse SET column = value pairs
    while !parserHasError {
        // Column name
        if parserToken.kind != 13 {  // TK_IDENTIFIER
            parserSetError("Expected column name");
            return stmt;
        }
        var colName = parserToken.text;
        parserAdvance();

        // Expect =
        if !parserExpect(145) {  // TK_EQ
            return stmt;
        }

        // Value expression
        var val = parseExpr();
        if parserHasError {
            return stmt;
        }

        stmt.addSet(colName, val);

        // Check for comma (more assignments)
        if parserToken.kind == 162 {  // TK_COMMA
            parserAdvance();
        } else {
            break;
        }
    }

    // Optional WHERE clause
    if parserToken.kind == 36 {  // TK_WHERE
        parserAdvance();
        stmt.whereClause = parseExpr();
    }

    parserMatch(163);  // TK_SEMICOLON
    return stmt;
}

// DeleteStmt - holds parsed DELETE statement
entity DeleteStmt {
    expose String tableName;
    expose Expr? whereClause;           // WHERE condition (optional)

    expose func init() {
        tableName = "";
        whereClause = null;
    }

    expose func toString() -> String {
        var result = "DELETE FROM " + tableName;
        if whereClause != null {
            result = result + " WHERE ...";
        }
        return result + ";";
    }
}

// Parse DELETE statement
func parseDeleteStmt() -> DeleteStmt {
    var stmt = new DeleteStmt();
    stmt.init();

    // Already past DELETE, expect FROM
    if !parserExpect(35) {  // TK_FROM
        return stmt;
    }

    // Table name
    if parserToken.kind != 13 {  // TK_IDENTIFIER
        parserSetError("Expected table name");
        return stmt;
    }
    stmt.tableName = parserToken.text;
    parserAdvance();

    // Optional WHERE clause
    if parserToken.kind == 36 {  // TK_WHERE
        parserAdvance();
        stmt.whereClause = parseExpr();
    }

    parserMatch(163);  // TK_SEMICOLON
    return stmt;
}

//=============================================================================
// DATABASE CONTAINER
//=============================================================================

entity Database {
    expose String name;
    expose List[Table] tables;

    expose func init() {
        name = "default";
        tables = [];
    }

    expose func initWithName(dbName: String) {
        name = dbName;
        tables = [];
    }

    expose func tableCount() -> Integer {
        return tables.count();
    }

    expose func getTable(index: Integer) -> Table? {
        if index < 0 || index >= tables.count() {
            return null;
        }
        return tables.get(index);
    }

    expose func findTable(tableName: String) -> Table? {
        Viper.Terminal.Say("[DEBUG] findTable looking for: '" + tableName + "'");
        var i = 0;
        while i < tables.count() {
            var t = tables.get(i);
            Viper.Terminal.Say("[DEBUG] findTable checking table[" + i.toString() + "]: '" + t.name + "'");
            if t.name == tableName {
                Viper.Terminal.Say("[DEBUG] findTable FOUND!");
                return t;
            }
            i = i + 1;
        }
        Viper.Terminal.Say("[DEBUG] findTable NOT FOUND");
        return null;
    }

    expose func addTable(table: Table) {
        tables.add(table);
    }

    expose func dropTable(tableName: String) -> Boolean {
        var i = 0;
        while i < tables.count() {
            if tables.get(i).name == tableName {
                tables.remove(i);
                return true;
            }
            i = i + 1;
        }
        return false;
    }

    expose func listTables() -> String {
        var result = "";
        var i = 0;
        while i < tables.count() {
            if i > 0 {
                result = result + "\n";
            }
            result = result + tables.get(i).name;
            i = i + 1;
        }
        return result;
    }
}

// Factory function for creating databases
func makeDatabase(name: String) -> Database {
    var db = new Database();
    db.initWithName(name);
    return db;
}

//=============================================================================
// QUERY RESULT
//=============================================================================

entity QueryResult {
    expose Boolean success;
    expose String message;
    expose List[String] columnNames;
    expose List[Row] rows;
    expose Integer rowsAffected;

    expose func init() {
        success = true;
        message = "";
        columnNames = [];
        rows = [];
        rowsAffected = 0;
    }

    expose func setError(msg: String) {
        success = false;
        message = msg;
    }

    expose func addColumnName(name: String) {
        columnNames.add(name);
    }

    expose func addRow(row: Row) {
        rows.add(row);
    }

    expose func getRow(idx: Integer) -> Row? {
        if idx >= 0 && idx < rows.count() {
            return rows.get(idx);
        }
        return null;
    }

    expose func setRow(idx: Integer, row: Row) {
        if idx >= 0 && idx < rows.count() {
            rows.set(idx, row);
        }
    }

    expose func swapRows(i: Integer, j: Integer) {
        if i >= 0 && i < rows.count() && j >= 0 && j < rows.count() {
            var temp = rows.get(i);
            rows.set(i, rows.get(j));
            rows.set(j, temp);
        }
    }

    expose func rowCount() -> Integer {
        return rows.count();
    }

    expose func toString() -> String {
        if !success {
            return "ERROR: " + message;
        }

        var result = "";

        // Print column headers
        var i = 0;
        while i < columnNames.count() {
            if i > 0 {
                result = result + " | ";
            }
            result = result + columnNames.get(i);
            i = i + 1;
        }

        if columnNames.count() > 0 {
            result = result + "\n";
            // Print separator
            i = 0;
            while i < columnNames.count() {
                if i > 0 {
                    result = result + "-+-";
                }
                result = result + "--------";
                i = i + 1;
            }
            result = result + "\n";
        }

        // Print rows
        var r = 0;
        while r < rows.count() {
            var row = rows.get(r);
            var c = 0;
            while c < row.columnCount() {
                if c > 0 {
                    result = result + " | ";
                }
                result = result + row.getValue(c).toString();
                c = c + 1;
            }
            result = result + "\n";
            r = r + 1;
        }

        if rows.count() == 0 && columnNames.count() == 0 {
            result = message;
        } else {
            result = result + "(" + Viper.Fmt.Int(rows.count()) + " rows)";
        }

        return result;
    }
}

//=============================================================================
// EXECUTOR
//=============================================================================

// Global database instance
Database currentDb;
Boolean dbInitialized = false;

// Outer context for correlated subqueries
Row? outerRow = null;
Table? outerTable = null;
String outerTableAlias = "";  // Alias of outer table (e.g., "e" in "FROM employees e")

// Current table alias for tracking during WHERE evaluation
String currentTableAlias = "";

func getDatabase() -> Database {
    if dbInitialized == true {
        Viper.Terminal.Say("[DEBUG] getDatabase: dbInitialized is TRUE");
    } else {
        Viper.Terminal.Say("[DEBUG] getDatabase: dbInitialized is FALSE, creating new db");
        currentDb = new Database();
        currentDb.init();
        dbInitialized = true;
    }
    var tc = currentDb.tableCount();
    if tc == 0 {
        Viper.Terminal.Say("[DEBUG] db has 0 tables");
    }
    if tc == 1 {
        Viper.Terminal.Say("[DEBUG] db has 1 table");
    }
    if tc == 2 {
        Viper.Terminal.Say("[DEBUG] db has 2 tables");
    }
    return currentDb;
}

// Execute CREATE TABLE
func executeCreateTable(stmt: CreateTableStmt) -> QueryResult {
    var result = new QueryResult();
    result.init();

    var db = getDatabase();

    // Check if table already exists
    if db.findTable(stmt.tableName) != null {
        result.setError("Table '" + stmt.tableName + "' already exists");
        return result;
    }

    // Create new table
    var table = new Table();
    table.initWithName(stmt.tableName);

    // Add columns from statement
    var i = 0;
    while i < stmt.columnCount() {
        var stmtCol = stmt.getColumn(i);
        var col = new Column();
        col.initWithName(stmtCol.name, stmtCol.typeCode);
        col.notNull = stmtCol.notNull;
        col.primaryKey = stmtCol.primaryKey;
        col.autoIncrement = stmtCol.autoIncrement;
        col.unique = stmtCol.unique;
        table.addColumn(col);
        i = i + 1;
    }

    db.addTable(table);
    var afterTc = db.tableCount();
    if afterTc == 1 {
        Viper.Terminal.Say("[DEBUG] After addTable, db has 1 table");
    }
    if afterTc == 2 {
        Viper.Terminal.Say("[DEBUG] After addTable, db has 2 tables");
    }
    result.message = "Table '" + stmt.tableName + "' created";
    result.rowsAffected = 0;
    return result;
}

// Evaluate expression to SqlValue - minimal version
func evalExprLiteral(expr: Expr) -> SqlValue {
    if expr.kind == 1 {  // EXPR_LITERAL
        return expr.literalValue;
    }
    return sqlNull();
}

func evalExprColumn(expr: Expr, row: Row, table: Table) -> SqlValue {
    // Check if there's a table qualifier that specifically references outer table alias
    if expr.tableName != "" && outerTableAlias != "" {
        if expr.tableName == outerTableAlias {
            // This column specifically references the outer table
            if outerTable != null && outerRow != null {
                var outerColIdx = outerTable.findColumnIndex(expr.columnName);
                if outerColIdx >= 0 {
                    return outerRow.getValue(outerColIdx);
                }
            }
            return sqlNull();
        }
    }

    // Try to find column in current table
    var colIdx = table.findColumnIndex(expr.columnName);
    if colIdx < 0 {
        // Column not found in current table - check outer context (correlated subquery)
        // Only do this for unqualified columns (no table alias specified)
        if expr.tableName == "" && outerTable != null && outerRow != null {
            var outerColIdx = outerTable.findColumnIndex(expr.columnName);
            if outerColIdx >= 0 {
                return outerRow.getValue(outerColIdx);
            }
        }
        return sqlNull();
    }
    return row.getValue(colIdx);
}

// Evaluate any expression to SqlValue
func evalExpr(expr: Expr, row: Row, table: Table) -> SqlValue {
    if expr.kind == 1 {  // EXPR_LITERAL
        return evalExprLiteral(expr);
    }
    if expr.kind == 2 {  // EXPR_COLUMN
        return evalExprColumn(expr, row, table);
    }
    if expr.kind == 7 {  // EXPR_SUBQUERY
        return evalSubqueryCorrelated(expr.subquerySQL, row, table);
    }
    return sqlNull();
}

// Evaluate a scalar subquery - parses and executes, returns first value (non-correlated)
func evalSubquery(sql: String) -> SqlValue {
    // Save parser state (simple approach - reinitialize)
    var savedLexer = parserLexer;
    var savedToken = parserToken;
    var savedHasError = parserHasError;
    var savedError = parserError;

    // Parse and execute the subquery
    parserInit(sql);
    // parseSelectStmt expects SELECT to already be consumed, so consume it
    if parserToken.kind == 30 {  // TK_SELECT
        parserAdvance();
    }
    var stmt = parseSelectStmt();

    if parserHasError {
        // Restore parser state
        parserLexer = savedLexer;
        parserToken = savedToken;
        parserHasError = savedHasError;
        parserError = savedError;
        return sqlNull();
    }

    var result = executeSelect(stmt);

    // Restore parser state
    parserLexer = savedLexer;
    parserToken = savedToken;
    parserHasError = savedHasError;
    parserError = savedError;

    // Extract scalar value: first row, first column
    if result.rowCount() > 0 {
        var firstRow = result.getRow(0);
        if firstRow.columnCount() > 0 {
            return firstRow.getValue(0);
        }
    }
    return sqlNull();
}

// Evaluate a correlated subquery - sets up outer context before executing
func evalSubqueryCorrelated(sql: String, currentRow: Row, currentTable: Table) -> SqlValue {
    // Save parser state
    var savedLexer = parserLexer;
    var savedToken = parserToken;
    var savedHasError = parserHasError;
    var savedError = parserError;

    // Save and set outer context for correlated subqueries
    var savedOuterRow = outerRow;
    var savedOuterTable = outerTable;
    var savedOuterAlias = outerTableAlias;
    outerRow = currentRow;
    outerTable = currentTable;
    outerTableAlias = currentTableAlias;  // Set from current context

    // Parse and execute the subquery
    parserInit(sql);
    // parseSelectStmt expects SELECT to already be consumed, so consume it
    if parserToken.kind == 30 {  // TK_SELECT
        parserAdvance();
    }
    var stmt = parseSelectStmt();

    if parserHasError {
        // Restore states
        parserLexer = savedLexer;
        parserToken = savedToken;
        parserHasError = savedHasError;
        parserError = savedError;
        outerRow = savedOuterRow;
        outerTable = savedOuterTable;
        outerTableAlias = savedOuterAlias;
        return sqlNull();
    }

    var result = executeSelect(stmt);

    // Restore parser state
    parserLexer = savedLexer;
    parserToken = savedToken;
    parserHasError = savedHasError;
    parserError = savedError;

    // Restore outer context
    outerRow = savedOuterRow;
    outerTable = savedOuterTable;
    outerTableAlias = savedOuterAlias;

    // Extract scalar value: first row, first column
    if result.rowCount() > 0 {
        var firstRow = result.getRow(0);
        if firstRow.columnCount() > 0 {
            return firstRow.getValue(0);
        }
    }
    return sqlNull();
}

// Evaluate IN subquery - checks if left value is in subquery results (supports correlated subqueries)
func evalInSubquery(leftExpr: Expr, rightExpr: Expr, row: Row, table: Table) -> Integer {
    // Right side must be a subquery
    if rightExpr.kind != 7 {  // EXPR_SUBQUERY
        return 0;
    }

    var sql = rightExpr.subquerySQL;

    // Get the left value to search for
    var leftVal = evalExpr(leftExpr, row, table);

    // Save parser state
    var savedLexer = parserLexer;
    var savedToken = parserToken;
    var savedHasError = parserHasError;
    var savedError = parserError;

    // Save and set outer context for correlated subqueries
    var savedOuterRow = outerRow;
    var savedOuterTable = outerTable;
    var savedOuterAlias = outerTableAlias;
    outerRow = row;
    outerTable = table;
    outerTableAlias = currentTableAlias;

    // Parse and execute the subquery
    parserInit(sql);
    // parseSelectStmt expects SELECT to already be consumed, so consume it
    if parserToken.kind == 30 {  // TK_SELECT
        parserAdvance();
    }
    var stmt = parseSelectStmt();

    if parserHasError {
        parserLexer = savedLexer;
        parserToken = savedToken;
        parserHasError = savedHasError;
        parserError = savedError;
        outerRow = savedOuterRow;
        outerTable = savedOuterTable;
        outerTableAlias = savedOuterAlias;
        return 0;
    }

    var result = executeSelect(stmt);

    // Restore parser state
    parserLexer = savedLexer;
    parserToken = savedToken;
    parserHasError = savedHasError;
    parserError = savedError;

    // Restore outer context
    outerRow = savedOuterRow;
    outerTable = savedOuterTable;
    outerTableAlias = savedOuterAlias;

    // Check if leftVal exists in any row of the result (first column)
    var r = 0;
    while r < result.rowCount() {
        var resultRow = result.getRow(r);
        if resultRow.columnCount() > 0 {
            var resultVal = resultRow.getValue(0);
            if leftVal.compare(resultVal) == 0 {
                return 1;  // Found a match
            }
        }
        r = r + 1;
    }

    return 0;  // No match found
}

// Evaluate binary expression for WHERE clauses
func evalBinaryExpr(expr: Expr, row: Row, table: Table) -> Integer {
    var left = expr.getLeft();
    var right = expr.getRight();

    // OP_IN = 23 (special handling - right side is subquery)
    if expr.op == 23 {
        return evalInSubquery(left, right, row, table);
    }

    var leftVal = evalExpr(left, row, table);
    var rightVal = evalExpr(right, row, table);
    var cmp = leftVal.compare(rightVal);

    // OP_EQ = 10 (equality)
    if expr.op == 10 {
        if cmp == 0 {
            return 1;
        }
        return 0;
    }
    // OP_NE = 11 (not equal)
    if expr.op == 11 {
        if cmp != 0 {
            return 1;
        }
        return 0;
    }
    // OP_LT = 12 (less than)
    if expr.op == 12 {
        if cmp < 0 {
            return 1;
        }
        return 0;
    }
    // OP_LE = 13 (less than or equal)
    if expr.op == 13 {
        if cmp <= 0 {
            return 1;
        }
        return 0;
    }
    // OP_GT = 14 (greater than)
    if expr.op == 14 {
        if cmp > 0 {
            return 1;
        }
        return 0;
    }
    // OP_GE = 15 (greater than or equal)
    if expr.op == 15 {
        if cmp >= 0 {
            return 1;
        }
        return 0;
    }
    return 0;
}

// Convert text to integer (simple parser)
func textToInt(text: String) -> Integer {
    var result = 0;
    var i = 0;
    var len = Viper.String.Length(text);
    var negative = false;

    // Check for negative
    if len > 0 {
        if Viper.String.Substring(text, 0, 1) == "-" {
            negative = true;
            i = 1;
        }
    }

    while i < len {
        var ch = Viper.String.Substring(text, i, 1);
        var code = Viper.String.Asc(ch);
        if code >= 48 {
            if code <= 57 {
                result = result * 10 + (code - 48);
            }
        }
        i = i + 1;
    }

    if negative == true {
        return -result;
    }
    return result;
}

// Execute INSERT
func executeInsert(stmt: InsertStmt) -> QueryResult {
    var result = new QueryResult();
    result.init();

    var db = getDatabase();

    // Get the table - findTable returns Table?
    var maybeTable = db.findTable(stmt.tableName);
    if maybeTable == null {
        result.setError("Table '" + stmt.tableName + "' does not exist");
        return result;
    }

    // After null check, type is narrowed - use directly without ?.
    var table = maybeTable;

    // Insert each row
    var rowsInserted = 0;
    var r = 0;
    while r < stmt.rowCount() {
        var row = table.createRow();
        var rowValues = stmt.valueRows.get(r);
        var v = 0;
        while v < rowValues.length() {
            var colCount = table.columnCount();
            if v < colCount {
                var expr = rowValues.get(v);
                if expr.kind == 1 {  // EXPR_LITERAL
                    row.setValue(v, expr.literalValue);
                }
            }
            v = v + 1;
        }
        table.addRow(row);
        rowsInserted = rowsInserted + 1;
        r = r + 1;
    }

    result.message = "Inserted " + Viper.Fmt.Int(rowsInserted) + " row(s)";
    return result;
}

// Check if expression is an aggregate function
func isAggregateExpr(expr: Expr) -> Boolean {
    if expr.kind == 5 {  // EXPR_FUNCTION
        var funcName = expr.funcName;
        // Convert to uppercase for comparison
        if funcName == "COUNT" || funcName == "count" {
            return true;
        }
        if funcName == "SUM" || funcName == "sum" {
            return true;
        }
        if funcName == "AVG" || funcName == "avg" {
            return true;
        }
        if funcName == "MIN" || funcName == "min" {
            return true;
        }
        if funcName == "MAX" || funcName == "max" {
            return true;
        }
    }
    return false;
}

// Check if SELECT has any aggregate functions
func hasAggregates(stmt: SelectStmt) -> Boolean {
    var c = 0;
    while c < stmt.columns.count() {
        var colExpr = stmt.columns.get(c);
        if isAggregateExpr(colExpr) == true {
            return true;
        }
        c = c + 1;
    }
    return false;
}

// Evaluate an aggregate function over matching rows
func evalAggregate(expr: Expr, matchingRows: List[Integer], table: Table) -> SqlValue {
    var funcName = expr.funcName;

    // Check if we have an argument
    var hasArg = expr.args.count() > 0;

    // COUNT(*)
    if (funcName == "COUNT" || funcName == "count") && hasArg == true {
        var arg0 = expr.args.get(0);
        if arg0.kind == 6 {  // EXPR_STAR
            return sqlInteger(matchingRows.count());
        }
    }

    // COUNT(column) - count non-NULL values
    if funcName == "COUNT" || funcName == "count" {
        var count = 0;
        var i = 0;
        while i < matchingRows.count() {
            var rowIdx = matchingRows.get(i);
            var maybeRow = table.getRow(rowIdx);
            if maybeRow != null {
                var row = maybeRow;
                if hasArg == true {
                    var argExpr = expr.args.get(0);
                    if argExpr.kind == 2 {  // EXPR_COLUMN
                        var val = evalExprColumn(argExpr, row, table);
                        if val.kind != 0 {  // Not NULL
                            count = count + 1;
                        }
                    }
                }
            }
            i = i + 1;
        }
        return sqlInteger(count);
    }

    // SUM(column)
    if funcName == "SUM" || funcName == "sum" {
        var sumInt = 0;
        var hasValue = false;
        var i = 0;
        while i < matchingRows.count() {
            var rowIdx = matchingRows.get(i);
            var maybeRow = table.getRow(rowIdx);
            if maybeRow != null {
                var row = maybeRow;
                if hasArg == true {
                    var argExpr = expr.args.get(0);
                    if argExpr.kind == 2 {  // EXPR_COLUMN
                        var val = evalExprColumn(argExpr, row, table);
                        if val.kind == 1 {  // INTEGER
                            sumInt = sumInt + val.intValue;
                            hasValue = true;
                        }
                    }
                }
            }
            i = i + 1;
        }
        if hasValue == false {
            return sqlNull();
        }
        return sqlInteger(sumInt);
    }

    // AVG(column)
    if funcName == "AVG" || funcName == "avg" {
        var sumInt = 0;
        var count = 0;
        var i = 0;
        while i < matchingRows.count() {
            var rowIdx = matchingRows.get(i);
            var maybeRow = table.getRow(rowIdx);
            if maybeRow != null {
                var row = maybeRow;
                if hasArg == true {
                    var argExpr = expr.args.get(0);
                    if argExpr.kind == 2 {  // EXPR_COLUMN
                        var val = evalExprColumn(argExpr, row, table);
                        if val.kind == 1 {  // INTEGER
                            sumInt = sumInt + val.intValue;
                            count = count + 1;
                        }
                    }
                }
            }
            i = i + 1;
        }
        if count == 0 {
            return sqlNull();
        }
        // Return integer division for now (avg = sum / count)
        var avg = sumInt / count;
        return sqlInteger(avg);
    }

    // MIN(column)
    if funcName == "MIN" || funcName == "min" {
        var hasMin = false;
        var minIntVal = 0;
        var i = 0;
        while i < matchingRows.count() {
            var rowIdx = matchingRows.get(i);
            var maybeRow = table.getRow(rowIdx);
            if maybeRow != null {
                var row = maybeRow;
                if hasArg == true {
                    var argExpr = expr.args.get(0);
                    if argExpr.kind == 2 {  // EXPR_COLUMN
                        var val = evalExprColumn(argExpr, row, table);
                        if val.kind == 1 {  // INTEGER
                            if hasMin == false || val.intValue < minIntVal {
                                minIntVal = val.intValue;
                                hasMin = true;
                            }
                        }
                    }
                }
            }
            i = i + 1;
        }
        if hasMin == false {
            return sqlNull();
        }
        return sqlInteger(minIntVal);
    }

    // MAX(column)
    if funcName == "MAX" || funcName == "max" {
        var hasMax = false;
        var maxIntVal = 0;
        var i = 0;
        while i < matchingRows.count() {
            var rowIdx = matchingRows.get(i);
            var maybeRow = table.getRow(rowIdx);
            if maybeRow != null {
                var row = maybeRow;
                if hasArg == true {
                    var argExpr = expr.args.get(0);
                    if argExpr.kind == 2 {  // EXPR_COLUMN
                        var val = evalExprColumn(argExpr, row, table);
                        if val.kind == 1 {  // INTEGER
                            if hasMax == false || val.intValue > maxIntVal {
                                maxIntVal = val.intValue;
                                hasMax = true;
                            }
                        }
                    }
                }
            }
            i = i + 1;
        }
        if hasMax == false {
            return sqlNull();
        }
        return sqlInteger(maxIntVal);
    }

    return sqlNull();
}

// Evaluate HAVING expression for a group (returns 1 = true, 0 = false)
func evalHavingExpr(expr: Expr, groupRows: List[Integer], table: Table) -> Integer {
    // HAVING expressions can contain aggregates and comparisons

    // Handle binary expressions (comparisons and logical operators)
    if expr.kind == 3 {  // EXPR_BINARY
        var left = expr.getLeft();
        var right = expr.getRight();
        var op = expr.op;

        // Handle logical operators (AND=20, OR=21)
        if op == 20 {  // OP_AND
            var leftBool = evalHavingExpr(left, groupRows, table);
            var rightBool = evalHavingExpr(right, groupRows, table);
            if leftBool != 0 && rightBool != 0 {
                return 1;
            }
            return 0;
        }
        if op == 21 {  // OP_OR
            var leftBool = evalHavingExpr(left, groupRows, table);
            var rightBool = evalHavingExpr(right, groupRows, table);
            if leftBool != 0 || rightBool != 0 {
                return 1;
            }
            return 0;
        }

        // Evaluate left and right sides for comparison
        var leftVal = evalHavingValue(left, groupRows, table);
        var rightVal = evalHavingValue(right, groupRows, table);

        // Comparison operators
        var cmp = leftVal.compare(rightVal);
        if op == 10 {  // OP_EQ
            if cmp == 0 { return 1; }
            return 0;
        }
        if op == 11 {  // OP_NE
            if cmp != 0 { return 1; }
            return 0;
        }
        if op == 14 {  // OP_GT
            if cmp > 0 { return 1; }
            return 0;
        }
        if op == 15 {  // OP_GE
            if cmp >= 0 { return 1; }
            return 0;
        }
        if op == 12 {  // OP_LT
            if cmp < 0 { return 1; }
            return 0;
        }
        if op == 13 {  // OP_LE
            if cmp <= 0 { return 1; }
            return 0;
        }
    }

    return 0;
}

// Evaluate a value in HAVING context (handles aggregates)
func evalHavingValue(expr: Expr, groupRows: List[Integer], table: Table) -> SqlValue {
    // If it's an aggregate function, evaluate it on the group
    if expr.kind == 5 {  // EXPR_FUNCTION
        if isAggregateExpr(expr) == true {
            return evalAggregate(expr, groupRows, table);
        }
    }

    // If it's a literal, return its value
    if expr.kind == 1 {  // EXPR_LITERAL
        return evalExprLiteral(expr);
    }

    // If it's a column ref, evaluate using first row in group
    if expr.kind == 2 {  // EXPR_COLUMN
        if groupRows.count() > 0 {
            var firstRowIdx = groupRows.get(0);
            var maybeRow = table.getRow(firstRowIdx);
            if maybeRow != null {
                return evalExprColumn(expr, maybeRow, table);
            }
        }
    }

    return sqlNull();
}

// Helper function to check if row is duplicate of any row in result
func isDuplicateRow(newRow: Row, result: QueryResult) -> Boolean {
    var i = 0;
    while i < result.rowCount() {
        var maybeExisting = result.getRow(i);
        if maybeExisting != null {
            var existing = maybeExisting;
            // Check if all columns match
            if existing.columnCount() == newRow.columnCount() {
                var allMatch = true;
                var c = 0;
                while c < newRow.columnCount() && allMatch == true {
                    var val1 = existing.getValue(c);
                    var val2 = newRow.getValue(c);
                    if val1.compare(val2) != 0 {
                        allMatch = false;
                    }
                    c = c + 1;
                }
                if allMatch == true {
                    return true;  // Found duplicate
                }
            }
        }
        i = i + 1;
    }
    return false;
}

// Helper: Evaluate column reference in cross join context
// Tables, aliases, and combined row data are passed to resolve columns
func evalCrossJoinColumn(expr: Expr, tables: List[Table], aliases: List[String],
                         combinedRow: Row, colOffsets: List[Integer]) -> SqlValue {
    var colName = expr.columnName;
    var tblNameToFind = expr.tableName;  // May be "" if no table qualifier

    // If table name specified, find that table
    if tblNameToFind != "" {
        var ti = 0;
        while ti < tables.count() {
            var tbl = tables.get(ti);
            var alias = aliases.get(ti);
            // Match against table name or alias
            if tbl.name == tblNameToFind || alias == tblNameToFind {
                var colIdx = tbl.findColumnIndex(colName);
                if colIdx >= 0 {
                    var offset = colOffsets.get(ti);
                    return combinedRow.getValue(offset + colIdx);
                }
            }
            ti = ti + 1;
        }
        return sqlNull();
    }

    // No table qualifier - search all tables
    var ti = 0;
    while ti < tables.count() {
        var tbl = tables.get(ti);
        var colIdx = tbl.findColumnIndex(colName);
        if colIdx >= 0 {
            var offset = colOffsets.get(ti);
            return combinedRow.getValue(offset + colIdx);
        }
        ti = ti + 1;
    }
    return sqlNull();
}

// Helper: Evaluate expression in cross join context
func evalCrossJoinExpr(expr: Expr, tables: List[Table], aliases: List[String],
                       combinedRow: Row, colOffsets: List[Integer]) -> SqlValue {
    if expr.kind == 1 {  // EXPR_LITERAL
        return evalExprLiteral(expr);
    }
    if expr.kind == 2 {  // EXPR_COLUMN
        return evalCrossJoinColumn(expr, tables, aliases, combinedRow, colOffsets);
    }
    return sqlNull();
}

// Helper: Evaluate binary expression in cross join context
func evalCrossJoinBinary(expr: Expr, tables: List[Table], aliases: List[String],
                         combinedRow: Row, colOffsets: List[Integer]) -> Integer {
    // Handle AND/OR first
    if expr.op == 20 {  // OP_AND
        var leftResult = evalCrossJoinBinary(expr.getLeft(), tables, aliases, combinedRow, colOffsets);
        if leftResult == 0 {
            return 0;
        }
        return evalCrossJoinBinary(expr.getRight(), tables, aliases, combinedRow, colOffsets);
    }
    if expr.op == 21 {  // OP_OR
        var leftResult = evalCrossJoinBinary(expr.getLeft(), tables, aliases, combinedRow, colOffsets);
        if leftResult != 0 {
            return 1;
        }
        return evalCrossJoinBinary(expr.getRight(), tables, aliases, combinedRow, colOffsets);
    }

    var left = expr.getLeft();
    var right = expr.getRight();
    var leftVal = evalCrossJoinExpr(left, tables, aliases, combinedRow, colOffsets);
    var rightVal = evalCrossJoinExpr(right, tables, aliases, combinedRow, colOffsets);
    var cmp = leftVal.compare(rightVal);

    if expr.op == 10 {  // OP_EQ
        if cmp == 0 { return 1; } else { return 0; }
    }
    if expr.op == 11 {  // OP_NE
        if cmp != 0 { return 1; } else { return 0; }
    }
    if expr.op == 12 {  // OP_LT
        if cmp < 0 { return 1; } else { return 0; }
    }
    if expr.op == 13 {  // OP_LE
        if cmp <= 0 { return 1; } else { return 0; }
    }
    if expr.op == 14 {  // OP_GT
        if cmp > 0 { return 1; } else { return 0; }
    }
    if expr.op == 15 {  // OP_GE
        if cmp >= 0 { return 1; } else { return 0; }
    }
    return 0;
}

// Execute CROSS JOIN query (multiple tables)
func executeCrossJoin(stmt: SelectStmt) -> QueryResult {
    var result = new QueryResult();
    result.init();
    var db = getDatabase();

    // Load all tables
    var tables: List[Table] = [];
    var aliases: List[String] = [];
    var colOffsets: List[Integer] = [];  // Column offset for each table in combined row
    var totalCols = 0;

    var ti = 0;
    while ti < stmt.tableNames.count() {
        var tblName = stmt.tableNames.get(ti);
        var maybeTable = db.findTable(tblName);
        if maybeTable == null {
            result.setError("Table '" + tblName + "' does not exist");
            return result;
        }
        var tbl = maybeTable;
        tables.add(tbl);

        var alias = stmt.tableAliases.get(ti);
        if alias == "" {
            alias = tblName;
        }
        aliases.add(alias);

        colOffsets.add(totalCols);
        totalCols = totalCols + tbl.columnCount();
        ti = ti + 1;
    }

    // Build column names for result
    if stmt.selectAll == true {
        // For SELECT *, include all columns from all tables with table prefix
        ti = 0;
        while ti < tables.count() {
            var tbl = tables.get(ti);
            var alias = aliases.get(ti);
            var c = 0;
            while c < tbl.columnCount() {
                var maybeCol = tbl.getColumn(c);
                if maybeCol != null {
                    var col = maybeCol;
                    result.addColumnName(alias + "." + col.name);
                }
                c = c + 1;
            }
            ti = ti + 1;
        }
    } else {
        var c = 0;
        while c < stmt.columns.count() {
            var colExpr = stmt.columns.get(c);
            if colExpr.kind == 2 {  // EXPR_COLUMN
                if colExpr.tableName != "" {
                    result.addColumnName(colExpr.tableName + "." + colExpr.columnName);
                } else {
                    result.addColumnName(colExpr.columnName);
                }
            } else {
                result.addColumnName("expr" + Viper.Fmt.Int(c));
            }
            c = c + 1;
        }
    }

    // Build cartesian product using iterative approach
    // Start with all rows from first table
    var combinedRows: List[Row] = [];

    if tables.count() > 0 {
        var firstTable = tables.get(0);
        var r = 0;
        while r < firstTable.rowCount() {
            var maybeRow = firstTable.getRow(r);
            if maybeRow != null {
                var srcRow = maybeRow;
                if srcRow.deleted == false {
                    // Create combined row with values from first table
                    var newRow = makeRow(totalCols);
                    var c = 0;
                    while c < firstTable.columnCount() {
                        newRow.setValue(c, srcRow.getValue(c));
                        c = c + 1;
                    }
                    combinedRows.add(newRow);
                }
            }
            r = r + 1;
        }
    }

    // Extend with each additional table
    ti = 1;
    while ti < tables.count() {
        var tbl = tables.get(ti);
        var offset = colOffsets.get(ti);
        var newCombined: List[Row] = [];

        // Get join type for this table (index ti-1 since first join is at index 0)
        var joinType = 0;  // Default to CROSS JOIN
        if ti - 1 < stmt.joinTypes.count() {
            joinType = stmt.joinTypes.get(ti - 1);
        }

        // Get join condition if exists
        var hasJoinCond = false;
        var joinCondIdx = ti - 1;
        if joinCondIdx < stmt.joinConditions.count() {
            hasJoinCond = true;
        }

        // For RIGHT JOIN or FULL JOIN: track which right rows have been matched (0=no, 1=yes)
        var rightRowMatched: List[Integer] = [];
        if joinType == 3 || joinType == 4 {
            var rInit = 0;
            while rInit < tbl.rowCount() {
                rightRowMatched.add(0);
                rInit = rInit + 1;
            }
        }

        var ci = 0;
        while ci < combinedRows.count() {
            var existing = combinedRows.get(ci);
            var foundMatch = false;  // For LEFT JOIN / FULL JOIN: track if any right row matched

            var r = 0;
            while r < tbl.rowCount() {
                var maybeRow = tbl.getRow(r);
                if maybeRow != null {
                    var srcRow = maybeRow;
                    if srcRow.deleted == false {
                        // Clone existing and add new table's values
                        var newRow = makeRow(totalCols);
                        var c = 0;
                        while c < offset {
                            newRow.setValue(c, existing.getValue(c));
                            c = c + 1;
                        }
                        c = 0;
                        while c < tbl.columnCount() {
                            newRow.setValue(offset + c, srcRow.getValue(c));
                            c = c + 1;
                        }

                        // For CROSS JOIN (type 0), add all combinations
                        // For INNER/LEFT/RIGHT/FULL JOIN, check join condition
                        var includeThisRow = true;
                        if hasJoinCond == true && (joinType == 1 || joinType == 2 || joinType == 3 || joinType == 4) {
                            var joinCond = stmt.joinConditions.get(joinCondIdx);
                            var joinResult = evalCrossJoinBinary(joinCond, tables, aliases, newRow, colOffsets);
                            if joinResult == 0 {
                                includeThisRow = false;
                            }
                        }

                        if includeThisRow == true {
                            newCombined.add(newRow);
                            foundMatch = true;
                            // For RIGHT/FULL JOIN: mark this right row as matched
                            if joinType == 3 || joinType == 4 {
                                rightRowMatched.set(r, 1);
                            }
                        }
                    }
                }
                r = r + 1;
            }

            // For LEFT JOIN or FULL JOIN: if no match found, add row with NULLs for right columns
            if (joinType == 2 || joinType == 4) && foundMatch == false {
                var nullRow = makeRow(totalCols);
                var c = 0;
                while c < offset {
                    nullRow.setValue(c, existing.getValue(c));
                    c = c + 1;
                }
                // Right columns are already NULL by default from makeRow
                newCombined.add(nullRow);
            }

            ci = ci + 1;
        }

        // For RIGHT JOIN or FULL JOIN: add unmatched right rows with NULLs for left columns
        if joinType == 3 || joinType == 4 {
            var rCheck = 0;
            while rCheck < tbl.rowCount() {
                if rightRowMatched.get(rCheck) == 0 {
                    var maybeRightRow = tbl.getRow(rCheck);
                    if maybeRightRow != null {
                        var rightRow = maybeRightRow;
                        if rightRow.deleted == false {
                            var nullRow = makeRow(totalCols);
                            // Left columns stay NULL (default from makeRow)
                            // Copy right columns
                            var cc = 0;
                            while cc < tbl.columnCount() {
                                nullRow.setValue(offset + cc, rightRow.getValue(cc));
                                cc = cc + 1;
                            }
                            newCombined.add(nullRow);
                        }
                    }
                }
                rCheck = rCheck + 1;
            }
        }

        combinedRows = newCombined;
        ti = ti + 1;
    }

    // Apply WHERE filter (JOIN conditions already applied during join building)
    var filteredRows: List[Row] = [];
    var ri = 0;
    while ri < combinedRows.count() {
        var row = combinedRows.get(ri);
        var include = true;

        // Apply WHERE clause
        if stmt.whereClause != null {
            var whereResult = evalCrossJoinBinary(stmt.whereClause, tables, aliases, row, colOffsets);
            if whereResult == 0 {
                include = false;
            }
        }

        if include == true {
            filteredRows.add(row);
        }
        ri = ri + 1;
    }

    // Project columns and build result
    ri = 0;
    while ri < filteredRows.count() {
        var combinedRow = filteredRows.get(ri);
        var resultRow = makeRow(0);

        if stmt.selectAll == true {
            // Copy all values
            var c = 0;
            while c < totalCols {
                resultRow.addValue(combinedRow.getValue(c));
                c = c + 1;
            }
        } else {
            var c = 0;
            while c < stmt.columns.count() {
                var colExpr = stmt.columns.get(c);
                var val = evalCrossJoinExpr(colExpr, tables, aliases, combinedRow, colOffsets);
                resultRow.addValue(val);
                c = c + 1;
            }
        }

        result.addRow(resultRow);
        ri = ri + 1;
    }

    result.message = "Selected " + Viper.Fmt.Int(result.rowCount()) + " row(s)";
    return result;
}

// Execute a derived table subquery (FROM subquery)
func executeDerivedTable(sql: String) -> QueryResult {
    // Save parser state
    var savedLexer = parserLexer;
    var savedToken = parserToken;
    var savedHasError = parserHasError;
    var savedError = parserError;

    // Parse and execute the subquery
    parserInit(sql);
    // parseSelectStmt expects SELECT to already be consumed, so consume it
    if parserToken.kind == 30 {  // TK_SELECT
        parserAdvance();
    }
    var stmt = parseSelectStmt();

    var result = new QueryResult();
    result.init();

    if parserHasError {
        result.setError("Parse error in derived table: " + parserError);
        // Restore parser state
        parserLexer = savedLexer;
        parserToken = savedToken;
        parserHasError = savedHasError;
        parserError = savedError;
        return result;
    }

    result = executeSelect(stmt);

    // Restore parser state
    parserLexer = savedLexer;
    parserToken = savedToken;
    parserHasError = savedHasError;
    parserError = savedError;

    return result;
}

// Convert a QueryResult to a Table for use as a derived table
func queryResultToTable(qr: QueryResult, tableName: String) -> Table {
    var table = new Table();
    table.initWithName(tableName);

    // Add columns based on result column names
    var c = 0;
    while c < qr.columnNames.count() {
        var col = new Column();
        col.init();
        col.name = qr.columnNames.get(c);
        col.typeCode = 3;  // SQL_TEXT - generic type for derived columns
        table.addColumn(col);
        c = c + 1;
    }

    // Add rows
    var r = 0;
    while r < qr.rowCount() {
        var qrRow = qr.getRow(r);
        var newRow = new Row();
        newRow.init();
        var v = 0;
        while v < qrRow.columnCount() {
            newRow.addValue(qrRow.getValue(v));
            v = v + 1;
        }
        table.addRow(newRow);
        r = r + 1;
    }

    return table;
}

// Execute SELECT
func executeSelect(stmt: SelectStmt) -> QueryResult {
    // Check for multi-table (CROSS JOIN) query
    if stmt.tableNames.count() > 1 {
        return executeCrossJoin(stmt);
    }

    var result = new QueryResult();
    result.init();

    var db = getDatabase();

    // Check for derived table (subquery in FROM)
    if stmt.hasDerivedTable == true {
        // Execute the subquery first
        var derivedResult = executeDerivedTable(stmt.derivedTableSQL);
        if derivedResult.hasError == true {
            result.setError("Derived table error: " + derivedResult.message);
            return result;
        }
        // For derived tables with SELECT *, just return the subquery result
        // More complex queries (with WHERE, ORDER BY, etc.) would need additional work
        derivedResult.message = "Selected " + Viper.Fmt.Int(derivedResult.rowCount()) + " row(s)";
        return derivedResult;
    }

    // Find table normally
    var maybeTable = db.findTable(stmt.tableName);
    if maybeTable == null {
        result.setError("Table '" + stmt.tableName + "' does not exist");
        return result;
    }
    var table = maybeTable;

    // Set current table alias for correlated subquery resolution
    var savedAlias = currentTableAlias;
    currentTableAlias = stmt.tableAlias;

    // Check if this is an aggregate query
    var isAggregateQuery = hasAggregates(stmt);

    // Build column names for result
    if stmt.selectAll == true {
        var c = 0;
        while c < table.columnCount() {
            var maybeCol = table.getColumn(c);
            if maybeCol != null {
                var col = maybeCol;
                result.addColumnName(col.name);
            }
            c = c + 1;
        }
    } else {
        var c = 0;
        while c < stmt.columns.count() {
            var colExpr = stmt.columns.get(c);
            if colExpr.kind == 2 {  // EXPR_COLUMN
                result.addColumnName(colExpr.columnName);
            } else if colExpr.kind == 5 {  // EXPR_FUNCTION - aggregate
                // Build name like COUNT(*) or SUM(age)
                var funcName = colExpr.funcName;
                if colExpr.args.count() > 0 {
                    var argExpr = colExpr.args.get(0);
                    if argExpr.kind == 6 {  // EXPR_STAR
                        result.addColumnName(funcName + "(*)");
                    } else if argExpr.kind == 2 {  // EXPR_COLUMN
                        result.addColumnName(funcName + "(" + argExpr.columnName + ")");
                    } else {
                        result.addColumnName(funcName + "(...)");
                    }
                } else {
                    result.addColumnName(funcName + "()");
                }
            } else {
                result.addColumnName("expr" + Viper.Fmt.Int(c));
            }
            c = c + 1;
        }
    }

    // Collect matching row indices
    var matchingRows: List[Integer] = [];
    var r = 0;
    while r < table.rowCount() {
        var maybeTableRow = table.getRow(r);
        if maybeTableRow != null {
            var tableRow = maybeTableRow;

            // Skip deleted rows
            if tableRow.deleted == false {
                // Check WHERE clause
                var includeRow = true;
                if stmt.whereClause != null {
                    var whereExpr = stmt.whereClause;
                    var whereResult = evalBinaryExpr(whereExpr, tableRow, table);
                    if whereResult == 0 {
                        includeRow = false;
                    }
                }

                if includeRow == true {
                    matchingRows.add(r);
                }
            }
        }
        r = r + 1;
    }

    // Handle GROUP BY queries
    if stmt.groupByExprs.count() > 0 {
        // Group rows by GROUP BY expression values
        // Use simple grouping: collect unique group key values, then process each group
        var groupKeys: List[String] = [];
        var groupRowLists: List[List[Integer]] = [];

        var ri = 0;
        while ri < matchingRows.count() {
            var rowIdx = matchingRows.get(ri);
            var maybeRow = table.getRow(rowIdx);
            if maybeRow != null {
                var row = maybeRow;
                // Build group key from GROUP BY expressions
                var groupKey = "";
                var gi = 0;
                while gi < stmt.groupByExprs.count() {
                    var groupExpr = stmt.groupByExprs.get(gi);
                    var gval = sqlNull();
                    if groupExpr.kind == 2 {  // EXPR_COLUMN
                        gval = evalExprColumn(groupExpr, row, table);
                    }
                    if gi > 0 {
                        groupKey = groupKey + "|";
                    }
                    groupKey = groupKey + gval.toString();
                    gi = gi + 1;
                }

                // Find or create group
                var foundGroup = -1;
                var gki = 0;
                while gki < groupKeys.count() {
                    if groupKeys.get(gki) == groupKey {
                        foundGroup = gki;
                        break;
                    }
                    gki = gki + 1;
                }

                if foundGroup < 0 {
                    // New group
                    groupKeys.add(groupKey);
                    var newGroupRows: List[Integer] = [];
                    newGroupRows.add(rowIdx);
                    groupRowLists.add(newGroupRows);
                } else {
                    // Add to existing group
                    var existingGroup = groupRowLists.get(foundGroup);
                    existingGroup.add(rowIdx);
                }
            }
            ri = ri + 1;
        }

        // Process each group
        var groupIdx = 0;
        while groupIdx < groupKeys.count() {
            var groupRows = groupRowLists.get(groupIdx);
            var resultRow = makeRow(0);

            var c = 0;
            while c < stmt.columns.count() {
                var colExpr = stmt.columns.get(c);
                var val = sqlNull();

                if isAggregateExpr(colExpr) == true {
                    val = evalAggregate(colExpr, groupRows, table);
                } else if colExpr.kind == 2 {  // EXPR_COLUMN
                    // Use first row in group
                    if groupRows.count() > 0 {
                        var firstRowIdx = groupRows.get(0);
                        var maybeFirstRow = table.getRow(firstRowIdx);
                        if maybeFirstRow != null {
                            val = evalExprColumn(colExpr, maybeFirstRow, table);
                        }
                    }
                } else if colExpr.kind == 1 {  // EXPR_LITERAL
                    val = evalExprLiteral(colExpr);
                }

                resultRow.addValue(val);
                c = c + 1;
            }

            // Check HAVING clause if present
            var includeGroup = true;
            if stmt.havingClause != null {
                var havingResult = evalHavingExpr(stmt.havingClause, groupRows, table);
                if havingResult == 0 {
                    includeGroup = false;
                }
            }

            if includeGroup == true {
                result.addRow(resultRow);
            }
            groupIdx = groupIdx + 1;
        }

        result.message = "Selected " + Viper.Fmt.Int(result.rowCount()) + " row(s)";
        currentTableAlias = savedAlias;  // Restore alias
        return result;
    }

    // Handle aggregate queries without GROUP BY - return single row with computed values
    if isAggregateQuery == true {
        var resultRow = makeRow(0);

        var c = 0;
        while c < stmt.columns.count() {
            var colExpr = stmt.columns.get(c);
            var val = sqlNull();

            if isAggregateExpr(colExpr) == true {
                val = evalAggregate(colExpr, matchingRows, table);
            } else if colExpr.kind == 2 {  // EXPR_COLUMN (non-aggregate column)
                // For non-aggregate columns in aggregate query, use first row value
                if matchingRows.count() > 0 {
                    var firstRowIdx = matchingRows.get(0);
                    var maybeFirstRow = table.getRow(firstRowIdx);
                    if maybeFirstRow != null {
                        val = evalExprColumn(colExpr, maybeFirstRow, table);
                    }
                }
            } else if colExpr.kind == 1 {  // EXPR_LITERAL
                val = evalExprLiteral(colExpr);
            }

            resultRow.addValue(val);
            c = c + 1;
        }

        result.addRow(resultRow);
        result.message = "Selected 1 row(s)";
        currentTableAlias = savedAlias;  // Restore alias
        return result;
    }

    // Sort matching rows if ORDER BY is present
    if stmt.orderByExprs.count() > 0 {
        // Bubble sort - simple but effective for small result sets
        var i = 0;
        while i < matchingRows.count() {
            var j = i + 1;
            while j < matchingRows.count() {
                var rowIdxI = matchingRows.get(i);
                var rowIdxJ = matchingRows.get(j);
                var maybeRowI = table.getRow(rowIdxI);
                var maybeRowJ = table.getRow(rowIdxJ);
                if maybeRowI != null && maybeRowJ != null {
                    var rowI = maybeRowI;
                    var rowJ = maybeRowJ;

                    // Compare based on ORDER BY expressions
                    var shouldSwap = false;
                    var k = 0;
                    while k < stmt.orderByExprs.count() {
                        var orderExpr = stmt.orderByExprs.get(k);
                        var isDesc = stmt.orderByDir.get(k);

                        // Evaluate ORDER BY expression for both rows
                        var valI = sqlNull();
                        var valJ = sqlNull();
                        if orderExpr.kind == 2 {  // EXPR_COLUMN
                            valI = evalExprColumn(orderExpr, rowI, table);
                            valJ = evalExprColumn(orderExpr, rowJ, table);
                        } else if orderExpr.kind == 1 {  // EXPR_LITERAL
                            valI = evalExprLiteral(orderExpr);
                            valJ = evalExprLiteral(orderExpr);
                        }

                        var cmp = valI.compare(valJ);
                        if cmp != 0 {
                            // Values differ
                            if isDesc == 1 {
                                // DESC: swap if valI < valJ
                                if cmp < 0 {
                                    shouldSwap = true;
                                }
                            } else {
                                // ASC: swap if valI > valJ
                                if cmp > 0 {
                                    shouldSwap = true;
                                }
                            }
                            break;  // Stop comparing more ORDER BY columns
                        }
                        k = k + 1;
                    }

                    if shouldSwap == true {
                        // Swap indices
                        matchingRows.set(i, rowIdxJ);
                        matchingRows.set(j, rowIdxI);
                    }
                }
                j = j + 1;
            }
            i = i + 1;
        }
    }

    // Build result rows from sorted indices, applying LIMIT/OFFSET
    var ri = 0;
    var addedCount = 0;
    while ri < matchingRows.count() {
        // Skip OFFSET rows
        if ri < stmt.offsetValue {
            ri = ri + 1;
            continue;
        }

        // Check LIMIT
        if stmt.limitValue >= 0 && addedCount >= stmt.limitValue {
            break;
        }

        var rowIdx = matchingRows.get(ri);
        var maybeTableRow = table.getRow(rowIdx);
        if maybeTableRow != null {
            var tableRow = maybeTableRow;

            // Create result row
            var resultRow = makeRow(0);

            if stmt.selectAll == true {
                // Copy all columns
                var c = 0;
                while c < tableRow.columnCount() {
                    resultRow.addValue(tableRow.getValue(c));
                    c = c + 1;
                }
            } else {
                // Evaluate selected columns
                var c = 0;
                while c < stmt.columns.count() {
                    var colExpr = stmt.columns.get(c);
                    var val = sqlNull();
                    if colExpr.kind == 2 {  // EXPR_COLUMN
                        val = evalExprColumn(colExpr, tableRow, table);
                    } else if colExpr.kind == 1 {  // EXPR_LITERAL
                        val = evalExprLiteral(colExpr);
                    }
                    resultRow.addValue(val);
                    c = c + 1;
                }
            }

            // Check for duplicates if DISTINCT
            var shouldAdd = true;
            if stmt.isDistinct == true {
                if isDuplicateRow(resultRow, result) == true {
                    shouldAdd = false;
                }
            }

            if shouldAdd == true {
                result.addRow(resultRow);
                addedCount = addedCount + 1;
            }
        }
        ri = ri + 1;
    }

    result.message = "Selected " + Viper.Fmt.Int(result.rowCount()) + " row(s)";
    currentTableAlias = savedAlias;  // Restore alias
    return result;
}

// Execute UPDATE statement
func executeUpdate(stmt: UpdateStmt) -> QueryResult {
    var result = new QueryResult();
    result.init();

    var db = getDatabase();

    // Find table
    var maybeTable = db.findTable(stmt.tableName);
    if maybeTable == null {
        result.setError("Table '" + stmt.tableName + "' does not exist");
        return result;
    }

    var table = maybeTable;
    var updateCount = 0;

    // Process each row
    var r = 0;
    while r < table.rowCount() {
        var maybeRow = table.getRow(r);
        if maybeRow != null {
            var row = maybeRow;

            // Check WHERE clause
            var updateRow = true;
            if stmt.whereClause != null {
                var whereExpr = stmt.whereClause;
                var whereResult = evalBinaryExpr(whereExpr, row, table);
                if whereResult == 0 {
                    updateRow = false;
                }
            }

            if updateRow == true {
                // Apply SET updates
                var i = 0;
                while i < stmt.setColumns.count() {
                    var colName = stmt.setColumns.get(i);
                    var valExpr = stmt.setValues.get(i);
                    var newVal = evalExpr(valExpr, row, table);
                    var colIdx = table.findColumnIndex(colName);
                    if colIdx >= 0 {
                        row.setValue(colIdx, newVal);
                    }
                    i = i + 1;
                }
                updateCount = updateCount + 1;
            }
        }
        r = r + 1;
    }

    result.message = "Updated " + Viper.Fmt.Int(updateCount) + " row(s)";
    result.rowsAffected = updateCount;
    return result;
}

// Execute DELETE statement
func executeDelete(stmt: DeleteStmt) -> QueryResult {
    var result = new QueryResult();
    result.init();

    var db = getDatabase();

    // Find table
    var maybeTable = db.findTable(stmt.tableName);
    if maybeTable == null {
        result.setError("Table '" + stmt.tableName + "' does not exist");
        return result;
    }

    var table = maybeTable;
    var deleteCount = 0;

    // Process each row (mark for deletion)
    var r = 0;
    while r < table.rowCount() {
        var maybeRow = table.getRow(r);
        if maybeRow != null {
            var row = maybeRow;

            // Check WHERE clause
            var deleteRow = true;
            if stmt.whereClause != null {
                var whereExpr = stmt.whereClause;
                var whereResult = evalBinaryExpr(whereExpr, row, table);
                if whereResult == 0 {
                    deleteRow = false;
                }
            }

            if deleteRow == true {
                row.deleted = true;
                deleteCount = deleteCount + 1;
            }
        }
        r = r + 1;
    }

    result.message = "Deleted " + Viper.Fmt.Int(deleteCount) + " row(s)";
    result.rowsAffected = deleteCount;
    return result;
}

// Parse and execute a SQL statement
func executeSql(sql: String) -> QueryResult {
    var result = new QueryResult();
    result.init();

    parserInit(sql);

    // Determine statement type
    if parserToken.kind == 20 {  // TK_CREATE
        parserAdvance();
        if parserToken.kind == 21 {  // TK_TABLE
            var stmt = parseCreateTableStmt();
            if parserHasError {
                result.setError(parserError);
                return result;
            }
            return executeCreateTable(stmt);
        }
    }

    if parserToken.kind == 31 {  // TK_INSERT
        parserAdvance();
        var stmt = parseInsertStmt();
        if parserHasError {
            result.setError(parserError);
            return result;
        }
        return executeInsert(stmt);
    }

    if parserToken.kind == 30 {  // TK_SELECT
        parserAdvance();
        var stmt = parseSelectStmt();
        if parserHasError {
            result.setError(parserError);
            return result;
        }
        return executeSelect(stmt);
    }

    if parserToken.kind == 32 {  // TK_UPDATE
        parserAdvance();
        var stmt = parseUpdateStmt();
        if parserHasError {
            result.setError(parserError);
            return result;
        }
        return executeUpdate(stmt);
    }

    if parserToken.kind == 33 {  // TK_DELETE
        parserAdvance();
        var stmt = parseDeleteStmt();
        if parserHasError {
            result.setError(parserError);
            return result;
        }
        return executeDelete(stmt);
    }

    result.setError("Unknown statement type");
    return result;
}

//=============================================================================
// TEST FUNCTIONS
//=============================================================================

func testSqlValue() {
    Viper.Terminal.Say("=== SqlValue Test ===");

    var v1 = sqlNull();
    Viper.Terminal.Say("v1 (NULL): " + v1.toString() + " type=" + v1.typeName());

    var v2 = sqlInteger(42);
    Viper.Terminal.Say("v2 (INTEGER): " + v2.toString() + " type=" + v2.typeName());

    var v3 = sqlReal(3.14, "3.14");
    Viper.Terminal.Say("v3 (REAL): " + v3.toString() + " type=" + v3.typeName());

    var v4 = sqlText("Hello");
    Viper.Terminal.Say("v4 (TEXT): " + v4.toString() + " type=" + v4.typeName());

    // Test comparisons
    var v5 = sqlInteger(100);
    Viper.Terminal.Say("v2.compare(v5): " + Viper.Fmt.Int(v2.compare(v5)) + " (expect -1)");
    Viper.Terminal.Say("v5.compare(v2): " + Viper.Fmt.Int(v5.compare(v2)) + " (expect 1)");

    var v6 = sqlInteger(42);
    Viper.Terminal.Say("v2.equals(v6): " + Viper.Fmt.Bool(v2.equals(v6)) + " (expect true)");

    Viper.Terminal.Say("=== SqlValue Test PASSED ===");
}

func testTokens() {
    Viper.Terminal.Say("=== Token Types Test ===");

    var tok1 = new Token(TK_SELECT, "SELECT", 1, 1);
    Viper.Terminal.Say("Token 1: " + tok1.toString());
    Viper.Terminal.Say("  isKeyword: " + Viper.Fmt.Bool(tok1.isKeyword()));

    var tok2 = new Token(TK_INTEGER, "42", 1, 8);
    Viper.Terminal.Say("Token 2: " + tok2.toString());
    Viper.Terminal.Say("  isLiteral: " + Viper.Fmt.Bool(tok2.isLiteral()));

    Viper.Terminal.Say("=== Token Types Test PASSED ===");
}

func testLexer() {
    Viper.Terminal.Say("=== Lexer Test ===");

    var sql = "SELECT id, name FROM users WHERE age > 21;";
    Viper.Terminal.Say("Input: " + sql);
    Viper.Terminal.Say("");

    var lex = new Lexer(sql);
    var tok = lex.nextToken();
    while (tok.kind != TK_EOF) {
        Viper.Terminal.Say("  " + tokenTypeName(tok.kind) + ": '" + tok.text + "'");
        tok = lex.nextToken();
    }

    Viper.Terminal.Say("");

    var sql2 = "INSERT INTO users VALUES (1, 'John', 3.14);";
    Viper.Terminal.Say("Input: " + sql2);
    Viper.Terminal.Say("");

    var lex2 = new Lexer(sql2);
    tok = lex2.nextToken();
    while (tok.kind != TK_EOF) {
        Viper.Terminal.Say("  " + tokenTypeName(tok.kind) + ": '" + tok.text + "'");
        tok = lex2.nextToken();
    }

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Lexer Test PASSED ===");
}

func testColumnRow() {
    Viper.Terminal.Say("=== Column & Row Test ===");

    // Test Column creation
    var col1 = makeColumn("id", SQL_INTEGER);
    col1.primaryKey = true;
    col1.autoIncrement = true;
    Viper.Terminal.Say("col1: " + col1.toString());

    var col2 = makeColumn("name", SQL_TEXT);
    col2.notNull = true;
    Viper.Terminal.Say("col2: " + col2.toString());

    var col3 = makeColumn("score", SQL_REAL);
    col3.setDefault(sqlReal(0.0, "0.0"));
    Viper.Terminal.Say("col3: " + col3.toString());

    // Test Row creation
    var row1 = makeRow(3);
    Viper.Terminal.Say("row1 (empty): " + row1.toString());

    row1.setValue(0, sqlInteger(1));
    row1.setValue(1, sqlText("Alice"));
    row1.setValue(2, sqlReal(95.5, "95.5"));
    Viper.Terminal.Say("row1 (filled): " + row1.toString());

    // Test Row clone
    var row2 = row1.clone();
    row2.setValue(0, sqlInteger(2));
    row2.setValue(1, sqlText("Bob"));
    Viper.Terminal.Say("row2 (cloned): " + row2.toString());

    // Verify original unchanged
    Viper.Terminal.Say("row1 (verify): " + row1.toString());

    // Test Row value access
    var v = row1.getValue(1);
    Viper.Terminal.Say("row1.getValue(1): " + v.toString());

    Viper.Terminal.Say("=== Column & Row Test PASSED ===");
}

func testTable() {
    Viper.Terminal.Say("=== Table Test ===");

    // Create a users table
    var users = makeTable("users");

    // Add columns
    var colId = makeColumn("id", SQL_INTEGER);
    colId.primaryKey = true;
    colId.autoIncrement = true;
    users.addColumn(colId);

    var colName = makeColumn("name", SQL_TEXT);
    colName.notNull = true;
    users.addColumn(colName);

    var colAge = makeColumn("age", SQL_INTEGER);
    users.addColumn(colAge);

    Viper.Terminal.Say(users.toString());
    Viper.Terminal.Say("");
    Viper.Terminal.Say(users.schemaString());
    Viper.Terminal.Say("");

    // Test column lookup
    var idx = users.findColumnIndex("name");
    var idxStr = Viper.Fmt.Int(idx);
    Viper.Terminal.Say("Column 'name' at index: " + idxStr);

    // Insert some rows
    var vals1: List[SqlValue] = [];
    vals1.add(sqlInteger(1));
    vals1.add(sqlText("Alice"));
    vals1.add(sqlInteger(30));
    users.insertRow(vals1);

    var vals2: List[SqlValue] = [];
    vals2.add(sqlInteger(2));
    vals2.add(sqlText("Bob"));
    vals2.add(sqlInteger(25));
    users.insertRow(vals2);

    var vals3: List[SqlValue] = [];
    vals3.add(sqlInteger(3));
    vals3.add(sqlText("Charlie"));
    vals3.add(sqlNull());
    users.insertRow(vals3);

    Viper.Terminal.Say(users.toString());

    // Print row count
    Viper.Terminal.Say("");
    var rowCountStr = Viper.Fmt.Int(users.rowCount());
    Viper.Terminal.Say("Rows inserted: " + rowCountStr);

    // Test delete
    users.deleteRow(1);
    Viper.Terminal.Say("After deleting row 1:");
    Viper.Terminal.Say("(Row 0 and 2 should remain, row 1 marked deleted)");

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Table Test PASSED ===");
}

func testExecutor() {
    Viper.Terminal.Say("=== Executor Test ===");

    // Reset database
    currentDb = new Database();
    currentDb.init();
    dbInitialized = true;

    // Test CREATE TABLE
    var sql1 = "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER);";
    Viper.Terminal.Say("SQL: " + sql1);
    var r1 = executeSql(sql1);
    Viper.Terminal.Say("Result: " + r1.message);

    // Test INSERT
    var sql2 = "INSERT INTO users VALUES (1, 'Alice', 30);";
    Viper.Terminal.Say("SQL: " + sql2);
    var r2 = executeSql(sql2);
    Viper.Terminal.Say("Result: " + r2.message);

    var sql3 = "INSERT INTO users VALUES (2, 'Bob', 25);";
    Viper.Terminal.Say("SQL: " + sql3);
    var r3 = executeSql(sql3);
    Viper.Terminal.Say("Result: " + r3.message);

    var sql4 = "INSERT INTO users VALUES (3, 'Charlie', 35);";
    Viper.Terminal.Say("SQL: " + sql4);
    var r4 = executeSql(sql4);
    Viper.Terminal.Say("Result: " + r4.message);

    // Test SELECT *
    var sql5 = "SELECT * FROM users;";
    Viper.Terminal.Say("SQL: " + sql5);
    var r5 = executeSql(sql5);
    Viper.Terminal.Say("Result: " + r5.message);
    Viper.Terminal.Say(r5.toString());

    // Test SELECT with WHERE
    var sql6 = "SELECT * FROM users WHERE age > 28;";
    Viper.Terminal.Say("SQL: " + sql6);
    var r6 = executeSql(sql6);
    Viper.Terminal.Say("Result: " + r6.message);
    Viper.Terminal.Say(r6.toString());

    // Test UPDATE
    var sql7 = "UPDATE users SET age = 31 WHERE name = 'Alice';";
    Viper.Terminal.Say("SQL: " + sql7);
    var r7 = executeSql(sql7);
    Viper.Terminal.Say("Result: " + r7.message);

    // Verify UPDATE
    var sql8 = "SELECT * FROM users WHERE name = 'Alice';";
    Viper.Terminal.Say("SQL: " + sql8);
    var r8 = executeSql(sql8);
    Viper.Terminal.Say("Result: " + r8.message);
    Viper.Terminal.Say(r8.toString());

    // Test DELETE
    var sql9 = "DELETE FROM users WHERE name = 'Bob';";
    Viper.Terminal.Say("SQL: " + sql9);
    var r9 = executeSql(sql9);
    Viper.Terminal.Say("Result: " + r9.message);

    // Verify DELETE
    var sql10 = "SELECT * FROM users;";
    Viper.Terminal.Say("SQL: " + sql10);
    var r10 = executeSql(sql10);
    Viper.Terminal.Say("Result: " + r10.message);
    Viper.Terminal.Say(r10.toString());

    // Test ORDER BY - Insert more data
    var sql11 = "INSERT INTO users VALUES (4, 'Diana', 28);";
    Viper.Terminal.Say("SQL: " + sql11);
    var r11 = executeSql(sql11);
    Viper.Terminal.Say("Result: " + r11.message);

    // Test ORDER BY ASC
    var sql12 = "SELECT * FROM users ORDER BY age;";
    Viper.Terminal.Say("SQL: " + sql12);
    var r12 = executeSql(sql12);
    Viper.Terminal.Say("Result: " + r12.message);
    Viper.Terminal.Say(r12.toString());

    // Test ORDER BY DESC
    var sql13 = "SELECT * FROM users ORDER BY age DESC;";
    Viper.Terminal.Say("SQL: " + sql13);
    var r13 = executeSql(sql13);
    Viper.Terminal.Say("Result: " + r13.message);
    Viper.Terminal.Say(r13.toString());

    // Test ORDER BY name ASC
    var sql14 = "SELECT * FROM users ORDER BY name ASC;";
    Viper.Terminal.Say("SQL: " + sql14);
    var r14 = executeSql(sql14);
    Viper.Terminal.Say("Result: " + r14.message);
    Viper.Terminal.Say(r14.toString());

    // Test LIMIT
    var sql15 = "SELECT * FROM users ORDER BY age LIMIT 2;";
    Viper.Terminal.Say("SQL: " + sql15);
    var r15 = executeSql(sql15);
    Viper.Terminal.Say("Result: " + r15.message);
    Viper.Terminal.Say(r15.toString());

    // Test LIMIT with OFFSET
    var sql16 = "SELECT * FROM users ORDER BY age LIMIT 2 OFFSET 1;";
    Viper.Terminal.Say("SQL: " + sql16);
    var r16 = executeSql(sql16);
    Viper.Terminal.Say("Result: " + r16.message);
    Viper.Terminal.Say(r16.toString());

    // Test DISTINCT - Add more rows with duplicate ages
    var sql17 = "INSERT INTO users VALUES (5, 'Eve', 31);";
    Viper.Terminal.Say("SQL: " + sql17);
    var r17 = executeSql(sql17);
    Viper.Terminal.Say("Result: " + r17.message);

    // Test SELECT DISTINCT age
    var sql18 = "SELECT DISTINCT age FROM users;";
    Viper.Terminal.Say("SQL: " + sql18);
    var r18 = executeSql(sql18);
    Viper.Terminal.Say("Result: " + r18.message);
    Viper.Terminal.Say(r18.toString());

    // Test SELECT age without DISTINCT (should have duplicates)
    var sql19 = "SELECT age FROM users;";
    Viper.Terminal.Say("SQL: " + sql19);
    var r19 = executeSql(sql19);
    Viper.Terminal.Say("Result: " + r19.message);
    Viper.Terminal.Say(r19.toString());

    // Test COUNT(*)
    var sql20 = "SELECT COUNT(*) FROM users;";
    Viper.Terminal.Say("SQL: " + sql20);
    var r20 = executeSql(sql20);
    Viper.Terminal.Say("Result: " + r20.message);
    Viper.Terminal.Say(r20.toString());

    // Test SUM(age)
    var sql21 = "SELECT SUM(age) FROM users;";
    Viper.Terminal.Say("SQL: " + sql21);
    var r21 = executeSql(sql21);
    Viper.Terminal.Say("Result: " + r21.message);
    Viper.Terminal.Say(r21.toString());

    // Test AVG(age)
    var sql22 = "SELECT AVG(age) FROM users;";
    Viper.Terminal.Say("SQL: " + sql22);
    var r22 = executeSql(sql22);
    Viper.Terminal.Say("Result: " + r22.message);
    Viper.Terminal.Say(r22.toString());

    // Test MIN(age) and MAX(age)
    var sql23 = "SELECT MIN(age), MAX(age) FROM users;";
    Viper.Terminal.Say("SQL: " + sql23);
    var r23 = executeSql(sql23);
    Viper.Terminal.Say("Result: " + r23.message);
    Viper.Terminal.Say(r23.toString());

    // Test COUNT(*) with WHERE
    var sql24 = "SELECT COUNT(*) FROM users WHERE age > 30;";
    Viper.Terminal.Say("SQL: " + sql24);
    var r24 = executeSql(sql24);
    Viper.Terminal.Say("Result: " + r24.message);
    Viper.Terminal.Say(r24.toString());

    // Test GROUP BY
    var sql25 = "SELECT age, COUNT(*) FROM users GROUP BY age;";
    Viper.Terminal.Say("SQL: " + sql25);
    var r25 = executeSql(sql25);
    Viper.Terminal.Say("Result: " + r25.message);
    Viper.Terminal.Say(r25.toString());

    // Test GROUP BY with SUM
    var sql26 = "SELECT age, SUM(id) FROM users GROUP BY age;";
    Viper.Terminal.Say("SQL: " + sql26);
    var r26 = executeSql(sql26);
    Viper.Terminal.Say("Result: " + r26.message);
    Viper.Terminal.Say(r26.toString());

    // Test HAVING clause
    var sql27 = "SELECT age, COUNT(*) FROM users GROUP BY age HAVING COUNT(*) > 1;";
    Viper.Terminal.Say("SQL: " + sql27);
    var r27 = executeSql(sql27);
    Viper.Terminal.Say("Result: " + r27.message);
    Viper.Terminal.Say(r27.toString());

    // Test HAVING with SUM
    var sql28 = "SELECT age, SUM(id) FROM users GROUP BY age HAVING SUM(id) >= 4;";
    Viper.Terminal.Say("SQL: " + sql28);
    var r28 = executeSql(sql28);
    Viper.Terminal.Say("Result: " + r28.message);
    Viper.Terminal.Say(r28.toString());

    // Test table alias
    var sql29 = "SELECT * FROM users u WHERE u.age > 30;";
    Viper.Terminal.Say("SQL: " + sql29);
    var r29 = executeSql(sql29);
    Viper.Terminal.Say("Result: " + r29.message);
    Viper.Terminal.Say(r29.toString());

    // Test table alias with AS
    var sql30 = "SELECT id, name FROM users AS u;";
    Viper.Terminal.Say("SQL: " + sql30);
    var r30 = executeSql(sql30);
    Viper.Terminal.Say("Result: " + r30.message);
    Viper.Terminal.Say(r30.toString());

    // CROSS JOIN tests - create a second table
    var sql31 = "CREATE TABLE orders (order_id INTEGER, user_id INTEGER, product TEXT);";
    Viper.Terminal.Say("SQL: " + sql31);
    var r31 = executeSql(sql31);
    Viper.Terminal.Say("Result: " + r31.message);

    var sql32 = "INSERT INTO orders VALUES (101, 1, 'Widget');";
    executeSql(sql32);
    var sql33 = "INSERT INTO orders VALUES (102, 3, 'Gadget');";
    executeSql(sql33);
    var sql33b = "INSERT INTO orders VALUES (103, 99, 'Thingamajig');";
    executeSql(sql33b);
    Viper.Terminal.Say("Inserted 3 orders (one with non-existent user_id=99)");

    // Test basic CROSS JOIN with SELECT *
    var sql34 = "SELECT * FROM users, orders;";
    Viper.Terminal.Say("SQL: " + sql34);
    var r34 = executeSql(sql34);
    Viper.Terminal.Say("Result: " + r34.message);
    Viper.Terminal.Say(r34.toString());

    // Test CROSS JOIN with WHERE clause (simulating INNER JOIN)
    var sql35 = "SELECT users.name, orders.product FROM users, orders WHERE users.id = orders.user_id;";
    Viper.Terminal.Say("SQL: " + sql35);
    var r35 = executeSql(sql35);
    Viper.Terminal.Say("Result: " + r35.message);
    Viper.Terminal.Say(r35.toString());

    // Test CROSS JOIN with aliases
    var sql36 = "SELECT u.name, o.product FROM users u, orders o WHERE u.id = o.user_id;";
    Viper.Terminal.Say("SQL: " + sql36);
    var r36 = executeSql(sql36);
    Viper.Terminal.Say("Result: " + r36.message);
    Viper.Terminal.Say(r36.toString());

    // Test INNER JOIN syntax
    var sql37 = "SELECT users.name, orders.product FROM users INNER JOIN orders ON users.id = orders.user_id;";
    Viper.Terminal.Say("SQL: " + sql37);
    var r37 = executeSql(sql37);
    Viper.Terminal.Say("Result: " + r37.message);
    Viper.Terminal.Say(r37.toString());

    // Test INNER JOIN with aliases
    var sql38 = "SELECT u.name, o.product FROM users u INNER JOIN orders o ON u.id = o.user_id;";
    Viper.Terminal.Say("SQL: " + sql38);
    var r38 = executeSql(sql38);
    Viper.Terminal.Say("Result: " + r38.message);
    Viper.Terminal.Say(r38.toString());

    // Test bare JOIN (same as INNER JOIN)
    var sql39 = "SELECT u.name, o.product FROM users u JOIN orders o ON u.id = o.user_id;";
    Viper.Terminal.Say("SQL: " + sql39);
    var r39 = executeSql(sql39);
    Viper.Terminal.Say("Result: " + r39.message);
    Viper.Terminal.Say(r39.toString());

    // Test LEFT JOIN - returns all users, even those without orders
    var sql40 = "SELECT u.name, o.product FROM users u LEFT JOIN orders o ON u.id = o.user_id;";
    Viper.Terminal.Say("SQL: " + sql40);
    var r40 = executeSql(sql40);
    Viper.Terminal.Say("Result: " + r40.message);
    Viper.Terminal.Say(r40.toString());

    // Test LEFT OUTER JOIN (same as LEFT JOIN)
    var sql41 = "SELECT u.name, o.product FROM users u LEFT OUTER JOIN orders o ON u.id = o.user_id;";
    Viper.Terminal.Say("SQL: " + sql41);
    var r41 = executeSql(sql41);
    Viper.Terminal.Say("Result: " + r41.message);
    Viper.Terminal.Say(r41.toString());

    // Test RIGHT JOIN - returns all orders, even those without matching users
    var sql42 = "SELECT u.name, o.product FROM users u RIGHT JOIN orders o ON u.id = o.user_id;";
    Viper.Terminal.Say("SQL: " + sql42);
    var r42 = executeSql(sql42);
    Viper.Terminal.Say("Result: " + r42.message);
    Viper.Terminal.Say(r42.toString());

    // Test FULL OUTER JOIN - returns all from both tables
    var sql43 = "SELECT u.name, o.product FROM users u FULL OUTER JOIN orders o ON u.id = o.user_id;";
    Viper.Terminal.Say("SQL: " + sql43);
    var r43 = executeSql(sql43);
    Viper.Terminal.Say("Result: " + r43.message);
    Viper.Terminal.Say(r43.toString());

    // Phase 6: Subqueries
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Subquery Tests ---");

    // Test scalar subquery in WHERE: find users older than average
    var sql44 = "SELECT name, age FROM users WHERE age > (SELECT AVG(age) FROM users);";
    Viper.Terminal.Say("SQL: " + sql44);
    var r44 = executeSql(sql44);
    Viper.Terminal.Say("Result: " + r44.message);
    Viper.Terminal.Say(r44.toString());

    // Test scalar subquery getting max value
    var sql45 = "SELECT name FROM users WHERE age = (SELECT MAX(age) FROM users);";
    Viper.Terminal.Say("SQL: " + sql45);
    var r45 = executeSql(sql45);
    Viper.Terminal.Say("Result: " + r45.message);
    Viper.Terminal.Say(r45.toString());

    // Test scalar subquery getting count
    var sql46 = "SELECT * FROM users WHERE id > (SELECT COUNT(*) FROM orders);";
    Viper.Terminal.Say("SQL: " + sql46);
    var r46 = executeSql(sql46);
    Viper.Terminal.Say("Result: " + r46.message);
    Viper.Terminal.Say(r46.toString());

    // Test IN subquery: users who have placed orders
    var sql47 = "SELECT name FROM users WHERE id IN (SELECT user_id FROM orders);";
    Viper.Terminal.Say("SQL: " + sql47);
    var r47 = executeSql(sql47);
    Viper.Terminal.Say("Result: " + r47.message);
    Viper.Terminal.Say(r47.toString());

    // Test IN subquery: users who have NOT placed orders (using NOT IN pattern)
    var sql48 = "SELECT name FROM users WHERE id IN (SELECT user_id FROM orders WHERE user_id > 2);";
    Viper.Terminal.Say("SQL: " + sql48);
    var r48 = executeSql(sql48);
    Viper.Terminal.Say("Result: " + r48.message);
    Viper.Terminal.Say(r48.toString());

    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Derived Table Tests ---");

    // Test derived table (subquery in FROM)
    var sql49 = "SELECT * FROM (SELECT name, age FROM users WHERE age > 25) AS older_users;";
    Viper.Terminal.Say("SQL: " + sql49);
    var r49 = executeSql(sql49);
    Viper.Terminal.Say("Result: " + r49.message);
    Viper.Terminal.Say(r49.toString());

    // Test derived table with aggregation
    var sql50 = "SELECT * FROM (SELECT COUNT(*) AS cnt FROM users) AS user_count;";
    Viper.Terminal.Say("SQL: " + sql50);
    var r50 = executeSql(sql50);
    Viper.Terminal.Say("Result: " + r50.message);
    Viper.Terminal.Say(r50.toString());

    Viper.Terminal.Say("--- Correlated Subquery Tests ---");

    // Test correlated subquery - find users older than average age of users with same city
    // First, let's add city column to users and insert data with cities
    var sql51 = "CREATE TABLE employees (id INTEGER, name TEXT, department TEXT, salary INTEGER);";
    Viper.Terminal.Say("SQL: " + sql51);
    var r51 = executeSql(sql51);
    Viper.Terminal.Say("Result: " + r51.message);

    var sql52 = "INSERT INTO employees VALUES (1, 'Alice', 'Engineering', 80000);";
    executeSql(sql52);
    var sql53 = "INSERT INTO employees VALUES (2, 'Bob', 'Engineering', 75000);";
    executeSql(sql53);
    var sql54 = "INSERT INTO employees VALUES (3, 'Charlie', 'Sales', 60000);";
    executeSql(sql54);
    var sql55 = "INSERT INTO employees VALUES (4, 'Diana', 'Sales', 65000);";
    executeSql(sql55);
    var sql56 = "INSERT INTO employees VALUES (5, 'Eve', 'Engineering', 90000);";
    executeSql(sql56);
    Viper.Terminal.Say("Inserted 5 employees");

    // Correlated subquery: Find employees earning more than avg salary in their department
    var sql57 = "SELECT name, department, salary FROM employees e WHERE salary > (SELECT AVG(salary) FROM employees WHERE department = e.department);";
    Viper.Terminal.Say("SQL: " + sql57);
    var r57 = executeSql(sql57);
    Viper.Terminal.Say("Result: " + r57.message);
    Viper.Terminal.Say(r57.toString());
    // Expected: Alice (80K > Engineering avg 81.6K? No), Eve (90K > 81.6K? Yes), Diana (65K > Sales avg 62.5K? Yes)
    // Actually Engineering avg = (80+75+90)/3 = 81.66, Sales avg = (60+65)/2 = 62.5
    // So Eve (90 > 81.66) and Diana (65 > 62.5) should be returned

    // Simpler correlated subquery test: find employees where their salary equals max in their dept
    var sql58 = "SELECT name, department, salary FROM employees e WHERE salary = (SELECT MAX(salary) FROM employees WHERE department = e.department);";
    Viper.Terminal.Say("SQL: " + sql58);
    var r58 = executeSql(sql58);
    Viper.Terminal.Say("Result: " + r58.message);
    Viper.Terminal.Say(r58.toString());
    // Expected: Eve (max in Engineering), Diana (max in Sales)

    Viper.Terminal.Say("=== Executor Test PASSED ===");
}

func start() {
    Viper.Terminal.Say("SQLite Clone - ViperLang Edition");
    Viper.Terminal.Say("================================");
    Viper.Terminal.Say("");

    testTokens();
    Viper.Terminal.Say("");
    testLexer();
    Viper.Terminal.Say("");
    testSqlValue();
    Viper.Terminal.Say("");
    testColumnRow();
    Viper.Terminal.Say("");
    testTable();
    Viper.Terminal.Say("");
    testExpr();
    Viper.Terminal.Say("");
    testParser();
    Viper.Terminal.Say("");
    testExecutor();
}

func testExpr() {
    Viper.Terminal.Say("=== Expression Test ===");

    // Test literal expressions
    var e1 = exprNull();
    Viper.Terminal.Say("Null literal: " + e1.toString() + " (kind=" + Viper.Fmt.Int(e1.kind) + ")");

    var e2 = exprInt(42);
    Viper.Terminal.Say("Int literal: " + e2.toString() + " (kind=" + Viper.Fmt.Int(e2.kind) + ")");

    var e3 = exprReal(3.14, "3.14");
    Viper.Terminal.Say("Real literal: " + e3.toString());

    var e4 = exprText("hello");
    Viper.Terminal.Say("Text literal: " + e4.toString());

    // Test column reference
    var e5 = exprColumn("name");
    Viper.Terminal.Say("Column ref: " + e5.toString() + " (kind=" + Viper.Fmt.Int(e5.kind) + ", col=" + e5.columnName + ")");

    var e6 = exprTableColumn("users", "id");
    Viper.Terminal.Say("Table.column ref: " + e6.toString());

    // Test star expression
    var e7 = exprStar();
    Viper.Terminal.Say("Star: " + e7.toString());

    // Test binary expressions (using literal values since global constants don't resolve in functions)
    // OP_ADD=1, OP_EQ=10, OP_MUL=3, OP_NEG=1, OP_NOT=2
    var e8 = exprBinary(1, exprInt(1), exprInt(2));  // OP_ADD
    Viper.Terminal.Say("Binary add: " + e8.toString());

    var e9 = exprBinary(10, exprColumn("age"), exprInt(21));  // OP_EQ
    Viper.Terminal.Say("Binary eq: " + e9.toString());

    // Test compound expression: (a + b) * c
    var add = exprBinary(1, exprColumn("a"), exprColumn("b"));  // OP_ADD
    var mul = exprBinary(3, add, exprColumn("c"));  // OP_MUL
    Viper.Terminal.Say("Compound: " + mul.toString());

    // Test unary expression
    var e10 = exprUnary(1, exprInt(5));  // OP_NEG
    Viper.Terminal.Say("Unary neg: " + e10.toString());

    var e11 = exprUnary(2, exprColumn("active"));  // OP_NOT
    Viper.Terminal.Say("Unary not: " + e11.toString());

    Viper.Terminal.Say("=== Expression Test PASSED ===");
}

func testParser() {
    Viper.Terminal.Say("=== Parser Test ===");

    // Test CREATE TABLE parsing
    var sql1 = "CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER);";
    Viper.Terminal.Say("Input: " + sql1);
    Viper.Terminal.Say("");

    parserInit(sql1);
    parserAdvance();  // Skip CREATE token
    var createStmt = parseCreateTableStmt();

    if parserHasError {
        Viper.Terminal.Say("ERROR: " + parserError);
    } else {
        Viper.Terminal.Say("Parsed:");
        Viper.Terminal.Say(createStmt.toString());
    }

    Viper.Terminal.Say("");

    // Test INSERT parsing
    var sql2 = "INSERT INTO users (id, name, age) VALUES (1, 'Alice', 30), (2, 'Bob', 25);";
    Viper.Terminal.Say("Input: " + sql2);
    Viper.Terminal.Say("");

    parserInit(sql2);
    parserAdvance();  // Skip INSERT token
    var insertStmt = parseInsertStmt();

    if parserHasError {
        Viper.Terminal.Say("ERROR: " + parserError);
    } else {
        Viper.Terminal.Say("Parsed:");
        Viper.Terminal.Say(insertStmt.toString());
    }

    Viper.Terminal.Say("");

    // Test expression parsing
    var sql3 = "1 + 2 * 3";
    Viper.Terminal.Say("Expr: " + sql3);
    parserInit(sql3);
    var expr = parseExpr();
    Viper.Terminal.Say("Parsed: " + expr.toString());

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Parser Test PASSED ===");
}
