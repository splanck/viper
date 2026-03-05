' schema.bas - Column, Row, Table Classes
' Part of SQLite Clone - Viper Basic Implementation

AddFile "types.bas"

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
    PUBLIC isForeignKey AS INTEGER
    PUBLIC refTableName AS STRING
    PUBLIC refColumnName AS STRING

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
        isForeignKey = 0
        refTableName = ""
        refColumnName = ""
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
        IF isForeignKey <> 0 THEN
            result = result + " REFERENCES " + refTableName + "(" + refColumnName + ")"
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

    PUBLIC SUB CopyFrom(srcRow AS SqlRow)
        DIM i AS INTEGER
        DIM cloned AS SqlValue
        ' Initialize to source row's column count
        columnCount = srcRow.columnCount
        deleted = srcRow.deleted
        FOR i = 0 TO srcRow.columnCount - 1
            LET cloned = NEW SqlValue()
            cloned.kind = srcRow.values(i).kind
            cloned.intValue = srcRow.values(i).intValue
            cloned.realValue = srcRow.values(i).realValue
            cloned.textValue = srcRow.values(i).textValue
            LET values(i) = cloned
        NEXT i
    END SUB

    PUBLIC SUB InitEmpty()
        columnCount = 0
        deleted = 0
    END SUB

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

