' database.bas - Database and QueryResult Classes
' Part of SQLite Clone - Viper Basic Implementation

AddFile "index.bas"
AddFile "parser.bas"

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
DIM gIndexManager AS SqlIndexManager

' Outer context for correlated subqueries
DIM gOuterRow AS SqlRow
DIM gOuterTable AS SqlTable
DIM gHasOuterContext AS INTEGER
DIM gOuterTableAlias AS STRING
DIM gCurrentTableAlias AS STRING

SUB InitDatabase()
    IF gDbInitialized = 0 THEN
        LET gDatabase = NEW SqlDatabase()
        gDatabase.Init()
        LET gIndexManager = NEW SqlIndexManager()
        gIndexManager.Init()
        gDbInitialized = -1
    END IF
END SUB

