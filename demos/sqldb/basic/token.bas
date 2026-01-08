' token.bas - SQL Token Types and Constants
' Part of SQLite Clone - Viper Basic Implementation

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

' Additional keywords
CONST TK_BLOB = 120
CONST TK_VARCHAR = 121
CONST TK_CHAR = 122
CONST TK_BOOLEAN = 123
CONST TK_DATE = 124
CONST TK_DATETIME = 125
CONST TK_TIME = 126
CONST TK_IMPORT = 127

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
' TOKEN HELPER FUNCTIONS
'=============================================================================

FUNCTION TokenTypeName$(kind AS INTEGER)
    IF kind = TK_EOF THEN
        TokenTypeName$ = "EOF"
    ELSEIF kind = TK_ERROR THEN
        TokenTypeName$ = "ERROR"
    ELSEIF kind = TK_INTEGER THEN
        TokenTypeName$ = "INTEGER"
    ELSEIF kind = TK_NUMBER THEN
        TokenTypeName$ = "NUMBER"
    ELSEIF kind = TK_STRING THEN
        TokenTypeName$ = "STRING"
    ELSEIF kind = TK_IDENTIFIER THEN
        TokenTypeName$ = "IDENTIFIER"
    ELSEIF kind = TK_SELECT THEN
        TokenTypeName$ = "SELECT"
    ELSEIF kind = TK_FROM THEN
        TokenTypeName$ = "FROM"
    ELSEIF kind = TK_WHERE THEN
        TokenTypeName$ = "WHERE"
    ELSEIF kind = TK_INSERT THEN
        TokenTypeName$ = "INSERT"
    ELSEIF kind = TK_INTO THEN
        TokenTypeName$ = "INTO"
    ELSEIF kind = TK_VALUES THEN
        TokenTypeName$ = "VALUES"
    ELSEIF kind = TK_CREATE THEN
        TokenTypeName$ = "CREATE"
    ELSEIF kind = TK_TABLE THEN
        TokenTypeName$ = "TABLE"
    ELSEIF kind = TK_STAR THEN
        TokenTypeName$ = "STAR"
    ELSEIF kind = TK_COMMA THEN
        TokenTypeName$ = "COMMA"
    ELSEIF kind = TK_LPAREN THEN
        TokenTypeName$ = "LPAREN"
    ELSEIF kind = TK_RPAREN THEN
        TokenTypeName$ = "RPAREN"
    ELSEIF kind = TK_SEMICOLON THEN
        TokenTypeName$ = "SEMICOLON"
    ELSEIF kind = TK_GT THEN
        TokenTypeName$ = "GT"
    ELSE
        TokenTypeName$ = "TOKEN_" + STR$(kind)
    END IF
END FUNCTION

FUNCTION LookupKeyword(word AS STRING) AS INTEGER
    DIM upper AS STRING
    upper = UCASE$(word)

    IF upper = "SELECT" THEN
        LookupKeyword = TK_SELECT
    ELSEIF upper = "FROM" THEN
        LookupKeyword = TK_FROM
    ELSEIF upper = "WHERE" THEN
        LookupKeyword = TK_WHERE
    ELSEIF upper = "INSERT" THEN
        LookupKeyword = TK_INSERT
    ELSEIF upper = "INTO" THEN
        LookupKeyword = TK_INTO
    ELSEIF upper = "VALUES" THEN
        LookupKeyword = TK_VALUES
    ELSEIF upper = "UPDATE" THEN
        LookupKeyword = TK_UPDATE
    ELSEIF upper = "DELETE" THEN
        LookupKeyword = TK_DELETE
    ELSEIF upper = "SET" THEN
        LookupKeyword = TK_SET
    ELSEIF upper = "CREATE" THEN
        LookupKeyword = TK_CREATE
    ELSEIF upper = "TABLE" THEN
        LookupKeyword = TK_TABLE
    ELSEIF upper = "DROP" THEN
        LookupKeyword = TK_DROP
    ELSEIF upper = "ALTER" THEN
        LookupKeyword = TK_ALTER
    ELSEIF upper = "INDEX" THEN
        LookupKeyword = TK_INDEX
    ELSEIF upper = "AND" THEN
        LookupKeyword = TK_AND
    ELSEIF upper = "OR" THEN
        LookupKeyword = TK_OR
    ELSEIF upper = "NOT" THEN
        LookupKeyword = TK_NOT
    ELSEIF upper = "NULL" THEN
        LookupKeyword = TK_NULL
    ELSEIF upper = "TRUE" THEN
        LookupKeyword = TK_TRUE
    ELSEIF upper = "FALSE" THEN
        LookupKeyword = TK_FALSE
    ELSEIF upper = "PRIMARY" THEN
        LookupKeyword = TK_PRIMARY
    ELSEIF upper = "FOREIGN" THEN
        LookupKeyword = TK_FOREIGN
    ELSEIF upper = "KEY" THEN
        LookupKeyword = TK_KEY
    ELSEIF upper = "REFERENCES" THEN
        LookupKeyword = TK_REFERENCES
    ELSEIF upper = "UNIQUE" THEN
        LookupKeyword = TK_UNIQUE
    ELSEIF upper = "DEFAULT" THEN
        LookupKeyword = TK_DEFAULT
    ELSEIF upper = "AUTOINCREMENT" THEN
        LookupKeyword = TK_AUTOINCREMENT
    ELSEIF upper = "INT" THEN
        LookupKeyword = TK_INT
    ELSEIF upper = "INTEGER" THEN
        LookupKeyword = TK_INTEGER_TYPE
    ELSEIF upper = "REAL" THEN
        LookupKeyword = TK_REAL
    ELSEIF upper = "TEXT" THEN
        LookupKeyword = TK_TEXT
    ELSEIF upper = "BLOB" THEN
        LookupKeyword = TK_BLOB
    ELSEIF upper = "VARCHAR" THEN
        LookupKeyword = TK_VARCHAR
    ELSEIF upper = "CHAR" THEN
        LookupKeyword = TK_CHAR
    ELSEIF upper = "BOOLEAN" THEN
        LookupKeyword = TK_BOOLEAN
    ELSEIF upper = "DATE" THEN
        LookupKeyword = TK_DATE
    ELSEIF upper = "DATETIME" THEN
        LookupKeyword = TK_DATETIME
    ELSEIF upper = "TIME" THEN
        LookupKeyword = TK_TIME
    ELSEIF upper = "ORDER" THEN
        LookupKeyword = TK_ORDER
    ELSEIF upper = "BY" THEN
        LookupKeyword = TK_BY
    ELSEIF upper = "ASC" THEN
        LookupKeyword = TK_ASC
    ELSEIF upper = "DESC" THEN
        LookupKeyword = TK_DESC
    ELSEIF upper = "LIMIT" THEN
        LookupKeyword = TK_LIMIT
    ELSEIF upper = "OFFSET" THEN
        LookupKeyword = TK_OFFSET
    ELSEIF upper = "GROUP" THEN
        LookupKeyword = TK_GROUP
    ELSEIF upper = "HAVING" THEN
        LookupKeyword = TK_HAVING
    ELSEIF upper = "DISTINCT" THEN
        LookupKeyword = TK_DISTINCT
    ELSEIF upper = "JOIN" THEN
        LookupKeyword = TK_JOIN
    ELSEIF upper = "INNER" THEN
        LookupKeyword = TK_INNER
    ELSEIF upper = "LEFT" THEN
        LookupKeyword = TK_LEFT
    ELSEIF upper = "RIGHT" THEN
        LookupKeyword = TK_RIGHT
    ELSEIF upper = "FULL" THEN
        LookupKeyword = TK_FULL
    ELSEIF upper = "OUTER" THEN
        LookupKeyword = TK_OUTER
    ELSEIF upper = "CROSS" THEN
        LookupKeyword = TK_CROSS
    ELSEIF upper = "ON" THEN
        LookupKeyword = TK_ON
    ELSEIF upper = "IN" THEN
        LookupKeyword = TK_IN
    ELSEIF upper = "IS" THEN
        LookupKeyword = TK_IS
    ELSEIF upper = "LIKE" THEN
        LookupKeyword = TK_LIKE
    ELSEIF upper = "BETWEEN" THEN
        LookupKeyword = TK_BETWEEN
    ELSEIF upper = "EXISTS" THEN
        LookupKeyword = TK_EXISTS
    ELSEIF upper = "BEGIN" THEN
        LookupKeyword = TK_BEGIN
    ELSEIF upper = "COMMIT" THEN
        LookupKeyword = TK_COMMIT
    ELSEIF upper = "ROLLBACK" THEN
        LookupKeyword = TK_ROLLBACK
    ELSEIF upper = "TRANSACTION" THEN
        LookupKeyword = TK_TRANSACTION
    ELSEIF upper = "AS" THEN
        LookupKeyword = TK_AS
    ELSEIF upper = "CASE" THEN
        LookupKeyword = TK_CASE
    ELSEIF upper = "WHEN" THEN
        LookupKeyword = TK_WHEN
    ELSEIF upper = "THEN" THEN
        LookupKeyword = TK_THEN
    ELSEIF upper = "ELSE" THEN
        LookupKeyword = TK_ELSE
    ELSEIF upper = "END" THEN
        LookupKeyword = TK_END
    ELSEIF upper = "UNION" THEN
        LookupKeyword = TK_UNION
    ELSEIF upper = "ALL" THEN
        LookupKeyword = TK_ALL
    ELSEIF upper = "CAST" THEN
        LookupKeyword = TK_CAST
    ELSE
        LookupKeyword = TK_IDENTIFIER
    END IF
END FUNCTION
