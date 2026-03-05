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

    PUBLIC FUNCTION ToString$()
        DIM result AS STRING
        DIM i AS INTEGER
        DIM j AS INTEGER
        DIM r AS SqlRow
        DIM val AS SqlValue
        DIM colWidth AS INTEGER

        ' If it's an error, just return the error message
        IF success = 0 THEN
            ToString$ = "ERROR: " + message
            EXIT FUNCTION
        END IF

        ' If there are no rows, return the message
        IF rowCount = 0 THEN
            ToString$ = message
            EXIT FUNCTION
        END IF

        result = ""
        colWidth = 12

        ' Print column headers
        FOR i = 0 TO columnCount - 1
            IF i > 0 THEN
                result = result + " | "
            END IF
            result = result + columnNames(i)
        NEXT i
        result = result + CHR$(10)

        ' Print separator
        FOR i = 0 TO columnCount - 1
            IF i > 0 THEN
                result = result + "-+-"
            END IF
            result = result + "---------"
        NEXT i
        result = result + CHR$(10)

        ' Print rows
        FOR i = 0 TO rowCount - 1
            LET r = rows(i)
            FOR j = 0 TO columnCount - 1
                IF j > 0 THEN
                    result = result + " | "
                END IF
                LET val = r.values(j)
                result = result + val.ToString$()
            NEXT j
            result = result + CHR$(10)
        NEXT i

        result = result + "(" + STR$(rowCount) + " rows)"
        ToString$ = result
    END FUNCTION
END CLASS

'=============================================================================
' TRANSACTION MANAGER CLASS
'=============================================================================

CONST MAX_SAVEPOINTS = 10

CLASS TransactionManager
    PUBLIC inTransaction AS INTEGER
    PUBLIC savedTables(MAX_TABLES) AS SqlTable
    PUBLIC savedTableCount AS INTEGER
    PUBLIC savepointNames(MAX_SAVEPOINTS) AS STRING
    PUBLIC savepointTableData(MAX_SAVEPOINTS, MAX_TABLES) AS SqlTable
    PUBLIC savepointTableCounts(MAX_SAVEPOINTS) AS INTEGER
    PUBLIC savepointCount AS INTEGER

    PUBLIC SUB Init()
        inTransaction = 0
        savedTableCount = 0
        savepointCount = 0
    END SUB

    PUBLIC FUNCTION BeginTransaction(db AS SqlDatabase) AS INTEGER
        DIM i AS INTEGER
        DIM j AS INTEGER
        DIM srcTbl AS SqlTable
        DIM dstTbl AS SqlTable
        DIM srcRow AS SqlRow
        DIM dstRow AS SqlRow

        IF inTransaction <> 0 THEN
            BeginTransaction = 0
            EXIT FUNCTION
        END IF

        ' Save snapshot of all tables
        savedTableCount = db.tableCount
        FOR i = 0 TO db.tableCount - 1
            LET srcTbl = db.tables(i)
            LET dstTbl = NEW SqlTable()
            dstTbl.Init(srcTbl.name)

            ' Copy columns
            FOR j = 0 TO srcTbl.columnCount - 1
                dstTbl.AddColumn(srcTbl.columns(j))
            NEXT j

            ' Copy rows
            FOR j = 0 TO srcTbl.rowCount - 1
                LET srcRow = srcTbl.rows(j)
                LET dstRow = NEW SqlRow()
                dstRow.InitEmpty()
                dstRow.CopyFrom(srcRow)
                dstTbl.AddRow(dstRow)
            NEXT j

            dstTbl.autoIncrementValue = srcTbl.autoIncrementValue
            LET savedTables(i) = dstTbl
        NEXT i

        inTransaction = -1
        savepointCount = 0
        BeginTransaction = -1
    END FUNCTION

    PUBLIC FUNCTION Commit() AS INTEGER
        IF inTransaction = 0 THEN
            Commit = 0
            EXIT FUNCTION
        END IF

        ' Just clear the saved state
        inTransaction = 0
        savedTableCount = 0
        savepointCount = 0
        Commit = -1
    END FUNCTION

    PUBLIC FUNCTION Rollback(db AS SqlDatabase) AS INTEGER
        DIM i AS INTEGER

        IF inTransaction = 0 THEN
            Rollback = 0
            EXIT FUNCTION
        END IF

        ' Restore all tables from snapshot
        db.tableCount = savedTableCount
        FOR i = 0 TO savedTableCount - 1
            LET db.tables(i) = savedTables(i)
        NEXT i

        inTransaction = 0
        savedTableCount = 0
        savepointCount = 0
        Rollback = -1
    END FUNCTION

    PUBLIC FUNCTION CreateSavepoint(spName AS STRING, db AS SqlDatabase) AS INTEGER
        DIM i AS INTEGER
        DIM j AS INTEGER
        DIM srcTbl AS SqlTable
        DIM dstTbl AS SqlTable
        DIM srcRow AS SqlRow
        DIM dstRow AS SqlRow

        IF inTransaction = 0 THEN
            CreateSavepoint = 0
            EXIT FUNCTION
        END IF

        IF savepointCount >= MAX_SAVEPOINTS THEN
            CreateSavepoint = 0
            EXIT FUNCTION
        END IF

        savepointNames(savepointCount) = spName
        savepointTableCounts(savepointCount) = db.tableCount

        ' Save snapshot of all tables at this savepoint
        FOR i = 0 TO db.tableCount - 1
            LET srcTbl = db.tables(i)
            LET dstTbl = NEW SqlTable()
            dstTbl.Init(srcTbl.name)

            ' Copy columns
            FOR j = 0 TO srcTbl.columnCount - 1
                dstTbl.AddColumn(srcTbl.columns(j))
            NEXT j

            ' Copy rows
            FOR j = 0 TO srcTbl.rowCount - 1
                LET srcRow = srcTbl.rows(j)
                LET dstRow = NEW SqlRow()
                dstRow.InitEmpty()
                dstRow.CopyFrom(srcRow)
                dstTbl.AddRow(dstRow)
            NEXT j

            dstTbl.autoIncrementValue = srcTbl.autoIncrementValue
            LET savepointTableData(savepointCount, i) = dstTbl
        NEXT i

        savepointCount = savepointCount + 1
        CreateSavepoint = -1
    END FUNCTION

    PUBLIC FUNCTION RollbackToSavepoint(spName AS STRING, db AS SqlDatabase) AS INTEGER
        DIM i AS INTEGER
        DIM j AS INTEGER
        DIM spIdx AS INTEGER

        IF inTransaction = 0 THEN
            RollbackToSavepoint = 0
            EXIT FUNCTION
        END IF

        ' Find the savepoint
        spIdx = -1
        FOR i = savepointCount - 1 TO 0 STEP -1
            IF savepointNames(i) = spName THEN
                spIdx = i
                EXIT FOR
            END IF
        NEXT i

        IF spIdx < 0 THEN
            RollbackToSavepoint = 0
            EXIT FUNCTION
        END IF

        ' Restore tables from this savepoint
        db.tableCount = savepointTableCounts(spIdx)
        FOR i = 0 TO db.tableCount - 1
            LET db.tables(i) = savepointTableData(spIdx, i)
        NEXT i

        ' Remove all savepoints after this one
        savepointCount = spIdx + 1

        RollbackToSavepoint = -1
    END FUNCTION

    PUBLIC FUNCTION ReleaseSavepoint(spName AS STRING) AS INTEGER
        DIM i AS INTEGER
        DIM j AS INTEGER
        DIM spIdx AS INTEGER

        IF inTransaction = 0 THEN
            ReleaseSavepoint = 0
            EXIT FUNCTION
        END IF

        ' Find the savepoint
        spIdx = -1
        FOR i = savepointCount - 1 TO 0 STEP -1
            IF savepointNames(i) = spName THEN
                spIdx = i
                EXIT FOR
            END IF
        NEXT i

        IF spIdx < 0 THEN
            ReleaseSavepoint = 0
            EXIT FUNCTION
        END IF

        ' Remove this savepoint and all after it
        savepointCount = spIdx

        ReleaseSavepoint = -1
    END FUNCTION
END CLASS

'=============================================================================
' EXECUTOR - Global Database
'=============================================================================

DIM gDatabase AS SqlDatabase
DIM gDbInitialized AS INTEGER
DIM gIndexManager AS SqlIndexManager
DIM gTransactionMgr AS TransactionManager

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
        LET gTransactionMgr = NEW TransactionManager()
        gTransactionMgr.Init()
        gDbInitialized = -1
    END IF
END SUB

