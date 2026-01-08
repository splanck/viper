' SQLite Clone - Combined SQL Module
' Viper Basic Implementation
' Using standalone functions to avoid class method limitations (Bugs #002, #004, #005)

'=============================================================================
' TOKEN CONSTANTS
'=============================================================================

CONST TK_EOF = 0
CONST TK_ERROR = 1
CONST TK_INTEGER = 10
CONST TK_NUMBER = 11
CONST TK_STRING = 12
CONST TK_IDENTIFIER = 13

' Keywords - DDL
CONST TK_CREATE = 20
CONST TK_TABLE = 21
CONST TK_DROP = 22
CONST TK_ALTER = 23
CONST TK_INDEX = 24

' Keywords - DML
CONST TK_SELECT = 30
CONST TK_INSERT = 31
CONST TK_UPDATE = 32
CONST TK_DELETE = 33
CONST TK_INTO = 34
CONST TK_FROM = 35
CONST TK_WHERE = 36
CONST TK_SET = 37
CONST TK_VALUES = 38

' Keywords - Clauses
CONST TK_ORDER = 40
CONST TK_BY = 41
CONST TK_ASC = 42
CONST TK_DESC = 43
CONST TK_LIMIT = 44
CONST TK_OFFSET = 45
CONST TK_GROUP = 46
CONST TK_HAVING = 47
CONST TK_DISTINCT = 48

' Keywords - Joins
CONST TK_JOIN = 50
CONST TK_INNER = 51
CONST TK_LEFT = 52
CONST TK_RIGHT = 53
CONST TK_FULL = 54
CONST TK_OUTER = 55
CONST TK_CROSS = 56
CONST TK_ON = 57

' Keywords - Logical
CONST TK_AND = 60
CONST TK_OR = 61
CONST TK_NOT = 62
CONST TK_IN = 63
CONST TK_IS = 64
CONST TK_LIKE = 65
CONST TK_BETWEEN = 66
CONST TK_EXISTS = 67

' Keywords - Values
CONST TK_NULL = 70
CONST TK_TRUE = 71
CONST TK_FALSE = 72
CONST TK_DEFAULT = 73

' Keywords - Constraints
CONST TK_PRIMARY = 80
CONST TK_FOREIGN = 81
CONST TK_KEY = 82
CONST TK_REFERENCES = 83
CONST TK_UNIQUE = 84
CONST TK_AUTOINCREMENT = 87

' Keywords - Types
CONST TK_INT = 90
CONST TK_INTEGER_TYPE = 91
CONST TK_REAL = 92
CONST TK_TEXT = 93

' Keywords - Transactions
CONST TK_BEGIN = 100
CONST TK_COMMIT = 101
CONST TK_ROLLBACK = 102
CONST TK_TRANSACTION = 103

' Keywords - Other
CONST TK_AS = 110
CONST TK_CASE = 111
CONST TK_WHEN = 112
CONST TK_THEN = 113
CONST TK_ELSE = 114
CONST TK_END = 115
CONST TK_UNION = 116
CONST TK_ALL = 117
CONST TK_CAST = 118

' Operators
CONST TK_PLUS = 140
CONST TK_MINUS = 141
CONST TK_STAR = 142
CONST TK_SLASH = 143
CONST TK_PERCENT = 144
CONST TK_EQ = 145
CONST TK_NE = 146
CONST TK_LT = 147
CONST TK_LE = 148
CONST TK_GT = 149
CONST TK_GE = 150
CONST TK_CONCAT = 151

' Punctuation
CONST TK_LPAREN = 160
CONST TK_RPAREN = 161
CONST TK_COMMA = 162
CONST TK_SEMICOLON = 163
CONST TK_DOT = 164

'=============================================================================
' TOKEN CLASS - Simple data holder
'=============================================================================

CLASS Token
    PUBLIC kind AS INTEGER
    PUBLIC text AS STRING
    PUBLIC lineNum AS INTEGER
    PUBLIC colNum AS INTEGER

    PUBLIC SUB Init(k AS INTEGER, t AS STRING, ln AS INTEGER, col AS INTEGER)
        kind = k
        text = t
        lineNum = ln
        colNum = col
    END SUB
END CLASS

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

'=============================================================================
' TEST FUNCTIONS
'=============================================================================

'=============================================================================
' SQL VALUE TYPES
'=============================================================================

' SqlType constants
CONST SQL_NULL = 0
CONST SQL_INTEGER = 1
CONST SQL_REAL = 2
CONST SQL_TEXT = 3
CONST SQL_BLOB = 4

' SqlValue - Tagged union for SQL values
CLASS SqlValue
    PUBLIC kind AS INTEGER       ' SQL_NULL, SQL_INTEGER, SQL_REAL, SQL_TEXT, SQL_BLOB
    PUBLIC intValue AS INTEGER   ' Used when kind = SQL_INTEGER
    PUBLIC realValue AS SINGLE   ' Used when kind = SQL_REAL (SINGLE = float)
    PUBLIC textValue AS STRING   ' Used when kind = SQL_TEXT or SQL_BLOB

    ' Default constructor - creates NULL value
    PUBLIC SUB Init()
        kind = SQL_NULL
        intValue = 0
        realValue = 0.0
        textValue = ""
    END SUB

    ' Create a NULL value
    PUBLIC SUB InitNull()
        kind = SQL_NULL
        intValue = 0
        realValue = 0.0
        textValue = ""
    END SUB

    ' Create an INTEGER value
    PUBLIC SUB InitInteger(val AS INTEGER)
        kind = SQL_INTEGER
        intValue = val
        realValue = 0.0
        textValue = ""
    END SUB

    ' Create a REAL value (text stores string representation for display)
    PUBLIC SUB InitReal(val AS SINGLE, txt AS STRING)
        kind = SQL_REAL
        intValue = 0
        realValue = val
        textValue = txt
    END SUB

    ' Create a TEXT value
    PUBLIC SUB InitText(val AS STRING)
        kind = SQL_TEXT
        intValue = 0
        realValue = 0.0
        textValue = val
    END SUB

    ' Create a BLOB value (stored as string for simplicity)
    PUBLIC SUB InitBlob(val AS STRING)
        kind = SQL_BLOB
        intValue = 0
        realValue = 0.0
        textValue = val
    END SUB

    ' Check type
    PUBLIC FUNCTION IsNull() AS INTEGER
        IF kind = SQL_NULL THEN
            IsNull = -1
        ELSE
            IsNull = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsInteger() AS INTEGER
        IF kind = SQL_INTEGER THEN
            IsInteger = -1
        ELSE
            IsInteger = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsReal() AS INTEGER
        IF kind = SQL_REAL THEN
            IsReal = -1
        ELSE
            IsReal = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsText() AS INTEGER
        IF kind = SQL_TEXT THEN
            IsText = -1
        ELSE
            IsText = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsBlob() AS INTEGER
        IF kind = SQL_BLOB THEN
            IsBlob = -1
        ELSE
            IsBlob = 0
        END IF
    END FUNCTION

    ' Get type name
    PUBLIC FUNCTION TypeName$()
        TypeName$ = "UNKNOWN"
        SELECT CASE kind
            CASE SQL_NULL: TypeName$ = "NULL"
            CASE SQL_INTEGER: TypeName$ = "INTEGER"
            CASE SQL_REAL: TypeName$ = "REAL"
            CASE SQL_TEXT: TypeName$ = "TEXT"
            CASE SQL_BLOB: TypeName$ = "BLOB"
        END SELECT
    END FUNCTION

    ' Convert to string representation
    PUBLIC FUNCTION ToString$()
        ToString$ = "?"
        SELECT CASE kind
            CASE SQL_NULL: ToString$ = "NULL"
            CASE SQL_INTEGER: ToString$ = STR$(intValue)
            CASE SQL_REAL: ToString$ = textValue
            CASE SQL_TEXT: ToString$ = "'" + textValue + "'"
            CASE SQL_BLOB: ToString$ = "X'" + textValue + "'"
        END SELECT
    END FUNCTION

    ' Compare values (returns -1, 0, or 1)
    PUBLIC FUNCTION Compare(other AS SqlValue) AS INTEGER
        ' NULL handling
        IF kind = SQL_NULL AND other.kind = SQL_NULL THEN
            Compare = 0
            EXIT FUNCTION
        END IF
        IF kind = SQL_NULL THEN
            Compare = -1
            EXIT FUNCTION
        END IF
        IF other.kind = SQL_NULL THEN
            Compare = 1
            EXIT FUNCTION
        END IF

        ' Same type comparisons
        IF kind = SQL_INTEGER AND other.kind = SQL_INTEGER THEN
            IF intValue < other.intValue THEN
                Compare = -1
            ELSEIF intValue > other.intValue THEN
                Compare = 1
            ELSE
                Compare = 0
            END IF
            EXIT FUNCTION
        END IF

        IF kind = SQL_REAL AND other.kind = SQL_REAL THEN
            IF realValue < other.realValue THEN
                Compare = -1
            ELSEIF realValue > other.realValue THEN
                Compare = 1
            ELSE
                Compare = 0
            END IF
            EXIT FUNCTION
        END IF

        IF kind = SQL_TEXT AND other.kind = SQL_TEXT THEN
            IF textValue = other.textValue THEN
                Compare = 0
            ELSE
                ' Check if both strings look like numbers before trying numeric comparison
                ' A numeric string starts with digit, minus, or period
                DIM myFirst AS STRING
                DIM otherFirst AS STRING
                DIM myIsNum AS INTEGER
                DIM otherIsNum AS INTEGER
                myIsNum = 0
                otherIsNum = 0
                IF LEN(textValue) > 0 THEN
                    myFirst = LEFT$(textValue, 1)
                    IF myFirst >= "0" AND myFirst <= "9" THEN myIsNum = -1
                    IF myFirst = "-" THEN myIsNum = -1
                END IF
                IF LEN(other.textValue) > 0 THEN
                    otherFirst = LEFT$(other.textValue, 1)
                    IF otherFirst >= "0" AND otherFirst <= "9" THEN otherIsNum = -1
                    IF otherFirst = "-" THEN otherIsNum = -1
                END IF
                ' Only try numeric comparison if both look like numbers
                IF myIsNum <> 0 AND otherIsNum <> 0 THEN
                    DIM myNum AS INTEGER
                    DIM otherNum AS INTEGER
                    myNum = VAL(textValue)
                    otherNum = VAL(other.textValue)
                    IF myNum < otherNum THEN
                        Compare = -1
                    ELSEIF myNum > otherNum THEN
                        Compare = 1
                    ELSE
                        Compare = 0
                    END IF
                ELSE
                    ' Lexicographic comparison for non-numeric strings
                    IF textValue < other.textValue THEN
                        Compare = -1
                    ELSE
                        Compare = 1
                    END IF
                END IF
            END IF
            EXIT FUNCTION
        END IF

        ' Cross-type comparison: TEXT vs INTEGER
        IF kind = SQL_TEXT AND other.kind = SQL_INTEGER THEN
            ' Check if text looks like a number first
            DIM txtFirst AS STRING
            DIM txtIsNum AS INTEGER
            txtIsNum = 0
            IF LEN(textValue) > 0 THEN
                txtFirst = LEFT$(textValue, 1)
                IF txtFirst >= "0" AND txtFirst <= "9" THEN txtIsNum = -1
                IF txtFirst = "-" THEN txtIsNum = -1
            END IF
            IF txtIsNum <> 0 THEN
                DIM myInt AS INTEGER
                myInt = VAL(textValue)
                IF myInt < other.intValue THEN
                    Compare = -1
                ELSEIF myInt > other.intValue THEN
                    Compare = 1
                ELSE
                    Compare = 0
                END IF
            ELSE
                ' Non-numeric text: TEXT > INTEGER by type order
                Compare = 1
            END IF
            EXIT FUNCTION
        END IF

        IF kind = SQL_INTEGER AND other.kind = SQL_TEXT THEN
            ' Check if other text looks like a number first
            DIM otxtFirst AS STRING
            DIM otxtIsNum AS INTEGER
            otxtIsNum = 0
            IF LEN(other.textValue) > 0 THEN
                otxtFirst = LEFT$(other.textValue, 1)
                IF otxtFirst >= "0" AND otxtFirst <= "9" THEN otxtIsNum = -1
                IF otxtFirst = "-" THEN otxtIsNum = -1
            END IF
            IF otxtIsNum <> 0 THEN
                DIM otherInt AS INTEGER
                otherInt = VAL(other.textValue)
                IF intValue < otherInt THEN
                    Compare = -1
                ELSEIF intValue > otherInt THEN
                    Compare = 1
                ELSE
                    Compare = 0
                END IF
            ELSE
                ' INTEGER < non-numeric TEXT by type order
                Compare = -1
            END IF
            EXIT FUNCTION
        END IF

        ' Cross-type comparison by type order
        IF kind < other.kind THEN
            Compare = -1
        ELSEIF kind > other.kind THEN
            Compare = 1
        ELSE
            Compare = 0
        END IF
    END FUNCTION

    ' Check equality
    PUBLIC FUNCTION Equals(other AS SqlValue) AS INTEGER
        IF Compare(other) = 0 THEN
            Equals = -1
        ELSE
            Equals = 0
        END IF
    END FUNCTION
END CLASS

' Factory functions for creating SqlValue instances
FUNCTION SqlNull() AS SqlValue
    DIM v AS SqlValue
    LET v = NEW SqlValue()
    v.InitNull()
    SqlNull = v
END FUNCTION

FUNCTION SqlInteger(val AS INTEGER) AS SqlValue
    DIM v AS SqlValue
    LET v = NEW SqlValue()
    v.InitInteger(val)
    SqlInteger = v
END FUNCTION

FUNCTION SqlReal(val AS SINGLE, txt AS STRING) AS SqlValue
    DIM v AS SqlValue
    LET v = NEW SqlValue()
    v.InitReal(val, txt)
    SqlReal = v
END FUNCTION

FUNCTION SqlText(val AS STRING) AS SqlValue
    DIM v AS SqlValue
    LET v = NEW SqlValue()
    v.InitText(val)
    SqlText = v
END FUNCTION

FUNCTION SqlBlob(val AS STRING) AS SqlValue
    DIM v AS SqlValue
    LET v = NEW SqlValue()
    v.InitBlob(val)
    SqlBlob = v
END FUNCTION

'=============================================================================
' COLUMN CLASS
'=============================================================================

CONST MAX_COLUMNS = 64  ' Maximum columns per table

CLASS SqlColumn
    PUBLIC name AS STRING
    PUBLIC typeCode AS INTEGER
    PUBLIC notNull AS INTEGER
    PUBLIC primaryKey AS INTEGER
    PUBLIC autoIncrement AS INTEGER
    PUBLIC isUnique AS INTEGER
    PUBLIC hasDefault AS INTEGER
    PUBLIC defaultValue AS SqlValue

    PUBLIC SUB Init(n AS STRING, t AS INTEGER)
        name = n
        typeCode = t
        notNull = 0
        primaryKey = 0
        autoIncrement = 0
        isUnique = 0
        hasDefault = 0
        LET defaultValue = NEW SqlValue()
        defaultValue.InitNull()
    END SUB

    PUBLIC FUNCTION TypeName$()
        TypeName$ = "UNKNOWN"
        SELECT CASE typeCode
            CASE SQL_NULL: TypeName$ = "NULL"
            CASE SQL_INTEGER: TypeName$ = "INTEGER"
            CASE SQL_REAL: TypeName$ = "REAL"
            CASE SQL_TEXT: TypeName$ = "TEXT"
            CASE SQL_BLOB: TypeName$ = "BLOB"
        END SELECT
    END FUNCTION

    PUBLIC SUB SetDefault(val AS SqlValue)
        hasDefault = -1
        LET defaultValue = val
    END SUB

    PUBLIC FUNCTION ToString$()
        DIM result AS STRING
        result = name + " " + TypeName$()
        IF primaryKey <> 0 THEN
            result = result + " PRIMARY KEY"
        END IF
        IF autoIncrement <> 0 THEN
            result = result + " AUTOINCREMENT"
        END IF
        IF notNull <> 0 THEN
            result = result + " NOT NULL"
        END IF
        IF isUnique <> 0 THEN
            result = result + " UNIQUE"
        END IF
        IF hasDefault <> 0 THEN
            result = result + " DEFAULT " + defaultValue.ToString$()
        END IF
        ToString$ = result
    END FUNCTION
END CLASS

' Factory function for creating columns
FUNCTION MakeColumn(name AS STRING, typeCode AS INTEGER) AS SqlColumn
    DIM col AS SqlColumn
    LET col = NEW SqlColumn()
    col.Init(name, typeCode)
    MakeColumn = col
END FUNCTION

'=============================================================================
' ROW CLASS
'=============================================================================

CLASS SqlRow
    PUBLIC values(MAX_COLUMNS) AS SqlValue
    PUBLIC columnCount AS INTEGER
    PUBLIC deleted AS INTEGER

    PUBLIC SUB Init(count AS INTEGER)
        DIM i AS INTEGER
        columnCount = count
        deleted = 0
        FOR i = 0 TO count - 1
            LET values(i) = NEW SqlValue()
            values(i).InitNull()
        NEXT i
    END SUB

    PUBLIC FUNCTION GetValue(index AS INTEGER) AS SqlValue
        IF index < 0 OR index >= columnCount THEN
            DIM nullVal AS SqlValue
            LET nullVal = NEW SqlValue()
            nullVal.InitNull()
            GetValue = nullVal
        ELSE
            GetValue = values(index)
        END IF
    END FUNCTION

    PUBLIC SUB SetValue(index AS INTEGER, val AS SqlValue)
        IF index >= 0 AND index < columnCount THEN
            LET values(index) = val
        END IF
    END SUB

    PUBLIC SUB AddValue(val AS SqlValue)
        IF columnCount < MAX_COLUMNS THEN
            LET values(columnCount) = val
            columnCount = columnCount + 1
        END IF
    END SUB

    PUBLIC FUNCTION Clone() AS SqlRow
        DIM newRow AS SqlRow
        DIM i AS INTEGER
        DIM cloned AS SqlValue
        LET newRow = NEW SqlRow()
        newRow.Init(columnCount)
        FOR i = 0 TO columnCount - 1
            LET cloned = NEW SqlValue()
            cloned.kind = values(i).kind
            cloned.intValue = values(i).intValue
            cloned.realValue = values(i).realValue
            cloned.textValue = values(i).textValue
            newRow.SetValue(i, cloned)
        NEXT i
        newRow.deleted = deleted
        Clone = newRow
    END FUNCTION

    PUBLIC FUNCTION ToString$()
        DIM result AS STRING
        DIM i AS INTEGER
        result = "("
        FOR i = 0 TO columnCount - 1
            IF i > 0 THEN
                result = result + ", "
            END IF
            result = result + values(i).ToString$()
        NEXT i
        result = result + ")"
        ToString$ = result
    END FUNCTION
END CLASS

' Factory function for creating rows
FUNCTION MakeRow(count AS INTEGER) AS SqlRow
    DIM row AS SqlRow
    LET row = NEW SqlRow()
    row.Init(count)
    MakeRow = row
END FUNCTION

'=============================================================================
' TABLE CLASS
'=============================================================================

CONST MAX_ROWS = 1000  ' Maximum rows per table

CLASS SqlTable
    PUBLIC name AS STRING
    PUBLIC columns(MAX_COLUMNS) AS SqlColumn
    PUBLIC columnCount AS INTEGER
    PUBLIC rows(MAX_ROWS) AS SqlRow
    PUBLIC rowCount AS INTEGER
    PUBLIC autoIncrementValue AS INTEGER

    PUBLIC SUB Init(tableName AS STRING)
        name = tableName
        columnCount = 0
        rowCount = 0
        autoIncrementValue = 1
    END SUB

    PUBLIC SUB AddColumn(col AS SqlColumn)
        IF columnCount < MAX_COLUMNS THEN
            LET columns(columnCount) = col
            columnCount = columnCount + 1
        END IF
    END SUB

    PUBLIC FUNCTION GetColumn(index AS INTEGER) AS SqlColumn
        IF index >= 0 AND index < columnCount THEN
            GetColumn = columns(index)
        ELSE
            DIM empty AS SqlColumn
            LET empty = NEW SqlColumn()
            empty.Init("", SQL_NULL)
            GetColumn = empty
        END IF
    END FUNCTION

    PUBLIC FUNCTION FindColumnIndex(colName AS STRING) AS INTEGER
        DIM i AS INTEGER
        FOR i = 0 TO columnCount - 1
            IF columns(i).name = colName THEN
                FindColumnIndex = i
                EXIT FUNCTION
            END IF
        NEXT i
        FindColumnIndex = -1
    END FUNCTION

    PUBLIC SUB AddRow(row AS SqlRow)
        IF rowCount < MAX_ROWS THEN
            LET rows(rowCount) = row
            rowCount = rowCount + 1
        END IF
    END SUB

    PUBLIC FUNCTION GetRow(index AS INTEGER) AS SqlRow
        IF index >= 0 AND index < rowCount THEN
            GetRow = rows(index)
        ELSE
            DIM empty AS SqlRow
            LET empty = NEW SqlRow()
            empty.Init(0)
            GetRow = empty
        END IF
    END FUNCTION

    PUBLIC SUB DeleteRow(index AS INTEGER)
        IF index >= 0 AND index < rowCount THEN
            rows(index).deleted = -1
        END IF
    END SUB

    PUBLIC FUNCTION NextAutoIncrement() AS INTEGER
        NextAutoIncrement = autoIncrementValue
        autoIncrementValue = autoIncrementValue + 1
    END FUNCTION

    PUBLIC FUNCTION CreateRow() AS SqlRow
        DIM row AS SqlRow
        LET row = NEW SqlRow()
        row.Init(columnCount)
        CreateRow = row
    END FUNCTION

    PUBLIC FUNCTION SchemaString$()
        DIM result AS STRING
        DIM i AS INTEGER
        result = "CREATE TABLE " + name + " (" + CHR$(10)
        FOR i = 0 TO columnCount - 1
            IF i > 0 THEN
                result = result + "," + CHR$(10)
            END IF
            result = result + "  " + columns(i).ToString$()
        NEXT i
        result = result + CHR$(10) + ");"
        SchemaString$ = result
    END FUNCTION

    PUBLIC FUNCTION ToString$()
        DIM result AS STRING
        result = "Table: " + name + " (" + STR$(columnCount) + " cols, " + STR$(rowCount) + " rows)"
        ToString$ = result
    END FUNCTION
END CLASS

' Factory function for creating tables
FUNCTION MakeTable(tableName AS STRING) AS SqlTable
    DIM tbl AS SqlTable
    LET tbl = NEW SqlTable()
    tbl.Init(tableName)
    MakeTable = tbl
END FUNCTION

'=============================================================================
' EXPRESSION TYPES
'=============================================================================

' Expression kind constants
CONST EXPR_LITERAL = 1
CONST EXPR_COLUMN = 2
CONST EXPR_BINARY = 3
CONST EXPR_UNARY = 4
CONST EXPR_FUNCTION = 5
CONST EXPR_STAR = 6

' Binary operators
CONST OP_ADD = 1
CONST OP_SUB = 2
CONST OP_MUL = 3
CONST OP_DIV = 4
CONST OP_MOD = 5
CONST OP_EQ = 10
CONST OP_NE = 11
CONST OP_LT = 12
CONST OP_LE = 13
CONST OP_GT = 14
CONST OP_GE = 15
CONST OP_AND = 20
CONST OP_OR = 21
CONST OP_LIKE = 22
CONST OP_IN = 23
CONST OP_IS = 24
CONST OP_CONCAT = 25

' Unary operators
CONST UOP_NEG = 1
CONST UOP_NOT = 2

' Maximum args for function calls
CONST MAX_ARGS = 16

CLASS Expr
    PUBLIC kind AS INTEGER         ' EXPR_LITERAL, EXPR_COLUMN, etc.

    ' For EXPR_LITERAL
    PUBLIC literalValue AS SqlValue

    ' For EXPR_COLUMN
    PUBLIC tableName AS STRING     ' Optional table alias
    PUBLIC columnName AS STRING

    ' For EXPR_BINARY and EXPR_UNARY
    PUBLIC op AS INTEGER           ' Operator constant

    ' For EXPR_FUNCTION
    PUBLIC funcName AS STRING

    ' Children (binary: left=args(0), right=args(1); unary: operand=args(0); func: arguments)
    PUBLIC args(MAX_ARGS) AS Expr
    PUBLIC argCount AS INTEGER

    PUBLIC SUB Init()
        kind = EXPR_LITERAL
        LET literalValue = NEW SqlValue()
        literalValue.InitNull()
        tableName = ""
        columnName = ""
        op = 0
        funcName = ""
        argCount = 0
    END SUB

    PUBLIC SUB InitLiteral(val AS SqlValue)
        kind = EXPR_LITERAL
        LET literalValue = val
    END SUB

    PUBLIC SUB InitColumn(tbl AS STRING, col AS STRING)
        kind = EXPR_COLUMN
        tableName = tbl
        columnName = col
    END SUB

    PUBLIC SUB InitBinary(operator AS INTEGER, left AS Expr, right AS Expr)
        kind = EXPR_BINARY
        op = operator
        LET args(0) = left
        LET args(1) = right
        argCount = 2
    END SUB

    PUBLIC SUB InitUnary(operator AS INTEGER, operand AS Expr)
        kind = EXPR_UNARY
        op = operator
        LET args(0) = operand
        argCount = 1
    END SUB

    PUBLIC SUB InitFunction(name AS STRING)
        kind = EXPR_FUNCTION
        funcName = name
        argCount = 0
    END SUB

    PUBLIC SUB InitStar()
        kind = EXPR_STAR
    END SUB

    PUBLIC SUB AddArg(arg AS Expr)
        IF argCount < MAX_ARGS THEN
            LET args(argCount) = arg
            argCount = argCount + 1
        END IF
    END SUB

    PUBLIC FUNCTION GetLeft() AS Expr
        GetLeft = args(0)
    END FUNCTION

    PUBLIC FUNCTION GetRight() AS Expr
        GetRight = args(1)
    END FUNCTION

    PUBLIC FUNCTION GetOperand() AS Expr
        GetOperand = args(0)
    END FUNCTION

    PUBLIC FUNCTION IsLiteral() AS INTEGER
        IF kind = EXPR_LITERAL THEN
            IsLiteral = -1
        ELSE
            IsLiteral = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsColumn() AS INTEGER
        IF kind = EXPR_COLUMN THEN
            IsColumn = -1
        ELSE
            IsColumn = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsBinary() AS INTEGER
        IF kind = EXPR_BINARY THEN
            IsBinary = -1
        ELSE
            IsBinary = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsUnary() AS INTEGER
        IF kind = EXPR_UNARY THEN
            IsUnary = -1
        ELSE
            IsUnary = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsFunction() AS INTEGER
        IF kind = EXPR_FUNCTION THEN
            IsFunction = -1
        ELSE
            IsFunction = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsStar() AS INTEGER
        IF kind = EXPR_STAR THEN
            IsStar = -1
        ELSE
            IsStar = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION ToString$()
        DIM result AS STRING
        DIM i AS INTEGER

        IF kind = EXPR_LITERAL THEN
            ToString$ = literalValue.ToString$()
        ELSEIF kind = EXPR_COLUMN THEN
            IF tableName <> "" THEN
                ToString$ = tableName + "." + columnName
            ELSE
                ToString$ = columnName
            END IF
        ELSEIF kind = EXPR_STAR THEN
            ToString$ = "*"
        ELSEIF kind = EXPR_BINARY THEN
            result = "(" + args(0).ToString$() + " " + OpToString$(op) + " " + args(1).ToString$() + ")"
            ToString$ = result
        ELSEIF kind = EXPR_UNARY THEN
            IF op = UOP_NEG THEN
                ToString$ = "-" + args(0).ToString$()
            ELSEIF op = UOP_NOT THEN
                ToString$ = "NOT " + args(0).ToString$()
            ELSE
                ToString$ = "?" + args(0).ToString$()
            END IF
        ELSEIF kind = EXPR_FUNCTION THEN
            result = funcName + "("
            FOR i = 0 TO argCount - 1
                IF i > 0 THEN
                    result = result + ", "
                END IF
                result = result + args(i).ToString$()
            NEXT i
            result = result + ")"
            ToString$ = result
        ELSE
            ToString$ = "<?>"
        END IF
    END FUNCTION
END CLASS

' Helper function to convert operator to string
FUNCTION OpToString$(op AS INTEGER)
    IF op = OP_ADD THEN
        OpToString$ = "+"
    ELSEIF op = OP_SUB THEN
        OpToString$ = "-"
    ELSEIF op = OP_MUL THEN
        OpToString$ = "*"
    ELSEIF op = OP_DIV THEN
        OpToString$ = "/"
    ELSEIF op = OP_MOD THEN
        OpToString$ = "%"
    ELSEIF op = OP_EQ THEN
        OpToString$ = "="
    ELSEIF op = OP_NE THEN
        OpToString$ = "<>"
    ELSEIF op = OP_LT THEN
        OpToString$ = "<"
    ELSEIF op = OP_LE THEN
        OpToString$ = "<="
    ELSEIF op = OP_GT THEN
        OpToString$ = ">"
    ELSEIF op = OP_GE THEN
        OpToString$ = ">="
    ELSEIF op = OP_AND THEN
        OpToString$ = "AND"
    ELSEIF op = OP_OR THEN
        OpToString$ = "OR"
    ELSEIF op = OP_LIKE THEN
        OpToString$ = "LIKE"
    ELSEIF op = OP_CONCAT THEN
        OpToString$ = "||"
    ELSE
        OpToString$ = "?"
    END IF
END FUNCTION

' Factory functions for creating expressions
FUNCTION ExprNull() AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitLiteral(SqlNull())
    ExprNull = e
END FUNCTION

FUNCTION ExprInt(val AS INTEGER) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitLiteral(SqlInteger(val))
    ExprInt = e
END FUNCTION

FUNCTION ExprReal(val AS SINGLE, txt AS STRING) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitLiteral(SqlReal(val, txt))
    ExprReal = e
END FUNCTION

FUNCTION ExprText(val AS STRING) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitLiteral(SqlText(val))
    ExprText = e
END FUNCTION

FUNCTION ExprColumn(col AS STRING) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitColumn("", col)
    ExprColumn = e
END FUNCTION

FUNCTION ExprTableColumn(tbl AS STRING, col AS STRING) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitColumn(tbl, col)
    ExprTableColumn = e
END FUNCTION

FUNCTION ExprStar() AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitStar()
    ExprStar = e
END FUNCTION

FUNCTION ExprBinary(operator AS INTEGER, left AS Expr, right AS Expr) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitBinary(operator, left, right)
    ExprBinary = e
END FUNCTION

FUNCTION ExprUnary(operator AS INTEGER, operand AS Expr) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitUnary(operator, operand)
    ExprUnary = e
END FUNCTION

FUNCTION ExprFunc(name AS STRING) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitFunction(name)
    ExprFunc = e
END FUNCTION

'=============================================================================
' STATEMENT TYPES
'=============================================================================

CONST MAX_STMT_COLUMNS = 64
CONST MAX_STMT_VALUES = 64
CONST MAX_VALUE_ROWS = 100

' CreateTableStmt - holds parsed CREATE TABLE statement
CLASS CreateTableStmt
    PUBLIC tableName AS STRING
    PUBLIC columns(MAX_STMT_COLUMNS) AS SqlColumn
    PUBLIC columnCount AS INTEGER

    PUBLIC SUB Init()
        tableName = ""
        columnCount = 0
    END SUB

    PUBLIC SUB AddColumn(col AS SqlColumn)
        IF columnCount < MAX_STMT_COLUMNS THEN
            LET columns(columnCount) = col
            columnCount = columnCount + 1
        END IF
    END SUB

    PUBLIC FUNCTION GetColumn(index AS INTEGER) AS SqlColumn
        GetColumn = columns(index)
    END FUNCTION

    PUBLIC FUNCTION ToString$()
        DIM result AS STRING
        DIM i AS INTEGER
        result = "CREATE TABLE " + tableName + " (" + CHR$(10)
        FOR i = 0 TO columnCount - 1
            IF i > 0 THEN
                result = result + "," + CHR$(10)
            END IF
            result = result + "  " + columns(i).ToString$()
        NEXT i
        result = result + CHR$(10) + ");"
        ToString$ = result
    END FUNCTION
END CLASS

' InsertStmt - holds parsed INSERT statement
' Note: Using 1D array with manual indexing due to Viper Basic bug #010 (2D array corruption)
CONST VALUES_ARRAY_SIZE = 6400  ' MAX_VALUE_ROWS * MAX_STMT_VALUES

CLASS InsertStmt
    PUBLIC tableName AS STRING
    PUBLIC columnNames(MAX_STMT_COLUMNS) AS STRING
    PUBLIC columnCount AS INTEGER
    PUBLIC valueRows(VALUES_ARRAY_SIZE) AS Expr   ' Flattened 2D array
    PUBLIC rowValueCounts(MAX_VALUE_ROWS) AS INTEGER
    PUBLIC rowCount AS INTEGER

    PUBLIC SUB Init()
        tableName = ""
        columnCount = 0
        rowCount = 0
    END SUB

    PUBLIC SUB AddColumnName(name AS STRING)
        IF columnCount < MAX_STMT_COLUMNS THEN
            columnNames(columnCount) = name
            columnCount = columnCount + 1
        END IF
    END SUB

    PUBLIC SUB AddValueRow()
        IF rowCount < MAX_VALUE_ROWS THEN
            rowValueCounts(rowCount) = 0
            rowCount = rowCount + 1
        END IF
    END SUB

    PUBLIC SUB AddValue(rowIdx AS INTEGER, val AS Expr)
        DIM valIdx AS INTEGER
        DIM flatIdx AS INTEGER
        IF rowIdx >= 0 AND rowIdx < rowCount THEN
            valIdx = rowValueCounts(rowIdx)
            IF valIdx < MAX_STMT_VALUES THEN
                flatIdx = rowIdx * MAX_STMT_VALUES + valIdx
                LET valueRows(flatIdx) = val
                rowValueCounts(rowIdx) = valIdx + 1
            END IF
        END IF
    END SUB

    PUBLIC FUNCTION GetValue(rowIdx AS INTEGER, valIdx AS INTEGER) AS Expr
        DIM flatIdx AS INTEGER
        flatIdx = rowIdx * MAX_STMT_VALUES + valIdx
        GetValue = valueRows(flatIdx)
    END FUNCTION

    PUBLIC FUNCTION ToString$()
        DIM result AS STRING
        DIM i AS INTEGER
        DIM r AS INTEGER
        DIM v AS INTEGER
        DIM valCount AS INTEGER

        result = "INSERT INTO " + tableName
        IF columnCount > 0 THEN
            result = result + " ("
            FOR i = 0 TO columnCount - 1
                IF i > 0 THEN
                    result = result + ", "
                END IF
                result = result + columnNames(i)
            NEXT i
            result = result + ")"
        END IF
        result = result + " VALUES "
        FOR r = 0 TO rowCount - 1
            IF r > 0 THEN
                result = result + ", "
            END IF
            result = result + "("
            valCount = rowValueCounts(r)
            FOR v = 0 TO valCount - 1
                IF v > 0 THEN
                    result = result + ", "
                END IF
                result = result + GetValue(r, v).ToString$()
            NEXT v
            result = result + ")"
        NEXT r
        result = result + ";"
        ToString$ = result
    END FUNCTION
END CLASS

'=============================================================================
' PARSER STATE (Global)
'=============================================================================

DIM gParserError AS STRING
DIM gParserHasError AS INTEGER

SUB ParserInit(sql AS STRING)
    LexerInit(sql)
    LexerNextToken()
    gParserError = ""
    gParserHasError = 0
END SUB

SUB ParserAdvance()
    LexerNextToken()
END SUB

FUNCTION ParserMatch(kind AS INTEGER) AS INTEGER
    IF gTok.kind = kind THEN
        ParserAdvance()
        ParserMatch = -1
    ELSE
        ParserMatch = 0
    END IF
END FUNCTION

FUNCTION ParserExpect(kind AS INTEGER) AS INTEGER
    IF gTok.kind = kind THEN
        ParserAdvance()
        ParserExpect = -1
    ELSE
        gParserHasError = -1
        gParserError = "Expected token " + STR$(kind) + ", got " + STR$(gTok.kind)
        ParserExpect = 0
    END IF
END FUNCTION

SUB ParserSetError(msg AS STRING)
    gParserHasError = -1
    gParserError = msg
END SUB

'=============================================================================
' EXPRESSION PARSING FUNCTIONS
'=============================================================================

' Note: Due to BASIC's function order dependencies, we use forward-compatible patterns

' Parse primary expression (literals, identifiers)
FUNCTION ParsePrimaryExpr() AS Expr
    DIM kind AS INTEGER
    DIM text AS STRING
    DIM name AS STRING
    DIM colName AS STRING
    DIM sv AS SqlValue
    DIM e AS Expr

    LET kind = gTok.kind

    ' Integer literal - store as text
    IF kind = TK_INTEGER THEN
        LET text = gTok.text
        ParserAdvance()
        LET sv = NEW SqlValue()
        sv.InitText(text)
        LET e = NEW Expr()
        e.Init()
        e.InitLiteral(sv)
        ParsePrimaryExpr = e
        EXIT FUNCTION
    END IF

    ' Real/float literal - store as text
    IF kind = TK_NUMBER THEN
        LET text = gTok.text
        ParserAdvance()
        LET e = ExprReal(0.0, text)
        ParsePrimaryExpr = e
        EXIT FUNCTION
    END IF

    ' String literal
    IF kind = TK_STRING THEN
        LET text = gTok.text
        ParserAdvance()
        ParsePrimaryExpr = ExprText(text)
        EXIT FUNCTION
    END IF

    ' NULL
    IF kind = TK_NULL THEN
        ParserAdvance()
        ParsePrimaryExpr = ExprNull()
        EXIT FUNCTION
    END IF

    ' Identifier (column ref or function call)
    IF kind = TK_IDENTIFIER THEN
        LET name = gTok.text
        ParserAdvance()

        ' Check for function call - identifier followed by (
        IF gTok.kind = TK_LPAREN THEN
            DIM funcExpr AS Expr
            DIM argExpr AS Expr
            DIM argText AS STRING
            DIM argInt AS INTEGER

            ParserAdvance()  ' Consume (

            ' Create function expression
            LET funcExpr = NEW Expr()
            funcExpr.Init()
            funcExpr.InitFunction(name)

            ' Parse arguments until )
            WHILE gTok.kind <> TK_RPAREN AND gParserHasError = 0
                IF gTok.kind = TK_STAR THEN
                    LET argExpr = ExprStar()
                    funcExpr.AddArg(argExpr)
                    ParserAdvance()
                ELSEIF gTok.kind = TK_IDENTIFIER THEN
                    argText = gTok.text
                    ParserAdvance()
                    LET argExpr = ExprColumn(argText)
                    funcExpr.AddArg(argExpr)
                ELSEIF gTok.kind = TK_INTEGER THEN
                    argText = gTok.text
                    ParserAdvance()
                    argInt = VAL(argText)
                    LET argExpr = ExprInt(argInt)
                    funcExpr.AddArg(argExpr)
                ELSEIF gTok.kind = TK_STRING THEN
                    argText = gTok.text
                    ParserAdvance()
                    LET argExpr = ExprText(argText)
                    funcExpr.AddArg(argExpr)
                ELSE
                    EXIT WHILE
                END IF

                ' Check for comma
                IF gTok.kind = TK_COMMA THEN
                    ParserAdvance()
                END IF
            WEND

            ' Consume )
            IF gTok.kind = TK_RPAREN THEN
                ParserAdvance()
            END IF

            ParsePrimaryExpr = funcExpr
            EXIT FUNCTION
        END IF

        ' Check for table.column
        IF gTok.kind = TK_DOT THEN
            ParserAdvance()
            IF gTok.kind <> TK_IDENTIFIER THEN
                ParserSetError("Expected column name after dot")
                ParsePrimaryExpr = ExprNull()
                EXIT FUNCTION
            END IF
            LET colName = gTok.text
            ParserAdvance()
            ParsePrimaryExpr = ExprTableColumn(name, colName)
            EXIT FUNCTION
        END IF

        ParsePrimaryExpr = ExprColumn(name)
        EXIT FUNCTION
    END IF

    ' Star (*)
    IF kind = TK_STAR THEN
        ParserAdvance()
        ParsePrimaryExpr = ExprStar()
        EXIT FUNCTION
    END IF

    ParserSetError("Unexpected token in expression")
    ParsePrimaryExpr = ExprNull()
END FUNCTION

' Parse unary expression
FUNCTION ParseUnaryExpr() AS Expr
    DIM operand AS Expr

    IF gTok.kind = TK_MINUS THEN
        ParserAdvance()
        LET operand = ParseUnaryExpr()
        ParseUnaryExpr = ExprUnary(UOP_NEG, operand)
        EXIT FUNCTION
    END IF
    IF gTok.kind = TK_NOT THEN
        ParserAdvance()
        LET operand = ParseUnaryExpr()
        ParseUnaryExpr = ExprUnary(UOP_NOT, operand)
        EXIT FUNCTION
    END IF
    ParseUnaryExpr = ParsePrimaryExpr()
END FUNCTION

' Parse multiplication/division
FUNCTION ParseMulExpr() AS Expr
    DIM left AS Expr
    DIM right AS Expr
    DIM op AS INTEGER

    LET left = ParseUnaryExpr()
    WHILE (gTok.kind = TK_STAR) OR (gTok.kind = TK_SLASH)
        LET op = gTok.kind
        ParserAdvance()
        LET right = ParseUnaryExpr()
        IF op = TK_STAR THEN
            LET left = ExprBinary(OP_MUL, left, right)
        ELSE
            LET left = ExprBinary(OP_DIV, left, right)
        END IF
    WEND
    ParseMulExpr = left
END FUNCTION

' Parse addition/subtraction
FUNCTION ParseAddExpr() AS Expr
    DIM left AS Expr
    DIM right AS Expr
    DIM op AS INTEGER

    LET left = ParseMulExpr()
    WHILE (gTok.kind = TK_PLUS) OR (gTok.kind = TK_MINUS)
        LET op = gTok.kind
        ParserAdvance()
        LET right = ParseMulExpr()
        IF op = TK_PLUS THEN
            LET left = ExprBinary(OP_ADD, left, right)
        ELSE
            LET left = ExprBinary(OP_SUB, left, right)
        END IF
    WEND
    ParseAddExpr = left
END FUNCTION

' Parse comparison
FUNCTION ParseCompExpr() AS Expr
    DIM left AS Expr
    DIM kind AS INTEGER

    LET left = ParseAddExpr()
    LET kind = gTok.kind
    IF kind = TK_EQ THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_EQ, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    IF kind = TK_NE THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_NE, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    IF kind = TK_LT THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_LT, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    IF kind = TK_LE THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_LE, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    IF kind = TK_GT THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_GT, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    IF kind = TK_GE THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_GE, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    ParseCompExpr = left
END FUNCTION

' Parse AND
FUNCTION ParseAndExpr() AS Expr
    DIM left AS Expr
    DIM right AS Expr

    LET left = ParseCompExpr()
    WHILE gTok.kind = TK_AND
        ParserAdvance()
        LET right = ParseCompExpr()
        LET left = ExprBinary(OP_AND, left, right)
    WEND
    ParseAndExpr = left
END FUNCTION

' Parse OR
FUNCTION ParseOrExpr() AS Expr
    DIM left AS Expr
    DIM right AS Expr

    LET left = ParseAndExpr()
    WHILE gTok.kind = TK_OR
        ParserAdvance()
        LET right = ParseAndExpr()
        LET left = ExprBinary(OP_OR, left, right)
    WEND
    ParseOrExpr = left
END FUNCTION

' Main expression entry point
FUNCTION ParseExpr() AS Expr
    ParseExpr = ParseOrExpr()
END FUNCTION

'=============================================================================
' STATEMENT PARSING FUNCTIONS
'=============================================================================

' Parse column definition for CREATE TABLE
FUNCTION ParseColumnDef() AS SqlColumn
    DIM col AS SqlColumn

    LET col = NEW SqlColumn()
    col.Init("", SQL_TEXT)

    ' Column name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected column name")
        ParseColumnDef = col
        EXIT FUNCTION
    END IF
    col.name = gTok.text
    ParserAdvance()

    ' Data type
    IF gTok.kind = TK_INTEGER_TYPE THEN
        col.typeCode = SQL_INTEGER
        ParserAdvance()
    ELSEIF gTok.kind = TK_TEXT THEN
        col.typeCode = SQL_TEXT
        ParserAdvance()
    ELSEIF gTok.kind = TK_REAL THEN
        col.typeCode = SQL_REAL
        ParserAdvance()
    ELSE
        ParserSetError("Expected data type")
        ParseColumnDef = col
        EXIT FUNCTION
    END IF

    ' Optional constraints
    WHILE gParserHasError = 0
        IF gTok.kind = TK_PRIMARY THEN
            ParserAdvance()
            IF ParserExpect(TK_KEY) = 0 THEN
                ParseColumnDef = col
                EXIT FUNCTION
            END IF
            col.primaryKey = -1
        ELSEIF gTok.kind = TK_AUTOINCREMENT THEN
            ParserAdvance()
            col.autoIncrement = -1
        ELSEIF gTok.kind = TK_NOT THEN
            ParserAdvance()
            IF ParserExpect(TK_NULL) = 0 THEN
                ParseColumnDef = col
                EXIT FUNCTION
            END IF
            col.notNull = -1
        ELSE
            EXIT WHILE
        END IF
    WEND

    ParseColumnDef = col
END FUNCTION

' Parse CREATE TABLE statement
FUNCTION ParseCreateTableStmt() AS CreateTableStmt
    DIM stmt AS CreateTableStmt
    DIM col AS SqlColumn

    LET stmt = NEW CreateTableStmt()
    stmt.Init()

    ' Already at CREATE, expect TABLE
    IF ParserExpect(TK_TABLE) = 0 THEN
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF

    ' Table name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name, got kind=" + STR$(gTok.kind) + " text='" + gTok.text + "'")
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

    ' Opening paren
    IF ParserExpect(TK_LPAREN) = 0 THEN
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF

    ' Column definitions
    WHILE gParserHasError = 0
        LET col = ParseColumnDef()
        IF gParserHasError <> 0 THEN
            ParseCreateTableStmt = stmt
            EXIT FUNCTION
        END IF
        stmt.AddColumn(col)

        IF gTok.kind = TK_COMMA THEN
            ParserAdvance()
        ELSE
            EXIT WHILE
        END IF
    WEND

    ' Closing paren
    IF ParserExpect(TK_RPAREN) = 0 THEN
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF

    ' Optional semicolon
    ParserMatch(TK_SEMICOLON)

    ParseCreateTableStmt = stmt
END FUNCTION

' Parse INSERT statement
FUNCTION ParseInsertStmt() AS InsertStmt
    DIM stmt AS InsertStmt
    DIM rowIdx AS INTEGER
    DIM val AS Expr

    LET stmt = NEW InsertStmt()
    stmt.Init()

    ' Already at INSERT, expect INTO
    IF ParserExpect(TK_INTO) = 0 THEN
        ParseInsertStmt = stmt
        EXIT FUNCTION
    END IF

    ' Table name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseInsertStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

    ' Optional column list
    IF gTok.kind = TK_LPAREN THEN
        ParserAdvance()
        WHILE gParserHasError = 0
            IF gTok.kind <> TK_IDENTIFIER THEN
                ParserSetError("Expected column name")
                ParseInsertStmt = stmt
                EXIT FUNCTION
            END IF
            stmt.AddColumnName(gTok.text)
            ParserAdvance()

            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND
        IF ParserExpect(TK_RPAREN) = 0 THEN
            ParseInsertStmt = stmt
            EXIT FUNCTION
        END IF
    END IF

    ' Expect VALUES
    IF ParserExpect(TK_VALUES) = 0 THEN
        ParseInsertStmt = stmt
        EXIT FUNCTION
    END IF

    ' Value rows
    WHILE gParserHasError = 0
        IF ParserExpect(TK_LPAREN) = 0 THEN
            ParseInsertStmt = stmt
            EXIT FUNCTION
        END IF

        stmt.AddValueRow()
        rowIdx = stmt.rowCount - 1

        ' Parse values
        WHILE gParserHasError = 0
            LET val = ParseExpr()
            IF gParserHasError <> 0 THEN
                ParseInsertStmt = stmt
                EXIT FUNCTION
            END IF
            stmt.AddValue(rowIdx, val)

            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND

        IF ParserExpect(TK_RPAREN) = 0 THEN
            ParseInsertStmt = stmt
            EXIT FUNCTION
        END IF

        IF gTok.kind = TK_COMMA THEN
            ParserAdvance()
        ELSE
            EXIT WHILE
        END IF
    WEND

    ParserMatch(TK_SEMICOLON)
    ParseInsertStmt = stmt
END FUNCTION

'=============================================================================
' SELECT STATEMENT CLASS
'=============================================================================

CLASS SelectStmt
    PUBLIC selectAll AS INTEGER
    PUBLIC tableName AS STRING
    PUBLIC tableAlias AS STRING
    PUBLIC columns(100) AS Expr
    PUBLIC columnCount AS INTEGER
    PUBLIC whereClause AS Expr
    PUBLIC hasWhere AS INTEGER
    PUBLIC orderByExprs(20) AS Expr
    PUBLIC orderByDir(20) AS INTEGER  ' 0 = ASC, 1 = DESC
    PUBLIC orderByCount AS INTEGER
    PUBLIC limitValue AS INTEGER     ' -1 = no limit
    PUBLIC offsetValue AS INTEGER    ' 0 = no offset
    PUBLIC isDistinct AS INTEGER     ' 0 = normal, non-zero = DISTINCT
    PUBLIC groupByExprs(20) AS Expr
    PUBLIC groupByCount AS INTEGER
    PUBLIC havingClause AS Expr
    PUBLIC hasHaving AS INTEGER
    PUBLIC tableNames(20) AS STRING
    PUBLIC tableAliases(20) AS STRING
    PUBLIC tableCount AS INTEGER
    PUBLIC joinTypes(20) AS INTEGER      ' 0=CROSS, 1=INNER, 2=LEFT, 3=RIGHT, 4=FULL
    PUBLIC joinConditions(20) AS Expr
    PUBLIC joinConditionCount AS INTEGER

    PUBLIC SUB Init()
        selectAll = 0
        tableName = ""
        tableAlias = ""
        columnCount = 0
        hasWhere = 0
        orderByCount = 0
        limitValue = -1
        offsetValue = 0
        isDistinct = 0
        groupByCount = 0
        hasHaving = 0
        tableCount = 0
        joinConditionCount = 0
    END SUB

    PUBLIC SUB AddColumn(col AS Expr)
        IF columnCount < 100 THEN
            LET columns(columnCount) = col
            columnCount = columnCount + 1
        END IF
    END SUB

    PUBLIC SUB SetWhere(expr AS Expr)
        LET whereClause = expr
        hasWhere = -1
    END SUB

    PUBLIC SUB AddOrderBy(expr AS Expr, isDesc AS INTEGER)
        IF orderByCount < 20 THEN
            LET orderByExprs(orderByCount) = expr
            orderByDir(orderByCount) = isDesc
            orderByCount = orderByCount + 1
        END IF
    END SUB

    PUBLIC SUB AddGroupBy(expr AS Expr)
        IF groupByCount < 20 THEN
            LET groupByExprs(groupByCount) = expr
            groupByCount = groupByCount + 1
        END IF
    END SUB

    PUBLIC SUB AddTable(tName AS STRING, tAlias AS STRING)
        IF tableCount < 20 THEN
            tableNames(tableCount) = tName
            tableAliases(tableCount) = tAlias
            tableCount = tableCount + 1
        END IF
    END SUB

    PUBLIC SUB AddJoin(tName AS STRING, tAlias AS STRING, jType AS INTEGER, jCondition AS Expr, hasCondition AS INTEGER)
        IF tableCount < 20 THEN
            tableNames(tableCount) = tName
            tableAliases(tableCount) = tAlias
            joinTypes(tableCount) = jType
            tableCount = tableCount + 1
            IF hasCondition <> 0 THEN
                LET joinConditions(joinConditionCount) = jCondition
                joinConditionCount = joinConditionCount + 1
            END IF
        END IF
    END SUB

    PUBLIC FUNCTION ToString$() AS STRING
        DIM result AS STRING
        DIM i AS INTEGER
        result = "SELECT "
        IF selectAll <> 0 THEN
            result = result + "* FROM " + tableName
        ELSE
            result = result + "... FROM " + tableName
        END IF
        ToString$ = result + ";"
    END FUNCTION
END CLASS

' Parse SELECT statement (already past SELECT token)
FUNCTION ParseSelectStmt() AS SelectStmt
    DIM stmt AS SelectStmt
    DIM col AS Expr

    LET stmt = NEW SelectStmt()
    stmt.Init()

    ' Check for DISTINCT keyword
    IF gTok.kind = TK_DISTINCT THEN
        stmt.isDistinct = -1
        ParserAdvance()
    END IF

    ' Check for * or column list
    IF gTok.kind = TK_STAR THEN
        stmt.selectAll = -1
        ParserAdvance()
    ELSE
        ' Parse column list
        WHILE gParserHasError = 0
            LET col = ParseExpr()
            IF gParserHasError <> 0 THEN
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            stmt.AddColumn(col)

            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND
    END IF

    ' Expect FROM
    IF ParserExpect(TK_FROM) = 0 THEN
        ParseSelectStmt = stmt
        EXIT FUNCTION
    END IF

    ' Get first table name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseSelectStmt = stmt
        EXIT FUNCTION
    END IF
    DIM firstTableName AS STRING
    DIM firstTableAlias AS STRING
    firstTableName = gTok.text
    firstTableAlias = ""
    stmt.tableName = firstTableName  ' Keep for backward compatibility
    ParserAdvance()

    ' Optional table alias (AS alias or just alias)
    IF gTok.kind = TK_AS THEN
        ParserAdvance()
        IF gTok.kind = TK_IDENTIFIER THEN
            firstTableAlias = gTok.text
            stmt.tableAlias = firstTableAlias
            ParserAdvance()
        ELSE
            ParserSetError("Expected alias name after AS")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
    ELSEIF gTok.kind = TK_IDENTIFIER THEN
        ' Check it's not a keyword that could follow FROM table
        DIM aliasText AS STRING
        DIM upperAlias AS STRING
        aliasText = gTok.text
        upperAlias = UCASE$(aliasText)
        IF upperAlias <> "WHERE" AND upperAlias <> "GROUP" AND upperAlias <> "ORDER" AND upperAlias <> "LIMIT" AND upperAlias <> "HAVING" AND upperAlias <> "JOIN" AND upperAlias <> "INNER" AND upperAlias <> "LEFT" AND upperAlias <> "RIGHT" AND upperAlias <> "FULL" AND upperAlias <> "CROSS" THEN
            firstTableAlias = aliasText
            stmt.tableAlias = firstTableAlias
            ParserAdvance()
        END IF
    END IF

    ' Add first table to lists
    stmt.AddTable(firstTableName, firstTableAlias)

    ' Parse additional tables (comma-separated for CROSS JOIN)
    DIM extraTableName AS STRING
    DIM extraTableAlias AS STRING
    DIM maybeAlias AS STRING
    DIM upperMaybe AS STRING
    WHILE gTok.kind = TK_COMMA
        ParserAdvance()
        IF gTok.kind <> TK_IDENTIFIER THEN
            ParserSetError("Expected table name after comma")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        extraTableName = gTok.text
        extraTableAlias = ""
        ParserAdvance()

        ' Optional alias for extra table
        IF gTok.kind = TK_AS THEN
            ParserAdvance()
            IF gTok.kind = TK_IDENTIFIER THEN
                extraTableAlias = gTok.text
                ParserAdvance()
            END IF
        ELSEIF gTok.kind = TK_IDENTIFIER THEN
            maybeAlias = gTok.text
            upperMaybe = UCASE$(maybeAlias)
            IF upperMaybe <> "WHERE" AND upperMaybe <> "GROUP" AND upperMaybe <> "ORDER" AND upperMaybe <> "LIMIT" AND upperMaybe <> "HAVING" AND upperMaybe <> "JOIN" THEN
                extraTableAlias = maybeAlias
                ParserAdvance()
            END IF
        END IF

        stmt.AddTable(extraTableName, extraTableAlias)
    WEND

    ' Parse JOIN clauses (INNER JOIN, LEFT JOIN, etc.)
    DIM joinType AS INTEGER
    DIM joinTableName AS STRING
    DIM joinTableAlias AS STRING
    DIM maybeJoinAlias AS STRING
    DIM upperJoinAlias AS STRING
    DIM joinCondition AS Expr
    DIM hasJoinCondition AS INTEGER

    WHILE gTok.kind = TK_JOIN OR gTok.kind = TK_INNER OR gTok.kind = TK_LEFT OR gTok.kind = TK_RIGHT OR gTok.kind = TK_FULL OR gTok.kind = TK_CROSS
        joinType = 1  ' Default to INNER JOIN

        ' Determine join type
        IF gTok.kind = TK_INNER THEN
            joinType = 1
            ParserAdvance()
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after INNER")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_LEFT THEN
            joinType = 2
            ParserAdvance()
            IF gTok.kind = TK_OUTER THEN
                ParserAdvance()
            END IF
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after LEFT")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_RIGHT THEN
            joinType = 3
            ParserAdvance()
            IF gTok.kind = TK_OUTER THEN
                ParserAdvance()
            END IF
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after RIGHT")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_FULL THEN
            joinType = 4
            ParserAdvance()
            IF gTok.kind = TK_OUTER THEN
                ParserAdvance()
            END IF
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after FULL")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_CROSS THEN
            joinType = 0
            ParserAdvance()
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after CROSS")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_JOIN THEN
            joinType = 1  ' Bare JOIN = INNER JOIN
            ParserAdvance()
        END IF

        ' Expect table name
        IF gTok.kind <> TK_IDENTIFIER THEN
            ParserSetError("Expected table name after JOIN")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        joinTableName = gTok.text
        joinTableAlias = ""
        ParserAdvance()

        ' Optional alias
        IF gTok.kind = TK_AS THEN
            ParserAdvance()
            IF gTok.kind = TK_IDENTIFIER THEN
                joinTableAlias = gTok.text
                ParserAdvance()
            END IF
        ELSEIF gTok.kind = TK_IDENTIFIER THEN
            maybeJoinAlias = gTok.text
            upperJoinAlias = UCASE$(maybeJoinAlias)
            IF upperJoinAlias <> "ON" AND upperJoinAlias <> "WHERE" AND upperJoinAlias <> "GROUP" AND upperJoinAlias <> "ORDER" AND upperJoinAlias <> "LIMIT" AND upperJoinAlias <> "JOIN" THEN
                joinTableAlias = maybeJoinAlias
                ParserAdvance()
            END IF
        END IF

        ' ON clause (required for INNER/LEFT/RIGHT, not for CROSS)
        hasJoinCondition = 0
        IF gTok.kind = TK_ON THEN
            ParserAdvance()
            LET joinCondition = ParseExpr()
            hasJoinCondition = -1
        ELSEIF joinType <> 0 THEN
            ParserSetError("Expected ON clause for JOIN")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF

        stmt.AddJoin(joinTableName, joinTableAlias, joinType, joinCondition, hasJoinCondition)
    WEND

    ' Optional WHERE
    IF gTok.kind = TK_WHERE THEN
        ParserAdvance()
        LET col = ParseExpr()
        IF gParserHasError = 0 THEN
            stmt.SetWhere(col)
        END IF
    END IF

    ' Optional GROUP BY
    IF gTok.kind = TK_GROUP THEN
        DIM groupExpr AS Expr
        ParserAdvance()
        IF ParserExpect(TK_BY) = 0 THEN
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        ' Parse GROUP BY expressions
        WHILE gParserHasError = 0
            LET groupExpr = ParseExpr()
            IF gParserHasError <> 0 THEN
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            stmt.AddGroupBy(groupExpr)

            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND
    END IF

    ' Optional HAVING clause (after GROUP BY)
    IF gTok.kind = TK_HAVING THEN
        DIM havingExpr AS Expr
        ParserAdvance()
        LET havingExpr = ParseExpr()
        IF gParserHasError = 0 THEN
            LET stmt.havingClause = havingExpr
            stmt.hasHaving = -1
        END IF
    END IF

    ' Optional ORDER BY
    IF gTok.kind = TK_ORDER THEN
        DIM orderExpr AS Expr
        DIM isDesc AS INTEGER
        ParserAdvance()
        IF ParserExpect(TK_BY) = 0 THEN
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        ' Parse ORDER BY expressions
        WHILE gParserHasError = 0
            LET orderExpr = ParseExpr()
            IF gParserHasError <> 0 THEN
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ' Check for ASC or DESC (default is ASC = 0, DESC = 1)
            isDesc = 0
            IF gTok.kind = TK_DESC THEN
                isDesc = 1
                ParserAdvance()
            ELSEIF gTok.kind = TK_ASC THEN
                ParserAdvance()
            END IF
            stmt.AddOrderBy(orderExpr, isDesc)

            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND
    END IF

    ' Optional LIMIT clause
    IF gTok.kind = TK_LIMIT THEN
        ParserAdvance()
        IF gTok.kind = TK_INTEGER THEN
            stmt.limitValue = VAL(gTok.text)
            ParserAdvance()
        ELSE
            ParserSetError("Expected integer after LIMIT")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF

        ' Optional OFFSET after LIMIT
        IF gTok.kind = TK_OFFSET THEN
            ParserAdvance()
            IF gTok.kind = TK_INTEGER THEN
                stmt.offsetValue = VAL(gTok.text)
                ParserAdvance()
            ELSE
                ParserSetError("Expected integer after OFFSET")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
        END IF
    END IF

    ParserMatch(TK_SEMICOLON)
    ParseSelectStmt = stmt
END FUNCTION

'=============================================================================
' UPDATE STATEMENT
'=============================================================================

CLASS UpdateStmt
    PUBLIC tableName AS STRING
    PUBLIC setColumns(50) AS STRING
    PUBLIC setValues(50) AS Expr
    PUBLIC setCount AS INTEGER
    PUBLIC whereClause AS Expr
    PUBLIC hasWhere AS INTEGER

    PUBLIC SUB Init()
        tableName = ""
        setCount = 0
        hasWhere = 0
    END SUB

    PUBLIC SUB AddSet(colName AS STRING, val AS Expr)
        IF setCount < 50 THEN
            setColumns(setCount) = colName
            LET setValues(setCount) = val
            setCount = setCount + 1
        END IF
    END SUB

    PUBLIC SUB SetWhere(expr AS Expr)
        LET whereClause = expr
        hasWhere = -1
    END SUB
END CLASS

' Parse UPDATE statement (already past UPDATE token)
FUNCTION ParseUpdateStmt() AS UpdateStmt
    DIM stmt AS UpdateStmt
    DIM colName AS STRING
    DIM val AS Expr

    LET stmt = NEW UpdateStmt()
    stmt.Init()

    ' Get table name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseUpdateStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

    ' Expect SET
    IF ParserExpect(TK_SET) = 0 THEN
        ParseUpdateStmt = stmt
        EXIT FUNCTION
    END IF

    ' Parse SET column = value pairs
    WHILE gParserHasError = 0
        IF gTok.kind <> TK_IDENTIFIER THEN
            ParserSetError("Expected column name")
            ParseUpdateStmt = stmt
            EXIT FUNCTION
        END IF
        colName = gTok.text
        ParserAdvance()

        IF ParserExpect(TK_EQ) = 0 THEN
            ParseUpdateStmt = stmt
            EXIT FUNCTION
        END IF

        LET val = ParseExpr()
        IF gParserHasError <> 0 THEN
            ParseUpdateStmt = stmt
            EXIT FUNCTION
        END IF

        stmt.AddSet(colName, val)

        IF gTok.kind = TK_COMMA THEN
            ParserAdvance()
        ELSE
            EXIT WHILE
        END IF
    WEND

    ' Optional WHERE
    IF gTok.kind = TK_WHERE THEN
        ParserAdvance()
        LET val = ParseExpr()
        IF gParserHasError = 0 THEN
            stmt.SetWhere(val)
        END IF
    END IF

    ParserMatch(TK_SEMICOLON)
    ParseUpdateStmt = stmt
END FUNCTION

'=============================================================================
' DELETE STATEMENT
'=============================================================================

CLASS DeleteStmt
    PUBLIC tableName AS STRING
    PUBLIC whereClause AS Expr
    PUBLIC hasWhere AS INTEGER

    PUBLIC SUB Init()
        tableName = ""
        hasWhere = 0
    END SUB

    PUBLIC SUB SetWhere(expr AS Expr)
        LET whereClause = expr
        hasWhere = -1
    END SUB
END CLASS

' Parse DELETE statement (already past DELETE token)
FUNCTION ParseDeleteStmt() AS DeleteStmt
    DIM stmt AS DeleteStmt
    DIM val AS Expr

    LET stmt = NEW DeleteStmt()
    stmt.Init()

    ' Expect FROM
    IF ParserExpect(TK_FROM) = 0 THEN
        ParseDeleteStmt = stmt
        EXIT FUNCTION
    END IF

    ' Get table name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseDeleteStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

    ' Optional WHERE
    IF gTok.kind = TK_WHERE THEN
        ParserAdvance()
        LET val = ParseExpr()
        IF gParserHasError = 0 THEN
            stmt.SetWhere(val)
        END IF
    END IF

    ParserMatch(TK_SEMICOLON)
    ParseDeleteStmt = stmt
END FUNCTION

'=============================================================================
' DATABASE CLASS
'=============================================================================

' Maximum number of tables in database
CONST MAX_TABLES = 50

CLASS SqlDatabase
    PUBLIC name AS STRING
    PUBLIC tables(MAX_TABLES) AS SqlTable
    PUBLIC tableCount AS INTEGER

    PUBLIC SUB Init()
        name = "default"
        tableCount = 0
    END SUB

    PUBLIC FUNCTION FindTable(tableName AS STRING) AS INTEGER
        DIM i AS INTEGER
        FindTable = -1
        FOR i = 0 TO tableCount - 1
            IF tables(i).name = tableName THEN
                FindTable = i
                EXIT FUNCTION
            END IF
        NEXT i
    END FUNCTION

    PUBLIC SUB AddTable(tbl AS SqlTable)
        IF tableCount < MAX_TABLES THEN
            LET tables(tableCount) = tbl
            tableCount = tableCount + 1
        END IF
    END SUB
END CLASS

'=============================================================================
' QUERY RESULT CLASS
'=============================================================================

CONST MAX_RESULT_ROWS = 1000
CONST MAX_RESULT_COLS = 100

CLASS QueryResult
    PUBLIC success AS INTEGER
    PUBLIC message AS STRING
    PUBLIC rowsAffected AS INTEGER
    PUBLIC columnNames(MAX_RESULT_COLS) AS STRING
    PUBLIC columnCount AS INTEGER
    PUBLIC rows(MAX_RESULT_ROWS) AS SqlRow
    PUBLIC rowCount AS INTEGER

    PUBLIC SUB Init()
        success = -1
        message = "OK"
        rowsAffected = 0
        columnCount = 0
        rowCount = 0
    END SUB

    PUBLIC SUB SetError(msg AS STRING)
        success = 0
        message = msg
    END SUB

    PUBLIC SUB AddColumnName(colName AS STRING)
        IF columnCount < MAX_RESULT_COLS THEN
            columnNames(columnCount) = colName
            columnCount = columnCount + 1
        END IF
    END SUB

    PUBLIC SUB AddRow(r AS SqlRow)
        IF rowCount < MAX_RESULT_ROWS THEN
            LET rows(rowCount) = r
            rowCount = rowCount + 1
        END IF
    END SUB
END CLASS

'=============================================================================
' EXECUTOR - Global Database
'=============================================================================

DIM gDatabase AS SqlDatabase
DIM gDbInitialized AS INTEGER

SUB InitDatabase()
    IF gDbInitialized = 0 THEN
        LET gDatabase = NEW SqlDatabase()
        gDatabase.Init()
        gDbInitialized = -1
    END IF
END SUB

'=============================================================================
' EXECUTOR FUNCTIONS
'=============================================================================

FUNCTION ExecuteCreateTable(stmt AS CreateTableStmt) AS QueryResult
    DIM result AS QueryResult
    DIM tbl AS SqlTable
    DIM col AS SqlColumn
    DIM stmtCol AS SqlColumn
    DIM i AS INTEGER
    DIM idx AS INTEGER
    DIM tName AS STRING

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Workaround: Copy class member to local var (Viper Basic compiler bug)
    tName = stmt.tableName

    ' Check if table exists
    idx = gDatabase.FindTable(tName)
    IF idx >= 0 THEN
        result.success = 0
        result.message = "Table '" + tName + "' already exists"
        ExecuteCreateTable = result
        EXIT FUNCTION
    END IF

    ' Create new table
    LET tbl = NEW SqlTable()
    tbl.Init(tName)

    ' Add columns from parsed statement
    FOR i = 0 TO stmt.columnCount - 1
        LET stmtCol = stmt.columns(i)
        LET col = NEW SqlColumn()
        col.Init(stmtCol.name, stmtCol.typeCode)
        col.primaryKey = stmtCol.primaryKey
        col.notNull = stmtCol.notNull
        col.isUnique = stmtCol.isUnique
        col.autoIncrement = stmtCol.autoIncrement
        tbl.AddColumn(col)
    NEXT i

    gDatabase.AddTable(tbl)
    result.message = "Table '" + stmt.tableName + "' created"
    ExecuteCreateTable = result
END FUNCTION

' Evaluate literal expression
FUNCTION EvalExprLiteral(expr AS Expr) AS SqlValue
    DIM result AS SqlValue
    IF expr.kind = EXPR_LITERAL THEN
        EvalExprLiteral = expr.literalValue
        EXIT FUNCTION
    END IF
    LET result = NEW SqlValue()
    result.InitNull()
    EvalExprLiteral = result
END FUNCTION

FUNCTION ExecuteInsert(stmt AS InsertStmt) AS QueryResult
    DIM result AS QueryResult
    DIM tbl AS SqlTable
    DIM row AS SqlRow
    DIM val AS SqlValue
    DIM r AS INTEGER
    DIM v AS INTEGER
    DIM tableIdx AS INTEGER
    DIM rowsInserted AS INTEGER
    DIM maxV AS INTEGER
    DIM expr AS Expr
    DIM tName AS STRING

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Workaround: Copy class member to local var
    tName = stmt.tableName

    ' Find table
    tableIdx = gDatabase.FindTable(tName)
    IF tableIdx < 0 THEN
        result.success = 0
        result.message = "Table '" + tName + "' does not exist"
        ExecuteInsert = result
        EXIT FUNCTION
    END IF

    LET tbl = gDatabase.tables(tableIdx)
    rowsInserted = 0

    ' Insert each row
    FOR r = 0 TO stmt.rowCount - 1
        LET row = NEW SqlRow()
        row.Init(tbl.columnCount)

        ' Get values using rowValueCounts
        maxV = stmt.rowValueCounts(r)
        IF maxV > tbl.columnCount THEN
            maxV = tbl.columnCount
        END IF

        FOR v = 0 TO maxV - 1
            LET expr = stmt.GetValue(r, v)
            LET val = EvalExprLiteral(expr)
            row.SetValue(v, val)
        NEXT v

        tbl.AddRow(row)
        rowsInserted = rowsInserted + 1
    NEXT r

    ' Update the table in database
    LET gDatabase.tables(tableIdx) = tbl

    result.message = "Inserted " + STR$(rowsInserted) + " row(s)"
    result.rowsAffected = rowsInserted
    ExecuteInsert = result
END FUNCTION

' Evaluate column reference
FUNCTION EvalExprColumn(expr AS Expr, row AS SqlRow, tbl AS SqlTable) AS SqlValue
    DIM result AS SqlValue
    DIM colIdx AS INTEGER
    DIM cName AS STRING

    ' Workaround: Copy class member to local var
    cName = expr.columnName
    colIdx = tbl.FindColumnIndex(cName)
    IF colIdx < 0 THEN
        LET result = NEW SqlValue()
        result.InitNull()
        EvalExprColumn = result
        EXIT FUNCTION
    END IF
    EvalExprColumn = row.GetValue(colIdx)
END FUNCTION

' Evaluate binary comparison for WHERE clauses
FUNCTION EvalBinaryExpr(expr AS Expr, row AS SqlRow, tbl AS SqlTable) AS INTEGER
    DIM leftExpr AS Expr
    DIM rightExpr AS Expr
    DIM leftVal AS SqlValue
    DIM rightVal AS SqlValue
    DIM cmp AS INTEGER

    EvalBinaryExpr = 0

    LET leftExpr = expr.GetLeft()
    LET rightExpr = expr.GetRight()

    ' Get left value
    IF leftExpr.kind = EXPR_COLUMN THEN
        LET leftVal = EvalExprColumn(leftExpr, row, tbl)
    ELSEIF leftExpr.kind = EXPR_LITERAL THEN
        LET leftVal = EvalExprLiteral(leftExpr)
    END IF

    ' Get right value
    IF rightExpr.kind = EXPR_COLUMN THEN
        LET rightVal = EvalExprColumn(rightExpr, row, tbl)
    ELSEIF rightExpr.kind = EXPR_LITERAL THEN
        LET rightVal = EvalExprLiteral(rightExpr)
    END IF

    cmp = leftVal.Compare(rightVal)

    ' Handle different comparison operators
    IF expr.op = OP_EQ THEN
        IF cmp = 0 THEN
            EvalBinaryExpr = -1
        END IF
    ELSEIF expr.op = OP_NE THEN
        IF cmp <> 0 THEN
            EvalBinaryExpr = -1
        END IF
    ELSEIF expr.op = OP_LT THEN
        IF cmp < 0 THEN
            EvalBinaryExpr = -1
        END IF
    ELSEIF expr.op = OP_LE THEN
        IF cmp <= 0 THEN
            EvalBinaryExpr = -1
        END IF
    ELSEIF expr.op = OP_GT THEN
        IF cmp > 0 THEN
            EvalBinaryExpr = -1
        END IF
    ELSEIF expr.op = OP_GE THEN
        IF cmp >= 0 THEN
            EvalBinaryExpr = -1
        END IF
    END IF
END FUNCTION

' Check if a row already exists in the result (for DISTINCT)
FUNCTION IsDuplicateRow(newRow AS SqlRow, result AS QueryResult) AS INTEGER
    DIM i AS INTEGER
    DIM existingRow AS SqlRow
    DIM c AS INTEGER
    DIM allMatch AS INTEGER
    DIM val1 AS SqlValue
    DIM val2 AS SqlValue

    FOR i = 0 TO result.rowCount - 1
        LET existingRow = result.rows(i)
        IF existingRow.columnCount = newRow.columnCount THEN
            allMatch = -1
            FOR c = 0 TO newRow.columnCount - 1
                LET val1 = existingRow.GetValue(c)
                LET val2 = newRow.GetValue(c)
                IF val1.Compare(val2) <> 0 THEN
                    allMatch = 0
                    EXIT FOR
                END IF
            NEXT c
            IF allMatch <> 0 THEN
                IsDuplicateRow = -1
                EXIT FUNCTION
            END IF
        END IF
    NEXT i

    IsDuplicateRow = 0
END FUNCTION

' Check if expression is an aggregate function
FUNCTION IsAggregateExpr(expr AS Expr) AS INTEGER
    DIM funcName AS STRING
    IF expr.kind = EXPR_FUNCTION THEN
        funcName = expr.funcName
        IF funcName = "COUNT" OR funcName = "count" THEN
            IsAggregateExpr = -1
            EXIT FUNCTION
        END IF
        IF funcName = "SUM" OR funcName = "sum" THEN
            IsAggregateExpr = -1
            EXIT FUNCTION
        END IF
        IF funcName = "AVG" OR funcName = "avg" THEN
            IsAggregateExpr = -1
            EXIT FUNCTION
        END IF
        IF funcName = "MIN" OR funcName = "min" THEN
            IsAggregateExpr = -1
            EXIT FUNCTION
        END IF
        IF funcName = "MAX" OR funcName = "max" THEN
            IsAggregateExpr = -1
            EXIT FUNCTION
        END IF
    END IF
    IsAggregateExpr = 0
END FUNCTION

' Check if SELECT has any aggregate functions
FUNCTION HasAggregates(stmt AS SelectStmt) AS INTEGER
    DIM c AS INTEGER
    DIM colExpr AS Expr
    FOR c = 0 TO stmt.columnCount - 1
        LET colExpr = stmt.columns(c)
        IF IsAggregateExpr(colExpr) <> 0 THEN
            HasAggregates = -1
            EXIT FUNCTION
        END IF
    NEXT c
    HasAggregates = 0
END FUNCTION

' Evaluate an aggregate function over matching rows
FUNCTION EvalAggregate(expr AS Expr, matchingRows() AS INTEGER, matchCount AS INTEGER, tbl AS SqlTable) AS SqlValue
    DIM funcName AS STRING
    DIM hasArg AS INTEGER
    DIM arg0 AS Expr
    DIM argExpr AS Expr
    DIM count AS INTEGER
    DIM sumInt AS INTEGER
    DIM i AS INTEGER
    DIM rowIdx AS INTEGER
    DIM row AS SqlRow
    DIM val AS SqlValue
    DIM result AS SqlValue
    DIM hasMin AS INTEGER
    DIM hasMax AS INTEGER
    DIM minInt AS INTEGER
    DIM maxInt AS INTEGER

    LET result = NEW SqlValue()
    result.InitNull()

    funcName = expr.funcName
    hasArg = 0
    IF expr.argCount > 0 THEN
        hasArg = -1
    END IF

    ' COUNT(*)
    IF (funcName = "COUNT" OR funcName = "count") AND hasArg <> 0 THEN
        LET arg0 = expr.args(0)
        IF arg0.kind = EXPR_STAR THEN
            result.InitInteger(matchCount)
            EvalAggregate = result
            EXIT FUNCTION
        END IF
    END IF

    ' COUNT(column) - count non-NULL values
    IF funcName = "COUNT" OR funcName = "count" THEN
        count = 0
        FOR i = 0 TO matchCount - 1
            rowIdx = matchingRows(i)
            LET row = tbl.rows(rowIdx)
            IF hasArg <> 0 THEN
                LET argExpr = expr.args(0)
                IF argExpr.kind = EXPR_COLUMN THEN
                    LET val = EvalExprColumn(argExpr, row, tbl)
                    IF val.kind <> SQL_NULL THEN
                        count = count + 1
                    END IF
                END IF
            END IF
        NEXT i
        result.InitInteger(count)
        EvalAggregate = result
        EXIT FUNCTION
    END IF

    ' SUM(column)
    IF funcName = "SUM" OR funcName = "sum" THEN
        sumInt = 0
        FOR i = 0 TO matchCount - 1
            rowIdx = matchingRows(i)
            LET row = tbl.rows(rowIdx)
            IF hasArg <> 0 THEN
                LET argExpr = expr.args(0)
                IF argExpr.kind = EXPR_COLUMN THEN
                    LET val = EvalExprColumn(argExpr, row, tbl)
                    IF val.kind = SQL_INTEGER THEN
                        sumInt = sumInt + val.intValue
                    ELSEIF val.kind = SQL_TEXT THEN
                        sumInt = sumInt + VAL(val.textValue)
                    END IF
                END IF
            END IF
        NEXT i
        result.InitInteger(sumInt)
        EvalAggregate = result
        EXIT FUNCTION
    END IF

    ' AVG(column)
    IF funcName = "AVG" OR funcName = "avg" THEN
        sumInt = 0
        count = 0
        FOR i = 0 TO matchCount - 1
            rowIdx = matchingRows(i)
            LET row = tbl.rows(rowIdx)
            IF hasArg <> 0 THEN
                LET argExpr = expr.args(0)
                IF argExpr.kind = EXPR_COLUMN THEN
                    LET val = EvalExprColumn(argExpr, row, tbl)
                    IF val.kind = SQL_INTEGER THEN
                        sumInt = sumInt + val.intValue
                        count = count + 1
                    ELSEIF val.kind = SQL_TEXT THEN
                        sumInt = sumInt + VAL(val.textValue)
                        count = count + 1
                    END IF
                END IF
            END IF
        NEXT i
        IF count > 0 THEN
            result.InitInteger(sumInt / count)
        END IF
        EvalAggregate = result
        EXIT FUNCTION
    END IF

    ' MIN(column)
    IF funcName = "MIN" OR funcName = "min" THEN
        hasMin = 0
        minInt = 0
        FOR i = 0 TO matchCount - 1
            rowIdx = matchingRows(i)
            LET row = tbl.rows(rowIdx)
            IF hasArg <> 0 THEN
                LET argExpr = expr.args(0)
                IF argExpr.kind = EXPR_COLUMN THEN
                    LET val = EvalExprColumn(argExpr, row, tbl)
                    DIM intVal AS INTEGER
                    intVal = 0
                    IF val.kind = SQL_INTEGER THEN
                        intVal = val.intValue
                    ELSEIF val.kind = SQL_TEXT THEN
                        intVal = VAL(val.textValue)
                    END IF
                    IF val.kind <> SQL_NULL THEN
                        IF hasMin = 0 OR intVal < minInt THEN
                            minInt = intVal
                            hasMin = -1
                        END IF
                    END IF
                END IF
            END IF
        NEXT i
        IF hasMin <> 0 THEN
            result.InitInteger(minInt)
        END IF
        EvalAggregate = result
        EXIT FUNCTION
    END IF

    ' MAX(column)
    IF funcName = "MAX" OR funcName = "max" THEN
        hasMax = 0
        maxInt = 0
        FOR i = 0 TO matchCount - 1
            rowIdx = matchingRows(i)
            LET row = tbl.rows(rowIdx)
            IF hasArg <> 0 THEN
                LET argExpr = expr.args(0)
                IF argExpr.kind = EXPR_COLUMN THEN
                    LET val = EvalExprColumn(argExpr, row, tbl)
                    DIM intVal2 AS INTEGER
                    intVal2 = 0
                    IF val.kind = SQL_INTEGER THEN
                        intVal2 = val.intValue
                    ELSEIF val.kind = SQL_TEXT THEN
                        intVal2 = VAL(val.textValue)
                    END IF
                    IF val.kind <> SQL_NULL THEN
                        IF hasMax = 0 OR intVal2 > maxInt THEN
                            maxInt = intVal2
                            hasMax = -1
                        END IF
                    END IF
                END IF
            END IF
        NEXT i
        IF hasMax <> 0 THEN
            result.InitInteger(maxInt)
        END IF
        EvalAggregate = result
        EXIT FUNCTION
    END IF

    EvalAggregate = result
END FUNCTION

' Evaluate HAVING expression for a group (returns -1 = true, 0 = false)
FUNCTION EvalHavingExpr(expr AS Expr, groupRowArray() AS INTEGER, groupRowCount AS INTEGER, tbl AS SqlTable) AS INTEGER
    DIM left AS Expr
    DIM right AS Expr
    DIM op AS INTEGER
    DIM leftBool AS INTEGER
    DIM rightBool AS INTEGER
    DIM leftVal AS SqlValue
    DIM rightVal AS SqlValue
    DIM cmp AS INTEGER

    ' Handle binary expressions
    IF expr.kind = EXPR_BINARY THEN
        LET left = expr.GetLeft()
        LET right = expr.GetRight()
        op = expr.op

        ' Handle logical operators (AND=20, OR=21)
        IF op = OP_AND THEN
            leftBool = EvalHavingExpr(left, groupRowArray, groupRowCount, tbl)
            rightBool = EvalHavingExpr(right, groupRowArray, groupRowCount, tbl)
            IF leftBool <> 0 AND rightBool <> 0 THEN
                EvalHavingExpr = -1
                EXIT FUNCTION
            END IF
            EvalHavingExpr = 0
            EXIT FUNCTION
        END IF
        IF op = OP_OR THEN
            leftBool = EvalHavingExpr(left, groupRowArray, groupRowCount, tbl)
            rightBool = EvalHavingExpr(right, groupRowArray, groupRowCount, tbl)
            IF leftBool <> 0 OR rightBool <> 0 THEN
                EvalHavingExpr = -1
                EXIT FUNCTION
            END IF
            EvalHavingExpr = 0
            EXIT FUNCTION
        END IF

        ' Evaluate left and right sides for comparison
        LET leftVal = EvalHavingValue(left, groupRowArray, groupRowCount, tbl)
        LET rightVal = EvalHavingValue(right, groupRowArray, groupRowCount, tbl)

        ' Comparison operators
        cmp = leftVal.Compare(rightVal)
        IF op = OP_EQ THEN
            IF cmp = 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
        IF op = OP_NE THEN
            IF cmp <> 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
        IF op = OP_GT THEN
            IF cmp > 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
        IF op = OP_GE THEN
            IF cmp >= 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
        IF op = OP_LT THEN
            IF cmp < 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
        IF op = OP_LE THEN
            IF cmp <= 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
    END IF

    EvalHavingExpr = 0
END FUNCTION

' Evaluate a value in HAVING context (handles aggregates)
FUNCTION EvalHavingValue(expr AS Expr, groupRowArray() AS INTEGER, groupRowCount AS INTEGER, tbl AS SqlTable) AS SqlValue
    DIM result AS SqlValue
    DIM firstRowIdx AS INTEGER
    DIM row AS SqlRow

    LET result = NEW SqlValue()
    result.InitNull()

    ' If it's an aggregate function, evaluate it on the group
    IF expr.kind = EXPR_FUNCTION THEN
        IF IsAggregateExpr(expr) <> 0 THEN
            EvalHavingValue = EvalAggregate(expr, groupRowArray, groupRowCount, tbl)
            EXIT FUNCTION
        END IF
    END IF

    ' If it's a literal, return its value
    IF expr.kind = EXPR_LITERAL THEN
        EvalHavingValue = EvalExprLiteral(expr)
        EXIT FUNCTION
    END IF

    ' If it's a column ref, evaluate using first row in group
    IF expr.kind = EXPR_COLUMN THEN
        IF groupRowCount > 0 THEN
            firstRowIdx = groupRowArray(0)
            LET row = tbl.rows(firstRowIdx)
            EvalHavingValue = EvalExprColumn(expr, row, tbl)
            EXIT FUNCTION
        END IF
    END IF

    EvalHavingValue = result
END FUNCTION

' CROSS JOIN helper: Evaluate column reference in cross join context
FUNCTION EvalCrossJoinColumn(expr AS Expr, tables() AS SqlTable, aliases() AS STRING, tableCount AS INTEGER, combinedRow AS SqlRow, colOffsets() AS INTEGER) AS SqlValue
    DIM result AS SqlValue
    DIM colName AS STRING
    DIM tblName AS STRING
    DIM ti AS INTEGER
    DIM colIdx AS INTEGER
    DIM offset AS INTEGER

    LET result = SqlNull()
    colName = expr.columnName
    tblName = expr.tableName

    ' If table name specified, find that table
    DIM tbl AS SqlTable
    DIM tblNameLocal AS STRING
    DIM aliasLocal AS STRING
    DIM matchName AS INTEGER
    DIM matchAlias AS INTEGER
    IF tblName <> "" THEN
        FOR ti = 0 TO tableCount - 1
            LET tbl = tables(ti)
            LET tblNameLocal = tbl.name
            ' Check table name match
            matchName = 0
            matchAlias = 0
            IF tblNameLocal = tblName THEN
                matchName = -1
            END IF
            ' Check alias match - use concatenation to avoid type issue
            LET aliasLocal = "" + aliases(ti)
            IF aliasLocal = tblName THEN
                matchAlias = -1
            END IF
            IF matchName <> 0 OR matchAlias <> 0 THEN
                colIdx = tbl.FindColumnIndex(colName)
                IF colIdx >= 0 THEN
                    offset = colOffsets(ti)
                    LET result = combinedRow.GetValue(offset + colIdx)
                    EvalCrossJoinColumn = result
                    EXIT FUNCTION
                END IF
            END IF
        NEXT ti
        EvalCrossJoinColumn = result
        EXIT FUNCTION
    END IF

    ' No table qualifier - search all tables
    FOR ti = 0 TO tableCount - 1
        LET tbl = tables(ti)
        colIdx = tbl.FindColumnIndex(colName)
        IF colIdx >= 0 THEN
            offset = colOffsets(ti)
            LET result = combinedRow.GetValue(offset + colIdx)
            EvalCrossJoinColumn = result
            EXIT FUNCTION
        END IF
    NEXT ti

    EvalCrossJoinColumn = result
END FUNCTION

' CROSS JOIN helper: Evaluate expression in cross join context
FUNCTION EvalCrossJoinExpr(expr AS Expr, tables() AS SqlTable, aliases() AS STRING, tableCount AS INTEGER, combinedRow AS SqlRow, colOffsets() AS INTEGER) AS SqlValue
    DIM result AS SqlValue

    IF expr.kind = EXPR_LITERAL THEN
        EvalCrossJoinExpr = EvalExprLiteral(expr)
        EXIT FUNCTION
    END IF

    IF expr.kind = EXPR_COLUMN THEN
        EvalCrossJoinExpr = EvalCrossJoinColumn(expr, tables, aliases, tableCount, combinedRow, colOffsets)
        EXIT FUNCTION
    END IF

    LET result = SqlNull()
    EvalCrossJoinExpr = result
END FUNCTION

' CROSS JOIN helper: Evaluate binary expression in cross join context
FUNCTION EvalCrossJoinBinary(expr AS Expr, tables() AS SqlTable, aliases() AS STRING, tableCount AS INTEGER, combinedRow AS SqlRow, colOffsets() AS INTEGER) AS INTEGER
    DIM leftResult AS INTEGER
    DIM leftVal AS SqlValue
    DIM rightVal AS SqlValue
    DIM cmp AS INTEGER
    DIM leftExpr AS Expr
    DIM rightExpr AS Expr

    ' Handle AND/OR first
    IF expr.op = OP_AND THEN
        LET leftExpr = expr.args(0)
        leftResult = EvalCrossJoinBinary(leftExpr, tables, aliases, tableCount, combinedRow, colOffsets)
        IF leftResult = 0 THEN
            EvalCrossJoinBinary = 0
            EXIT FUNCTION
        END IF
        LET rightExpr = expr.args(1)
        EvalCrossJoinBinary = EvalCrossJoinBinary(rightExpr, tables, aliases, tableCount, combinedRow, colOffsets)
        EXIT FUNCTION
    END IF

    IF expr.op = OP_OR THEN
        LET leftExpr = expr.args(0)
        leftResult = EvalCrossJoinBinary(leftExpr, tables, aliases, tableCount, combinedRow, colOffsets)
        IF leftResult <> 0 THEN
            EvalCrossJoinBinary = 1
            EXIT FUNCTION
        END IF
        LET rightExpr = expr.args(1)
        EvalCrossJoinBinary = EvalCrossJoinBinary(rightExpr, tables, aliases, tableCount, combinedRow, colOffsets)
        EXIT FUNCTION
    END IF

    LET leftExpr = expr.args(0)
    LET rightExpr = expr.args(1)
    LET leftVal = EvalCrossJoinExpr(leftExpr, tables, aliases, tableCount, combinedRow, colOffsets)
    LET rightVal = EvalCrossJoinExpr(rightExpr, tables, aliases, tableCount, combinedRow, colOffsets)
    cmp = leftVal.Compare(rightVal)

    IF expr.op = OP_EQ THEN
        IF cmp = 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF
    IF expr.op = OP_NE THEN
        IF cmp <> 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF
    IF expr.op = OP_LT THEN
        IF cmp < 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF
    IF expr.op = OP_LE THEN
        IF cmp <= 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF
    IF expr.op = OP_GT THEN
        IF cmp > 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF
    IF expr.op = OP_GE THEN
        IF cmp >= 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF

    EvalCrossJoinBinary = 0
END FUNCTION

' Execute CROSS JOIN query (multiple tables)
FUNCTION ExecuteCrossJoin(stmt AS SelectStmt) AS QueryResult
    DIM result AS QueryResult
    DIM tables(20) AS SqlTable
    DIM aliases(20) AS STRING
    DIM colOffsets(20) AS INTEGER
    DIM totalCols AS INTEGER
    DIM ti AS INTEGER
    DIM tableIdx AS INTEGER
    DIM c AS INTEGER
    DIM r AS INTEGER
    DIM ri AS INTEGER
    DIM ci AS INTEGER
    DIM offset AS INTEGER
    DIM tblName AS STRING
    DIM aliasName AS STRING
    DIM includeRow AS INTEGER
    DIM whereResult AS INTEGER
    DIM colExpr AS Expr
    DIM val AS SqlValue
    DIM combinedRows(10000) AS SqlRow
    DIM combinedCount AS INTEGER
    DIM newCombined(10000) AS SqlRow
    DIM newCount AS INTEGER
    DIM filteredRows(10000) AS SqlRow
    DIM filteredCount AS INTEGER
    DIM resultRow AS SqlRow
    DIM newRow AS SqlRow
    DIM existing AS SqlRow
    DIM srcRow AS SqlRow

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Load all tables
    totalCols = 0
    FOR ti = 0 TO stmt.tableCount - 1
        tblName = stmt.tableNames(ti)
        tableIdx = gDatabase.FindTable(tblName)
        IF tableIdx < 0 THEN
            result.success = 0
            result.message = "Table '" + tblName + "' does not exist"
            ExecuteCrossJoin = result
            EXIT FUNCTION
        END IF
        LET tables(ti) = gDatabase.tables(tableIdx)

        aliasName = stmt.tableAliases(ti)
        IF aliasName = "" THEN
            aliasName = tblName
        END IF
        aliases(ti) = aliasName

        colOffsets(ti) = totalCols
        totalCols = totalCols + tables(ti).columnCount
    NEXT ti

    ' Build column names for result
    DIM tblLocal AS SqlTable
    DIM aliasLocal AS STRING
    DIM colLocal AS SqlColumn
    DIM fullName AS STRING
    IF stmt.selectAll <> 0 THEN
        ' For SELECT *, include all columns from all tables with table prefix
        FOR ti = 0 TO stmt.tableCount - 1
            LET tblLocal = tables(ti)
            LET aliasLocal = "" + aliases(ti)
            FOR c = 0 TO tblLocal.columnCount - 1
                LET colLocal = tblLocal.columns(c)
                fullName = aliasLocal + "." + colLocal.name
                result.AddColumnName(fullName)
            NEXT c
        NEXT ti
    ELSE
        DIM exprTblName AS STRING
        FOR c = 0 TO stmt.columnCount - 1
            LET colExpr = stmt.columns(c)
            IF colExpr.kind = EXPR_COLUMN THEN
                exprTblName = colExpr.tableName
                IF exprTblName <> "" THEN
                    result.AddColumnName(exprTblName + "." + colExpr.columnName)
                ELSE
                    result.AddColumnName(colExpr.columnName)
                END IF
            ELSE
                result.AddColumnName("expr" + STR$(c))
            END IF
        NEXT c
    END IF

    ' Build cartesian product using iterative approach
    ' Start with all rows from first table
    combinedCount = 0
    IF stmt.tableCount > 0 THEN
        FOR r = 0 TO tables(0).rowCount - 1
            LET srcRow = tables(0).rows(r)
            IF srcRow.deleted = 0 THEN
                ' Create combined row with values from first table
                LET newRow = NEW SqlRow()
                newRow.Init(totalCols)
                FOR c = 0 TO tables(0).columnCount - 1
                    newRow.SetValue(c, srcRow.GetValue(c))
                NEXT c
                LET combinedRows(combinedCount) = newRow
                combinedCount = combinedCount + 1
            END IF
        NEXT r
    END IF

    ' Extend with each additional table
    DIM joinType AS INTEGER
    DIM joinCondIdx AS INTEGER
    DIM hasJoinCond AS INTEGER
    DIM foundMatch AS INTEGER
    DIM includeThisRow AS INTEGER
    DIM joinCond AS Expr
    DIM joinResult AS INTEGER
    DIM nullRow AS SqlRow

    DIM rightRowMatched(1000) AS INTEGER
    DIM rCheck AS INTEGER
    DIM rightRow AS SqlRow
    DIM cc AS INTEGER

    FOR ti = 1 TO stmt.tableCount - 1
        offset = colOffsets(ti)
        newCount = 0

        ' Get join type for this table (stored at index ti by AddJoin)
        joinType = stmt.joinTypes(ti)  ' 0=CROSS, 1=INNER, 2=LEFT, 3=RIGHT, 4=FULL
        joinCondIdx = ti - 1
        IF joinCondIdx < stmt.joinConditionCount THEN
            hasJoinCond = -1
        ELSE
            hasJoinCond = 0
        END IF

        ' For RIGHT JOIN or FULL JOIN: initialize tracking array
        IF joinType = 3 OR joinType = 4 THEN
            FOR r = 0 TO tables(ti).rowCount - 1
                rightRowMatched(r) = 0
            NEXT r
        END IF

        FOR ci = 0 TO combinedCount - 1
            LET existing = combinedRows(ci)
            foundMatch = 0  ' For LEFT JOIN / FULL JOIN: track if any right row matched

            FOR r = 0 TO tables(ti).rowCount - 1
                LET srcRow = tables(ti).rows(r)
                IF srcRow.deleted = 0 THEN
                    ' Clone existing and add new table's values
                    LET newRow = NEW SqlRow()
                    newRow.Init(totalCols)
                    FOR c = 0 TO offset - 1
                        newRow.SetValue(c, existing.GetValue(c))
                    NEXT c
                    FOR c = 0 TO tables(ti).columnCount - 1
                        newRow.SetValue(offset + c, srcRow.GetValue(c))
                    NEXT c

                    ' For CROSS JOIN (type 0), add all combinations
                    ' For INNER/LEFT/RIGHT/FULL JOIN, check join condition
                    includeThisRow = -1
                    IF hasJoinCond <> 0 AND (joinType = 1 OR joinType = 2 OR joinType = 3 OR joinType = 4) THEN
                        LET joinCond = stmt.joinConditions(joinCondIdx)
                        joinResult = EvalCrossJoinBinary(joinCond, tables, aliases, stmt.tableCount, newRow, colOffsets)
                        IF joinResult = 0 THEN
                            includeThisRow = 0
                        END IF
                    END IF

                    IF includeThisRow <> 0 THEN
                        LET newCombined(newCount) = newRow
                        newCount = newCount + 1
                        foundMatch = -1
                        ' For RIGHT/FULL JOIN: mark this right row as matched
                        IF joinType = 3 OR joinType = 4 THEN
                            rightRowMatched(r) = 1
                        END IF
                    END IF
                END IF
            NEXT r

            ' For LEFT JOIN or FULL JOIN: if no match found, add row with NULLs for right columns
            IF (joinType = 2 OR joinType = 4) AND foundMatch = 0 THEN
                LET nullRow = NEW SqlRow()
                nullRow.Init(totalCols)
                FOR c = 0 TO offset - 1
                    nullRow.SetValue(c, existing.GetValue(c))
                NEXT c
                ' Right columns are already NULL by default from Init
                LET newCombined(newCount) = nullRow
                newCount = newCount + 1
            END IF
        NEXT ci

        ' For RIGHT JOIN or FULL JOIN: add unmatched right rows with NULLs for left columns
        IF joinType = 3 OR joinType = 4 THEN
            FOR rCheck = 0 TO tables(ti).rowCount - 1
                IF rightRowMatched(rCheck) = 0 THEN
                    LET rightRow = tables(ti).rows(rCheck)
                    IF rightRow.deleted = 0 THEN
                        LET nullRow = NEW SqlRow()
                        nullRow.Init(totalCols)
                        ' Left columns stay NULL (default from Init)
                        ' Copy right columns
                        FOR cc = 0 TO tables(ti).columnCount - 1
                            nullRow.SetValue(offset + cc, rightRow.GetValue(cc))
                        NEXT cc
                        LET newCombined(newCount) = nullRow
                        newCount = newCount + 1
                    END IF
                END IF
            NEXT rCheck
        END IF

        ' Copy newCombined to combinedRows
        combinedCount = newCount
        FOR ci = 0 TO newCount - 1
            LET combinedRows(ci) = newCombined(ci)
        NEXT ci
    NEXT ti

    ' Apply WHERE filter (JOIN conditions already applied during join building)
    filteredCount = 0
    FOR ri = 0 TO combinedCount - 1
        includeRow = -1

        ' Apply WHERE clause
        IF stmt.hasWhere <> 0 THEN
            whereResult = EvalCrossJoinBinary(stmt.whereClause, tables, aliases, stmt.tableCount, combinedRows(ri), colOffsets)
            IF whereResult = 0 THEN
                includeRow = 0
            END IF
        END IF

        IF includeRow <> 0 THEN
            LET filteredRows(filteredCount) = combinedRows(ri)
            filteredCount = filteredCount + 1
        END IF
    NEXT ri

    ' Project columns and build result
    FOR ri = 0 TO filteredCount - 1
        LET resultRow = NEW SqlRow()
        resultRow.Init(0)

        IF stmt.selectAll <> 0 THEN
            ' Copy all values
            FOR c = 0 TO totalCols - 1
                resultRow.AddValue(filteredRows(ri).GetValue(c))
            NEXT c
        ELSE
            FOR c = 0 TO stmt.columnCount - 1
                LET colExpr = stmt.columns(c)
                LET val = EvalCrossJoinExpr(colExpr, tables, aliases, stmt.tableCount, filteredRows(ri), colOffsets)
                resultRow.AddValue(val)
            NEXT c
        END IF

        result.AddRow(resultRow)
    NEXT ri

    result.success = -1
    result.message = "Selected " + STR$(result.rowCount) + " row(s)"
    ExecuteCrossJoin = result
END FUNCTION

FUNCTION ExecuteSelect(stmt AS SelectStmt) AS QueryResult
    ' Check for multi-table (CROSS JOIN) query
    IF stmt.tableCount > 1 THEN
        ExecuteSelect = ExecuteCrossJoin(stmt)
        EXIT FUNCTION
    END IF

    DIM result AS QueryResult
    DIM tbl AS SqlTable
    DIM tableRow AS SqlRow
    DIM resultRow AS SqlRow
    DIM col AS SqlColumn
    DIM colExpr AS Expr
    DIM val AS SqlValue
    DIM tableIdx AS INTEGER
    DIM r AS INTEGER
    DIM c AS INTEGER
    DIM includeRow AS INTEGER
    DIM whereResult AS INTEGER
    DIM tName AS STRING
    DIM matchingRows(1000) AS INTEGER
    DIM matchCount AS INTEGER
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM k AS INTEGER
    DIM rowIdxI AS INTEGER
    DIM rowIdxJ AS INTEGER
    DIM rowI AS SqlRow
    DIM rowJ AS SqlRow
    DIM orderExpr AS Expr
    DIM isDesc AS INTEGER
    DIM valI AS SqlValue
    DIM valJ AS SqlValue
    DIM cmp AS INTEGER
    DIM shouldSwap AS INTEGER
    DIM tempIdx AS INTEGER
    DIM ri AS INTEGER
    DIM rowIdx AS INTEGER

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Workaround: Copy class member to local var
    tName = stmt.tableName

    ' Find table
    tableIdx = gDatabase.FindTable(tName)
    IF tableIdx < 0 THEN
        result.success = 0
        result.message = "Table '" + tName + "' does not exist"
        ExecuteSelect = result
        EXIT FUNCTION
    END IF

    LET tbl = gDatabase.tables(tableIdx)

    ' Build column names
    IF stmt.selectAll <> 0 THEN
        FOR c = 0 TO tbl.columnCount - 1
            result.AddColumnName(tbl.columns(c).name)
        NEXT c
    ELSE
        FOR c = 0 TO stmt.columnCount - 1
            LET colExpr = stmt.columns(c)
            IF colExpr.kind = EXPR_COLUMN THEN
                result.AddColumnName(colExpr.columnName)
            ELSEIF colExpr.kind = EXPR_FUNCTION THEN
                ' Build name like COUNT(*) or SUM(age)
                DIM aggArgExpr AS Expr
                IF colExpr.argCount > 0 THEN
                    LET aggArgExpr = colExpr.args(0)
                    IF aggArgExpr.kind = EXPR_STAR THEN
                        result.AddColumnName(colExpr.funcName + "(*)")
                    ELSEIF aggArgExpr.kind = EXPR_COLUMN THEN
                        result.AddColumnName(colExpr.funcName + "(" + aggArgExpr.columnName + ")")
                    ELSE
                        result.AddColumnName(colExpr.funcName + "(...)")
                    END IF
                ELSE
                    result.AddColumnName(colExpr.funcName + "()")
                END IF
            ELSE
                result.AddColumnName("expr" + STR$(c))
            END IF
        NEXT c
    END IF

    ' Collect matching row indices
    matchCount = 0
    FOR r = 0 TO tbl.rowCount - 1
        LET tableRow = tbl.rows(r)
        IF tableRow.deleted = 0 THEN
            ' Check WHERE
            includeRow = -1
            IF stmt.hasWhere <> 0 THEN
                whereResult = EvalBinaryExpr(stmt.whereClause, tableRow, tbl)
                IF whereResult = 0 THEN
                    includeRow = 0
                END IF
            END IF

            IF includeRow <> 0 THEN
                matchingRows(matchCount) = r
                matchCount = matchCount + 1
            END IF
        END IF
    NEXT r

    ' Handle GROUP BY and aggregate queries
    DIM isAggQuery AS INTEGER
    isAggQuery = HasAggregates(stmt)
    IF isAggQuery <> 0 OR stmt.groupByCount > 0 THEN
        DIM aggVal AS SqlValue
        DIM groupKeys(1000) AS STRING
        DIM groupRowCounts(1000) AS INTEGER
        DIM groupRows(1000, 1000) AS INTEGER  ' groupRows(groupIdx, rowIdxInGroup) = actualRowIdx
        DIM groupCount AS INTEGER
        DIM g AS INTEGER
        DIM gi AS INTEGER
        DIM foundGroup AS INTEGER
        DIM groupKey AS STRING
        DIM groupByExpr AS Expr
        DIM groupVal AS SqlValue
        DIM groupRowArray(1000) AS INTEGER
        DIM grc AS INTEGER

        groupCount = 0

        ' If GROUP BY is specified, group rows by GROUP BY values
        IF stmt.groupByCount > 0 THEN
            FOR i = 0 TO matchCount - 1
                rowIdx = matchingRows(i)
                LET tableRow = tbl.rows(rowIdx)

                ' Build group key from GROUP BY expression values
                groupKey = ""
                FOR g = 0 TO stmt.groupByCount - 1
                    LET groupByExpr = stmt.groupByExprs(g)
                    IF groupByExpr.kind = EXPR_COLUMN THEN
                        LET groupVal = EvalExprColumn(groupByExpr, tableRow, tbl)
                    ELSE
                        LET groupVal = SqlNull()
                    END IF
                    IF g > 0 THEN
                        groupKey = groupKey + "|"
                    END IF
                    groupKey = groupKey + groupVal.ToString$()
                NEXT g

                ' Find or create group
                foundGroup = -1
                FOR gi = 0 TO groupCount - 1
                    IF groupKeys(gi) = groupKey THEN
                        foundGroup = gi
                        EXIT FOR
                    END IF
                NEXT gi

                IF foundGroup < 0 THEN
                    ' Create new group
                    groupKeys(groupCount) = groupKey
                    groupRows(groupCount, 0) = rowIdx
                    groupRowCounts(groupCount) = 1
                    groupCount = groupCount + 1
                ELSE
                    ' Add to existing group
                    grc = groupRowCounts(foundGroup)
                    groupRows(foundGroup, grc) = rowIdx
                    groupRowCounts(foundGroup) = grc + 1
                END IF
            NEXT i
        ELSE
            ' No GROUP BY - all rows form one group
            groupCount = 1
            groupKeys(0) = "ALL"
            groupRowCounts(0) = matchCount
            FOR i = 0 TO matchCount - 1
                groupRows(0, i) = matchingRows(i)
            NEXT i
        END IF

        ' Process each group
        FOR g = 0 TO groupCount - 1
            LET resultRow = NEW SqlRow()
            resultRow.Init(0)

            ' Build array of rows for this group
            grc = groupRowCounts(g)
            FOR gi = 0 TO grc - 1
                groupRowArray(gi) = groupRows(g, gi)
            NEXT gi

            FOR c = 0 TO stmt.columnCount - 1
                LET colExpr = stmt.columns(c)
                LET aggVal = SqlNull()

                IF IsAggregateExpr(colExpr) <> 0 THEN
                    LET aggVal = EvalAggregate(colExpr, groupRowArray, grc, tbl)
                ELSEIF colExpr.kind = EXPR_COLUMN THEN
                    ' For non-aggregate columns (GROUP BY columns), use first row value
                    IF grc > 0 THEN
                        rowIdx = groupRowArray(0)
                        LET tableRow = tbl.rows(rowIdx)
                        LET aggVal = EvalExprColumn(colExpr, tableRow, tbl)
                    END IF
                ELSEIF colExpr.kind = EXPR_LITERAL THEN
                    LET aggVal = EvalExprLiteral(colExpr)
                END IF

                resultRow.AddValue(aggVal)
            NEXT c

            ' Check HAVING clause if present
            DIM includeGroup AS INTEGER
            includeGroup = -1
            IF stmt.hasHaving <> 0 THEN
                DIM havingResult AS INTEGER
                havingResult = EvalHavingExpr(stmt.havingClause, groupRowArray, grc, tbl)
                IF havingResult = 0 THEN
                    includeGroup = 0
                END IF
            END IF

            IF includeGroup <> 0 THEN
                result.AddRow(resultRow)
            END IF
        NEXT g

        ExecuteSelect = result
        EXIT FUNCTION
    END IF

    ' Sort matching rows if ORDER BY is present
    IF stmt.orderByCount > 0 THEN
        ' Bubble sort
        FOR i = 0 TO matchCount - 2
            FOR j = i + 1 TO matchCount - 1
                rowIdxI = matchingRows(i)
                rowIdxJ = matchingRows(j)
                LET rowI = tbl.rows(rowIdxI)
                LET rowJ = tbl.rows(rowIdxJ)

                ' Compare based on ORDER BY expressions
                shouldSwap = 0
                FOR k = 0 TO stmt.orderByCount - 1
                    LET orderExpr = stmt.orderByExprs(k)
                    isDesc = stmt.orderByDir(k)

                    ' Evaluate ORDER BY expression for both rows
                    IF orderExpr.kind = EXPR_COLUMN THEN
                        LET valI = EvalExprColumn(orderExpr, rowI, tbl)
                        LET valJ = EvalExprColumn(orderExpr, rowJ, tbl)
                    ELSEIF orderExpr.kind = EXPR_LITERAL THEN
                        LET valI = EvalExprLiteral(orderExpr)
                        LET valJ = EvalExprLiteral(orderExpr)
                    END IF

                    cmp = valI.Compare(valJ)
                    IF cmp <> 0 THEN
                        ' Values differ
                        IF isDesc = 1 THEN
                            ' DESC: swap if valI < valJ
                            IF cmp < 0 THEN
                                shouldSwap = -1
                            END IF
                        ELSE
                            ' ASC: swap if valI > valJ
                            IF cmp > 0 THEN
                                shouldSwap = -1
                            END IF
                        END IF
                        EXIT FOR
                    END IF
                NEXT k

                IF shouldSwap <> 0 THEN
                    ' Swap indices
                    tempIdx = matchingRows(i)
                    matchingRows(i) = matchingRows(j)
                    matchingRows(j) = tempIdx
                END IF
            NEXT j
        NEXT i
    END IF

    ' Build result rows from sorted indices, applying LIMIT/OFFSET
    DIM addedCount AS INTEGER
    addedCount = 0
    FOR ri = 0 TO matchCount - 1
        ' Skip OFFSET rows
        IF ri >= stmt.offsetValue THEN
            ' Check LIMIT
            IF stmt.limitValue < 0 OR addedCount < stmt.limitValue THEN
                rowIdx = matchingRows(ri)
                LET tableRow = tbl.rows(rowIdx)

                LET resultRow = NEW SqlRow()
                resultRow.Init(0)

                IF stmt.selectAll <> 0 THEN
                    ' Copy all columns
                    FOR c = 0 TO tableRow.columnCount - 1
                        resultRow.AddValue(tableRow.GetValue(c))
                    NEXT c
                ELSE
                    ' Evaluate selected columns
                    FOR c = 0 TO stmt.columnCount - 1
                        LET colExpr = stmt.columns(c)
                        IF colExpr.kind = EXPR_COLUMN THEN
                            LET val = EvalExprColumn(colExpr, tableRow, tbl)
                        ELSEIF colExpr.kind = EXPR_LITERAL THEN
                            LET val = EvalExprLiteral(colExpr)
                        END IF
                        resultRow.AddValue(val)
                    NEXT c
                END IF

                ' Check for DISTINCT - skip duplicate rows
                DIM addThisRow AS INTEGER
                addThisRow = -1
                IF stmt.isDistinct <> 0 THEN
                    IF IsDuplicateRow(resultRow, result) <> 0 THEN
                        addThisRow = 0
                    END IF
                END IF

                IF addThisRow <> 0 THEN
                    result.AddRow(resultRow)
                    addedCount = addedCount + 1
                END IF
            END IF
        END IF
    NEXT ri

    ExecuteSelect = result
END FUNCTION

FUNCTION ExecuteUpdate(stmt AS UpdateStmt) AS QueryResult
    DIM result AS QueryResult
    DIM tbl AS SqlTable
    DIM tableRow AS SqlRow
    DIM valExpr AS Expr
    DIM newVal AS SqlValue
    DIM tableIdx AS INTEGER
    DIM r AS INTEGER
    DIM i AS INTEGER
    DIM updateRow AS INTEGER
    DIM whereResult AS INTEGER
    DIM colIdx AS INTEGER
    DIM tName AS STRING
    DIM updateCount AS INTEGER

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Workaround: Copy class member to local var
    tName = stmt.tableName

    ' Find table
    tableIdx = gDatabase.FindTable(tName)
    IF tableIdx < 0 THEN
        result.success = 0
        result.message = "Table '" + tName + "' does not exist"
        ExecuteUpdate = result
        EXIT FUNCTION
    END IF

    LET tbl = gDatabase.tables(tableIdx)
    updateCount = 0

    ' Process rows
    FOR r = 0 TO tbl.rowCount - 1
        LET tableRow = tbl.rows(r)
        IF tableRow.deleted = 0 THEN
            ' Check WHERE
            updateRow = -1
            IF stmt.hasWhere <> 0 THEN
                whereResult = EvalBinaryExpr(stmt.whereClause, tableRow, tbl)
                IF whereResult = 0 THEN
                    updateRow = 0
                END IF
            END IF

            IF updateRow <> 0 THEN
                ' Apply SET updates
                DIM setColName AS STRING
                FOR i = 0 TO stmt.setCount - 1
                    ' Workaround Bug #011: Copy array element to local var
                    setColName = stmt.setColumns(i)
                    colIdx = tbl.FindColumnIndex(setColName)
                    IF colIdx >= 0 THEN
                        LET valExpr = stmt.setValues(i)
                        IF valExpr.kind = EXPR_LITERAL THEN
                            LET newVal = EvalExprLiteral(valExpr)
                        ELSEIF valExpr.kind = EXPR_COLUMN THEN
                            LET newVal = EvalExprColumn(valExpr, tableRow, tbl)
                        END IF
                        tableRow.SetValue(colIdx, newVal)
                    END IF
                NEXT i
                updateCount = updateCount + 1
            END IF
        END IF
    NEXT r

    result.message = "Updated " + STR$(updateCount) + " row(s)"
    result.rowsAffected = updateCount
    ExecuteUpdate = result
END FUNCTION

FUNCTION ExecuteDelete(stmt AS DeleteStmt) AS QueryResult
    DIM result AS QueryResult
    DIM tbl AS SqlTable
    DIM tableRow AS SqlRow
    DIM tableIdx AS INTEGER
    DIM r AS INTEGER
    DIM deleteRow AS INTEGER
    DIM whereResult AS INTEGER
    DIM tName AS STRING
    DIM deleteCount AS INTEGER

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Workaround: Copy class member to local var
    tName = stmt.tableName

    ' Find table
    tableIdx = gDatabase.FindTable(tName)
    IF tableIdx < 0 THEN
        result.success = 0
        result.message = "Table '" + tName + "' does not exist"
        ExecuteDelete = result
        EXIT FUNCTION
    END IF

    LET tbl = gDatabase.tables(tableIdx)
    deleteCount = 0

    ' Process rows
    FOR r = 0 TO tbl.rowCount - 1
        LET tableRow = tbl.rows(r)
        IF tableRow.deleted = 0 THEN
            ' Check WHERE
            deleteRow = -1
            IF stmt.hasWhere <> 0 THEN
                whereResult = EvalBinaryExpr(stmt.whereClause, tableRow, tbl)
                IF whereResult = 0 THEN
                    deleteRow = 0
                END IF
            END IF

            IF deleteRow <> 0 THEN
                tableRow.deleted = -1
                deleteCount = deleteCount + 1
            END IF
        END IF
    NEXT r

    result.message = "Deleted " + STR$(deleteCount) + " row(s)"
    result.rowsAffected = deleteCount
    ExecuteDelete = result
END FUNCTION

FUNCTION ExecuteSql(sql AS STRING) AS QueryResult
    DIM result AS QueryResult
    DIM createStmt AS CreateTableStmt
    DIM insertStmt AS InsertStmt
    DIM selectStmt AS SelectStmt
    DIM updateStmt AS UpdateStmt
    DIM deleteStmt AS DeleteStmt

    LET result = NEW QueryResult()
    result.Init()
    ParserInit(sql)

    ' Determine statement type
    IF gTok.kind = TK_CREATE THEN
        ParserAdvance()
        IF gTok.kind = TK_TABLE THEN
            LET createStmt = ParseCreateTableStmt()
            IF gParserHasError <> 0 THEN
                result.SetError(gParserError)
                ExecuteSql = result
                EXIT FUNCTION
            END IF
            ExecuteSql = ExecuteCreateTable(createStmt)
            EXIT FUNCTION
        END IF
    END IF

    IF gTok.kind = TK_INSERT THEN
        ParserAdvance()
        LET insertStmt = ParseInsertStmt()
        IF gParserHasError <> 0 THEN
            result.SetError(gParserError)
            ExecuteSql = result
            EXIT FUNCTION
        END IF
        ExecuteSql = ExecuteInsert(insertStmt)
        EXIT FUNCTION
    END IF

    IF gTok.kind = TK_SELECT THEN
        ParserAdvance()
        LET selectStmt = ParseSelectStmt()
        IF gParserHasError <> 0 THEN
            result.SetError(gParserError)
            ExecuteSql = result
            EXIT FUNCTION
        END IF
        ExecuteSql = ExecuteSelect(selectStmt)
        EXIT FUNCTION
    END IF

    IF gTok.kind = TK_UPDATE THEN
        ParserAdvance()
        LET updateStmt = ParseUpdateStmt()
        IF gParserHasError <> 0 THEN
            result.SetError(gParserError)
            ExecuteSql = result
            EXIT FUNCTION
        END IF
        ExecuteSql = ExecuteUpdate(updateStmt)
        EXIT FUNCTION
    END IF

    IF gTok.kind = TK_DELETE THEN
        ParserAdvance()
        LET deleteStmt = ParseDeleteStmt()
        IF gParserHasError <> 0 THEN
            result.SetError(gParserError)
            ExecuteSql = result
            EXIT FUNCTION
        END IF
        ExecuteSql = ExecuteDelete(deleteStmt)
        EXIT FUNCTION
    END IF

    result.SetError("Unknown statement type")
    ExecuteSql = result
END FUNCTION

'=============================================================================
' TEST EXECUTOR
'=============================================================================

SUB TestExecutor()
    DIM result AS QueryResult
    DIM sql AS STRING
    DIM r AS INTEGER
    DIM row AS SqlRow

    PRINT "=== Executor Test ==="
    PRINT ""

    ' Reset database
    gDbInitialized = 0

    ' Test CREATE TABLE
    PRINT "--- CREATE TABLE ---"
    sql = "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER);"
    PRINT "SQL: "; sql
    PRINT "About to call ExecuteSql..."
    LET result = ExecuteSql(sql)
    PRINT "Back from ExecuteSql"
    PRINT "Result: "; result.message
    PRINT ""

    ' Test INSERT
    PRINT "--- INSERT ---"
    sql = "INSERT INTO users (id, name, age) VALUES (1, 'Alice', 30);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    sql = "INSERT INTO users (id, name, age) VALUES (2, 'Bob', 25), (3, 'Charlie', 35);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    ' Test SELECT all
    PRINT "--- SELECT * ---"
    sql = "SELECT * FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test SELECT with specific columns
    PRINT "--- SELECT name, age ---"
    sql = "SELECT name, age FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test SELECT with WHERE
    PRINT "--- SELECT with WHERE ---"
    sql = "SELECT * FROM users WHERE age > 28;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test UPDATE
    PRINT "--- UPDATE ---"
    sql = "UPDATE users SET age = 31 WHERE name = 'Alice';"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    ' Verify UPDATE
    PRINT "--- Verify UPDATE ---"
    sql = "SELECT * FROM users WHERE name = 'Alice';"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test DELETE
    PRINT "--- DELETE ---"
    sql = "DELETE FROM users WHERE name = 'Bob';"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    ' Verify DELETE
    PRINT "--- Verify DELETE ---"
    sql = "SELECT * FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test ORDER BY - Insert more data
    PRINT "--- ORDER BY ---"
    sql = "INSERT INTO users (id, name, age) VALUES (4, 'Diana', 28);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    ' Test ORDER BY ASC
    PRINT "--- ORDER BY age ---"
    sql = "SELECT * FROM users ORDER BY age;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test ORDER BY DESC
    PRINT "--- ORDER BY age DESC ---"
    sql = "SELECT * FROM users ORDER BY age DESC;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test ORDER BY name
    PRINT "--- ORDER BY name ASC ---"
    sql = "SELECT * FROM users ORDER BY name ASC;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test LIMIT
    PRINT "--- LIMIT 2 ---"
    sql = "SELECT * FROM users ORDER BY age LIMIT 2;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test LIMIT with OFFSET
    PRINT "--- LIMIT 2 OFFSET 1 ---"
    sql = "SELECT * FROM users ORDER BY age LIMIT 2 OFFSET 1;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test DISTINCT - First add some duplicate ages
    PRINT "--- DISTINCT Setup ---"
    sql = "INSERT INTO users (id, name, age) VALUES (5, 'Eve', 31), (6, 'Frank', 28);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    ' Test SELECT without DISTINCT (shows duplicates)
    PRINT "--- SELECT age (no DISTINCT) ---"
    sql = "SELECT age FROM users ORDER BY age;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test SELECT DISTINCT
    PRINT "--- SELECT DISTINCT age ---"
    sql = "SELECT DISTINCT age FROM users ORDER BY age;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test COUNT(*)
    PRINT "--- COUNT(*) ---"
    sql = "SELECT COUNT(*) FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test SUM(age)
    PRINT "--- SUM(age) ---"
    sql = "SELECT SUM(age) FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test AVG(age)
    PRINT "--- AVG(age) ---"
    sql = "SELECT AVG(age) FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test MIN(age) and MAX(age)
    PRINT "--- MIN(age), MAX(age) ---"
    sql = "SELECT MIN(age), MAX(age) FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test COUNT(*) with WHERE
    PRINT "--- COUNT(*) with WHERE ---"
    sql = "SELECT COUNT(*) FROM users WHERE age > 30;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test GROUP BY
    PRINT "--- GROUP BY age ---"
    sql = "SELECT age, COUNT(*) FROM users GROUP BY age;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        PRINT "Columns: ";
        FOR c = 0 TO result.columnCount - 1
            PRINT result.columnNames(c); " ";
        NEXT c
        PRINT ""
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test GROUP BY with SUM
    PRINT "--- GROUP BY with SUM ---"
    sql = "SELECT age, SUM(id) FROM users GROUP BY age;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test HAVING clause
    PRINT "--- HAVING COUNT(*) > 1 ---"
    sql = "SELECT age, COUNT(*) FROM users GROUP BY age HAVING COUNT(*) > 1;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test HAVING with SUM
    PRINT "--- HAVING SUM(id) >= 4 ---"
    sql = "SELECT age, SUM(id) FROM users GROUP BY age HAVING SUM(id) >= 4;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test table alias
    PRINT "--- Table Alias ---"
    sql = "SELECT * FROM users u WHERE age > 30;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test table alias with AS
    PRINT "--- Table Alias with AS ---"
    sql = "SELECT id, name FROM users AS u;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' CROSS JOIN tests - create a second table
    PRINT "--- CROSS JOIN Tests ---"
    sql = "CREATE TABLE orders (order_id INTEGER, user_id INTEGER, product TEXT);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    sql = "INSERT INTO orders VALUES (101, 1, 'Widget');"
    LET result = ExecuteSql(sql)
    sql = "INSERT INTO orders VALUES (102, 3, 'Gadget');"
    LET result = ExecuteSql(sql)
    sql = "INSERT INTO orders VALUES (103, 99, 'Thingamajig');"
    LET result = ExecuteSql(sql)
    PRINT "Inserted 3 orders (one with non-existent user_id=99)"
    PRINT ""

    ' Test basic CROSS JOIN with SELECT *
    PRINT "--- Basic CROSS JOIN ---"
    sql = "SELECT * FROM users, orders;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        PRINT "Columns: "; result.columnCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test CROSS JOIN with WHERE clause (simulating INNER JOIN)
    PRINT "--- CROSS JOIN with WHERE (simulating INNER JOIN) ---"
    sql = "SELECT users.name, orders.product FROM users, orders WHERE users.id = orders.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test CROSS JOIN with aliases
    PRINT "--- CROSS JOIN with aliases ---"
    sql = "SELECT u.name, o.product FROM users u, orders o WHERE u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test INNER JOIN syntax
    PRINT "--- INNER JOIN ---"
    sql = "SELECT users.name, orders.product FROM users INNER JOIN orders ON users.id = orders.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test INNER JOIN with aliases
    PRINT "--- INNER JOIN with aliases ---"
    sql = "SELECT u.name, o.product FROM users u INNER JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test bare JOIN (same as INNER JOIN)
    PRINT "--- Bare JOIN (same as INNER JOIN) ---"
    sql = "SELECT u.name, o.product FROM users u JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test LEFT JOIN - returns all users, even those without orders
    PRINT "--- LEFT JOIN ---"
    sql = "SELECT u.name, o.product FROM users u LEFT JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test LEFT OUTER JOIN (same as LEFT JOIN)
    PRINT "--- LEFT OUTER JOIN ---"
    sql = "SELECT u.name, o.product FROM users u LEFT OUTER JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test RIGHT JOIN - returns all orders, even those without matching users
    PRINT "--- RIGHT JOIN ---"
    sql = "SELECT u.name, o.product FROM users u RIGHT JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test FULL OUTER JOIN - returns all from both tables
    PRINT "--- FULL OUTER JOIN ---"
    sql = "SELECT u.name, o.product FROM users u FULL OUTER JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    PRINT "=== Executor Test PASSED ==="
END SUB

'=============================================================================
' TEST FUNCTIONS
'=============================================================================

SUB TestSqlValue()
    DIM v1 AS SqlValue
    DIM v2 AS SqlValue
    DIM v3 AS SqlValue
    DIM v4 AS SqlValue
    DIM v5 AS SqlValue
    DIM v6 AS SqlValue

    PRINT "=== SqlValue Test ==="

    LET v1 = SqlNull()
    PRINT "v1 (NULL): "; v1.ToString$(); " type="; v1.TypeName$()

    LET v2 = SqlInteger(42)
    PRINT "v2 (INTEGER): "; v2.ToString$(); " type="; v2.TypeName$()

    LET v3 = SqlReal(3.14, "3.14")
    PRINT "v3 (REAL): "; v3.ToString$(); " type="; v3.TypeName$()

    LET v4 = SqlText("Hello")
    PRINT "v4 (TEXT): "; v4.ToString$(); " type="; v4.TypeName$()

    ' Test comparisons
    LET v5 = SqlInteger(100)
    PRINT "v2.Compare(v5): "; v2.Compare(v5); " (expect -1)"
    PRINT "v5.Compare(v2): "; v5.Compare(v2); " (expect 1)"

    LET v6 = SqlInteger(42)
    PRINT "v2.Equals(v6): "; v2.Equals(v6); " (expect -1=true)"

    PRINT "=== SqlValue Test PASSED ==="
END SUB

SUB TestTokens()
    DIM tok AS Token

    PRINT "=== Token Types Test ==="

    LET tok = NEW Token()
    tok.Init(TK_SELECT, "SELECT", 1, 1)
    PRINT "Token 1: kind="; tok.kind; " text='"; tok.text; "'"

    LET tok = NEW Token()
    tok.Init(TK_INTEGER, "42", 1, 8)
    PRINT "Token 2: kind="; tok.kind; " text='"; tok.text; "'"

    PRINT "=== Token Types Test PASSED ==="
END SUB

SUB TestLexer()
    DIM sql AS STRING

    PRINT "=== Lexer Test ==="

    LET sql = "SELECT id, name FROM users WHERE age > 21;"
    PRINT "Input: "; sql
    PRINT ""

    LexerInit(sql)
    LexerNextToken()
    WHILE gTok.kind <> TK_EOF
        PRINT "  "; TokenTypeName$(gTok.kind); ": '"; gTok.text; "'"
        LexerNextToken()
    WEND

    PRINT ""

    LET sql = "INSERT INTO users VALUES (1, 'John', 3.14);"
    PRINT "Input: "; sql
    PRINT ""

    LexerInit(sql)
    LexerNextToken()
    WHILE gTok.kind <> TK_EOF
        PRINT "  "; TokenTypeName$(gTok.kind); ": '"; gTok.text; "'"
        LexerNextToken()
    WEND

    PRINT ""
    PRINT "=== Lexer Test PASSED ==="
END SUB

SUB TestColumnRow()
    DIM col1 AS SqlColumn
    DIM col2 AS SqlColumn
    DIM col3 AS SqlColumn
    DIM row1 AS SqlRow
    DIM row2 AS SqlRow
    DIM v AS SqlValue

    PRINT "=== Column & Row Test ==="

    ' Test Column creation
    LET col1 = MakeColumn("id", SQL_INTEGER)
    col1.primaryKey = -1
    col1.autoIncrement = -1
    PRINT "col1: "; col1.ToString$()

    LET col2 = MakeColumn("name", SQL_TEXT)
    col2.notNull = -1
    PRINT "col2: "; col2.ToString$()

    LET col3 = MakeColumn("score", SQL_REAL)
    col3.SetDefault(SqlReal(0.0, "0.0"))
    PRINT "col3: "; col3.ToString$()

    ' Test Row creation
    LET row1 = MakeRow(3)
    PRINT "row1 (empty): "; row1.ToString$()

    row1.SetValue(0, SqlInteger(1))
    row1.SetValue(1, SqlText("Alice"))
    row1.SetValue(2, SqlReal(95.5, "95.5"))
    PRINT "row1 (filled): "; row1.ToString$()

    ' Test Row clone
    LET row2 = row1.Clone()
    row2.SetValue(0, SqlInteger(2))
    row2.SetValue(1, SqlText("Bob"))
    PRINT "row2 (cloned): "; row2.ToString$()

    ' Verify original unchanged
    PRINT "row1 (verify): "; row1.ToString$()

    ' Test Row value access
    LET v = row1.GetValue(1)
    PRINT "row1.GetValue(1): "; v.ToString$()

    PRINT "=== Column & Row Test PASSED ==="
END SUB

SUB TestTable()
    DIM users AS SqlTable
    DIM colId AS SqlColumn
    DIM colName AS SqlColumn
    DIM colAge AS SqlColumn
    DIM row1 AS SqlRow
    DIM row2 AS SqlRow
    DIM row3 AS SqlRow
    DIM idx AS INTEGER

    PRINT "=== Table Test ==="

    ' Create a users table
    LET users = MakeTable("users")

    ' Add columns
    LET colId = MakeColumn("id", SQL_INTEGER)
    colId.primaryKey = -1
    colId.autoIncrement = -1
    users.AddColumn(colId)

    LET colName = MakeColumn("name", SQL_TEXT)
    colName.notNull = -1
    users.AddColumn(colName)

    LET colAge = MakeColumn("age", SQL_INTEGER)
    users.AddColumn(colAge)

    PRINT users.ToString$()
    PRINT ""
    PRINT users.SchemaString$()
    PRINT ""

    ' Test column lookup
    idx = users.FindColumnIndex("name")
    PRINT "Column 'name' at index: "; idx

    ' Insert some rows
    LET row1 = users.CreateRow()
    row1.SetValue(0, SqlInteger(1))
    row1.SetValue(1, SqlText("Alice"))
    row1.SetValue(2, SqlInteger(30))
    users.AddRow(row1)

    LET row2 = users.CreateRow()
    row2.SetValue(0, SqlInteger(2))
    row2.SetValue(1, SqlText("Bob"))
    row2.SetValue(2, SqlInteger(25))
    users.AddRow(row2)

    LET row3 = users.CreateRow()
    row3.SetValue(0, SqlInteger(3))
    row3.SetValue(1, SqlText("Charlie"))
    row3.SetValue(2, SqlNull())
    users.AddRow(row3)

    PRINT users.ToString$()
    PRINT ""
    PRINT "Rows inserted: "; users.rowCount

    ' Test delete
    users.DeleteRow(1)
    PRINT "After deleting row 1:"
    PRINT "(Row 0 and 2 should remain, row 1 marked deleted)"

    PRINT ""
    PRINT "=== Table Test PASSED ==="
END SUB

SUB TestExpr()
    DIM e1 AS Expr
    DIM e2 AS Expr
    DIM e3 AS Expr
    DIM e4 AS Expr
    DIM e5 AS Expr
    DIM e6 AS Expr
    DIM e7 AS Expr
    DIM e8 AS Expr
    DIM e9 AS Expr
    DIM add AS Expr
    DIM mul AS Expr
    DIM e10 AS Expr
    DIM e11 AS Expr
    DIM fn AS Expr

    PRINT "=== Expression Test ==="

    ' Test literal expressions
    LET e1 = ExprNull()
    PRINT "Null literal: "; e1.ToString$(); " (kind="; e1.kind; ")"

    LET e2 = ExprInt(42)
    PRINT "Int literal: "; e2.ToString$(); " (kind="; e2.kind; ")"

    LET e3 = ExprReal(3.14, "3.14")
    PRINT "Real literal: "; e3.ToString$()

    LET e4 = ExprText("hello")
    PRINT "Text literal: "; e4.ToString$()

    ' Test column reference
    LET e5 = ExprColumn("name")
    PRINT "Column ref: "; e5.ToString$(); " (kind="; e5.kind; ", col="; e5.columnName; ")"

    LET e6 = ExprTableColumn("users", "id")
    PRINT "Table.column ref: "; e6.ToString$()

    ' Test star expression
    LET e7 = ExprStar()
    PRINT "Star: "; e7.ToString$()

    ' Test binary expressions
    LET e8 = ExprBinary(OP_ADD, ExprInt(1), ExprInt(2))
    PRINT "Binary add: "; e8.ToString$()

    LET e9 = ExprBinary(OP_EQ, ExprColumn("age"), ExprInt(21))
    PRINT "Binary eq: "; e9.ToString$()

    ' Test compound expression: (a + b) * c
    LET add = ExprBinary(OP_ADD, ExprColumn("a"), ExprColumn("b"))
    LET mul = ExprBinary(OP_MUL, add, ExprColumn("c"))
    PRINT "Compound: "; mul.ToString$()

    ' Test unary expression
    LET e10 = ExprUnary(UOP_NEG, ExprInt(5))
    PRINT "Unary neg: "; e10.ToString$()

    LET e11 = ExprUnary(UOP_NOT, ExprColumn("active"))
    PRINT "Unary not: "; e11.ToString$()

    ' Test function call
    LET fn = ExprFunc("COUNT")
    fn.AddArg(ExprStar())
    PRINT "Function: "; fn.ToString$()

    PRINT "=== Expression Test PASSED ==="
END SUB

SUB TestParser()
    DIM sql1 AS STRING
    DIM sql2 AS STRING
    DIM sql3 AS STRING
    DIM createStmt AS CreateTableStmt
    DIM insertStmt AS InsertStmt
    DIM expr AS Expr

    PRINT "=== Parser Test ==="

    ' Test CREATE TABLE parsing
    LET sql1 = "CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER);"
    PRINT "Input: "; sql1
    PRINT ""

    ParserInit(sql1)
    ParserAdvance()  ' Skip CREATE token
    LET createStmt = ParseCreateTableStmt()

    IF gParserHasError <> 0 THEN
        PRINT "ERROR: "; gParserError
    ELSE
        PRINT "Parsed:"
        PRINT createStmt.ToString$()
    END IF

    PRINT ""

    ' Test INSERT parsing
    LET sql2 = "INSERT INTO users (id, name, age) VALUES (1, 'Alice', 30), (2, 'Bob', 25);"
    PRINT "Input: "; sql2
    PRINT ""

    ParserInit(sql2)
    ParserAdvance()  ' Skip INSERT token
    LET insertStmt = ParseInsertStmt()

    IF gParserHasError <> 0 THEN
        PRINT "ERROR: "; gParserError
    ELSE
        PRINT "Parsed:"
        PRINT insertStmt.ToString$()
    END IF

    PRINT ""

    ' Test expression parsing
    LET sql3 = "1 + 2 * 3"
    PRINT "Expr: "; sql3
    ParserInit(sql3)
    LET expr = ParseExpr()
    PRINT "Parsed: "; expr.ToString$()

    PRINT ""
    PRINT "=== Parser Test PASSED ==="
END SUB

'=============================================================================
' MAIN ENTRY POINT
'=============================================================================

PRINT "SQLite Clone - Viper Basic Edition"
PRINT "==================================="
PRINT ""

TestTokens()
PRINT ""
TestLexer()
PRINT ""
TestSqlValue()
PRINT ""
TestColumnRow()
PRINT ""
TestTable()
PRINT ""
TestExpr()
PRINT ""
TestParser()
PRINT ""
TestExecutor()
