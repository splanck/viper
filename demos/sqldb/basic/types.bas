' types.bas - SQL Value Types
' Part of SQLite Clone - Viper Basic Implementation

AddFile "lexer.bas"

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

