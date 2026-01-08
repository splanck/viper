' SQLite Clone - SQL Lexer
' Viper Basic Implementation

AddFile "token.bas"

' Lexer class - tokenizes SQL input
CLASS Lexer
    DIM source AS STRING
    DIM pos AS INTEGER
    DIM lineNum AS INTEGER
    DIM colNum AS INTEGER
    DIM srcLen AS INTEGER

    SUB NEW(src AS STRING)
        LET ME.source = src
        LET ME.pos = 0
        LET ME.lineNum = 1
        LET ME.colNum = 1
        LET ME.srcLen = LEN(src)
    END SUB

    ' Check if we've reached the end
    FUNCTION IsAtEnd() AS INTEGER
        RETURN ME.pos >= ME.srcLen
    END FUNCTION

    ' Peek at current character without consuming
    FUNCTION Peek() AS STRING
        IF ME.IsAtEnd() THEN RETURN ""
        RETURN MID$(ME.source, ME.pos + 1, 1)
    END FUNCTION

    ' Peek at next character
    FUNCTION PeekNext() AS STRING
        IF ME.pos + 1 >= ME.srcLen THEN RETURN ""
        RETURN MID$(ME.source, ME.pos + 2, 1)
    END FUNCTION

    ' Consume and return current character
    FUNCTION Advance() AS STRING
        DIM ch AS STRING
        LET ch = ME.Peek()
        LET ME.pos = ME.pos + 1
        IF ch = CHR$(10) THEN
            LET ME.lineNum = ME.lineNum + 1
            LET ME.colNum = 1
        ELSE
            LET ME.colNum = ME.colNum + 1
        END IF
        RETURN ch
    END FUNCTION

    ' Skip whitespace and comments
    SUB SkipWhitespace()
        DIM ch AS STRING
        WHILE NOT ME.IsAtEnd()
            LET ch = ME.Peek()
            IF ch = " " OR ch = CHR$(9) OR ch = CHR$(13) OR ch = CHR$(10) THEN
                ME.Advance()
            ELSEIF ch = "-" AND ME.PeekNext() = "-" THEN
                ' Single line comment
                WHILE NOT ME.IsAtEnd() AND ME.Peek() <> CHR$(10)
                    ME.Advance()
                WEND
            ELSEIF ch = "/" AND ME.PeekNext() = "*" THEN
                ' Multi-line comment
                ME.Advance()
                ME.Advance()
                WHILE NOT ME.IsAtEnd()
                    IF ME.Peek() = "*" AND ME.PeekNext() = "/" THEN
                        ME.Advance()
                        ME.Advance()
                        EXIT WHILE
                    END IF
                    ME.Advance()
                WEND
            ELSE
                EXIT WHILE
            END IF
        WEND
    END SUB

    ' Check if character is digit
    FUNCTION IsDigit(ch AS STRING) AS INTEGER
        RETURN ch >= "0" AND ch <= "9"
    END FUNCTION

    ' Check if character is letter or underscore
    FUNCTION IsAlpha(ch AS STRING) AS INTEGER
        RETURN (ch >= "a" AND ch <= "z") OR (ch >= "A" AND ch <= "Z") OR ch = "_"
    END FUNCTION

    ' Check if character is alphanumeric
    FUNCTION IsAlphaNum(ch AS STRING) AS INTEGER
        RETURN ME.IsAlpha(ch) OR ME.IsDigit(ch)
    END FUNCTION

    ' Read a number (integer or float)
    FUNCTION ReadNumber() AS Token
        DIM startLine AS INTEGER
        DIM startCol AS INTEGER
        DIM startPos AS INTEGER
        DIM text AS STRING

        LET startLine = ME.lineNum
        LET startCol = ME.colNum
        LET startPos = ME.pos

        WHILE NOT ME.IsAtEnd() AND ME.IsDigit(ME.Peek())
            ME.Advance()
        WEND

        ' Check for decimal
        IF ME.Peek() = "." AND ME.IsDigit(ME.PeekNext()) THEN
            ME.Advance()
            WHILE NOT ME.IsAtEnd() AND ME.IsDigit(ME.Peek())
                ME.Advance()
            WEND
            LET text = MID$(ME.source, startPos + 1, ME.pos - startPos)
            RETURN NEW Token(TK_NUMBER, text, startLine, startCol)
        END IF

        LET text = MID$(ME.source, startPos + 1, ME.pos - startPos)
        RETURN NEW Token(TK_INTEGER, text, startLine, startCol)
    END FUNCTION

    ' Read a string literal
    FUNCTION ReadString() AS Token
        DIM startLine AS INTEGER
        DIM startCol AS INTEGER
        DIM quote AS STRING
        DIM startPos AS INTEGER
        DIM text AS STRING

        LET startLine = ME.lineNum
        LET startCol = ME.colNum
        LET quote = ME.Advance()
        LET startPos = ME.pos

        WHILE NOT ME.IsAtEnd() AND ME.Peek() <> quote
            IF ME.Peek() = "\" THEN
                ME.Advance()
                IF NOT ME.IsAtEnd() THEN
                    ME.Advance()
                END IF
            ELSE
                ME.Advance()
            END IF
        WEND

        LET text = MID$(ME.source, startPos + 1, ME.pos - startPos)

        IF NOT ME.IsAtEnd() THEN
            ME.Advance()
        END IF

        RETURN NEW Token(TK_STRING, text, startLine, startCol)
    END FUNCTION

    ' Read an identifier or keyword
    FUNCTION ReadIdentifier() AS Token
        DIM startLine AS INTEGER
        DIM startCol AS INTEGER
        DIM startPos AS INTEGER
        DIM text AS STRING
        DIM upper AS STRING
        DIM kind AS INTEGER

        LET startLine = ME.lineNum
        LET startCol = ME.colNum
        LET startPos = ME.pos

        WHILE NOT ME.IsAtEnd() AND ME.IsAlphaNum(ME.Peek())
            ME.Advance()
        WEND

        LET text = MID$(ME.source, startPos + 1, ME.pos - startPos)
        LET upper = UCASE$(text)
        LET kind = ME.LookupKeyword(upper)

        RETURN NEW Token(kind, text, startLine, startCol)
    END FUNCTION

    ' Lookup keyword token type
    FUNCTION LookupKeyword(word AS STRING) AS INTEGER
        ' DDL
        IF word = "CREATE" THEN RETURN TK_CREATE
        IF word = "TABLE" THEN RETURN TK_TABLE
        IF word = "DROP" THEN RETURN TK_DROP
        IF word = "ALTER" THEN RETURN TK_ALTER
        IF word = "INDEX" THEN RETURN TK_INDEX
        IF word = "VIEW" THEN RETURN TK_VIEW
        IF word = "TRIGGER" THEN RETURN TK_TRIGGER

        ' DML
        IF word = "SELECT" THEN RETURN TK_SELECT
        IF word = "INSERT" THEN RETURN TK_INSERT
        IF word = "UPDATE" THEN RETURN TK_UPDATE
        IF word = "DELETE" THEN RETURN TK_DELETE
        IF word = "INTO" THEN RETURN TK_INTO
        IF word = "FROM" THEN RETURN TK_FROM
        IF word = "WHERE" THEN RETURN TK_WHERE
        IF word = "SET" THEN RETURN TK_SET
        IF word = "VALUES" THEN RETURN TK_VALUES

        ' Clauses
        IF word = "ORDER" THEN RETURN TK_ORDER
        IF word = "BY" THEN RETURN TK_BY
        IF word = "ASC" THEN RETURN TK_ASC
        IF word = "DESC" THEN RETURN TK_DESC
        IF word = "LIMIT" THEN RETURN TK_LIMIT
        IF word = "OFFSET" THEN RETURN TK_OFFSET
        IF word = "GROUP" THEN RETURN TK_GROUP
        IF word = "HAVING" THEN RETURN TK_HAVING
        IF word = "DISTINCT" THEN RETURN TK_DISTINCT

        ' Joins
        IF word = "JOIN" THEN RETURN TK_JOIN
        IF word = "INNER" THEN RETURN TK_INNER
        IF word = "LEFT" THEN RETURN TK_LEFT
        IF word = "RIGHT" THEN RETURN TK_RIGHT
        IF word = "FULL" THEN RETURN TK_FULL
        IF word = "OUTER" THEN RETURN TK_OUTER
        IF word = "CROSS" THEN RETURN TK_CROSS
        IF word = "ON" THEN RETURN TK_ON

        ' Logical
        IF word = "AND" THEN RETURN TK_AND
        IF word = "OR" THEN RETURN TK_OR
        IF word = "NOT" THEN RETURN TK_NOT
        IF word = "IN" THEN RETURN TK_IN
        IF word = "IS" THEN RETURN TK_IS
        IF word = "LIKE" THEN RETURN TK_LIKE
        IF word = "BETWEEN" THEN RETURN TK_BETWEEN
        IF word = "EXISTS" THEN RETURN TK_EXISTS

        ' Values
        IF word = "NULL" THEN RETURN TK_NULL
        IF word = "TRUE" THEN RETURN TK_TRUE
        IF word = "FALSE" THEN RETURN TK_FALSE
        IF word = "DEFAULT" THEN RETURN TK_DEFAULT

        ' Constraints
        IF word = "PRIMARY" THEN RETURN TK_PRIMARY
        IF word = "FOREIGN" THEN RETURN TK_FOREIGN
        IF word = "KEY" THEN RETURN TK_KEY
        IF word = "REFERENCES" THEN RETURN TK_REFERENCES
        IF word = "UNIQUE" THEN RETURN TK_UNIQUE
        IF word = "CHECK" THEN RETURN TK_CHECK
        IF word = "CONSTRAINT" THEN RETURN TK_CONSTRAINT
        IF word = "AUTOINCREMENT" THEN RETURN TK_AUTOINCREMENT

        ' Types
        IF word = "INT" THEN RETURN TK_INT
        IF word = "INTEGER" THEN RETURN TK_INTEGER_TYPE
        IF word = "REAL" THEN RETURN TK_REAL
        IF word = "TEXT" THEN RETURN TK_TEXT
        IF word = "BLOB" THEN RETURN TK_BLOB
        IF word = "BOOLEAN" THEN RETURN TK_BOOLEAN
        IF word = "VARCHAR" THEN RETURN TK_VARCHAR

        ' Transactions
        IF word = "BEGIN" THEN RETURN TK_BEGIN
        IF word = "COMMIT" THEN RETURN TK_COMMIT
        IF word = "ROLLBACK" THEN RETURN TK_ROLLBACK
        IF word = "TRANSACTION" THEN RETURN TK_TRANSACTION
        IF word = "SAVEPOINT" THEN RETURN TK_SAVEPOINT
        IF word = "RELEASE" THEN RETURN TK_RELEASE

        ' Other
        IF word = "AS" THEN RETURN TK_AS
        IF word = "CASE" THEN RETURN TK_CASE
        IF word = "WHEN" THEN RETURN TK_WHEN
        IF word = "THEN" THEN RETURN TK_THEN
        IF word = "ELSE" THEN RETURN TK_ELSE
        IF word = "END" THEN RETURN TK_END
        IF word = "UNION" THEN RETURN TK_UNION
        IF word = "ALL" THEN RETURN TK_ALL
        IF word = "CAST" THEN RETURN TK_CAST

        ' Utility
        IF word = "SHOW" THEN RETURN TK_SHOW
        IF word = "DESCRIBE" THEN RETURN TK_DESCRIBE
        IF word = "EXPLAIN" THEN RETURN TK_EXPLAIN
        IF word = "VACUUM" THEN RETURN TK_VACUUM
        IF word = "SAVE" THEN RETURN TK_SAVE
        IF word = "OPEN" THEN RETURN TK_OPEN
        IF word = "EXPORT" THEN RETURN TK_EXPORT
        IF word = "IMPORT" THEN RETURN TK_IMPORT
        IF word = "HELP" THEN RETURN TK_HELP
        IF word = "TO" THEN RETURN TK_TO
        IF word = "ADD" THEN RETURN TK_ADD
        IF word = "COLUMN" THEN RETURN TK_COLUMN
        IF word = "RENAME" THEN RETURN TK_RENAME
        IF word = "IF" THEN RETURN TK_IF

        RETURN TK_IDENTIFIER
    END FUNCTION

    ' Get next token
    FUNCTION NextToken() AS Token
        DIM startLine AS INTEGER
        DIM startCol AS INTEGER
        DIM ch AS STRING

        ME.SkipWhitespace()

        IF ME.IsAtEnd() THEN
            RETURN NEW Token(TK_EOF, "", ME.lineNum, ME.colNum)
        END IF

        LET startLine = ME.lineNum
        LET startCol = ME.colNum
        LET ch = ME.Peek()

        ' Numbers
        IF ME.IsDigit(ch) THEN
            RETURN ME.ReadNumber()
        END IF

        ' Identifiers and keywords
        IF ME.IsAlpha(ch) THEN
            RETURN ME.ReadIdentifier()
        END IF

        ' String literals
        IF ch = "'" OR ch = CHR$(34) THEN
            RETURN ME.ReadString()
        END IF

        ' Operators and punctuation
        ME.Advance()

        ' Two-character operators
        IF ch = "<" AND ME.Peek() = "=" THEN
            ME.Advance()
            RETURN NEW Token(TK_LE, "<=", startLine, startCol)
        END IF
        IF ch = ">" AND ME.Peek() = "=" THEN
            ME.Advance()
            RETURN NEW Token(TK_GE, ">=", startLine, startCol)
        END IF
        IF ch = "<" AND ME.Peek() = ">" THEN
            ME.Advance()
            RETURN NEW Token(TK_NE, "<>", startLine, startCol)
        END IF
        IF ch = "!" AND ME.Peek() = "=" THEN
            ME.Advance()
            RETURN NEW Token(TK_NE, "!=", startLine, startCol)
        END IF
        IF ch = "|" AND ME.Peek() = "|" THEN
            ME.Advance()
            RETURN NEW Token(TK_CONCAT, "||", startLine, startCol)
        END IF

        ' Single-character operators
        IF ch = "+" THEN RETURN NEW Token(TK_PLUS, "+", startLine, startCol)
        IF ch = "-" THEN RETURN NEW Token(TK_MINUS, "-", startLine, startCol)
        IF ch = "*" THEN RETURN NEW Token(TK_STAR, "*", startLine, startCol)
        IF ch = "/" THEN RETURN NEW Token(TK_SLASH, "/", startLine, startCol)
        IF ch = "%" THEN RETURN NEW Token(TK_PERCENT, "%", startLine, startCol)
        IF ch = "=" THEN RETURN NEW Token(TK_EQ, "=", startLine, startCol)
        IF ch = "<" THEN RETURN NEW Token(TK_LT, "<", startLine, startCol)
        IF ch = ">" THEN RETURN NEW Token(TK_GT, ">", startLine, startCol)

        ' Punctuation
        IF ch = "(" THEN RETURN NEW Token(TK_LPAREN, "(", startLine, startCol)
        IF ch = ")" THEN RETURN NEW Token(TK_RPAREN, ")", startLine, startCol)
        IF ch = "," THEN RETURN NEW Token(TK_COMMA, ",", startLine, startCol)
        IF ch = ";" THEN RETURN NEW Token(TK_SEMICOLON, ";", startLine, startCol)
        IF ch = "." THEN RETURN NEW Token(TK_DOT, ".", startLine, startCol)

        ' Unknown character - return error token
        RETURN NEW Token(TK_ERROR, ch, startLine, startCol)
    END FUNCTION
END CLASS

' Test subroutine for lexer
SUB TestLexer()
    DIM sql AS STRING
    DIM lex AS Lexer
    DIM tok AS Token

    PRINT "=== Lexer Test ==="

    ' Test basic SQL
    LET sql = "SELECT id, name FROM users WHERE age > 21;"
    PRINT "Input: "; sql
    PRINT ""

    LET lex = NEW Lexer(sql)
    LET tok = lex.NextToken()
    WHILE tok.kind <> TK_EOF
        PRINT "  "; TokenTypeName$(tok.kind); ": '"; tok.text; "'"
        LET tok = lex.NextToken()
    WEND

    PRINT ""

    ' Test with numbers and strings
    LET sql = "INSERT INTO users VALUES (1, 'John', 3.14);"
    PRINT "Input: "; sql
    PRINT ""

    LET lex = NEW Lexer(sql)
    LET tok = lex.NextToken()
    WHILE tok.kind <> TK_EOF
        PRINT "  "; TokenTypeName$(tok.kind); ": '"; tok.text; "'"
        LET tok = lex.NextToken()
    WEND

    PRINT ""

    ' Test operators
    LET sql = "x >= 10 AND y <= 20 OR z <> 0"
    PRINT "Input: "; sql
    PRINT ""

    LET lex = NEW Lexer(sql)
    LET tok = lex.NextToken()
    WHILE tok.kind <> TK_EOF
        PRINT "  "; TokenTypeName$(tok.kind); ": '"; tok.text; "'"
        LET tok = lex.NextToken()
    WEND

    PRINT ""
    PRINT "=== Lexer Test PASSED ==="
END SUB
