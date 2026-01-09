' executor.bas - SQL Executor
' Part of SQLite Clone - Viper Basic Implementation

AddFile "database.bas"

'=============================================================================
' EXECUTOR FUNCTIONS
'=============================================================================

FUNCTION ExecuteCreateTable(stmt AS CreateTableStmt) AS QueryResult
    DIM result AS QueryResult
    DIM tbl AS SqlTable
    DIM col AS SqlColumn
    DIM stmtCol AS SqlColumn
    DIM i AS INTEGER
    DIM idx AS INTEGER
    DIM tName AS STRING

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Workaround: Copy class member to local var (Viper Basic compiler bug)
    tName = stmt.tableName

    ' Check if table exists
    idx = gDatabase.FindTable(tName)
    IF idx >= 0 THEN
        result.success = 0
        result.message = "Table '" + tName + "' already exists"
        ExecuteCreateTable = result
        EXIT FUNCTION
    END IF

    ' Create new table
    LET tbl = NEW SqlTable()
    tbl.Init(tName)

    ' Add columns from parsed statement
    FOR i = 0 TO stmt.columnCount - 1
        LET stmtCol = stmt.columns(i)
        LET col = NEW SqlColumn()
        col.Init(stmtCol.name, stmtCol.typeCode)
        col.primaryKey = stmtCol.primaryKey
        col.notNull = stmtCol.notNull
        col.isUnique = stmtCol.isUnique
        col.autoIncrement = stmtCol.autoIncrement
        col.hasDefault = stmtCol.hasDefault
        LET col.defaultValue = stmtCol.defaultValue
        col.isForeignKey = stmtCol.isForeignKey
        col.refTableName = stmtCol.refTableName
        col.refColumnName = stmtCol.refColumnName
        tbl.AddColumn(col)
    NEXT i

    gDatabase.AddTable(tbl)
    result.message = "Table '" + stmt.tableName + "' created"
    ExecuteCreateTable = result
END FUNCTION

' Evaluate literal expression
FUNCTION EvalExprLiteral(expr AS Expr) AS SqlValue
    DIM result AS SqlValue
    IF expr.kind = EXPR_LITERAL THEN
        EvalExprLiteral = expr.literalValue
        EXIT FUNCTION
    END IF
    LET result = NEW SqlValue()
    result.InitNull()
    EvalExprLiteral = result
END FUNCTION

FUNCTION ExecuteInsert(stmt AS InsertStmt) AS QueryResult
    DIM result AS QueryResult
    DIM tbl AS SqlTable
    DIM row AS SqlRow
    DIM val AS SqlValue
    DIM r AS INTEGER
    DIM v AS INTEGER
    DIM tableIdx AS INTEGER
    DIM rowsInserted AS INTEGER
    DIM maxV AS INTEGER
    DIM expr AS Expr
    DIM tName AS STRING

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Workaround: Copy class member to local var
    tName = stmt.tableName

    ' Find table
    tableIdx = gDatabase.FindTable(tName)
    IF tableIdx < 0 THEN
        result.success = 0
        result.message = "Table '" + tName + "' does not exist"
        ExecuteInsert = result
        EXIT FUNCTION
    END IF

    LET tbl = gDatabase.tables(tableIdx)
    rowsInserted = 0

    DIM constraintError AS STRING
    DIM constraintFailed AS INTEGER
    DIM col AS SqlColumn
    DIM existingRow AS SqlRow
    DIM existingVal AS SqlValue
    DIM newVal AS SqlValue
    DIM rowIndex AS INTEGER

    ' Insert each row
    FOR r = 0 TO stmt.rowCount - 1
        LET row = NEW SqlRow()
        row.Init(tbl.columnCount)

        ' Get values using rowValueCounts
        maxV = stmt.rowValueCounts(r)
        IF maxV > tbl.columnCount THEN
            maxV = tbl.columnCount
        END IF

        FOR v = 0 TO maxV - 1
            LET expr = stmt.GetValue(r, v)
            LET val = EvalExprLiteral(expr)
            ' If column names were specified, map to correct position
            IF stmt.columnCount > 0 THEN
                DIM colName AS STRING
                DIM colIdx AS INTEGER
                colName = stmt.columnNames(v)
                colIdx = tbl.FindColumnIndex(colName)
                IF colIdx >= 0 THEN
                    row.SetValue(colIdx, val)
                END IF
            ELSE
                row.SetValue(v, val)
            END IF
        NEXT v

        ' Apply AUTOINCREMENT for columns that are autoincrement and value is NULL
        FOR v = 0 TO tbl.columnCount - 1
            LET col = tbl.columns(v)
            LET val = row.GetValue(v)
            IF col.autoIncrement <> 0 AND val.kind = SQL_NULL THEN
                DIM autoVal AS SqlValue
                DIM autoIntVal AS INTEGER
                ' Workaround: Store result in variable first to avoid double evaluation bug
                autoIntVal = tbl.NextAutoIncrement()
                LET autoVal = NEW SqlValue()
                autoVal.InitInteger(autoIntVal)
                row.SetValue(v, autoVal)
            END IF
        NEXT v

        ' Apply default values for columns that have defaults and value is NULL
        FOR v = 0 TO tbl.columnCount - 1
            LET col = tbl.columns(v)
            LET val = row.GetValue(v)
            IF val.kind = SQL_NULL AND col.hasDefault <> 0 THEN
                row.SetValue(v, col.defaultValue)
            END IF
        NEXT v

        ' Check constraints before inserting
        constraintFailed = 0
        constraintError = ""

        FOR v = 0 TO tbl.columnCount - 1
            LET col = tbl.columns(v)
            LET newVal = row.GetValue(v)

            ' Check NOT NULL constraint
            IF col.notNull <> 0 AND newVal.kind = SQL_NULL THEN
                constraintFailed = -1
                constraintError = "NOT NULL constraint failed: column '" + col.name + "' cannot be NULL"
                EXIT FOR
            END IF

            ' Check PRIMARY KEY constraint (unique and not null)
            IF col.primaryKey <> 0 THEN
                IF newVal.kind = SQL_NULL THEN
                    constraintFailed = -1
                    constraintError = "PRIMARY KEY constraint failed: column '" + col.name + "' cannot be NULL"
                    EXIT FOR
                END IF
                ' Check for duplicate
                FOR rowIndex = 0 TO tbl.rowCount - 1
                    LET existingRow = tbl.rows(rowIndex)
                    LET existingVal = existingRow.GetValue(v)
                    IF newVal.Compare(existingVal) = 0 THEN
                        constraintFailed = -1
                        constraintError = "PRIMARY KEY constraint failed: duplicate value in column '" + col.name + "'"
                        EXIT FOR
                    END IF
                NEXT rowIndex
                IF constraintFailed <> 0 THEN
                    EXIT FOR
                END IF
            END IF

            ' Check UNIQUE constraint
            IF col.isUnique <> 0 AND newVal.kind <> SQL_NULL THEN
                FOR rowIndex = 0 TO tbl.rowCount - 1
                    LET existingRow = tbl.rows(rowIndex)
                    LET existingVal = existingRow.GetValue(v)
                    IF newVal.Compare(existingVal) = 0 THEN
                        constraintFailed = -1
                        constraintError = "UNIQUE constraint failed: duplicate value in column '" + col.name + "'"
                        EXIT FOR
                    END IF
                NEXT rowIndex
                IF constraintFailed <> 0 THEN
                    EXIT FOR
                END IF
            END IF

            ' Check FOREIGN KEY constraint
            IF col.isForeignKey <> 0 AND newVal.kind <> SQL_NULL THEN
                DIM refTableIdx AS INTEGER
                DIM refTbl AS SqlTable
                DIM refColIdx AS INTEGER
                DIM foundRef AS INTEGER

                refTableIdx = gDatabase.FindTable(col.refTableName)
                IF refTableIdx < 0 THEN
                    constraintFailed = -1
                    constraintError = "FOREIGN KEY constraint failed: referenced table '" + col.refTableName + "' does not exist"
                ELSE
                    LET refTbl = gDatabase.tables(refTableIdx)
                    refColIdx = refTbl.FindColumnIndex(col.refColumnName)
                    IF refColIdx < 0 THEN
                        constraintFailed = -1
                        constraintError = "FOREIGN KEY constraint failed: referenced column '" + col.refColumnName + "' does not exist"
                    ELSE
                        foundRef = 0
                        FOR rowIndex = 0 TO refTbl.rowCount - 1
                            LET existingRow = refTbl.rows(rowIndex)
                            LET existingVal = existingRow.GetValue(refColIdx)
                            IF newVal.Compare(existingVal) = 0 THEN
                                foundRef = -1
                                EXIT FOR
                            END IF
                        NEXT rowIndex
                        IF foundRef = 0 THEN
                            constraintFailed = -1
                            constraintError = "FOREIGN KEY constraint failed: no matching value in " + col.refTableName + "(" + col.refColumnName + ")"
                        END IF
                    END IF
                END IF
                IF constraintFailed <> 0 THEN
                    EXIT FOR
                END IF
            END IF
        NEXT v

        IF constraintFailed <> 0 THEN
            result.success = 0
            result.message = constraintError
            ExecuteInsert = result
            EXIT FUNCTION
        END IF

        tbl.AddRow(row)
        rowsInserted = rowsInserted + 1
    NEXT r

    ' Update the table in database
    LET gDatabase.tables(tableIdx) = tbl

    result.message = "Inserted " + STR$(rowsInserted) + " row(s)"
    result.rowsAffected = rowsInserted
    ExecuteInsert = result
END FUNCTION

' Evaluate column reference (supports correlated subqueries via outer context)
FUNCTION EvalExprColumn(expr AS Expr, row AS SqlRow, tbl AS SqlTable) AS SqlValue
    DIM result AS SqlValue
    DIM colIdx AS INTEGER
    DIM cName AS STRING
    DIM tName AS STRING
    DIM outerColIdx AS INTEGER

    ' Workaround: Copy class member to local var
    cName = expr.columnName
    tName = expr.tableName

    ' Check if there's a table qualifier that specifically references outer table alias
    IF tName <> "" AND gOuterTableAlias <> "" THEN
        IF tName = gOuterTableAlias THEN
            ' This column specifically references the outer table
            IF gHasOuterContext <> 0 THEN
                outerColIdx = gOuterTable.FindColumnIndex(cName)
                IF outerColIdx >= 0 THEN
                    EvalExprColumn = gOuterRow.GetValue(outerColIdx)
                    EXIT FUNCTION
                END IF
            END IF
            LET result = NEW SqlValue()
            result.InitNull()
            EvalExprColumn = result
            EXIT FUNCTION
        END IF
    END IF

    ' Try to find column in current table
    colIdx = tbl.FindColumnIndex(cName)
    IF colIdx < 0 THEN
        ' Column not found in current table - check outer context (correlated subquery)
        ' Only do this for unqualified columns (no table alias specified)
        IF tName = "" AND gHasOuterContext <> 0 THEN
            outerColIdx = gOuterTable.FindColumnIndex(cName)
            IF outerColIdx >= 0 THEN
                EvalExprColumn = gOuterRow.GetValue(outerColIdx)
                EXIT FUNCTION
            END IF
        END IF
        LET result = NEW SqlValue()
        result.InitNull()
        EvalExprColumn = result
        EXIT FUNCTION
    END IF
    EvalExprColumn = row.GetValue(colIdx)
END FUNCTION

' Execute a derived table subquery (FROM subquery)
FUNCTION ExecuteDerivedTable(sql AS STRING) AS QueryResult
    DIM result AS QueryResult
    DIM savedSource AS STRING
    DIM savedPos AS INTEGER
    DIM savedLine AS INTEGER
    DIM savedCol AS INTEGER
    DIM savedLen AS INTEGER
    DIM savedTok AS Token
    DIM savedHasError AS INTEGER
    DIM savedError AS STRING
    DIM stmt AS SelectStmt

    ' Save lexer and parser state
    savedSource = gLexSource
    savedPos = gLexPos
    savedLine = gLexLine
    savedCol = gLexCol
    savedLen = gLexLen
    LET savedTok = gTok
    savedHasError = gParserHasError
    savedError = gParserError

    ' Parse and execute the subquery
    ParserInit(sql)
    ' ParseSelectStmt expects SELECT to already be consumed, so consume it
    IF gTok.kind = TK_SELECT THEN
        ParserAdvance()
    END IF
    LET stmt = ParseSelectStmt()

    LET result = NEW QueryResult()
    result.Init()

    IF gParserHasError <> 0 THEN
        result.success = 0
        result.message = "Parse error in derived table: " + gParserError
        ' Restore lexer and parser state
        gLexSource = savedSource
        gLexPos = savedPos
        gLexLine = savedLine
        gLexCol = savedCol
        gLexLen = savedLen
        LET gTok = savedTok
        gParserHasError = savedHasError
        gParserError = savedError
        ExecuteDerivedTable = result
        EXIT FUNCTION
    END IF

    LET result = ExecuteSelect(stmt)

    ' Restore lexer and parser state
    gLexSource = savedSource
    gLexPos = savedPos
    gLexLine = savedLine
    gLexCol = savedCol
    gLexLen = savedLen
    LET gTok = savedTok
    gParserHasError = savedHasError
    gParserError = savedError

    ExecuteDerivedTable = result
END FUNCTION

' Evaluate a scalar subquery - parses and executes, returns first value
FUNCTION EvalSubquery(sql AS STRING) AS SqlValue
    DIM result AS SqlValue
    DIM savedSource AS STRING
    DIM savedPos AS INTEGER
    DIM savedLine AS INTEGER
    DIM savedCol AS INTEGER
    DIM savedLen AS INTEGER
    DIM savedTok AS Token
    DIM savedHasError AS INTEGER
    DIM savedError AS STRING
    DIM stmt AS SelectStmt
    DIM queryResult AS QueryResult
    DIM firstRow AS SqlRow

    ' Save lexer and parser state
    savedSource = gLexSource
    savedPos = gLexPos
    savedLine = gLexLine
    savedCol = gLexCol
    savedLen = gLexLen
    LET savedTok = gTok
    savedHasError = gParserHasError
    savedError = gParserError

    ' Parse and execute the subquery
    ParserInit(sql)
    ' ParseSelectStmt expects SELECT to already be consumed, so consume it
    IF gTok.kind = TK_SELECT THEN
        ParserAdvance()
    END IF
    LET stmt = ParseSelectStmt()

    IF gParserHasError <> 0 THEN
        ' Restore lexer and parser state
        gLexSource = savedSource
        gLexPos = savedPos
        gLexLine = savedLine
        gLexCol = savedCol
        gLexLen = savedLen
        LET gTok = savedTok
        gParserHasError = savedHasError
        gParserError = savedError
        LET result = NEW SqlValue()
        result.InitNull()
        EvalSubquery = result
        EXIT FUNCTION
    END IF

    LET queryResult = ExecuteSelect(stmt)

    ' Restore lexer and parser state
    gLexSource = savedSource
    gLexPos = savedPos
    gLexLine = savedLine
    gLexCol = savedCol
    gLexLen = savedLen
    LET gTok = savedTok
    gParserHasError = savedHasError
    gParserError = savedError

    ' Extract scalar value: first row, first column
    IF queryResult.rowCount > 0 THEN
        LET firstRow = queryResult.rows(0)
        IF firstRow.columnCount > 0 THEN
            EvalSubquery = firstRow.GetValue(0)
            EXIT FUNCTION
        END IF
    END IF

    LET result = NEW SqlValue()
    result.InitNull()
    EvalSubquery = result
END FUNCTION

' Evaluate a correlated subquery - sets up outer context before executing
FUNCTION EvalSubqueryCorrelated(sql AS STRING, currentRow AS SqlRow, currentTable AS SqlTable) AS SqlValue
    DIM result AS SqlValue
    DIM savedSource AS STRING
    DIM savedPos AS INTEGER
    DIM savedLine AS INTEGER
    DIM savedCol AS INTEGER
    DIM savedLen AS INTEGER
    DIM savedTok AS Token
    DIM savedHasError AS INTEGER
    DIM savedError AS STRING
    DIM stmt AS SelectStmt
    DIM queryResult AS QueryResult
    DIM firstRow AS SqlRow
    DIM savedOuterRow AS SqlRow
    DIM savedOuterTable AS SqlTable
    DIM savedHasOuter AS INTEGER
    DIM savedOuterAlias AS STRING

    ' Save lexer and parser state
    savedSource = gLexSource
    savedPos = gLexPos
    savedLine = gLexLine
    savedCol = gLexCol
    savedLen = gLexLen
    LET savedTok = gTok
    savedHasError = gParserHasError
    savedError = gParserError

    ' Save and set outer context for correlated subqueries
    LET savedOuterRow = gOuterRow
    LET savedOuterTable = gOuterTable
    savedHasOuter = gHasOuterContext
    savedOuterAlias = gOuterTableAlias
    LET gOuterRow = currentRow
    LET gOuterTable = currentTable
    gHasOuterContext = -1
    gOuterTableAlias = gCurrentTableAlias

    ' Parse and execute the subquery
    ParserInit(sql)
    ' ParseSelectStmt expects SELECT to already be consumed, so consume it
    IF gTok.kind = TK_SELECT THEN
        ParserAdvance()
    END IF
    LET stmt = ParseSelectStmt()

    IF gParserHasError <> 0 THEN
        ' Restore states
        gLexSource = savedSource
        gLexPos = savedPos
        gLexLine = savedLine
        gLexCol = savedCol
        gLexLen = savedLen
        LET gTok = savedTok
        gParserHasError = savedHasError
        gParserError = savedError
        LET gOuterRow = savedOuterRow
        LET gOuterTable = savedOuterTable
        gHasOuterContext = savedHasOuter
        gOuterTableAlias = savedOuterAlias
        LET result = NEW SqlValue()
        result.InitNull()
        EvalSubqueryCorrelated = result
        EXIT FUNCTION
    END IF

    LET queryResult = ExecuteSelect(stmt)

    ' Restore lexer and parser state
    gLexSource = savedSource
    gLexPos = savedPos
    gLexLine = savedLine
    gLexCol = savedCol
    gLexLen = savedLen
    LET gTok = savedTok
    gParserHasError = savedHasError
    gParserError = savedError

    ' Restore outer context
    LET gOuterRow = savedOuterRow
    LET gOuterTable = savedOuterTable
    gHasOuterContext = savedHasOuter
    gOuterTableAlias = savedOuterAlias

    ' Extract scalar value: first row, first column
    IF queryResult.rowCount > 0 THEN
        LET firstRow = queryResult.rows(0)
        IF firstRow.columnCount > 0 THEN
            EvalSubqueryCorrelated = firstRow.GetValue(0)
            EXIT FUNCTION
        END IF
    END IF

    LET result = NEW SqlValue()
    result.InitNull()
    EvalSubqueryCorrelated = result
END FUNCTION

' Evaluate binary comparison for WHERE clauses
' Evaluate IN subquery - checks if left value is in subquery results (supports correlated)
FUNCTION EvalInSubquery(leftExpr AS Expr, rightExpr AS Expr, row AS SqlRow, tbl AS SqlTable) AS INTEGER
    DIM leftVal AS SqlValue
    DIM savedSource AS STRING
    DIM savedPos AS INTEGER
    DIM savedLine AS INTEGER
    DIM savedCol AS INTEGER
    DIM savedLen AS INTEGER
    DIM savedTok AS Token
    DIM savedHasError AS INTEGER
    DIM savedError AS STRING
    DIM stmt AS SelectStmt
    DIM queryResult AS QueryResult
    DIM resultRow AS SqlRow
    DIM resultVal AS SqlValue
    DIM r AS INTEGER
    DIM savedOuterRow AS SqlRow
    DIM savedOuterTable AS SqlTable
    DIM savedHasOuter AS INTEGER
    DIM savedOuterAlias AS STRING

    EvalInSubquery = 0

    ' Get the left value to search for
    IF leftExpr.kind = EXPR_COLUMN THEN
        LET leftVal = EvalExprColumn(leftExpr, row, tbl)
    ELSEIF leftExpr.kind = EXPR_LITERAL THEN
        LET leftVal = EvalExprLiteral(leftExpr)
    ELSE
        EXIT FUNCTION
    END IF

    ' Right side must be a subquery
    IF rightExpr.kind <> EXPR_SUBQUERY THEN
        EXIT FUNCTION
    END IF

    ' Save lexer and parser state
    savedSource = gLexSource
    savedPos = gLexPos
    savedLine = gLexLine
    savedCol = gLexCol
    savedLen = gLexLen
    LET savedTok = gTok
    savedHasError = gParserHasError
    savedError = gParserError

    ' Save and set outer context for correlated subqueries
    LET savedOuterRow = gOuterRow
    LET savedOuterTable = gOuterTable
    savedHasOuter = gHasOuterContext
    savedOuterAlias = gOuterTableAlias
    LET gOuterRow = row
    LET gOuterTable = tbl
    gHasOuterContext = -1
    gOuterTableAlias = gCurrentTableAlias

    ' Parse and execute the subquery
    ParserInit(rightExpr.subquerySQL)
    ' ParseSelectStmt expects SELECT to already be consumed, so consume it
    IF gTok.kind = TK_SELECT THEN
        ParserAdvance()
    END IF
    LET stmt = ParseSelectStmt()

    IF gParserHasError <> 0 THEN
        gLexSource = savedSource
        gLexPos = savedPos
        gLexLine = savedLine
        gLexCol = savedCol
        gLexLen = savedLen
        LET gTok = savedTok
        gParserHasError = savedHasError
        gParserError = savedError
        LET gOuterRow = savedOuterRow
        LET gOuterTable = savedOuterTable
        gHasOuterContext = savedHasOuter
        gOuterTableAlias = savedOuterAlias
        EXIT FUNCTION
    END IF

    LET queryResult = ExecuteSelect(stmt)

    ' Restore lexer and parser state
    gLexSource = savedSource
    gLexPos = savedPos
    gLexLine = savedLine
    gLexCol = savedCol
    gLexLen = savedLen
    LET gTok = savedTok
    gParserHasError = savedHasError
    gParserError = savedError

    ' Restore outer context
    LET gOuterRow = savedOuterRow
    LET gOuterTable = savedOuterTable
    gHasOuterContext = savedHasOuter
    gOuterTableAlias = savedOuterAlias

    ' Check if leftVal exists in any row of the result (first column)
    FOR r = 0 TO queryResult.rowCount - 1
        LET resultRow = queryResult.rows(r)
        IF resultRow.columnCount > 0 THEN
            LET resultVal = resultRow.GetValue(0)
            IF leftVal.Compare(resultVal) = 0 THEN
                EvalInSubquery = -1
                EXIT FUNCTION
            END IF
        END IF
    NEXT r
END FUNCTION

FUNCTION EvalBinaryExpr(expr AS Expr, row AS SqlRow, tbl AS SqlTable) AS INTEGER
    DIM leftExpr AS Expr
    DIM rightExpr AS Expr
    DIM leftVal AS SqlValue
    DIM rightVal AS SqlValue
    DIM cmp AS INTEGER

    EvalBinaryExpr = 0

    LET leftExpr = expr.GetLeft()
    LET rightExpr = expr.GetRight()

    ' OP_IN special handling - right side is subquery
    IF expr.op = OP_IN THEN
        EvalBinaryExpr = EvalInSubquery(leftExpr, rightExpr, row, tbl)
        EXIT FUNCTION
    END IF

    ' Get left value
    IF leftExpr.kind = EXPR_COLUMN THEN
        LET leftVal = EvalExprColumn(leftExpr, row, tbl)
    ELSEIF leftExpr.kind = EXPR_LITERAL THEN
        LET leftVal = EvalExprLiteral(leftExpr)
    ELSEIF leftExpr.kind = EXPR_SUBQUERY THEN
        LET leftVal = EvalSubqueryCorrelated(leftExpr.subquerySQL, row, tbl)
    END IF

    ' Get right value
    IF rightExpr.kind = EXPR_COLUMN THEN
        LET rightVal = EvalExprColumn(rightExpr, row, tbl)
    ELSEIF rightExpr.kind = EXPR_LITERAL THEN
        LET rightVal = EvalExprLiteral(rightExpr)
    ELSEIF rightExpr.kind = EXPR_SUBQUERY THEN
        LET rightVal = EvalSubqueryCorrelated(rightExpr.subquerySQL, row, tbl)
    END IF

    cmp = leftVal.Compare(rightVal)

    ' Handle different comparison operators
    IF expr.op = OP_EQ THEN
        IF cmp = 0 THEN
            EvalBinaryExpr = -1
        END IF
    ELSEIF expr.op = OP_NE THEN
        IF cmp <> 0 THEN
            EvalBinaryExpr = -1
        END IF
    ELSEIF expr.op = OP_LT THEN
        IF cmp < 0 THEN
            EvalBinaryExpr = -1
        END IF
    ELSEIF expr.op = OP_LE THEN
        IF cmp <= 0 THEN
            EvalBinaryExpr = -1
        END IF
    ELSEIF expr.op = OP_GT THEN
        IF cmp > 0 THEN
            EvalBinaryExpr = -1
        END IF
    ELSEIF expr.op = OP_GE THEN
        IF cmp >= 0 THEN
            EvalBinaryExpr = -1
        END IF
    END IF
END FUNCTION

' Check if a row already exists in the result (for DISTINCT)
FUNCTION IsDuplicateRow(newRow AS SqlRow, result AS QueryResult) AS INTEGER
    DIM i AS INTEGER
    DIM existingRow AS SqlRow
    DIM c AS INTEGER
    DIM allMatch AS INTEGER
    DIM val1 AS SqlValue
    DIM val2 AS SqlValue

    FOR i = 0 TO result.rowCount - 1
        LET existingRow = result.rows(i)
        IF existingRow.columnCount = newRow.columnCount THEN
            allMatch = -1
            FOR c = 0 TO newRow.columnCount - 1
                LET val1 = existingRow.GetValue(c)
                LET val2 = newRow.GetValue(c)
                IF val1.Compare(val2) <> 0 THEN
                    allMatch = 0
                    EXIT FOR
                END IF
            NEXT c
            IF allMatch <> 0 THEN
                IsDuplicateRow = -1
                EXIT FUNCTION
            END IF
        END IF
    NEXT i

    IsDuplicateRow = 0
END FUNCTION

' Check if expression is an aggregate function
FUNCTION IsAggregateExpr(expr AS Expr) AS INTEGER
    DIM funcName AS STRING
    IF expr.kind = EXPR_FUNCTION THEN
        funcName = expr.funcName
        IF funcName = "COUNT" OR funcName = "count" THEN
            IsAggregateExpr = -1
            EXIT FUNCTION
        END IF
        IF funcName = "SUM" OR funcName = "sum" THEN
            IsAggregateExpr = -1
            EXIT FUNCTION
        END IF
        IF funcName = "AVG" OR funcName = "avg" THEN
            IsAggregateExpr = -1
            EXIT FUNCTION
        END IF
        IF funcName = "MIN" OR funcName = "min" THEN
            IsAggregateExpr = -1
            EXIT FUNCTION
        END IF
        IF funcName = "MAX" OR funcName = "max" THEN
            IsAggregateExpr = -1
            EXIT FUNCTION
        END IF
    END IF
    IsAggregateExpr = 0
END FUNCTION

'=============================================================================
' INDEX LOOKUP HELPERS
'=============================================================================

' Check if a WHERE clause is a simple equality that can use an index
' Returns the column name if usable, empty string otherwise
FUNCTION GetIndexableColumn(expr AS Expr) AS STRING
    DIM left AS Expr
    DIM right AS Expr

    GetIndexableColumn = ""

    IF expr.kind <> EXPR_BINARY THEN
        EXIT FUNCTION
    END IF

    ' Only equality comparisons can use index for direct lookup
    IF expr.op <> OP_EQ THEN
        EXIT FUNCTION
    END IF

    LET left = expr.GetLeft()
    LET right = expr.GetRight()

    ' Pattern: column = literal
    IF left.kind = EXPR_COLUMN AND right.kind = EXPR_LITERAL THEN
        GetIndexableColumn = left.columnName
        EXIT FUNCTION
    END IF

    ' Pattern: literal = column
    IF left.kind = EXPR_LITERAL AND right.kind = EXPR_COLUMN THEN
        GetIndexableColumn = right.columnName
        EXIT FUNCTION
    END IF
END FUNCTION

' Get the literal value from an equality WHERE clause
FUNCTION GetIndexLookupValue(expr AS Expr) AS SqlValue
    DIM left AS Expr
    DIM right AS Expr
    DIM result AS SqlValue

    LET result = NEW SqlValue()
    result.InitNull()

    IF expr.kind <> EXPR_BINARY OR expr.op <> OP_EQ THEN
        GetIndexLookupValue = result
        EXIT FUNCTION
    END IF

    LET left = expr.GetLeft()
    LET right = expr.GetRight()

    ' Pattern: column = literal
    IF left.kind = EXPR_COLUMN AND right.kind = EXPR_LITERAL THEN
        GetIndexLookupValue = right.literalValue
        EXIT FUNCTION
    END IF

    ' Pattern: literal = column
    IF left.kind = EXPR_LITERAL AND right.kind = EXPR_COLUMN THEN
        GetIndexLookupValue = left.literalValue
        EXIT FUNCTION
    END IF

    GetIndexLookupValue = result
END FUNCTION

' Try to use an index for a WHERE clause lookup
' Returns number of matching row indices, or -1 if no index available
FUNCTION TryIndexLookup(whereExpr AS Expr, tblName AS STRING, matchingRows() AS INTEGER, maxRows AS INTEGER) AS INTEGER
    DIM colName AS STRING
    DIM lookupVal AS SqlValue
    DIM idxPos AS INTEGER
    DIM idx AS SqlIndex
    DIM count AS INTEGER

    TryIndexLookup = -1  ' No index available

    ' Check if WHERE can use an index
    colName = GetIndexableColumn(whereExpr)
    IF colName = "" THEN
        EXIT FUNCTION
    END IF

    ' Check if index exists for this column
    idxPos = gIndexManager.FindIndexForColumn(tblName, colName)
    IF idxPos < 0 THEN
        EXIT FUNCTION
    END IF

    LET idx = gIndexManager.indexes(idxPos)
    LET lookupVal = GetIndexLookupValue(whereExpr)

    ' Perform index lookup
    DIM tempTbl AS SqlTable
    LET tempTbl = NEW SqlTable()  ' Dummy table for lookup
    count = idx.LookupSingle(lookupVal, tempTbl, matchingRows, maxRows)

    TryIndexLookup = count
END FUNCTION

' Check if SELECT has any aggregate functions
FUNCTION HasAggregates(stmt AS SelectStmt) AS INTEGER
    DIM c AS INTEGER
    DIM colExpr AS Expr
    FOR c = 0 TO stmt.columnCount - 1
        LET colExpr = stmt.columns(c)
        IF IsAggregateExpr(colExpr) <> 0 THEN
            HasAggregates = -1
            EXIT FUNCTION
        END IF
    NEXT c
    HasAggregates = 0
END FUNCTION

' Evaluate an aggregate function over matching rows
FUNCTION EvalAggregate(expr AS Expr, matchingRows() AS INTEGER, matchCount AS INTEGER, tbl AS SqlTable) AS SqlValue
    DIM funcName AS STRING
    DIM hasArg AS INTEGER
    DIM arg0 AS Expr
    DIM argExpr AS Expr
    DIM count AS INTEGER
    DIM sumInt AS INTEGER
    DIM i AS INTEGER
    DIM rowIdx AS INTEGER
    DIM row AS SqlRow
    DIM val AS SqlValue
    DIM result AS SqlValue
    DIM hasMin AS INTEGER
    DIM hasMax AS INTEGER
    DIM minInt AS INTEGER
    DIM maxInt AS INTEGER

    LET result = NEW SqlValue()
    result.InitNull()

    funcName = expr.funcName
    hasArg = 0
    IF expr.argCount > 0 THEN
        hasArg = -1
    END IF

    ' COUNT(*)
    IF (funcName = "COUNT" OR funcName = "count") AND hasArg <> 0 THEN
        LET arg0 = expr.args(0)
        IF arg0.kind = EXPR_STAR THEN
            result.InitInteger(matchCount)
            EvalAggregate = result
            EXIT FUNCTION
        END IF
    END IF

    ' COUNT(column) - count non-NULL values
    IF funcName = "COUNT" OR funcName = "count" THEN
        count = 0
        FOR i = 0 TO matchCount - 1
            rowIdx = matchingRows(i)
            LET row = tbl.rows(rowIdx)
            IF hasArg <> 0 THEN
                LET argExpr = expr.args(0)
                IF argExpr.kind = EXPR_COLUMN THEN
                    LET val = EvalExprColumn(argExpr, row, tbl)
                    IF val.kind <> SQL_NULL THEN
                        count = count + 1
                    END IF
                END IF
            END IF
        NEXT i
        result.InitInteger(count)
        EvalAggregate = result
        EXIT FUNCTION
    END IF

    ' SUM(column)
    IF funcName = "SUM" OR funcName = "sum" THEN
        sumInt = 0
        FOR i = 0 TO matchCount - 1
            rowIdx = matchingRows(i)
            LET row = tbl.rows(rowIdx)
            IF hasArg <> 0 THEN
                LET argExpr = expr.args(0)
                IF argExpr.kind = EXPR_COLUMN THEN
                    LET val = EvalExprColumn(argExpr, row, tbl)
                    IF val.kind = SQL_INTEGER THEN
                        sumInt = sumInt + val.intValue
                    ELSEIF val.kind = SQL_TEXT THEN
                        sumInt = sumInt + VAL(val.textValue)
                    END IF
                END IF
            END IF
        NEXT i
        result.InitInteger(sumInt)
        EvalAggregate = result
        EXIT FUNCTION
    END IF

    ' AVG(column)
    IF funcName = "AVG" OR funcName = "avg" THEN
        sumInt = 0
        count = 0
        FOR i = 0 TO matchCount - 1
            rowIdx = matchingRows(i)
            LET row = tbl.rows(rowIdx)
            IF hasArg <> 0 THEN
                LET argExpr = expr.args(0)
                IF argExpr.kind = EXPR_COLUMN THEN
                    LET val = EvalExprColumn(argExpr, row, tbl)
                    IF val.kind = SQL_INTEGER THEN
                        sumInt = sumInt + val.intValue
                        count = count + 1
                    ELSEIF val.kind = SQL_TEXT THEN
                        sumInt = sumInt + VAL(val.textValue)
                        count = count + 1
                    END IF
                END IF
            END IF
        NEXT i
        IF count > 0 THEN
            result.InitInteger(sumInt / count)
        END IF
        EvalAggregate = result
        EXIT FUNCTION
    END IF

    ' MIN(column)
    IF funcName = "MIN" OR funcName = "min" THEN
        hasMin = 0
        minInt = 0
        FOR i = 0 TO matchCount - 1
            rowIdx = matchingRows(i)
            LET row = tbl.rows(rowIdx)
            IF hasArg <> 0 THEN
                LET argExpr = expr.args(0)
                IF argExpr.kind = EXPR_COLUMN THEN
                    LET val = EvalExprColumn(argExpr, row, tbl)
                    DIM intVal AS INTEGER
                    intVal = 0
                    IF val.kind = SQL_INTEGER THEN
                        intVal = val.intValue
                    ELSEIF val.kind = SQL_TEXT THEN
                        intVal = VAL(val.textValue)
                    END IF
                    IF val.kind <> SQL_NULL THEN
                        IF hasMin = 0 OR intVal < minInt THEN
                            minInt = intVal
                            hasMin = -1
                        END IF
                    END IF
                END IF
            END IF
        NEXT i
        IF hasMin <> 0 THEN
            result.InitInteger(minInt)
        END IF
        EvalAggregate = result
        EXIT FUNCTION
    END IF

    ' MAX(column)
    IF funcName = "MAX" OR funcName = "max" THEN
        hasMax = 0
        maxInt = 0
        FOR i = 0 TO matchCount - 1
            rowIdx = matchingRows(i)
            LET row = tbl.rows(rowIdx)
            IF hasArg <> 0 THEN
                LET argExpr = expr.args(0)
                IF argExpr.kind = EXPR_COLUMN THEN
                    LET val = EvalExprColumn(argExpr, row, tbl)
                    DIM intVal2 AS INTEGER
                    intVal2 = 0
                    IF val.kind = SQL_INTEGER THEN
                        intVal2 = val.intValue
                    ELSEIF val.kind = SQL_TEXT THEN
                        intVal2 = VAL(val.textValue)
                    END IF
                    IF val.kind <> SQL_NULL THEN
                        IF hasMax = 0 OR intVal2 > maxInt THEN
                            maxInt = intVal2
                            hasMax = -1
                        END IF
                    END IF
                END IF
            END IF
        NEXT i
        IF hasMax <> 0 THEN
            result.InitInteger(maxInt)
        END IF
        EvalAggregate = result
        EXIT FUNCTION
    END IF

    EvalAggregate = result
END FUNCTION

' Evaluate HAVING expression for a group (returns -1 = true, 0 = false)
FUNCTION EvalHavingExpr(expr AS Expr, groupRowArray() AS INTEGER, groupRowCount AS INTEGER, tbl AS SqlTable) AS INTEGER
    DIM left AS Expr
    DIM right AS Expr
    DIM op AS INTEGER
    DIM leftBool AS INTEGER
    DIM rightBool AS INTEGER
    DIM leftVal AS SqlValue
    DIM rightVal AS SqlValue
    DIM cmp AS INTEGER

    ' Handle binary expressions
    IF expr.kind = EXPR_BINARY THEN
        LET left = expr.GetLeft()
        LET right = expr.GetRight()
        op = expr.op

        ' Handle logical operators (AND=20, OR=21)
        IF op = OP_AND THEN
            leftBool = EvalHavingExpr(left, groupRowArray, groupRowCount, tbl)
            rightBool = EvalHavingExpr(right, groupRowArray, groupRowCount, tbl)
            IF leftBool <> 0 AND rightBool <> 0 THEN
                EvalHavingExpr = -1
                EXIT FUNCTION
            END IF
            EvalHavingExpr = 0
            EXIT FUNCTION
        END IF
        IF op = OP_OR THEN
            leftBool = EvalHavingExpr(left, groupRowArray, groupRowCount, tbl)
            rightBool = EvalHavingExpr(right, groupRowArray, groupRowCount, tbl)
            IF leftBool <> 0 OR rightBool <> 0 THEN
                EvalHavingExpr = -1
                EXIT FUNCTION
            END IF
            EvalHavingExpr = 0
            EXIT FUNCTION
        END IF

        ' Evaluate left and right sides for comparison
        LET leftVal = EvalHavingValue(left, groupRowArray, groupRowCount, tbl)
        LET rightVal = EvalHavingValue(right, groupRowArray, groupRowCount, tbl)

        ' Comparison operators
        cmp = leftVal.Compare(rightVal)
        IF op = OP_EQ THEN
            IF cmp = 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
        IF op = OP_NE THEN
            IF cmp <> 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
        IF op = OP_GT THEN
            IF cmp > 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
        IF op = OP_GE THEN
            IF cmp >= 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
        IF op = OP_LT THEN
            IF cmp < 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
        IF op = OP_LE THEN
            IF cmp <= 0 THEN
                EvalHavingExpr = -1
            ELSE
                EvalHavingExpr = 0
            END IF
            EXIT FUNCTION
        END IF
    END IF

    EvalHavingExpr = 0
END FUNCTION

' Evaluate a value in HAVING context (handles aggregates)
FUNCTION EvalHavingValue(expr AS Expr, groupRowArray() AS INTEGER, groupRowCount AS INTEGER, tbl AS SqlTable) AS SqlValue
    DIM result AS SqlValue
    DIM firstRowIdx AS INTEGER
    DIM row AS SqlRow

    LET result = NEW SqlValue()
    result.InitNull()

    ' If it's an aggregate function, evaluate it on the group
    IF expr.kind = EXPR_FUNCTION THEN
        IF IsAggregateExpr(expr) <> 0 THEN
            EvalHavingValue = EvalAggregate(expr, groupRowArray, groupRowCount, tbl)
            EXIT FUNCTION
        END IF
    END IF

    ' If it's a literal, return its value
    IF expr.kind = EXPR_LITERAL THEN
        EvalHavingValue = EvalExprLiteral(expr)
        EXIT FUNCTION
    END IF

    ' If it's a column ref, evaluate using first row in group
    IF expr.kind = EXPR_COLUMN THEN
        IF groupRowCount > 0 THEN
            firstRowIdx = groupRowArray(0)
            LET row = tbl.rows(firstRowIdx)
            EvalHavingValue = EvalExprColumn(expr, row, tbl)
            EXIT FUNCTION
        END IF
    END IF

    EvalHavingValue = result
END FUNCTION

' CROSS JOIN helper: Evaluate column reference in cross join context
FUNCTION EvalCrossJoinColumn(expr AS Expr, tables() AS SqlTable, aliases() AS STRING, tableCount AS INTEGER, combinedRow AS SqlRow, colOffsets() AS INTEGER) AS SqlValue
    DIM result AS SqlValue
    DIM colName AS STRING
    DIM tblName AS STRING
    DIM ti AS INTEGER
    DIM colIdx AS INTEGER
    DIM offset AS INTEGER

    LET result = SqlNull()
    colName = expr.columnName
    tblName = expr.tableName

    ' If table name specified, find that table
    DIM tbl AS SqlTable
    DIM tblNameLocal AS STRING
    DIM aliasLocal AS STRING
    DIM matchName AS INTEGER
    DIM matchAlias AS INTEGER
    IF tblName <> "" THEN
        FOR ti = 0 TO tableCount - 1
            LET tbl = tables(ti)
            LET tblNameLocal = tbl.name
            ' Check table name match
            matchName = 0
            matchAlias = 0
            IF tblNameLocal = tblName THEN
                matchName = -1
            END IF
            ' Check alias match - use concatenation to avoid type issue
            LET aliasLocal = "" + aliases(ti)
            IF aliasLocal = tblName THEN
                matchAlias = -1
            END IF
            IF matchName <> 0 OR matchAlias <> 0 THEN
                colIdx = tbl.FindColumnIndex(colName)
                IF colIdx >= 0 THEN
                    offset = colOffsets(ti)
                    LET result = combinedRow.GetValue(offset + colIdx)
                    EvalCrossJoinColumn = result
                    EXIT FUNCTION
                END IF
            END IF
        NEXT ti
        EvalCrossJoinColumn = result
        EXIT FUNCTION
    END IF

    ' No table qualifier - search all tables
    FOR ti = 0 TO tableCount - 1
        LET tbl = tables(ti)
        colIdx = tbl.FindColumnIndex(colName)
        IF colIdx >= 0 THEN
            offset = colOffsets(ti)
            LET result = combinedRow.GetValue(offset + colIdx)
            EvalCrossJoinColumn = result
            EXIT FUNCTION
        END IF
    NEXT ti

    EvalCrossJoinColumn = result
END FUNCTION

' CROSS JOIN helper: Evaluate expression in cross join context
FUNCTION EvalCrossJoinExpr(expr AS Expr, tables() AS SqlTable, aliases() AS STRING, tableCount AS INTEGER, combinedRow AS SqlRow, colOffsets() AS INTEGER) AS SqlValue
    DIM result AS SqlValue

    IF expr.kind = EXPR_LITERAL THEN
        EvalCrossJoinExpr = EvalExprLiteral(expr)
        EXIT FUNCTION
    END IF

    IF expr.kind = EXPR_COLUMN THEN
        EvalCrossJoinExpr = EvalCrossJoinColumn(expr, tables, aliases, tableCount, combinedRow, colOffsets)
        EXIT FUNCTION
    END IF

    LET result = SqlNull()
    EvalCrossJoinExpr = result
END FUNCTION

' CROSS JOIN helper: Evaluate binary expression in cross join context
FUNCTION EvalCrossJoinBinary(expr AS Expr, tables() AS SqlTable, aliases() AS STRING, tableCount AS INTEGER, combinedRow AS SqlRow, colOffsets() AS INTEGER) AS INTEGER
    DIM leftResult AS INTEGER
    DIM leftVal AS SqlValue
    DIM rightVal AS SqlValue
    DIM cmp AS INTEGER
    DIM leftExpr AS Expr
    DIM rightExpr AS Expr

    ' Handle AND/OR first
    IF expr.op = OP_AND THEN
        LET leftExpr = expr.args(0)
        leftResult = EvalCrossJoinBinary(leftExpr, tables, aliases, tableCount, combinedRow, colOffsets)
        IF leftResult = 0 THEN
            EvalCrossJoinBinary = 0
            EXIT FUNCTION
        END IF
        LET rightExpr = expr.args(1)
        EvalCrossJoinBinary = EvalCrossJoinBinary(rightExpr, tables, aliases, tableCount, combinedRow, colOffsets)
        EXIT FUNCTION
    END IF

    IF expr.op = OP_OR THEN
        LET leftExpr = expr.args(0)
        leftResult = EvalCrossJoinBinary(leftExpr, tables, aliases, tableCount, combinedRow, colOffsets)
        IF leftResult <> 0 THEN
            EvalCrossJoinBinary = 1
            EXIT FUNCTION
        END IF
        LET rightExpr = expr.args(1)
        EvalCrossJoinBinary = EvalCrossJoinBinary(rightExpr, tables, aliases, tableCount, combinedRow, colOffsets)
        EXIT FUNCTION
    END IF

    LET leftExpr = expr.args(0)
    LET rightExpr = expr.args(1)
    LET leftVal = EvalCrossJoinExpr(leftExpr, tables, aliases, tableCount, combinedRow, colOffsets)
    LET rightVal = EvalCrossJoinExpr(rightExpr, tables, aliases, tableCount, combinedRow, colOffsets)
    cmp = leftVal.Compare(rightVal)

    IF expr.op = OP_EQ THEN
        IF cmp = 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF
    IF expr.op = OP_NE THEN
        IF cmp <> 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF
    IF expr.op = OP_LT THEN
        IF cmp < 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF
    IF expr.op = OP_LE THEN
        IF cmp <= 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF
    IF expr.op = OP_GT THEN
        IF cmp > 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF
    IF expr.op = OP_GE THEN
        IF cmp >= 0 THEN EvalCrossJoinBinary = 1 ELSE EvalCrossJoinBinary = 0
        EXIT FUNCTION
    END IF

    EvalCrossJoinBinary = 0
END FUNCTION

' Execute CROSS JOIN query (multiple tables)
FUNCTION ExecuteCrossJoin(stmt AS SelectStmt) AS QueryResult
    DIM result AS QueryResult
    DIM tables(20) AS SqlTable
    DIM aliases(20) AS STRING
    DIM colOffsets(20) AS INTEGER
    DIM totalCols AS INTEGER
    DIM ti AS INTEGER
    DIM tableIdx AS INTEGER
    DIM c AS INTEGER
    DIM r AS INTEGER
    DIM ri AS INTEGER
    DIM ci AS INTEGER
    DIM offset AS INTEGER
    DIM tblName AS STRING
    DIM aliasName AS STRING
    DIM includeRow AS INTEGER
    DIM whereResult AS INTEGER
    DIM colExpr AS Expr
    DIM val AS SqlValue
    DIM combinedRows(10000) AS SqlRow
    DIM combinedCount AS INTEGER
    DIM newCombined(10000) AS SqlRow
    DIM newCount AS INTEGER
    DIM filteredRows(10000) AS SqlRow
    DIM filteredCount AS INTEGER
    DIM resultRow AS SqlRow
    DIM newRow AS SqlRow
    DIM existing AS SqlRow
    DIM srcRow AS SqlRow

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Load all tables
    totalCols = 0
    FOR ti = 0 TO stmt.tableCount - 1
        tblName = stmt.tableNames(ti)
        tableIdx = gDatabase.FindTable(tblName)
        IF tableIdx < 0 THEN
            result.success = 0
            result.message = "Table '" + tblName + "' does not exist"
            ExecuteCrossJoin = result
            EXIT FUNCTION
        END IF
        LET tables(ti) = gDatabase.tables(tableIdx)

        aliasName = stmt.tableAliases(ti)
        IF aliasName = "" THEN
            aliasName = tblName
        END IF
        aliases(ti) = aliasName

        colOffsets(ti) = totalCols
        totalCols = totalCols + tables(ti).columnCount
    NEXT ti

    ' Build column names for result
    DIM tblLocal AS SqlTable
    DIM aliasLocal AS STRING
    DIM colLocal AS SqlColumn
    DIM fullName AS STRING
    IF stmt.selectAll <> 0 THEN
        ' For SELECT *, include all columns from all tables with table prefix
        FOR ti = 0 TO stmt.tableCount - 1
            LET tblLocal = tables(ti)
            LET aliasLocal = "" + aliases(ti)
            FOR c = 0 TO tblLocal.columnCount - 1
                LET colLocal = tblLocal.columns(c)
                fullName = aliasLocal + "." + colLocal.name
                result.AddColumnName(fullName)
            NEXT c
        NEXT ti
    ELSE
        DIM exprTblName AS STRING
        FOR c = 0 TO stmt.columnCount - 1
            LET colExpr = stmt.columns(c)
            IF colExpr.kind = EXPR_COLUMN THEN
                exprTblName = colExpr.tableName
                IF exprTblName <> "" THEN
                    result.AddColumnName(exprTblName + "." + colExpr.columnName)
                ELSE
                    result.AddColumnName(colExpr.columnName)
                END IF
            ELSE
                result.AddColumnName("expr" + STR$(c))
            END IF
        NEXT c
    END IF

    ' Build cartesian product using iterative approach
    ' Start with all rows from first table
    combinedCount = 0
    IF stmt.tableCount > 0 THEN
        FOR r = 0 TO tables(0).rowCount - 1
            LET srcRow = tables(0).rows(r)
            IF srcRow.deleted = 0 THEN
                ' Create combined row with values from first table
                LET newRow = NEW SqlRow()
                newRow.Init(totalCols)
                FOR c = 0 TO tables(0).columnCount - 1
                    newRow.SetValue(c, srcRow.GetValue(c))
                NEXT c
                LET combinedRows(combinedCount) = newRow
                combinedCount = combinedCount + 1
            END IF
        NEXT r
    END IF

    ' Extend with each additional table
    DIM joinType AS INTEGER
    DIM joinCondIdx AS INTEGER
    DIM hasJoinCond AS INTEGER
    DIM foundMatch AS INTEGER
    DIM includeThisRow AS INTEGER
    DIM joinCond AS Expr
    DIM joinResult AS INTEGER
    DIM nullRow AS SqlRow

    DIM rightRowMatched(1000) AS INTEGER
    DIM rCheck AS INTEGER
    DIM rightRow AS SqlRow
    DIM cc AS INTEGER

    FOR ti = 1 TO stmt.tableCount - 1
        offset = colOffsets(ti)
        newCount = 0

        ' Get join type for this table (stored at index ti by AddJoin)
        joinType = stmt.joinTypes(ti)  ' 0=CROSS, 1=INNER, 2=LEFT, 3=RIGHT, 4=FULL
        joinCondIdx = ti - 1
        IF joinCondIdx < stmt.joinConditionCount THEN
            hasJoinCond = -1
        ELSE
            hasJoinCond = 0
        END IF

        ' For RIGHT JOIN or FULL JOIN: initialize tracking array
        IF joinType = 3 OR joinType = 4 THEN
            FOR r = 0 TO tables(ti).rowCount - 1
                rightRowMatched(r) = 0
            NEXT r
        END IF

        FOR ci = 0 TO combinedCount - 1
            LET existing = combinedRows(ci)
            foundMatch = 0  ' For LEFT JOIN / FULL JOIN: track if any right row matched

            FOR r = 0 TO tables(ti).rowCount - 1
                LET srcRow = tables(ti).rows(r)
                IF srcRow.deleted = 0 THEN
                    ' Clone existing and add new table's values
                    LET newRow = NEW SqlRow()
                    newRow.Init(totalCols)
                    FOR c = 0 TO offset - 1
                        newRow.SetValue(c, existing.GetValue(c))
                    NEXT c
                    FOR c = 0 TO tables(ti).columnCount - 1
                        newRow.SetValue(offset + c, srcRow.GetValue(c))
                    NEXT c

                    ' For CROSS JOIN (type 0), add all combinations
                    ' For INNER/LEFT/RIGHT/FULL JOIN, check join condition
                    includeThisRow = -1
                    IF hasJoinCond <> 0 AND (joinType = 1 OR joinType = 2 OR joinType = 3 OR joinType = 4) THEN
                        LET joinCond = stmt.joinConditions(joinCondIdx)
                        joinResult = EvalCrossJoinBinary(joinCond, tables, aliases, stmt.tableCount, newRow, colOffsets)
                        IF joinResult = 0 THEN
                            includeThisRow = 0
                        END IF
                    END IF

                    IF includeThisRow <> 0 THEN
                        LET newCombined(newCount) = newRow
                        newCount = newCount + 1
                        foundMatch = -1
                        ' For RIGHT/FULL JOIN: mark this right row as matched
                        IF joinType = 3 OR joinType = 4 THEN
                            rightRowMatched(r) = 1
                        END IF
                    END IF
                END IF
            NEXT r

            ' For LEFT JOIN or FULL JOIN: if no match found, add row with NULLs for right columns
            IF (joinType = 2 OR joinType = 4) AND foundMatch = 0 THEN
                LET nullRow = NEW SqlRow()
                nullRow.Init(totalCols)
                FOR c = 0 TO offset - 1
                    nullRow.SetValue(c, existing.GetValue(c))
                NEXT c
                ' Right columns are already NULL by default from Init
                LET newCombined(newCount) = nullRow
                newCount = newCount + 1
            END IF
        NEXT ci

        ' For RIGHT JOIN or FULL JOIN: add unmatched right rows with NULLs for left columns
        IF joinType = 3 OR joinType = 4 THEN
            FOR rCheck = 0 TO tables(ti).rowCount - 1
                IF rightRowMatched(rCheck) = 0 THEN
                    LET rightRow = tables(ti).rows(rCheck)
                    IF rightRow.deleted = 0 THEN
                        LET nullRow = NEW SqlRow()
                        nullRow.Init(totalCols)
                        ' Left columns stay NULL (default from Init)
                        ' Copy right columns
                        FOR cc = 0 TO tables(ti).columnCount - 1
                            nullRow.SetValue(offset + cc, rightRow.GetValue(cc))
                        NEXT cc
                        LET newCombined(newCount) = nullRow
                        newCount = newCount + 1
                    END IF
                END IF
            NEXT rCheck
        END IF

        ' Copy newCombined to combinedRows
        combinedCount = newCount
        FOR ci = 0 TO newCount - 1
            LET combinedRows(ci) = newCombined(ci)
        NEXT ci
    NEXT ti

    ' Apply WHERE filter (JOIN conditions already applied during join building)
    filteredCount = 0
    FOR ri = 0 TO combinedCount - 1
        includeRow = -1

        ' Apply WHERE clause
        IF stmt.hasWhere <> 0 THEN
            whereResult = EvalCrossJoinBinary(stmt.whereClause, tables, aliases, stmt.tableCount, combinedRows(ri), colOffsets)
            IF whereResult = 0 THEN
                includeRow = 0
            END IF
        END IF

        IF includeRow <> 0 THEN
            LET filteredRows(filteredCount) = combinedRows(ri)
            filteredCount = filteredCount + 1
        END IF
    NEXT ri

    ' Project columns and build result
    FOR ri = 0 TO filteredCount - 1
        LET resultRow = NEW SqlRow()
        resultRow.Init(0)

        IF stmt.selectAll <> 0 THEN
            ' Copy all values
            FOR c = 0 TO totalCols - 1
                resultRow.AddValue(filteredRows(ri).GetValue(c))
            NEXT c
        ELSE
            FOR c = 0 TO stmt.columnCount - 1
                LET colExpr = stmt.columns(c)
                LET val = EvalCrossJoinExpr(colExpr, tables, aliases, stmt.tableCount, filteredRows(ri), colOffsets)
                resultRow.AddValue(val)
            NEXT c
        END IF

        result.AddRow(resultRow)
    NEXT ri

    result.success = -1
    result.message = "Selected " + STR$(result.rowCount) + " row(s)"
    ExecuteCrossJoin = result
END FUNCTION

FUNCTION ExecuteSelect(stmt AS SelectStmt) AS QueryResult
    ' Check for multi-table (CROSS JOIN) query
    IF stmt.tableCount > 1 THEN
        ExecuteSelect = ExecuteCrossJoin(stmt)
        EXIT FUNCTION
    END IF

    DIM result AS QueryResult
    DIM tbl AS SqlTable
    DIM tableRow AS SqlRow
    DIM resultRow AS SqlRow
    DIM col AS SqlColumn
    DIM colExpr AS Expr
    DIM val AS SqlValue
    DIM tableIdx AS INTEGER
    DIM r AS INTEGER
    DIM c AS INTEGER
    DIM includeRow AS INTEGER
    DIM whereResult AS INTEGER
    DIM tName AS STRING
    DIM matchingRows(1000) AS INTEGER
    DIM matchCount AS INTEGER
    DIM i AS INTEGER
    DIM j AS INTEGER
    DIM k AS INTEGER
    DIM rowIdxI AS INTEGER
    DIM rowIdxJ AS INTEGER
    DIM rowI AS SqlRow
    DIM rowJ AS SqlRow
    DIM orderExpr AS Expr
    DIM isDesc AS INTEGER
    DIM valI AS SqlValue
    DIM valJ AS SqlValue
    DIM cmp AS INTEGER
    DIM shouldSwap AS INTEGER
    DIM tempIdx AS INTEGER
    DIM ri AS INTEGER
    DIM rowIdx AS INTEGER

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Check for derived table (subquery in FROM)
    IF stmt.hasDerivedTable <> 0 THEN
        ' Execute the subquery and return its result
        DIM derivedResult AS QueryResult
        LET derivedResult = ExecuteDerivedTable(stmt.derivedTableSQL)
        IF derivedResult.success = 0 THEN
            result.success = 0
            result.message = "Derived table error: " + derivedResult.message
            ExecuteSelect = result
            EXIT FUNCTION
        END IF
        ' For derived tables with SELECT *, just return the subquery result
        derivedResult.message = "Selected " + STR$(derivedResult.rowCount) + " row(s)"
        ExecuteSelect = derivedResult
        EXIT FUNCTION
    END IF

    ' Workaround: Copy class member to local var
    tName = stmt.tableName

    ' Find table
    tableIdx = gDatabase.FindTable(tName)
    IF tableIdx < 0 THEN
        result.success = 0
        result.message = "Table '" + tName + "' does not exist"
        ExecuteSelect = result
        EXIT FUNCTION
    END IF

    LET tbl = gDatabase.tables(tableIdx)

    ' Set current table alias for correlated subquery resolution
    DIM savedAlias AS STRING
    savedAlias = gCurrentTableAlias
    gCurrentTableAlias = stmt.tableAlias

    ' Build column names
    IF stmt.selectAll <> 0 THEN
        FOR c = 0 TO tbl.columnCount - 1
            result.AddColumnName(tbl.columns(c).name)
        NEXT c
    ELSE
        FOR c = 0 TO stmt.columnCount - 1
            LET colExpr = stmt.columns(c)
            IF colExpr.kind = EXPR_COLUMN THEN
                result.AddColumnName(colExpr.columnName)
            ELSEIF colExpr.kind = EXPR_FUNCTION THEN
                ' Build name like COUNT(*) or SUM(age)
                DIM aggArgExpr AS Expr
                IF colExpr.argCount > 0 THEN
                    LET aggArgExpr = colExpr.args(0)
                    IF aggArgExpr.kind = EXPR_STAR THEN
                        result.AddColumnName(colExpr.funcName + "(*)")
                    ELSEIF aggArgExpr.kind = EXPR_COLUMN THEN
                        result.AddColumnName(colExpr.funcName + "(" + aggArgExpr.columnName + ")")
                    ELSE
                        result.AddColumnName(colExpr.funcName + "(...)")
                    END IF
                ELSE
                    result.AddColumnName(colExpr.funcName + "()")
                END IF
            ELSE
                result.AddColumnName("expr" + STR$(c))
            END IF
        NEXT c
    END IF

    ' Collect matching row indices
    matchCount = 0
    FOR r = 0 TO tbl.rowCount - 1
        LET tableRow = tbl.rows(r)
        IF tableRow.deleted = 0 THEN
            ' Check WHERE
            includeRow = -1
            IF stmt.hasWhere <> 0 THEN
                whereResult = EvalBinaryExpr(stmt.whereClause, tableRow, tbl)
                IF whereResult = 0 THEN
                    includeRow = 0
                END IF
            END IF

            IF includeRow <> 0 THEN
                matchingRows(matchCount) = r
                matchCount = matchCount + 1
            END IF
        END IF
    NEXT r

    ' Handle GROUP BY and aggregate queries
    DIM isAggQuery AS INTEGER
    isAggQuery = HasAggregates(stmt)
    IF isAggQuery <> 0 OR stmt.groupByCount > 0 THEN
        DIM aggVal AS SqlValue
        DIM groupKeys(1000) AS STRING
        DIM groupRowCounts(1000) AS INTEGER
        ' Bug #010 now fixed - can use proper 2D arrays with CONST dimensions
        DIM groupRows(1000, 100) AS INTEGER
        DIM groupCount AS INTEGER
        DIM g AS INTEGER
        DIM gi AS INTEGER
        DIM foundGroup AS INTEGER
        DIM groupKey AS STRING
        DIM groupByExpr AS Expr
        DIM groupVal AS SqlValue
        DIM groupRowArray(1000) AS INTEGER
        DIM grc AS INTEGER

        groupCount = 0

        ' If GROUP BY is specified, group rows by GROUP BY values
        IF stmt.groupByCount > 0 THEN
            FOR i = 0 TO matchCount - 1
                rowIdx = matchingRows(i)
                LET tableRow = tbl.rows(rowIdx)

                ' Build group key from GROUP BY expression values
                groupKey = ""
                FOR g = 0 TO stmt.groupByCount - 1
                    LET groupByExpr = stmt.groupByExprs(g)
                    IF groupByExpr.kind = EXPR_COLUMN THEN
                        LET groupVal = EvalExprColumn(groupByExpr, tableRow, tbl)
                    ELSE
                        LET groupVal = SqlNull()
                    END IF
                    IF g > 0 THEN
                        groupKey = groupKey + "|"
                    END IF
                    groupKey = groupKey + groupVal.ToString$()
                NEXT g

                ' Find or create group
                foundGroup = -1
                FOR gi = 0 TO groupCount - 1
                    IF groupKeys(gi) = groupKey THEN
                        foundGroup = gi
                        EXIT FOR
                    END IF
                NEXT gi

                IF foundGroup < 0 THEN
                    ' Create new group
                    groupKeys(groupCount) = groupKey
                    groupRows(groupCount, 0) = rowIdx
                    groupRowCounts(groupCount) = 1
                    groupCount = groupCount + 1
                ELSE
                    ' Add to existing group
                    grc = groupRowCounts(foundGroup)
                    groupRows(foundGroup, grc) = rowIdx
                    groupRowCounts(foundGroup) = grc + 1
                END IF
            NEXT i
        ELSE
            ' No GROUP BY - all rows form one group
            groupCount = 1
            groupKeys(0) = "ALL"
            groupRowCounts(0) = matchCount
            FOR i = 0 TO matchCount - 1
                groupRows(0, i) = matchingRows(i)
            NEXT i
        END IF

        ' Process each group
        FOR g = 0 TO groupCount - 1
            LET resultRow = NEW SqlRow()
            resultRow.Init(0)

            ' Build array of rows for this group
            grc = groupRowCounts(g)
            FOR gi = 0 TO grc - 1
                groupRowArray(gi) = groupRows(g, gi)
            NEXT gi

            FOR c = 0 TO stmt.columnCount - 1
                LET colExpr = stmt.columns(c)
                LET aggVal = SqlNull()

                IF IsAggregateExpr(colExpr) <> 0 THEN
                    LET aggVal = EvalAggregate(colExpr, groupRowArray, grc, tbl)
                ELSEIF colExpr.kind = EXPR_COLUMN THEN
                    ' For non-aggregate columns (GROUP BY columns), use first row value
                    IF grc > 0 THEN
                        rowIdx = groupRowArray(0)
                        LET tableRow = tbl.rows(rowIdx)
                        LET aggVal = EvalExprColumn(colExpr, tableRow, tbl)
                    END IF
                ELSEIF colExpr.kind = EXPR_LITERAL THEN
                    LET aggVal = EvalExprLiteral(colExpr)
                END IF

                resultRow.AddValue(aggVal)
            NEXT c

            ' Check HAVING clause if present
            DIM includeGroup AS INTEGER
            includeGroup = -1
            IF stmt.hasHaving <> 0 THEN
                DIM havingResult AS INTEGER
                havingResult = EvalHavingExpr(stmt.havingClause, groupRowArray, grc, tbl)
                IF havingResult = 0 THEN
                    includeGroup = 0
                END IF
            END IF

            IF includeGroup <> 0 THEN
                result.AddRow(resultRow)
            END IF
        NEXT g

        gCurrentTableAlias = savedAlias
        ExecuteSelect = result
        EXIT FUNCTION
    END IF

    ' Sort matching rows if ORDER BY is present
    IF stmt.orderByCount > 0 THEN
        ' Bubble sort
        FOR i = 0 TO matchCount - 2
            FOR j = i + 1 TO matchCount - 1
                rowIdxI = matchingRows(i)
                rowIdxJ = matchingRows(j)
                LET rowI = tbl.rows(rowIdxI)
                LET rowJ = tbl.rows(rowIdxJ)

                ' Compare based on ORDER BY expressions
                shouldSwap = 0
                FOR k = 0 TO stmt.orderByCount - 1
                    LET orderExpr = stmt.orderByExprs(k)
                    isDesc = stmt.orderByDir(k)

                    ' Evaluate ORDER BY expression for both rows
                    IF orderExpr.kind = EXPR_COLUMN THEN
                        LET valI = EvalExprColumn(orderExpr, rowI, tbl)
                        LET valJ = EvalExprColumn(orderExpr, rowJ, tbl)
                    ELSEIF orderExpr.kind = EXPR_LITERAL THEN
                        LET valI = EvalExprLiteral(orderExpr)
                        LET valJ = EvalExprLiteral(orderExpr)
                    END IF

                    cmp = valI.Compare(valJ)
                    IF cmp <> 0 THEN
                        ' Values differ
                        IF isDesc = 1 THEN
                            ' DESC: swap if valI < valJ
                            IF cmp < 0 THEN
                                shouldSwap = -1
                            END IF
                        ELSE
                            ' ASC: swap if valI > valJ
                            IF cmp > 0 THEN
                                shouldSwap = -1
                            END IF
                        END IF
                        EXIT FOR
                    END IF
                NEXT k

                IF shouldSwap <> 0 THEN
                    ' Swap indices
                    tempIdx = matchingRows(i)
                    matchingRows(i) = matchingRows(j)
                    matchingRows(j) = tempIdx
                END IF
            NEXT j
        NEXT i
    END IF

    ' Build result rows from sorted indices, applying LIMIT/OFFSET
    DIM addedCount AS INTEGER
    addedCount = 0
    FOR ri = 0 TO matchCount - 1
        ' Skip OFFSET rows
        IF ri >= stmt.offsetValue THEN
            ' Check LIMIT
            IF stmt.limitValue < 0 OR addedCount < stmt.limitValue THEN
                rowIdx = matchingRows(ri)
                LET tableRow = tbl.rows(rowIdx)

                LET resultRow = NEW SqlRow()
                resultRow.Init(0)

                IF stmt.selectAll <> 0 THEN
                    ' Copy all columns
                    FOR c = 0 TO tableRow.columnCount - 1
                        resultRow.AddValue(tableRow.GetValue(c))
                    NEXT c
                ELSE
                    ' Evaluate selected columns
                    FOR c = 0 TO stmt.columnCount - 1
                        LET colExpr = stmt.columns(c)
                        IF colExpr.kind = EXPR_COLUMN THEN
                            LET val = EvalExprColumn(colExpr, tableRow, tbl)
                        ELSEIF colExpr.kind = EXPR_LITERAL THEN
                            LET val = EvalExprLiteral(colExpr)
                        END IF
                        resultRow.AddValue(val)
                    NEXT c
                END IF

                ' Check for DISTINCT - skip duplicate rows
                DIM addThisRow AS INTEGER
                addThisRow = -1
                IF stmt.isDistinct <> 0 THEN
                    IF IsDuplicateRow(resultRow, result) <> 0 THEN
                        addThisRow = 0
                    END IF
                END IF

                IF addThisRow <> 0 THEN
                    result.AddRow(resultRow)
                    addedCount = addedCount + 1
                END IF
            END IF
        END IF
    NEXT ri

    gCurrentTableAlias = savedAlias
    ExecuteSelect = result
END FUNCTION

FUNCTION ExecuteUpdate(stmt AS UpdateStmt) AS QueryResult
    DIM result AS QueryResult
    DIM tbl AS SqlTable
    DIM tableRow AS SqlRow
    DIM valExpr AS Expr
    DIM newVal AS SqlValue
    DIM tableIdx AS INTEGER
    DIM r AS INTEGER
    DIM i AS INTEGER
    DIM updateRow AS INTEGER
    DIM whereResult AS INTEGER
    DIM colIdx AS INTEGER
    DIM tName AS STRING
    DIM updateCount AS INTEGER

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Workaround: Copy class member to local var
    tName = stmt.tableName

    ' Find table
    tableIdx = gDatabase.FindTable(tName)
    IF tableIdx < 0 THEN
        result.success = 0
        result.message = "Table '" + tName + "' does not exist"
        ExecuteUpdate = result
        EXIT FUNCTION
    END IF

    LET tbl = gDatabase.tables(tableIdx)
    updateCount = 0

    ' Process rows
    FOR r = 0 TO tbl.rowCount - 1
        LET tableRow = tbl.rows(r)
        IF tableRow.deleted = 0 THEN
            ' Check WHERE
            updateRow = -1
            IF stmt.hasWhere <> 0 THEN
                whereResult = EvalBinaryExpr(stmt.whereClause, tableRow, tbl)
                IF whereResult = 0 THEN
                    updateRow = 0
                END IF
            END IF

            IF updateRow <> 0 THEN
                ' Apply SET updates
                DIM setColName AS STRING
                FOR i = 0 TO stmt.setCount - 1
                    ' Workaround Bug #011: Copy array element to local var
                    setColName = stmt.setColumns(i)
                    colIdx = tbl.FindColumnIndex(setColName)
                    IF colIdx >= 0 THEN
                        LET valExpr = stmt.setValues(i)
                        IF valExpr.kind = EXPR_LITERAL THEN
                            LET newVal = EvalExprLiteral(valExpr)
                        ELSEIF valExpr.kind = EXPR_COLUMN THEN
                            LET newVal = EvalExprColumn(valExpr, tableRow, tbl)
                        END IF
                        tableRow.SetValue(colIdx, newVal)
                    END IF
                NEXT i
                updateCount = updateCount + 1
            END IF
        END IF
    NEXT r

    result.message = "Updated " + STR$(updateCount) + " row(s)"
    result.rowsAffected = updateCount
    ExecuteUpdate = result
END FUNCTION

FUNCTION ExecuteDelete(stmt AS DeleteStmt) AS QueryResult
    DIM result AS QueryResult
    DIM tbl AS SqlTable
    DIM tableRow AS SqlRow
    DIM tableIdx AS INTEGER
    DIM r AS INTEGER
    DIM deleteRow AS INTEGER
    DIM whereResult AS INTEGER
    DIM tName AS STRING
    DIM deleteCount AS INTEGER

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Workaround: Copy class member to local var
    tName = stmt.tableName

    ' Find table
    tableIdx = gDatabase.FindTable(tName)
    IF tableIdx < 0 THEN
        result.success = 0
        result.message = "Table '" + tName + "' does not exist"
        ExecuteDelete = result
        EXIT FUNCTION
    END IF

    LET tbl = gDatabase.tables(tableIdx)
    deleteCount = 0

    ' Process rows
    FOR r = 0 TO tbl.rowCount - 1
        LET tableRow = tbl.rows(r)
        IF tableRow.deleted = 0 THEN
            ' Check WHERE
            deleteRow = -1
            IF stmt.hasWhere <> 0 THEN
                whereResult = EvalBinaryExpr(stmt.whereClause, tableRow, tbl)
                IF whereResult = 0 THEN
                    deleteRow = 0
                END IF
            END IF

            IF deleteRow <> 0 THEN
                tableRow.deleted = -1
                deleteCount = deleteCount + 1
            END IF
        END IF
    NEXT r

    result.message = "Deleted " + STR$(deleteCount) + " row(s)"
    result.rowsAffected = deleteCount
    ExecuteDelete = result
END FUNCTION

FUNCTION ExecuteCreateIndex(stmt AS CreateIndexStmt) AS QueryResult
    DIM result AS QueryResult
    DIM tbl AS SqlTable
    DIM idx AS SqlIndex
    DIM tableIdx AS INTEGER
    DIM existingIdx AS INTEGER
    DIM i AS INTEGER
    DIM row AS SqlRow
    DIM colIdx AS INTEGER
    DIM success AS INTEGER
    DIM colName AS STRING

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Check if index already exists
    existingIdx = gIndexManager.FindIndex(stmt.indexName)
    IF existingIdx >= 0 THEN
        result.success = 0
        result.message = "Index '" + stmt.indexName + "' already exists"
        ExecuteCreateIndex = result
        EXIT FUNCTION
    END IF

    ' Check if table exists
    tableIdx = gDatabase.FindTable(stmt.tableName)
    IF tableIdx < 0 THEN
        result.success = 0
        result.message = "Table '" + stmt.tableName + "' does not exist"
        ExecuteCreateIndex = result
        EXIT FUNCTION
    END IF

    LET tbl = gDatabase.tables(tableIdx)

    ' Validate all columns exist
    FOR i = 0 TO stmt.columnCount - 1
        colName = stmt.columnNames(i)
        colIdx = tbl.FindColumnIndex(colName)
        IF colIdx < 0 THEN
            result.success = 0
            result.message = "Column '" + colName + "' does not exist in table '" + stmt.tableName + "'"
            ExecuteCreateIndex = result
            EXIT FUNCTION
        END IF
    NEXT i

    ' Create the index
    LET idx = MakeIndex(stmt.indexName, stmt.tableName)
    idx.isUnique = stmt.isUnique
    FOR i = 0 TO stmt.columnCount - 1
        colName = stmt.columnNames(i)
        idx.AddColumn(colName)
    NEXT i

    ' Build the index from existing rows
    idx.Rebuild(tbl)

    ' Add to index manager
    gIndexManager.AddIndex(idx)

    result.message = "Index '" + stmt.indexName + "' created with " + STR$(idx.entryCount) + " entries"
    ExecuteCreateIndex = result
END FUNCTION

FUNCTION ExecuteDropIndex(stmt AS DropIndexStmt) AS QueryResult
    DIM result AS QueryResult
    DIM existingIdx AS INTEGER

    LET result = NEW QueryResult()
    result.Init()
    InitDatabase()

    ' Check if index exists
    existingIdx = gIndexManager.FindIndex(stmt.indexName)
    IF existingIdx < 0 THEN
        result.success = 0
        result.message = "Index '" + stmt.indexName + "' does not exist"
        ExecuteDropIndex = result
        EXIT FUNCTION
    END IF

    ' Drop the index
    gIndexManager.DropIndex(stmt.indexName)

    result.message = "Index '" + stmt.indexName + "' dropped"
    ExecuteDropIndex = result
END FUNCTION

FUNCTION ExecuteSql(sql AS STRING) AS QueryResult
    DIM result AS QueryResult
    DIM createStmt AS CreateTableStmt
    DIM createIdxStmt AS CreateIndexStmt
    DIM dropIdxStmt AS DropIndexStmt
    DIM insertStmt AS InsertStmt
    DIM selectStmt AS SelectStmt
    DIM updateStmt AS UpdateStmt
    DIM deleteStmt AS DeleteStmt

    LET result = NEW QueryResult()
    result.Init()
    ParserInit(sql)

    ' Determine statement type
    IF gTok.kind = TK_CREATE THEN
        ParserAdvance()
        IF gTok.kind = TK_TABLE THEN
            LET createStmt = ParseCreateTableStmt()
            IF gParserHasError <> 0 THEN
                result.SetError(gParserError)
                ExecuteSql = result
                EXIT FUNCTION
            END IF
            ExecuteSql = ExecuteCreateTable(createStmt)
            EXIT FUNCTION
        END IF
        ' CREATE INDEX or CREATE UNIQUE INDEX
        IF gTok.kind = TK_INDEX OR gTok.kind = TK_UNIQUE THEN
            LET createIdxStmt = ParseCreateIndexStmt()
            IF gParserHasError <> 0 THEN
                result.SetError(gParserError)
                ExecuteSql = result
                EXIT FUNCTION
            END IF
            ExecuteSql = ExecuteCreateIndex(createIdxStmt)
            EXIT FUNCTION
        END IF
    END IF

    IF gTok.kind = TK_DROP THEN
        ParserAdvance()
        IF gTok.kind = TK_INDEX THEN
            LET dropIdxStmt = ParseDropIndexStmt()
            IF gParserHasError <> 0 THEN
                result.SetError(gParserError)
                ExecuteSql = result
                EXIT FUNCTION
            END IF
            ExecuteSql = ExecuteDropIndex(dropIdxStmt)
            EXIT FUNCTION
        END IF
    END IF

    IF gTok.kind = TK_INSERT THEN
        ParserAdvance()
        LET insertStmt = ParseInsertStmt()
        IF gParserHasError <> 0 THEN
            result.SetError(gParserError)
            ExecuteSql = result
            EXIT FUNCTION
        END IF
        ExecuteSql = ExecuteInsert(insertStmt)
        EXIT FUNCTION
    END IF

    IF gTok.kind = TK_SELECT THEN
        ParserAdvance()
        LET selectStmt = ParseSelectStmt()
        IF gParserHasError <> 0 THEN
            result.SetError(gParserError)
            ExecuteSql = result
            EXIT FUNCTION
        END IF
        ExecuteSql = ExecuteSelect(selectStmt)
        EXIT FUNCTION
    END IF

    IF gTok.kind = TK_UPDATE THEN
        ParserAdvance()
        LET updateStmt = ParseUpdateStmt()
        IF gParserHasError <> 0 THEN
            result.SetError(gParserError)
            ExecuteSql = result
            EXIT FUNCTION
        END IF
        ExecuteSql = ExecuteUpdate(updateStmt)
        EXIT FUNCTION
    END IF

    IF gTok.kind = TK_DELETE THEN
        ParserAdvance()
        LET deleteStmt = ParseDeleteStmt()
        IF gParserHasError <> 0 THEN
            result.SetError(gParserError)
            ExecuteSql = result
            EXIT FUNCTION
        END IF
        ExecuteSql = ExecuteDelete(deleteStmt)
        EXIT FUNCTION
    END IF

    result.SetError("Unknown statement type")
    ExecuteSql = result
END FUNCTION

