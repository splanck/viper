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
' CONST TK_THEN = 113  ' Removed - may conflict with THEN keyword
' CONST TK_ELSE = 114  ' Removed - may conflict with ELSE keyword
' CONST TK_END = 115   ' Removed - may conflict with END keyword
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

FUNCTION LookupKeyword(word AS STRING) AS INTEGER
    IF word = "CREATE" THEN LookupKeyword = TK_CREATE : EXIT FUNCTION
    IF word = "TABLE" THEN LookupKeyword = TK_TABLE : EXIT FUNCTION
    IF word = "DROP" THEN LookupKeyword = TK_DROP : EXIT FUNCTION
    IF word = "ALTER" THEN LookupKeyword = TK_ALTER : EXIT FUNCTION
    IF word = "INDEX" THEN LookupKeyword = TK_INDEX : EXIT FUNCTION
    IF word = "SELECT" THEN LookupKeyword = TK_SELECT : EXIT FUNCTION
    IF word = "INSERT" THEN LookupKeyword = TK_INSERT : EXIT FUNCTION
    IF word = "UPDATE" THEN LookupKeyword = TK_UPDATE : EXIT FUNCTION
    IF word = "DELETE" THEN LookupKeyword = TK_DELETE : EXIT FUNCTION
    IF word = "INTO" THEN LookupKeyword = TK_INTO : EXIT FUNCTION
    IF word = "FROM" THEN LookupKeyword = TK_FROM : EXIT FUNCTION
    IF word = "WHERE" THEN LookupKeyword = TK_WHERE : EXIT FUNCTION
    IF word = "SET" THEN LookupKeyword = TK_SET : EXIT FUNCTION
    IF word = "VALUES" THEN LookupKeyword = TK_VALUES : EXIT FUNCTION
    IF word = "ORDER" THEN LookupKeyword = TK_ORDER : EXIT FUNCTION
    IF word = "BY" THEN LookupKeyword = TK_BY : EXIT FUNCTION
    IF word = "ASC" THEN LookupKeyword = TK_ASC : EXIT FUNCTION
    IF word = "DESC" THEN LookupKeyword = TK_DESC : EXIT FUNCTION
    IF word = "LIMIT" THEN LookupKeyword = TK_LIMIT : EXIT FUNCTION
    IF word = "OFFSET" THEN LookupKeyword = TK_OFFSET : EXIT FUNCTION
    IF word = "GROUP" THEN LookupKeyword = TK_GROUP : EXIT FUNCTION
    IF word = "HAVING" THEN LookupKeyword = TK_HAVING : EXIT FUNCTION
    IF word = "DISTINCT" THEN LookupKeyword = TK_DISTINCT : EXIT FUNCTION
    IF word = "JOIN" THEN LookupKeyword = TK_JOIN : EXIT FUNCTION
    IF word = "INNER" THEN LookupKeyword = TK_INNER : EXIT FUNCTION
    IF word = "LEFT" THEN LookupKeyword = TK_LEFT : EXIT FUNCTION
    IF word = "RIGHT" THEN LookupKeyword = TK_RIGHT : EXIT FUNCTION
    IF word = "FULL" THEN LookupKeyword = TK_FULL : EXIT FUNCTION
    IF word = "OUTER" THEN LookupKeyword = TK_OUTER : EXIT FUNCTION
    IF word = "CROSS" THEN LookupKeyword = TK_CROSS : EXIT FUNCTION
    IF word = "ON" THEN LookupKeyword = TK_ON : EXIT FUNCTION
    IF word = "AND" THEN LookupKeyword = TK_AND : EXIT FUNCTION
    IF word = "OR" THEN LookupKeyword = TK_OR : EXIT FUNCTION
    IF word = "NOT" THEN LookupKeyword = TK_NOT : EXIT FUNCTION
    IF word = "IN" THEN LookupKeyword = TK_IN : EXIT FUNCTION
    IF word = "IS" THEN LookupKeyword = TK_IS : EXIT FUNCTION
    IF word = "LIKE" THEN LookupKeyword = TK_LIKE : EXIT FUNCTION
    IF word = "BETWEEN" THEN LookupKeyword = TK_BETWEEN : EXIT FUNCTION
    IF word = "EXISTS" THEN LookupKeyword = TK_EXISTS : EXIT FUNCTION
    IF word = "NULL" THEN LookupKeyword = TK_NULL : EXIT FUNCTION
    IF word = "TRUE" THEN LookupKeyword = TK_TRUE : EXIT FUNCTION
    IF word = "FALSE" THEN LookupKeyword = TK_FALSE : EXIT FUNCTION
    IF word = "DEFAULT" THEN LookupKeyword = TK_DEFAULT : EXIT FUNCTION
    IF word = "PRIMARY" THEN LookupKeyword = TK_PRIMARY : EXIT FUNCTION
    IF word = "FOREIGN" THEN LookupKeyword = TK_FOREIGN : EXIT FUNCTION
    IF word = "KEY" THEN LookupKeyword = TK_KEY : EXIT FUNCTION
    IF word = "REFERENCES" THEN LookupKeyword = TK_REFERENCES : EXIT FUNCTION
    IF word = "UNIQUE" THEN LookupKeyword = TK_UNIQUE : EXIT FUNCTION
    IF word = "AUTOINCREMENT" THEN LookupKeyword = TK_AUTOINCREMENT : EXIT FUNCTION
    IF word = "INT" THEN LookupKeyword = TK_INT : EXIT FUNCTION
    IF word = "INTEGER" THEN LookupKeyword = TK_INTEGER_TYPE : EXIT FUNCTION
    IF word = "REAL" THEN LookupKeyword = TK_REAL : EXIT FUNCTION
    IF word = "TEXT" THEN LookupKeyword = TK_TEXT : EXIT FUNCTION
    IF word = "BEGIN" THEN LookupKeyword = TK_BEGIN : EXIT FUNCTION
    IF word = "COMMIT" THEN LookupKeyword = TK_COMMIT : EXIT FUNCTION
    IF word = "ROLLBACK" THEN LookupKeyword = TK_ROLLBACK : EXIT FUNCTION
    IF word = "TRANSACTION" THEN LookupKeyword = TK_TRANSACTION : EXIT FUNCTION
    IF word = "AS" THEN LookupKeyword = TK_AS : EXIT FUNCTION
    IF word = "CASE" THEN LookupKeyword = TK_CASE : EXIT FUNCTION
    IF word = "WHEN" THEN LookupKeyword = TK_WHEN : EXIT FUNCTION
    IF word = "THEN" THEN LookupKeyword = TK_THEN : EXIT FUNCTION
    IF word = "ELSE" THEN LookupKeyword = TK_ELSE : EXIT FUNCTION
    IF word = "END" THEN LookupKeyword = TK_END : EXIT FUNCTION
    IF word = "UNION" THEN LookupKeyword = TK_UNION : EXIT FUNCTION
    IF word = "ALL" THEN LookupKeyword = TK_ALL : EXIT FUNCTION
    IF word = "CAST" THEN LookupKeyword = TK_CAST : EXIT FUNCTION
    LookupKeyword = TK_IDENTIFIER
END FUNCTION

FUNCTION TokenTypeName$(kind AS INTEGER)
    IF kind = TK_EOF THEN TokenTypeName$ = "EOF" : EXIT FUNCTION
    IF kind = TK_ERROR THEN TokenTypeName$ = "ERROR" : EXIT FUNCTION
    IF kind = TK_INTEGER THEN TokenTypeName$ = "INTEGER" : EXIT FUNCTION
    IF kind = TK_NUMBER THEN TokenTypeName$ = "NUMBER" : EXIT FUNCTION
    IF kind = TK_STRING THEN TokenTypeName$ = "STRING" : EXIT FUNCTION
    IF kind = TK_IDENTIFIER THEN TokenTypeName$ = "IDENTIFIER" : EXIT FUNCTION
    IF kind = TK_SELECT THEN TokenTypeName$ = "SELECT" : EXIT FUNCTION
    IF kind = TK_INSERT THEN TokenTypeName$ = "INSERT" : EXIT FUNCTION
    IF kind = TK_UPDATE THEN TokenTypeName$ = "UPDATE" : EXIT FUNCTION
    IF kind = TK_DELETE THEN TokenTypeName$ = "DELETE" : EXIT FUNCTION
    IF kind = TK_CREATE THEN TokenTypeName$ = "CREATE" : EXIT FUNCTION
    IF kind = TK_TABLE THEN TokenTypeName$ = "TABLE" : EXIT FUNCTION
    IF kind = TK_DROP THEN TokenTypeName$ = "DROP" : EXIT FUNCTION
    IF kind = TK_FROM THEN TokenTypeName$ = "FROM" : EXIT FUNCTION
    IF kind = TK_WHERE THEN TokenTypeName$ = "WHERE" : EXIT FUNCTION
    IF kind = TK_INTO THEN TokenTypeName$ = "INTO" : EXIT FUNCTION
    IF kind = TK_VALUES THEN TokenTypeName$ = "VALUES" : EXIT FUNCTION
    IF kind = TK_AND THEN TokenTypeName$ = "AND" : EXIT FUNCTION
    IF kind = TK_OR THEN TokenTypeName$ = "OR" : EXIT FUNCTION
    IF kind = TK_NOT THEN TokenTypeName$ = "NOT" : EXIT FUNCTION
    IF kind = TK_NULL THEN TokenTypeName$ = "NULL" : EXIT FUNCTION
    IF kind = TK_PLUS THEN TokenTypeName$ = "PLUS" : EXIT FUNCTION
    IF kind = TK_MINUS THEN TokenTypeName$ = "MINUS" : EXIT FUNCTION
    IF kind = TK_STAR THEN TokenTypeName$ = "STAR" : EXIT FUNCTION
    IF kind = TK_SLASH THEN TokenTypeName$ = "SLASH" : EXIT FUNCTION
    IF kind = TK_EQ THEN TokenTypeName$ = "EQ" : EXIT FUNCTION
    IF kind = TK_NE THEN TokenTypeName$ = "NE" : EXIT FUNCTION
    IF kind = TK_LT THEN TokenTypeName$ = "LT" : EXIT FUNCTION
    IF kind = TK_GT THEN TokenTypeName$ = "GT" : EXIT FUNCTION
    IF kind = TK_LE THEN TokenTypeName$ = "LE" : EXIT FUNCTION
    IF kind = TK_GE THEN TokenTypeName$ = "GE" : EXIT FUNCTION
    IF kind = TK_LPAREN THEN TokenTypeName$ = "LPAREN" : EXIT FUNCTION
    IF kind = TK_RPAREN THEN TokenTypeName$ = "RPAREN" : EXIT FUNCTION
    IF kind = TK_COMMA THEN TokenTypeName$ = "COMMA" : EXIT FUNCTION
    IF kind = TK_SEMICOLON THEN TokenTypeName$ = "SEMICOLON" : EXIT FUNCTION
    IF kind = TK_DOT THEN TokenTypeName$ = "DOT" : EXIT FUNCTION
    TokenTypeName$ = "UNKNOWN"
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
        IF LexerPeek() = "\" THEN
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

PRINT "Test"
