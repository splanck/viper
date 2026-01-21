' table.bas - SQL Table Class
' Part of SQLite Clone - Viper Basic Implementation
' Requires: schema.bas (AddFile before this)

'=============================================================================
' CONSTANTS
'=============================================================================

CONST MAX_ROWS = 1000  ' Maximum rows per table

'=============================================================================
' SQLTABLE CLASS
'=============================================================================

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

FUNCTION MakeTable(tableName AS STRING) AS SqlTable
    DIM tbl AS SqlTable
    LET tbl = NEW SqlTable()
    tbl.Init(tableName)
    MakeTable = tbl
END FUNCTION
