' stmt.bas - SQL Statement Classes
' Part of SQLite Clone - Viper Basic Implementation
' Requires: expr.bas (AddFile before this)

'=============================================================================
' STATEMENT CONSTANTS
'=============================================================================

CONST MAX_STMT_COLUMNS = 64
CONST MAX_STMT_VALUES = 64
CONST MAX_VALUE_ROWS = 100
CONST VALUES_ARRAY_SIZE = 6400  ' MAX_VALUE_ROWS * MAX_STMT_VALUES

'=============================================================================
' CREATE TABLE STATEMENT
'=============================================================================

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

'=============================================================================
' INSERT STATEMENT
'=============================================================================

CLASS InsertStmt
    PUBLIC tableName AS STRING
    PUBLIC columnNames(MAX_STMT_COLUMNS) AS STRING
    PUBLIC columnCount AS INTEGER
    PUBLIC valueRows(VALUES_ARRAY_SIZE) AS Expr   ' Flattened 2D array (Bug #020 workaround)
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
' SELECT STATEMENT
'=============================================================================

CLASS SelectStmt
    PUBLIC selectAll AS INTEGER
    PUBLIC tableName AS STRING
    PUBLIC tableAlias AS STRING
    PUBLIC columns(100) AS Expr
    PUBLIC columnCount AS INTEGER
    PUBLIC whereClause AS Expr
    PUBLIC hasWhere AS INTEGER
    PUBLIC orderByExprs(20) AS Expr
    PUBLIC orderByDir(20) AS INTEGER  ' 0 = ASC, 1 = DESC
    PUBLIC orderByCount AS INTEGER
    PUBLIC limitValue AS INTEGER
    PUBLIC offsetValue AS INTEGER
    PUBLIC isDistinct AS INTEGER
    PUBLIC groupByExprs(20) AS Expr
    PUBLIC groupByCount AS INTEGER
    PUBLIC havingClause AS Expr
    PUBLIC hasHaving AS INTEGER
    PUBLIC tableNames(20) AS STRING
    PUBLIC tableAliases(20) AS STRING
    PUBLIC tableCount AS INTEGER
    PUBLIC joinTypes(20) AS INTEGER
    PUBLIC joinConditions(20) AS Expr
    PUBLIC joinConditionCount AS INTEGER
    PUBLIC derivedTableSQL AS STRING
    PUBLIC derivedTableAlias AS STRING
    PUBLIC hasDerivedTable AS INTEGER

    PUBLIC SUB Init()
        selectAll = 0
        tableName = ""
        tableAlias = ""
        columnCount = 0
        hasWhere = 0
        orderByCount = 0
        limitValue = -1
        offsetValue = 0
        isDistinct = 0
        groupByCount = 0
        hasHaving = 0
        tableCount = 0
        joinConditionCount = 0
        derivedTableSQL = ""
        derivedTableAlias = ""
        hasDerivedTable = 0
    END SUB

    PUBLIC SUB AddColumn(col AS Expr)
        IF columnCount < 100 THEN
            LET columns(columnCount) = col
            columnCount = columnCount + 1
        END IF
    END SUB

    PUBLIC SUB SetWhere(expr AS Expr)
        LET whereClause = expr
        hasWhere = -1
    END SUB

    PUBLIC SUB AddOrderBy(expr AS Expr, isDesc AS INTEGER)
        IF orderByCount < 20 THEN
            LET orderByExprs(orderByCount) = expr
            orderByDir(orderByCount) = isDesc
            orderByCount = orderByCount + 1
        END IF
    END SUB

    PUBLIC SUB AddGroupBy(expr AS Expr)
        IF groupByCount < 20 THEN
            LET groupByExprs(groupByCount) = expr
            groupByCount = groupByCount + 1
        END IF
    END SUB

    PUBLIC SUB AddTable(tName AS STRING, tAlias AS STRING)
        IF tableCount < 20 THEN
            tableNames(tableCount) = tName
            tableAliases(tableCount) = tAlias
            tableCount = tableCount + 1
        END IF
    END SUB

    PUBLIC SUB AddJoin(tName AS STRING, tAlias AS STRING, jType AS INTEGER, jCondition AS Expr, hasCondition AS INTEGER)
        IF tableCount < 20 THEN
            tableNames(tableCount) = tName
            tableAliases(tableCount) = tAlias
            joinTypes(tableCount) = jType
            tableCount = tableCount + 1
            IF hasCondition <> 0 THEN
                LET joinConditions(joinConditionCount) = jCondition
                joinConditionCount = joinConditionCount + 1
            END IF
        END IF
    END SUB

    PUBLIC FUNCTION ToString$() AS STRING
        DIM result AS STRING
        result = "SELECT "
        IF selectAll <> 0 THEN
            result = result + "* FROM " + tableName
        ELSE
            result = result + "... FROM " + tableName
        END IF
        ToString$ = result + ";"
    END FUNCTION
END CLASS

'=============================================================================
' UPDATE STATEMENT
'=============================================================================

CLASS UpdateStmt
    PUBLIC tableName AS STRING
    PUBLIC setColumns(50) AS STRING
    PUBLIC setValues(50) AS Expr
    PUBLIC setCount AS INTEGER
    PUBLIC whereClause AS Expr
    PUBLIC hasWhere AS INTEGER

    PUBLIC SUB Init()
        tableName = ""
        setCount = 0
        hasWhere = 0
    END SUB

    PUBLIC SUB AddSet(colName AS STRING, val AS Expr)
        IF setCount < 50 THEN
            setColumns(setCount) = colName
            LET setValues(setCount) = val
            setCount = setCount + 1
        END IF
    END SUB

    PUBLIC SUB SetWhere(expr AS Expr)
        LET whereClause = expr
        hasWhere = -1
    END SUB
END CLASS

'=============================================================================
' DELETE STATEMENT
'=============================================================================

CLASS DeleteStmt
    PUBLIC tableName AS STRING
    PUBLIC whereClause AS Expr
    PUBLIC hasWhere AS INTEGER

    PUBLIC SUB Init()
        tableName = ""
        hasWhere = 0
    END SUB

    PUBLIC SUB SetWhere(expr AS Expr)
        LET whereClause = expr
        hasWhere = -1
    END SUB
END CLASS
