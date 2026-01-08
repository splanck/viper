' schema.bas - SQL Column and Row Classes
' Part of SQLite Clone - Viper Basic Implementation
' Requires: types.bas (AddFile before this)

'=============================================================================
' CONSTANTS
'=============================================================================

CONST MAX_COLUMNS = 64  ' Maximum columns per table

'=============================================================================
' SQLCOLUMN CLASS
'=============================================================================

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

FUNCTION MakeColumn(name AS STRING, typeCode AS INTEGER) AS SqlColumn
    DIM col AS SqlColumn
    LET col = NEW SqlColumn()
    col.Init(name, typeCode)
    MakeColumn = col
END FUNCTION

'=============================================================================
' SQLROW CLASS
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

FUNCTION MakeRow(count AS INTEGER) AS SqlRow
    DIM row AS SqlRow
    LET row = NEW SqlRow()
    row.Init(count)
    MakeRow = row
END FUNCTION
