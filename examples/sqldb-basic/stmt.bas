' stmt.bas - Statement Types
' Part of SQLite Clone - Viper Basic Implementation

AddFile "expr.bas"

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
' CREATE INDEX STATEMENT
'=============================================================================

CLASS CreateIndexStmt
    PUBLIC indexName AS STRING
    PUBLIC tableName AS STRING
    PUBLIC columnNames(MAX_INDEX_COLUMNS) AS STRING
    PUBLIC columnCount AS INTEGER
    PUBLIC isUnique AS INTEGER

    PUBLIC SUB Init()
        indexName = ""
        tableName = ""
        columnCount = 0
        isUnique = 0
    END SUB

    PUBLIC SUB AddColumn(colName AS STRING)
        IF columnCount < MAX_INDEX_COLUMNS THEN
            columnNames(columnCount) = colName
            columnCount = columnCount + 1
        END IF
    END SUB

    PUBLIC FUNCTION ToString$()
        DIM result AS STRING
        DIM i AS INTEGER
        result = ""
        IF isUnique <> 0 THEN
            result = "UNIQUE "
        END IF
        result = result + "CREATE INDEX " + indexName + " ON " + tableName + " ("
        FOR i = 0 TO columnCount - 1
            IF i > 0 THEN
                result = result + ", "
            END IF
            result = result + columnNames(i)
        NEXT i
        result = result + ");"
        ToString$ = result
    END FUNCTION
END CLASS

'=============================================================================
' DROP INDEX STATEMENT
'=============================================================================

CLASS DropIndexStmt
    PUBLIC indexName AS STRING

    PUBLIC SUB Init()
        indexName = ""
    END SUB

    PUBLIC FUNCTION ToString$()
        ToString$ = "DROP INDEX " + indexName + ";"
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

