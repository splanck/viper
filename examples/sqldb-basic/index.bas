' index.bas - Index Classes for SQL Database
' Part of SQLite Clone - Viper Basic Implementation

'=============================================================================
' INDEX STRUCTURES
'=============================================================================

CONST MAX_INDEX_ENTRIES = 2000  ' Maximum entries in an index
CONST MAX_INDEX_COLUMNS = 10    ' Maximum columns in a composite index
CONST MAX_INDEXES = 20          ' Maximum indexes per table

'=============================================================================
' INDEX ENTRY CLASS - Maps key values to row indices
'=============================================================================

CLASS SqlIndexEntry
    PUBLIC keyHash AS INTEGER      ' Hash of the key value(s)
    PUBLIC keyString AS STRING     ' String representation of key for comparison
    PUBLIC rowIndex AS INTEGER     ' Index into table rows

    PUBLIC SUB Init(hash AS INTEGER, keyStr AS STRING, rowIdx AS INTEGER)
        keyHash = hash
        keyString = keyStr
        rowIndex = rowIdx
    END SUB
END CLASS

'=============================================================================
' INDEX CLASS - Hash-based index on one or more columns
'=============================================================================

CLASS SqlIndex
    PUBLIC name AS STRING
    PUBLIC tableName AS STRING
    PUBLIC columnNames(MAX_INDEX_COLUMNS) AS STRING
    PUBLIC columnCount AS INTEGER
    PUBLIC isUnique AS INTEGER
    PUBLIC entries(MAX_INDEX_ENTRIES) AS SqlIndexEntry
    PUBLIC entryCount AS INTEGER

    PUBLIC SUB Init(idxName AS STRING, tblName AS STRING)
        name = idxName
        tableName = tblName
        columnCount = 0
        isUnique = 0
        entryCount = 0
    END SUB

    PUBLIC SUB AddColumn(colName AS STRING)
        IF columnCount < MAX_INDEX_COLUMNS THEN
            columnNames(columnCount) = colName
            columnCount = columnCount + 1
        END IF
    END SUB

    ' Compute a simple hash for a string
    PUBLIC FUNCTION HashString(s AS STRING) AS INTEGER
        DIM hash AS INTEGER
        DIM i AS INTEGER
        DIM c AS INTEGER
        hash = 0
        FOR i = 1 TO LEN(s)
            c = ASC(MID$(s, i, 1))
            hash = (hash * 31 + c) MOD 32767
        NEXT i
        HashString = hash
    END FUNCTION

    ' Build key string from row values for indexed columns
    PUBLIC FUNCTION BuildKeyString(row AS SqlRow, tbl AS SqlTable) AS STRING
        DIM result AS STRING
        DIM i AS INTEGER
        DIM colIdx AS INTEGER
        DIM val AS SqlValue
        DIM colName AS STRING

        result = ""
        FOR i = 0 TO columnCount - 1
            colName = columnNames(i)
            colIdx = tbl.FindColumnIndex(colName)
            IF colIdx >= 0 THEN
                LET val = row.GetValue(colIdx)
                IF i > 0 THEN
                    result = result + "|"
                END IF
                result = result + val.ToString$()
            END IF
        NEXT i
        BuildKeyString = result
    END FUNCTION

    ' Add an entry to the index
    PUBLIC FUNCTION AddEntry(row AS SqlRow, rowIdx AS INTEGER, tbl AS SqlTable) AS INTEGER
        DIM keyStr AS STRING
        DIM hash AS INTEGER
        DIM entry AS SqlIndexEntry
        DIM i AS INTEGER

        keyStr = BuildKeyString(row, tbl)
        hash = HashString(keyStr)

        ' Check for unique constraint violation
        IF isUnique <> 0 THEN
            FOR i = 0 TO entryCount - 1
                IF entries(i).keyString = keyStr THEN
                    AddEntry = 0  ' Duplicate found
                    EXIT FUNCTION
                END IF
            NEXT i
        END IF

        ' Add new entry
        IF entryCount < MAX_INDEX_ENTRIES THEN
            LET entry = NEW SqlIndexEntry()
            entry.Init(hash, keyStr, rowIdx)
            LET entries(entryCount) = entry
            entryCount = entryCount + 1
            AddEntry = -1  ' Success
        ELSE
            AddEntry = 0  ' Index full
        END IF
    END FUNCTION

    ' Remove an entry from the index (for DELETE operations)
    PUBLIC SUB RemoveEntry(rowIdx AS INTEGER)
        DIM i AS INTEGER
        DIM j AS INTEGER

        FOR i = 0 TO entryCount - 1
            IF entries(i).rowIndex = rowIdx THEN
                ' Shift remaining entries down
                FOR j = i TO entryCount - 2
                    LET entries(j) = entries(j + 1)
                NEXT j
                entryCount = entryCount - 1
                EXIT SUB
            END IF
        NEXT i
    END SUB

    ' Lookup entries matching a single key value
    ' Returns array of row indices (up to maxResults) and count
    PUBLIC FUNCTION LookupSingle(keyVal AS SqlValue, tbl AS SqlTable, results() AS INTEGER, maxResults AS INTEGER) AS INTEGER
        DIM keyStr AS STRING
        DIM hash AS INTEGER
        DIM i AS INTEGER
        DIM count AS INTEGER

        keyStr = keyVal.ToString$()
        hash = HashString(keyStr)
        count = 0

        ' Linear search through entries with matching hash
        FOR i = 0 TO entryCount - 1
            IF entries(i).keyHash = hash THEN
                ' Verify key matches (hash collision check)
                IF entries(i).keyString = keyStr THEN
                    IF count < maxResults THEN
                        results(count) = entries(i).rowIndex
                        count = count + 1
                    END IF
                END IF
            END IF
        NEXT i

        LookupSingle = count
    END FUNCTION

    ' Rebuild the entire index from table data
    PUBLIC SUB Rebuild(tbl AS SqlTable)
        DIM i AS INTEGER
        DIM row AS SqlRow

        entryCount = 0

        FOR i = 0 TO tbl.rowCount - 1
            LET row = tbl.rows(i)
            IF row.deleted = 0 THEN
                AddEntry(row, i, tbl)
            END IF
        NEXT i
    END SUB

    PUBLIC FUNCTION ToString$()
        DIM result AS STRING
        DIM i AS INTEGER
        result = "INDEX " + name + " ON " + tableName + " ("
        FOR i = 0 TO columnCount - 1
            IF i > 0 THEN
                result = result + ", "
            END IF
            result = result + columnNames(i)
        NEXT i
        result = result + ")"
        IF isUnique <> 0 THEN
            result = "UNIQUE " + result
        END IF
        result = result + " [" + STR$(entryCount) + " entries]"
        ToString$ = result
    END FUNCTION
END CLASS

'=============================================================================
' INDEX MANAGER - Stores all indexes for the database
'=============================================================================

CONST MAX_DB_INDEXES = 100

CLASS SqlIndexManager
    PUBLIC indexes(MAX_DB_INDEXES) AS SqlIndex
    PUBLIC indexCount AS INTEGER

    PUBLIC SUB Init()
        indexCount = 0
    END SUB

    ' Find an index by name
    PUBLIC FUNCTION FindIndex(idxName AS STRING) AS INTEGER
        DIM i AS INTEGER
        FOR i = 0 TO indexCount - 1
            IF indexes(i).name = idxName THEN
                FindIndex = i
                EXIT FUNCTION
            END IF
        NEXT i
        FindIndex = -1
    END FUNCTION

    ' Find an index for a specific table and column combination
    PUBLIC FUNCTION FindIndexForColumn(tblName AS STRING, colName AS STRING) AS INTEGER
        DIM i AS INTEGER
        DIM idx AS SqlIndex
        FOR i = 0 TO indexCount - 1
            LET idx = indexes(i)
            IF idx.tableName = tblName AND idx.columnCount = 1 THEN
                IF idx.columnNames(0) = colName THEN
                    FindIndexForColumn = i
                    EXIT FUNCTION
                END IF
            END IF
        NEXT i
        FindIndexForColumn = -1
    END FUNCTION

    ' Add an index
    PUBLIC SUB AddIndex(idx AS SqlIndex)
        IF indexCount < MAX_DB_INDEXES THEN
            LET indexes(indexCount) = idx
            indexCount = indexCount + 1
        END IF
    END SUB

    ' Drop an index by name
    PUBLIC SUB DropIndex(idxName AS STRING)
        DIM i AS INTEGER
        DIM j AS INTEGER
        DIM foundIdx AS INTEGER

        foundIdx = FindIndex(idxName)
        IF foundIdx >= 0 THEN
            ' Shift remaining indexes down
            FOR j = foundIdx TO indexCount - 2
                LET indexes(j) = indexes(j + 1)
            NEXT j
            indexCount = indexCount - 1
        END IF
    END SUB

    ' Get all indexes for a table
    PUBLIC FUNCTION GetTableIndexCount(tblName AS STRING) AS INTEGER
        DIM i AS INTEGER
        DIM count AS INTEGER
        count = 0
        FOR i = 0 TO indexCount - 1
            IF indexes(i).tableName = tblName THEN
                count = count + 1
            END IF
        NEXT i
        GetTableIndexCount = count
    END FUNCTION
END CLASS

' Factory function for creating indexes
FUNCTION MakeIndex(idxName AS STRING, tblName AS STRING) AS SqlIndex
    DIM idx AS SqlIndex
    LET idx = NEW SqlIndex()
    idx.Init(idxName, tblName)
    MakeIndex = idx
END FUNCTION

