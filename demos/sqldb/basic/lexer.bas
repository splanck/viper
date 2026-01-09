' lexer.bas - SQL Lexer
' Part of SQLite Clone - Viper Basic Implementation

AddFile "token.bas"

'=============================================================================
' LEXER STATE - Global variables (simpler than class methods)
'=============================================================================

DIM gLexSource AS STRING
DIM gLexPos AS INTEGER
DIM gLexLine AS INTEGER
DIM gLexCol AS INTEGER
DIM gLexLen AS INTEGER

SUB LexerInit(src AS STRING)
    LET gLexSource = src
    LET gLexPos = 0
    LET gLexLine = 1
    LET gLexCol = 1
    LET gLexLen = LEN(src)
END SUB

FUNCTION LexerAtEnd() AS INTEGER
    IF gLexPos >= gLexLen THEN
        LexerAtEnd = -1
    ELSE
        LexerAtEnd = 0
    END IF
END FUNCTION

FUNCTION LexerPeek() AS STRING
    IF LexerAtEnd() <> 0 THEN
        LexerPeek = " "
    ELSE
        LexerPeek = MID$(gLexSource, gLexPos + 1, 1)
    END IF
END FUNCTION

FUNCTION LexerPeekNext() AS STRING
    IF gLexPos + 1 >= gLexLen THEN
        LexerPeekNext = " "
    ELSE
        LexerPeekNext = MID$(gLexSource, gLexPos + 2, 1)
    END IF
END FUNCTION

FUNCTION LexerAdvance() AS STRING
    DIM ch AS STRING
    LET ch = LexerPeek()
    LET gLexPos = gLexPos + 1
    IF ch = CHR$(10) THEN
        LET gLexLine = gLexLine + 1
        LET gLexCol = 1
    ELSE
        LET gLexCol = gLexCol + 1
    END IF
    LexerAdvance = ch
END FUNCTION

SUB LexerSkipWhitespace()
    DIM ch AS STRING
    WHILE LexerAtEnd() = 0
        LET ch = LexerPeek()
        IF ch = " " OR ch = CHR$(9) OR ch = CHR$(13) OR ch = CHR$(10) THEN
            LexerAdvance()
        ELSEIF ch = "-" THEN
            IF LexerPeekNext() = "-" THEN
                WHILE (LexerAtEnd() = 0) AND (LexerPeek() <> CHR$(10))
                    LexerAdvance()
                WEND
            ELSE
                EXIT WHILE
            END IF
        ELSE
            EXIT WHILE
        END IF
    WEND
END SUB

'=============================================================================
' CHARACTER CLASSIFICATION
'=============================================================================

FUNCTION IsDigitCh(ch AS STRING) AS INTEGER
    DIM code AS INTEGER
    LET code = ASC(ch)
    IF (code >= 48) AND (code <= 57) THEN
        IsDigitCh = -1
    ELSE
        IsDigitCh = 0
    END IF
END FUNCTION

FUNCTION IsAlphaCh(ch AS STRING) AS INTEGER
    DIM code AS INTEGER
    LET code = ASC(ch)
    IF (code >= 65) AND (code <= 90) THEN
        IsAlphaCh = -1
    ELSEIF (code >= 97) AND (code <= 122) THEN
        IsAlphaCh = -1
    ELSEIF ch = "_" THEN
        IsAlphaCh = -1
    ELSE
        IsAlphaCh = 0
    END IF
END FUNCTION

FUNCTION IsAlphaNumCh(ch AS STRING) AS INTEGER
    IF IsAlphaCh(ch) <> 0 THEN
        IsAlphaNumCh = -1
    ELSEIF IsDigitCh(ch) <> 0 THEN
        IsAlphaNumCh = -1
    ELSE
        IsAlphaNumCh = 0
    END IF
END FUNCTION

'=============================================================================
' KEYWORD LOOKUP
'=============================================================================

' Bug workaround: SELECT CASE with many string cases has codegen bug
' Using IF-ELSEIF chains instead
FUNCTION LookupKeyword(word AS STRING) AS INTEGER
    LookupKeyword = TK_IDENTIFIER
    IF word = "CREATE" THEN
        LookupKeyword = TK_CREATE
    ELSEIF word = "TABLE" THEN
        LookupKeyword = TK_TABLE
    ELSEIF word = "DROP" THEN
        LookupKeyword = TK_DROP
    ELSEIF word = "ALTER" THEN
        LookupKeyword = TK_ALTER
    ELSEIF word = "INDEX" THEN
        LookupKeyword = TK_INDEX
    ELSEIF word = "SELECT" THEN
        LookupKeyword = TK_SELECT
    ELSEIF word = "INSERT" THEN
        LookupKeyword = TK_INSERT
    ELSEIF word = "UPDATE" THEN
        LookupKeyword = TK_UPDATE
    ELSEIF word = "DELETE" THEN
        LookupKeyword = TK_DELETE
    ELSEIF word = "INTO" THEN
        LookupKeyword = TK_INTO
    ELSEIF word = "FROM" THEN
        LookupKeyword = TK_FROM
    ELSEIF word = "WHERE" THEN
        LookupKeyword = TK_WHERE
    ELSEIF word = "SET" THEN
        LookupKeyword = TK_SET
    ELSEIF word = "VALUES" THEN
        LookupKeyword = TK_VALUES
    ELSEIF word = "ORDER" THEN
        LookupKeyword = TK_ORDER
    ELSEIF word = "BY" THEN
        LookupKeyword = TK_BY
    ELSEIF word = "ASC" THEN
        LookupKeyword = TK_ASC
    ELSEIF word = "DESC" THEN
        LookupKeyword = TK_DESC
    ELSEIF word = "LIMIT" THEN
        LookupKeyword = TK_LIMIT
    ELSEIF word = "OFFSET" THEN
        LookupKeyword = TK_OFFSET
    ELSEIF word = "GROUP" THEN
        LookupKeyword = TK_GROUP
    ELSEIF word = "HAVING" THEN
        LookupKeyword = TK_HAVING
    ELSEIF word = "DISTINCT" THEN
        LookupKeyword = TK_DISTINCT
    ELSEIF word = "JOIN" THEN
        LookupKeyword = TK_JOIN
    ELSEIF word = "INNER" THEN
        LookupKeyword = TK_INNER
    ELSEIF word = "LEFT" THEN
        LookupKeyword = TK_LEFT
    ELSEIF word = "RIGHT" THEN
        LookupKeyword = TK_RIGHT
    ELSEIF word = "FULL" THEN
        LookupKeyword = TK_FULL
    ELSEIF word = "OUTER" THEN
        LookupKeyword = TK_OUTER
    ELSEIF word = "CROSS" THEN
        LookupKeyword = TK_CROSS
    ELSEIF word = "ON" THEN
        LookupKeyword = TK_ON
    ELSEIF word = "AND" THEN
        LookupKeyword = TK_AND
    ELSEIF word = "OR" THEN
        LookupKeyword = TK_OR
    ELSEIF word = "NOT" THEN
        LookupKeyword = TK_NOT
    ELSEIF word = "IN" THEN
        LookupKeyword = TK_IN
    ELSEIF word = "IS" THEN
        LookupKeyword = TK_IS
    ELSEIF word = "LIKE" THEN
        LookupKeyword = TK_LIKE
    ELSEIF word = "BETWEEN" THEN
        LookupKeyword = TK_BETWEEN
    ELSEIF word = "EXISTS" THEN
        LookupKeyword = TK_EXISTS
    ELSEIF word = "NULL" THEN
        LookupKeyword = TK_NULL
    ELSEIF word = "TRUE" THEN
        LookupKeyword = TK_TRUE
    ELSEIF word = "FALSE" THEN
        LookupKeyword = TK_FALSE
    ELSEIF word = "DEFAULT" THEN
        LookupKeyword = TK_DEFAULT
    ELSEIF word = "PRIMARY" THEN
        LookupKeyword = TK_PRIMARY
    ELSEIF word = "FOREIGN" THEN
        LookupKeyword = TK_FOREIGN
    ELSEIF word = "KEY" THEN
        LookupKeyword = TK_KEY
    ELSEIF word = "REFERENCES" THEN
        LookupKeyword = TK_REFERENCES
    ELSEIF word = "UNIQUE" THEN
        LookupKeyword = TK_UNIQUE
    ELSEIF word = "AUTOINCREMENT" THEN
        LookupKeyword = TK_AUTOINCREMENT
    ELSEIF word = "INT" THEN
        LookupKeyword = TK_INT
    ELSEIF word = "INTEGER" THEN
        LookupKeyword = TK_INTEGER_TYPE
    ELSEIF word = "REAL" THEN
        LookupKeyword = TK_REAL
    ELSEIF word = "TEXT" THEN
        LookupKeyword = TK_TEXT
    ELSEIF word = "BEGIN" THEN
        LookupKeyword = TK_BEGIN
    ELSEIF word = "COMMIT" THEN
        LookupKeyword = TK_COMMIT
    ELSEIF word = "ROLLBACK" THEN
        LookupKeyword = TK_ROLLBACK
    ELSEIF word = "TRANSACTION" THEN
        LookupKeyword = TK_TRANSACTION
    ELSEIF word = "AS" THEN
        LookupKeyword = TK_AS
    ELSEIF word = "CASE" THEN
        LookupKeyword = TK_CASE
    ELSEIF word = "WHEN" THEN
        LookupKeyword = TK_WHEN
    ELSEIF word = "THEN" THEN
        LookupKeyword = TK_THEN
    ELSEIF word = "ELSE" THEN
        LookupKeyword = TK_ELSE
    ELSEIF word = "END" THEN
        LookupKeyword = TK_END
    ELSEIF word = "UNION" THEN
        LookupKeyword = TK_UNION
    ELSEIF word = "ALL" THEN
        LookupKeyword = TK_ALL
    ELSEIF word = "CAST" THEN
        LookupKeyword = TK_CAST
    END IF
END FUNCTION

' Fixed: Bug #008 - Use SELECT CASE instead of single-line IF with EXIT FUNCTION
' Fixed: Bug #009 - CASE ELSE return value not working, must set default before SELECT
FUNCTION TokenTypeName$(kind AS INTEGER)
    TokenTypeName$ = "UNKNOWN"
    SELECT CASE kind
        CASE TK_EOF: TokenTypeName$ = "EOF"
        CASE TK_ERROR: TokenTypeName$ = "ERROR"
        CASE TK_INTEGER: TokenTypeName$ = "INTEGER"
        CASE TK_NUMBER: TokenTypeName$ = "NUMBER"
        CASE TK_STRING: TokenTypeName$ = "STRING"
        CASE TK_IDENTIFIER: TokenTypeName$ = "IDENTIFIER"
        CASE TK_SELECT: TokenTypeName$ = "SELECT"
        CASE TK_INSERT: TokenTypeName$ = "INSERT"
        CASE TK_UPDATE: TokenTypeName$ = "UPDATE"
        CASE TK_DELETE: TokenTypeName$ = "DELETE"
        CASE TK_CREATE: TokenTypeName$ = "CREATE"
        CASE TK_TABLE: TokenTypeName$ = "TABLE"
        CASE TK_DROP: TokenTypeName$ = "DROP"
        CASE TK_FROM: TokenTypeName$ = "FROM"
        CASE TK_WHERE: TokenTypeName$ = "WHERE"
        CASE TK_INTO: TokenTypeName$ = "INTO"
        CASE TK_VALUES: TokenTypeName$ = "VALUES"
        CASE TK_AND: TokenTypeName$ = "AND"
        CASE TK_OR: TokenTypeName$ = "OR"
        CASE TK_NOT: TokenTypeName$ = "NOT"
        CASE TK_NULL: TokenTypeName$ = "NULL"
        CASE TK_PLUS: TokenTypeName$ = "PLUS"
        CASE TK_MINUS: TokenTypeName$ = "MINUS"
        CASE TK_STAR: TokenTypeName$ = "STAR"
        CASE TK_SLASH: TokenTypeName$ = "SLASH"
        CASE TK_EQ: TokenTypeName$ = "EQ"
        CASE TK_NE: TokenTypeName$ = "NE"
        CASE TK_LT: TokenTypeName$ = "LT"
        CASE TK_GT: TokenTypeName$ = "GT"
        CASE TK_LE: TokenTypeName$ = "LE"
        CASE TK_GE: TokenTypeName$ = "GE"
        CASE TK_LPAREN: TokenTypeName$ = "LPAREN"
        CASE TK_RPAREN: TokenTypeName$ = "RPAREN"
        CASE TK_COMMA: TokenTypeName$ = "COMMA"
        CASE TK_SEMICOLON: TokenTypeName$ = "SEMICOLON"
        CASE TK_DOT: TokenTypeName$ = "DOT"
        CASE ELSE
    END SELECT
END FUNCTION

'=============================================================================
' LEXER FUNCTIONS - Token reading
'=============================================================================

' Global token for returning results
DIM gTok AS Token

SUB LexerMakeToken(k AS INTEGER, t AS STRING, ln AS INTEGER, col AS INTEGER)
    LET gTok = NEW Token()
    gTok.Init(k, t, ln, col)
END SUB

SUB LexerReadNumber()
    DIM startLine AS INTEGER
    DIM startCol AS INTEGER
    DIM startPos AS INTEGER
    DIM text AS STRING
    DIM isFloat AS INTEGER

    LET startLine = gLexLine
    LET startCol = gLexCol
    LET startPos = gLexPos
    LET isFloat = 0

    WHILE (LexerAtEnd() = 0) AND (IsDigitCh(LexerPeek()) <> 0)
        LexerAdvance()
    WEND

    IF (LexerPeek() = ".") AND (IsDigitCh(LexerPeekNext()) <> 0) THEN
        LexerAdvance()
        LET isFloat = -1
        WHILE (LexerAtEnd() = 0) AND (IsDigitCh(LexerPeek()) <> 0)
            LexerAdvance()
        WEND
    END IF

    LET text = MID$(gLexSource, startPos + 1, gLexPos - startPos)
    IF isFloat <> 0 THEN
        LexerMakeToken(TK_NUMBER, text, startLine, startCol)
    ELSE
        LexerMakeToken(TK_INTEGER, text, startLine, startCol)
    END IF
END SUB

SUB LexerReadString()
    DIM startLine AS INTEGER
    DIM startCol AS INTEGER
    DIM quote AS STRING
    DIM startPos AS INTEGER
    DIM text AS STRING

    LET startLine = gLexLine
    LET startCol = gLexCol
    LET quote = LexerAdvance()
    LET startPos = gLexPos

    WHILE (LexerAtEnd() = 0) AND (LexerPeek() <> quote)
        IF LexerPeek() = CHR$(92) THEN
            LexerAdvance()
            IF LexerAtEnd() = 0 THEN LexerAdvance()
        ELSE
            LexerAdvance()
        END IF
    WEND

    LET text = MID$(gLexSource, startPos + 1, gLexPos - startPos)
    IF LexerAtEnd() = 0 THEN LexerAdvance()

    LexerMakeToken(TK_STRING, text, startLine, startCol)
END SUB

SUB LexerReadIdentifier()
    DIM startLine AS INTEGER
    DIM startCol AS INTEGER
    DIM startPos AS INTEGER
    DIM text AS STRING
    DIM upper AS STRING
    DIM kind AS INTEGER

    LET startLine = gLexLine
    LET startCol = gLexCol
    LET startPos = gLexPos

    WHILE (LexerAtEnd() = 0) AND (IsAlphaNumCh(LexerPeek()) <> 0)
        LexerAdvance()
    WEND

    LET text = MID$(gLexSource, startPos + 1, gLexPos - startPos)
    LET upper = UCASE$(text)
    LET kind = LookupKeyword(upper)

    LexerMakeToken(kind, text, startLine, startCol)
END SUB

SUB LexerNextToken()
    DIM startLine AS INTEGER
    DIM startCol AS INTEGER
    DIM ch AS STRING
    DIM nextCh AS STRING
    DIM emptyStr AS STRING

    LET emptyStr = " "
    LexerSkipWhitespace()

    IF LexerAtEnd() <> 0 THEN
        LexerMakeToken(TK_EOF, emptyStr, gLexLine, gLexCol)
        EXIT SUB
    END IF

    LET startLine = gLexLine
    LET startCol = gLexCol
    LET ch = LexerPeek()

    IF IsDigitCh(ch) <> 0 THEN
        LexerReadNumber()
        EXIT SUB
    END IF

    IF IsAlphaCh(ch) <> 0 THEN
        LexerReadIdentifier()
        EXIT SUB
    END IF

    IF ch = "'" OR ch = CHR$(34) THEN
        LexerReadString()
        EXIT SUB
    END IF

    LexerAdvance()
    LET nextCh = LexerPeek()

    ' Two-character operators
    IF (ch = "<") AND (nextCh = "=") THEN
        LexerAdvance()
        LexerMakeToken(TK_LE, "<=", startLine, startCol)
        EXIT SUB
    END IF
    IF (ch = ">") AND (nextCh = "=") THEN
        LexerAdvance()
        LexerMakeToken(TK_GE, ">=", startLine, startCol)
        EXIT SUB
    END IF
    IF (ch = "<") AND (nextCh = ">") THEN
        LexerAdvance()
        LexerMakeToken(TK_NE, "<>", startLine, startCol)
        EXIT SUB
    END IF
    IF (ch = "!") AND (nextCh = "=") THEN
        LexerAdvance()
        LexerMakeToken(TK_NE, "!=", startLine, startCol)
        EXIT SUB
    END IF
    IF (ch = "|") AND (nextCh = "|") THEN
        LexerAdvance()
        LexerMakeToken(TK_CONCAT, "||", startLine, startCol)
        EXIT SUB
    END IF

    ' Single-character operators and punctuation
    IF ch = "+" THEN
        LexerMakeToken(TK_PLUS, "+", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "-" THEN
        LexerMakeToken(TK_MINUS, "-", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "*" THEN
        LexerMakeToken(TK_STAR, "*", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "/" THEN
        LexerMakeToken(TK_SLASH, "/", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "%" THEN
        LexerMakeToken(TK_PERCENT, "%", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "=" THEN
        LexerMakeToken(TK_EQ, "=", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "<" THEN
        LexerMakeToken(TK_LT, "<", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = ">" THEN
        LexerMakeToken(TK_GT, ">", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "(" THEN
        LexerMakeToken(TK_LPAREN, "(", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = ")" THEN
        LexerMakeToken(TK_RPAREN, ")", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "," THEN
        LexerMakeToken(TK_COMMA, ",", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = ";" THEN
        LexerMakeToken(TK_SEMICOLON, ";", startLine, startCol)
        EXIT SUB
    END IF
    IF ch = "." THEN
        LexerMakeToken(TK_DOT, ".", startLine, startCol)
        EXIT SUB
    END IF

    LexerMakeToken(TK_ERROR, ch, startLine, startCol)
END SUB

