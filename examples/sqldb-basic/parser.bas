' parser.bas - SQL Parser
' Part of SQLite Clone - Viper Basic Implementation

AddFile "stmt.bas"

'=============================================================================
' EXPRESSION PARSING FUNCTIONS
'=============================================================================

' Note: Due to BASIC's function order dependencies, we use forward-compatible patterns

' Parse primary expression (literals, identifiers)
FUNCTION ParsePrimaryExpr() AS Expr
    DIM kind AS INTEGER
    DIM text AS STRING
    DIM name AS STRING
    DIM colName AS STRING
    DIM sv AS SqlValue
    DIM e AS Expr

    LET kind = gTok.kind

    ' Integer literal - store as text
    IF kind = TK_INTEGER THEN
        LET text = gTok.text
        ParserAdvance()
        LET sv = NEW SqlValue()
        sv.InitText(text)
        LET e = NEW Expr()
        e.Init()
        e.InitLiteral(sv)
        ParsePrimaryExpr = e
        EXIT FUNCTION
    END IF

    ' Real/float literal - store as text
    IF kind = TK_NUMBER THEN
        LET text = gTok.text
        ParserAdvance()
        LET e = ExprReal(0.0, text)
        ParsePrimaryExpr = e
        EXIT FUNCTION
    END IF

    ' String literal
    IF kind = TK_STRING THEN
        LET text = gTok.text
        ParserAdvance()
        ParsePrimaryExpr = ExprText(text)
        EXIT FUNCTION
    END IF

    ' NULL
    IF kind = TK_NULL THEN
        ParserAdvance()
        ParsePrimaryExpr = ExprNull()
        EXIT FUNCTION
    END IF

    ' Identifier (column ref or function call)
    IF kind = TK_IDENTIFIER THEN
        LET name = gTok.text
        ParserAdvance()

        ' Check for function call - identifier followed by (
        IF gTok.kind = TK_LPAREN THEN
            DIM funcExpr AS Expr
            DIM argExpr AS Expr
            DIM argText AS STRING
            DIM argInt AS INTEGER

            ParserAdvance()  ' Consume (

            ' Create function expression
            LET funcExpr = NEW Expr()
            funcExpr.Init()
            funcExpr.InitFunction(name)

            ' Parse arguments until )
            WHILE gTok.kind <> TK_RPAREN AND gParserHasError = 0
                IF gTok.kind = TK_STAR THEN
                    LET argExpr = ExprStar()
                    funcExpr.AddArg(argExpr)
                    ParserAdvance()
                ELSEIF gTok.kind = TK_IDENTIFIER THEN
                    argText = gTok.text
                    ParserAdvance()
                    LET argExpr = ExprColumn(argText)
                    funcExpr.AddArg(argExpr)
                ELSEIF gTok.kind = TK_INTEGER THEN
                    argText = gTok.text
                    ParserAdvance()
                    argInt = VAL(argText)
                    LET argExpr = ExprInt(argInt)
                    funcExpr.AddArg(argExpr)
                ELSEIF gTok.kind = TK_STRING THEN
                    argText = gTok.text
                    ParserAdvance()
                    LET argExpr = ExprText(argText)
                    funcExpr.AddArg(argExpr)
                ELSE
                    EXIT WHILE
                END IF

                ' Check for comma
                IF gTok.kind = TK_COMMA THEN
                    ParserAdvance()
                END IF
            WEND

            ' Consume )
            IF gTok.kind = TK_RPAREN THEN
                ParserAdvance()
            END IF

            ParsePrimaryExpr = funcExpr
            EXIT FUNCTION
        END IF

        ' Check for table.column
        IF gTok.kind = TK_DOT THEN
            ParserAdvance()
            IF gTok.kind <> TK_IDENTIFIER THEN
                ParserSetError("Expected column name after dot")
                ParsePrimaryExpr = ExprNull()
                EXIT FUNCTION
            END IF
            LET colName = gTok.text
            ParserAdvance()
            ParsePrimaryExpr = ExprTableColumn(name, colName)
            EXIT FUNCTION
        END IF

        ParsePrimaryExpr = ExprColumn(name)
        EXIT FUNCTION
    END IF

    ' Star (*)
    IF kind = TK_STAR THEN
        ParserAdvance()
        ParsePrimaryExpr = ExprStar()
        EXIT FUNCTION
    END IF

    ' Handle parenthesized expressions including subqueries
    IF kind = TK_LPAREN THEN
        DIM subquerySql AS STRING
        DIM depth AS INTEGER
        DIM innerExpr AS Expr

        ParserAdvance()  ' Consume (

        ' Check if this is a subquery: (SELECT ...)
        IF gTok.kind = TK_SELECT THEN
            ' Collect tokens to build subquery SQL
            subquerySql = "SELECT"
            ParserAdvance()
            depth = 1  ' We're inside one level of parens

            WHILE depth > 0 AND gParserHasError = 0
                IF gTok.kind = TK_LPAREN THEN
                    depth = depth + 1
                    subquerySql = subquerySql + " ("
                ELSEIF gTok.kind = TK_RPAREN THEN
                    depth = depth - 1
                    IF depth > 0 THEN
                        subquerySql = subquerySql + ")"
                    END IF
                ELSEIF gTok.kind = TK_EOF THEN
                    ParserSetError("Unexpected end of input in subquery")
                    ParsePrimaryExpr = ExprNull()
                    EXIT FUNCTION
                ELSE
                    ' Add token text with space
                    subquerySql = subquerySql + " " + gTok.text
                END IF
                ParserAdvance()
            WEND

            ParsePrimaryExpr = ExprSubquery(subquerySql)
            EXIT FUNCTION
        END IF

        ' Regular parenthesized expression - parse inner expression
        LET innerExpr = ParseExpr()
        IF gTok.kind = TK_RPAREN THEN
            ParserAdvance()
        ELSE
            ParserSetError("Expected ) after parenthesized expression")
        END IF
        ParsePrimaryExpr = innerExpr
        EXIT FUNCTION
    END IF

    ParserSetError("Unexpected token in expression")
    ParsePrimaryExpr = ExprNull()
END FUNCTION

' Parse unary expression
FUNCTION ParseUnaryExpr() AS Expr
    DIM operand AS Expr

    IF gTok.kind = TK_MINUS THEN
        ParserAdvance()
        LET operand = ParseUnaryExpr()
        ParseUnaryExpr = ExprUnary(UOP_NEG, operand)
        EXIT FUNCTION
    END IF
    IF gTok.kind = TK_NOT THEN
        ParserAdvance()
        LET operand = ParseUnaryExpr()
        ParseUnaryExpr = ExprUnary(UOP_NOT, operand)
        EXIT FUNCTION
    END IF
    ParseUnaryExpr = ParsePrimaryExpr()
END FUNCTION

' Parse multiplication/division
FUNCTION ParseMulExpr() AS Expr
    DIM left AS Expr
    DIM right AS Expr
    DIM op AS INTEGER

    LET left = ParseUnaryExpr()
    WHILE (gTok.kind = TK_STAR) OR (gTok.kind = TK_SLASH)
        LET op = gTok.kind
        ParserAdvance()
        LET right = ParseUnaryExpr()
        IF op = TK_STAR THEN
            LET left = ExprBinary(OP_MUL, left, right)
        ELSE
            LET left = ExprBinary(OP_DIV, left, right)
        END IF
    WEND
    ParseMulExpr = left
END FUNCTION

' Parse addition/subtraction
FUNCTION ParseAddExpr() AS Expr
    DIM left AS Expr
    DIM right AS Expr
    DIM op AS INTEGER

    LET left = ParseMulExpr()
    WHILE (gTok.kind = TK_PLUS) OR (gTok.kind = TK_MINUS)
        LET op = gTok.kind
        ParserAdvance()
        LET right = ParseMulExpr()
        IF op = TK_PLUS THEN
            LET left = ExprBinary(OP_ADD, left, right)
        ELSE
            LET left = ExprBinary(OP_SUB, left, right)
        END IF
    WEND
    ParseAddExpr = left
END FUNCTION

' Parse comparison
FUNCTION ParseCompExpr() AS Expr
    DIM left AS Expr
    DIM kind AS INTEGER

    LET left = ParseAddExpr()
    LET kind = gTok.kind
    IF kind = TK_EQ THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_EQ, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    IF kind = TK_NE THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_NE, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    IF kind = TK_LT THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_LT, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    IF kind = TK_LE THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_LE, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    IF kind = TK_GT THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_GT, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    IF kind = TK_GE THEN
        ParserAdvance()
        ParseCompExpr = ExprBinary(OP_GE, left, ParseAddExpr())
        EXIT FUNCTION
    END IF
    ' Handle IN (subquery or value list)
    IF kind = TK_IN THEN
        DIM right AS Expr
        ParserAdvance()
        ' Expect ( after IN
        IF gTok.kind <> TK_LPAREN THEN
            ParserSetError("Expected '(' after IN")
            ParseCompExpr = left
            EXIT FUNCTION
        END IF
        ' The ParsePrimaryExpr will handle (SELECT ...) as a subquery
        LET right = ParsePrimaryExpr()
        ParseCompExpr = ExprBinary(OP_IN, left, right)
        EXIT FUNCTION
    END IF
    ParseCompExpr = left
END FUNCTION

' Parse AND
FUNCTION ParseAndExpr() AS Expr
    DIM left AS Expr
    DIM right AS Expr

    LET left = ParseCompExpr()
    WHILE gTok.kind = TK_AND
        ParserAdvance()
        LET right = ParseCompExpr()
        LET left = ExprBinary(OP_AND, left, right)
    WEND
    ParseAndExpr = left
END FUNCTION

' Parse OR
FUNCTION ParseOrExpr() AS Expr
    DIM left AS Expr
    DIM right AS Expr

    LET left = ParseAndExpr()
    WHILE gTok.kind = TK_OR
        ParserAdvance()
        LET right = ParseAndExpr()
        LET left = ExprBinary(OP_OR, left, right)
    WEND
    ParseOrExpr = left
END FUNCTION

' Main expression entry point
FUNCTION ParseExpr() AS Expr
    ParseExpr = ParseOrExpr()
END FUNCTION

'=============================================================================
' STATEMENT PARSING FUNCTIONS
'=============================================================================

' Parse column definition for CREATE TABLE
FUNCTION ParseColumnDef() AS SqlColumn
    DIM col AS SqlColumn

    LET col = NEW SqlColumn()
    col.Init("", SQL_TEXT)

    ' Column name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected column name")
        ParseColumnDef = col
        EXIT FUNCTION
    END IF
    col.name = gTok.text
    ParserAdvance()

    ' Data type
    IF gTok.kind = TK_INTEGER_TYPE THEN
        col.typeCode = SQL_INTEGER
        ParserAdvance()
    ELSEIF gTok.kind = TK_TEXT THEN
        col.typeCode = SQL_TEXT
        ParserAdvance()
    ELSEIF gTok.kind = TK_REAL THEN
        col.typeCode = SQL_REAL
        ParserAdvance()
    ELSE
        ParserSetError("Expected data type")
        ParseColumnDef = col
        EXIT FUNCTION
    END IF

    ' Optional constraints
    WHILE gParserHasError = 0
        IF gTok.kind = TK_PRIMARY THEN
            ParserAdvance()
            IF ParserExpect(TK_KEY) = 0 THEN
                ParseColumnDef = col
                EXIT FUNCTION
            END IF
            col.primaryKey = -1
        ELSEIF gTok.kind = TK_AUTOINCREMENT THEN
            ParserAdvance()
            col.autoIncrement = -1
        ELSEIF gTok.kind = TK_NOT THEN
            ParserAdvance()
            IF ParserExpect(TK_NULL) = 0 THEN
                ParseColumnDef = col
                EXIT FUNCTION
            END IF
            col.notNull = -1
        ELSEIF gTok.kind = TK_UNIQUE THEN
            ParserAdvance()
            col.isUnique = -1
        ELSEIF gTok.kind = TK_DEFAULT THEN
            ParserAdvance()
            ' Parse default value (literal)
            IF gTok.kind = TK_NUMBER OR gTok.kind = TK_INTEGER THEN
                DIM defVal AS SqlValue
                DIM defIntVal AS INTEGER
                LET defVal = NEW SqlValue()
                IF INSTR(gTok.text, ".") > 0 THEN
                    defVal.InitReal(VAL(gTok.text), gTok.text)
                ELSE
                    defIntVal = VAL(gTok.text)
                    defVal.InitInteger(defIntVal)
                END IF
                col.SetDefault(defVal)
                ParserAdvance()
            ELSEIF gTok.kind = TK_STRING THEN
                DIM defVal2 AS SqlValue
                LET defVal2 = NEW SqlValue()
                defVal2.InitText(gTok.text)
                col.SetDefault(defVal2)
                ParserAdvance()
            ELSEIF gTok.kind = TK_NULL THEN
                DIM defVal3 AS SqlValue
                LET defVal3 = NEW SqlValue()
                defVal3.InitNull()
                col.SetDefault(defVal3)
                ParserAdvance()
            ELSE
                ParserSetError("Expected default value")
                ParseColumnDef = col
                EXIT FUNCTION
            END IF
        ELSEIF gTok.kind = TK_REFERENCES THEN
            ' REFERENCES table_name(column_name)
            ParserAdvance()
            IF gTok.kind <> TK_IDENTIFIER THEN
                ParserSetError("Expected referenced table name")
                ParseColumnDef = col
                EXIT FUNCTION
            END IF
            col.refTableName = gTok.text
            col.isForeignKey = -1
            ParserAdvance()
            IF ParserExpect(TK_LPAREN) = 0 THEN
                ParseColumnDef = col
                EXIT FUNCTION
            END IF
            IF gTok.kind <> TK_IDENTIFIER THEN
                ParserSetError("Expected referenced column name")
                ParseColumnDef = col
                EXIT FUNCTION
            END IF
            col.refColumnName = gTok.text
            ParserAdvance()
            IF ParserExpect(TK_RPAREN) = 0 THEN
                ParseColumnDef = col
                EXIT FUNCTION
            END IF
        ELSE
            EXIT WHILE
        END IF
    WEND

    ParseColumnDef = col
END FUNCTION

' Parse CREATE TABLE statement
FUNCTION ParseCreateTableStmt() AS CreateTableStmt
    DIM stmt AS CreateTableStmt
    DIM col AS SqlColumn

    LET stmt = NEW CreateTableStmt()
    stmt.Init()

    ' Already at CREATE, expect TABLE
    IF ParserExpect(TK_TABLE) = 0 THEN
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF

    ' Table name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name, got kind=" + STR$(gTok.kind) + " text='" + gTok.text + "'")
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

    ' Opening paren
    IF ParserExpect(TK_LPAREN) = 0 THEN
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF

    ' Column definitions
    WHILE gParserHasError = 0
        LET col = ParseColumnDef()
        IF gParserHasError <> 0 THEN
            ParseCreateTableStmt = stmt
            EXIT FUNCTION
        END IF
        stmt.AddColumn(col)

        IF gTok.kind = TK_COMMA THEN
            ParserAdvance()
        ELSE
            EXIT WHILE
        END IF
    WEND

    ' Closing paren
    IF ParserExpect(TK_RPAREN) = 0 THEN
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF

    ' Optional semicolon
    ParserMatch(TK_SEMICOLON)

    ParseCreateTableStmt = stmt
END FUNCTION

' Parse INSERT statement
FUNCTION ParseInsertStmt() AS InsertStmt
    DIM stmt AS InsertStmt
    DIM rowIdx AS INTEGER
    DIM val AS Expr

    LET stmt = NEW InsertStmt()
    stmt.Init()

    ' Already at INSERT, expect INTO
    IF ParserExpect(TK_INTO) = 0 THEN
        ParseInsertStmt = stmt
        EXIT FUNCTION
    END IF

    ' Table name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseInsertStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

    ' Optional column list
    IF gTok.kind = TK_LPAREN THEN
        ParserAdvance()
        WHILE gParserHasError = 0
            IF gTok.kind <> TK_IDENTIFIER THEN
                ParserSetError("Expected column name")
                ParseInsertStmt = stmt
                EXIT FUNCTION
            END IF
            stmt.AddColumnName(gTok.text)
            ParserAdvance()

            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND
        IF ParserExpect(TK_RPAREN) = 0 THEN
            ParseInsertStmt = stmt
            EXIT FUNCTION
        END IF
    END IF

    ' Expect VALUES
    IF ParserExpect(TK_VALUES) = 0 THEN
        ParseInsertStmt = stmt
        EXIT FUNCTION
    END IF

    ' Value rows
    WHILE gParserHasError = 0
        IF ParserExpect(TK_LPAREN) = 0 THEN
            ParseInsertStmt = stmt
            EXIT FUNCTION
        END IF

        stmt.AddValueRow()
        rowIdx = stmt.rowCount - 1

        ' Parse values
        WHILE gParserHasError = 0
            LET val = ParseExpr()
            IF gParserHasError <> 0 THEN
                ParseInsertStmt = stmt
                EXIT FUNCTION
            END IF
            stmt.AddValue(rowIdx, val)

            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND

        IF ParserExpect(TK_RPAREN) = 0 THEN
            ParseInsertStmt = stmt
            EXIT FUNCTION
        END IF

        IF gTok.kind = TK_COMMA THEN
            ParserAdvance()
        ELSE
            EXIT WHILE
        END IF
    WEND

    ParserMatch(TK_SEMICOLON)
    ParseInsertStmt = stmt
END FUNCTION

'=============================================================================
' SELECT STATEMENT CLASS
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
    PUBLIC limitValue AS INTEGER     ' -1 = no limit
    PUBLIC offsetValue AS INTEGER    ' 0 = no offset
    PUBLIC isDistinct AS INTEGER     ' 0 = normal, non-zero = DISTINCT
    PUBLIC groupByExprs(20) AS Expr
    PUBLIC groupByCount AS INTEGER
    PUBLIC havingClause AS Expr
    PUBLIC hasHaving AS INTEGER
    PUBLIC tableNames(20) AS STRING
    PUBLIC tableAliases(20) AS STRING
    PUBLIC tableCount AS INTEGER
    PUBLIC joinTypes(20) AS INTEGER      ' 0=CROSS, 1=INNER, 2=LEFT, 3=RIGHT, 4=FULL
    PUBLIC joinConditions(20) AS Expr
    PUBLIC joinConditionCount AS INTEGER
    ' Derived tables (subquery in FROM clause)
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
        DIM i AS INTEGER
        result = "SELECT "
        IF selectAll <> 0 THEN
            result = result + "* FROM " + tableName
        ELSE
            result = result + "... FROM " + tableName
        END IF
        ToString$ = result + ";"
    END FUNCTION
END CLASS

' Parse SELECT statement (already past SELECT token)
FUNCTION ParseSelectStmt() AS SelectStmt
    DIM stmt AS SelectStmt
    DIM col AS Expr

    LET stmt = NEW SelectStmt()
    stmt.Init()

    ' Check for DISTINCT keyword
    IF gTok.kind = TK_DISTINCT THEN
        stmt.isDistinct = -1
        ParserAdvance()
    END IF

    ' Check for * or column list
    IF gTok.kind = TK_STAR THEN
        stmt.selectAll = -1
        ParserAdvance()
    ELSE
        ' Parse column list
        DIM maybeColAlias AS STRING
        DIM upperColAlias AS STRING
        WHILE gParserHasError = 0
            LET col = ParseExpr()
            IF gParserHasError <> 0 THEN
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF

            ' Handle column alias: expr AS alias or expr alias
            IF gTok.kind = TK_AS THEN
                ParserAdvance()
                IF gTok.kind = TK_IDENTIFIER THEN
                    ' Skip the alias (we don't store it for now)
                    ParserAdvance()
                END IF
            ELSEIF gTok.kind = TK_IDENTIFIER THEN
                maybeColAlias = gTok.text
                upperColAlias = UCASE$(maybeColAlias)
                IF upperColAlias <> "FROM" AND upperColAlias <> "WHERE" AND upperColAlias <> "GROUP" AND upperColAlias <> "ORDER" AND upperColAlias <> "LIMIT" AND upperColAlias <> "HAVING" THEN
                    ParserAdvance()  ' Skip the alias
                END IF
            END IF

            stmt.AddColumn(col)

            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND
    END IF

    ' Expect FROM
    IF ParserExpect(TK_FROM) = 0 THEN
        ParseSelectStmt = stmt
        EXIT FUNCTION
    END IF

    ' Check for derived table (subquery in FROM): FROM (SELECT ...)
    IF gTok.kind = TK_LPAREN THEN
        ParserAdvance()
        IF gTok.kind = TK_SELECT THEN
            ' This is a derived table - capture the full subquery
            DIM parenDepth AS INTEGER
            DIM subquerySQL AS STRING
            parenDepth = 1
            subquerySQL = "SELECT"
            ParserAdvance()  ' Move past SELECT

            ' Collect tokens until we close the parenthesis
            WHILE parenDepth > 0 AND gParserHasError = 0 AND gTok.kind <> TK_EOF
                IF gTok.kind = TK_LPAREN THEN
                    parenDepth = parenDepth + 1
                    subquerySQL = subquerySQL + " ("
                ELSEIF gTok.kind = TK_RPAREN THEN
                    parenDepth = parenDepth - 1
                    IF parenDepth > 0 THEN
                        subquerySQL = subquerySQL + " )"
                    END IF
                ELSE
                    subquerySQL = subquerySQL + " " + gTok.text
                END IF
                ParserAdvance()
            WEND

            stmt.hasDerivedTable = -1
            stmt.derivedTableSQL = subquerySQL
            stmt.tableName = "__derived__"

            ' Expect alias after derived table (required in SQL)
            IF gTok.kind = TK_AS THEN
                ParserAdvance()
            END IF
            IF gTok.kind = TK_IDENTIFIER THEN
                stmt.derivedTableAlias = gTok.text
                stmt.tableAlias = gTok.text
                ParserAdvance()
            ELSE
                ParserSetError("Derived table requires an alias")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF

            stmt.AddTable("__derived__", stmt.derivedTableAlias)
            ' Skip regular table parsing since we have a derived table
        ELSE
            ParserSetError("Expected SELECT in derived table")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
    ELSE
        ' Get first table name (regular table, not derived)
        IF gTok.kind <> TK_IDENTIFIER THEN
            ParserSetError("Expected table name")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        DIM firstTableName AS STRING
        DIM firstTableAlias AS STRING
        firstTableName = gTok.text
        firstTableAlias = ""
        stmt.tableName = firstTableName  ' Keep for backward compatibility
        ParserAdvance()

        ' Optional table alias (AS alias or just alias)
        IF gTok.kind = TK_AS THEN
            ParserAdvance()
            IF gTok.kind = TK_IDENTIFIER THEN
                firstTableAlias = gTok.text
                stmt.tableAlias = firstTableAlias
                ParserAdvance()
            ELSE
                ParserSetError("Expected alias name after AS")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
        ELSEIF gTok.kind = TK_IDENTIFIER THEN
            ' Check it's not a keyword that could follow FROM table
            DIM aliasText AS STRING
            DIM upperAlias AS STRING
            aliasText = gTok.text
            upperAlias = UCASE$(aliasText)
            IF upperAlias <> "WHERE" AND upperAlias <> "GROUP" AND upperAlias <> "ORDER" AND upperAlias <> "LIMIT" AND upperAlias <> "HAVING" AND upperAlias <> "JOIN" AND upperAlias <> "INNER" AND upperAlias <> "LEFT" AND upperAlias <> "RIGHT" AND upperAlias <> "FULL" AND upperAlias <> "CROSS" THEN
                firstTableAlias = aliasText
                stmt.tableAlias = firstTableAlias
                ParserAdvance()
            END IF
        END IF

        ' Add first table to lists
        stmt.AddTable(firstTableName, firstTableAlias)
    END IF

    ' Parse additional tables (comma-separated for CROSS JOIN)
    DIM extraTableName AS STRING
    DIM extraTableAlias AS STRING
    DIM maybeAlias AS STRING
    DIM upperMaybe AS STRING
    WHILE gTok.kind = TK_COMMA
        ParserAdvance()
        IF gTok.kind <> TK_IDENTIFIER THEN
            ParserSetError("Expected table name after comma")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        extraTableName = gTok.text
        extraTableAlias = ""
        ParserAdvance()

        ' Optional alias for extra table
        IF gTok.kind = TK_AS THEN
            ParserAdvance()
            IF gTok.kind = TK_IDENTIFIER THEN
                extraTableAlias = gTok.text
                ParserAdvance()
            END IF
        ELSEIF gTok.kind = TK_IDENTIFIER THEN
            maybeAlias = gTok.text
            upperMaybe = UCASE$(maybeAlias)
            IF upperMaybe <> "WHERE" AND upperMaybe <> "GROUP" AND upperMaybe <> "ORDER" AND upperMaybe <> "LIMIT" AND upperMaybe <> "HAVING" AND upperMaybe <> "JOIN" THEN
                extraTableAlias = maybeAlias
                ParserAdvance()
            END IF
        END IF

        stmt.AddTable(extraTableName, extraTableAlias)
    WEND

    ' Parse JOIN clauses (INNER JOIN, LEFT JOIN, etc.)
    DIM joinType AS INTEGER
    DIM joinTableName AS STRING
    DIM joinTableAlias AS STRING
    DIM maybeJoinAlias AS STRING
    DIM upperJoinAlias AS STRING
    DIM joinCondition AS Expr
    DIM hasJoinCondition AS INTEGER

    WHILE gTok.kind = TK_JOIN OR gTok.kind = TK_INNER OR gTok.kind = TK_LEFT OR gTok.kind = TK_RIGHT OR gTok.kind = TK_FULL OR gTok.kind = TK_CROSS
        joinType = 1  ' Default to INNER JOIN

        ' Determine join type
        IF gTok.kind = TK_INNER THEN
            joinType = 1
            ParserAdvance()
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after INNER")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_LEFT THEN
            joinType = 2
            ParserAdvance()
            IF gTok.kind = TK_OUTER THEN
                ParserAdvance()
            END IF
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after LEFT")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_RIGHT THEN
            joinType = 3
            ParserAdvance()
            IF gTok.kind = TK_OUTER THEN
                ParserAdvance()
            END IF
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after RIGHT")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_FULL THEN
            joinType = 4
            ParserAdvance()
            IF gTok.kind = TK_OUTER THEN
                ParserAdvance()
            END IF
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after FULL")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_CROSS THEN
            joinType = 0
            ParserAdvance()
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after CROSS")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_JOIN THEN
            joinType = 1  ' Bare JOIN = INNER JOIN
            ParserAdvance()
        END IF

        ' Expect table name
        IF gTok.kind <> TK_IDENTIFIER THEN
            ParserSetError("Expected table name after JOIN")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        joinTableName = gTok.text
        joinTableAlias = ""
        ParserAdvance()

        ' Optional alias
        IF gTok.kind = TK_AS THEN
            ParserAdvance()
            IF gTok.kind = TK_IDENTIFIER THEN
                joinTableAlias = gTok.text
                ParserAdvance()
            END IF
        ELSEIF gTok.kind = TK_IDENTIFIER THEN
            maybeJoinAlias = gTok.text
            upperJoinAlias = UCASE$(maybeJoinAlias)
            IF upperJoinAlias <> "ON" AND upperJoinAlias <> "WHERE" AND upperJoinAlias <> "GROUP" AND upperJoinAlias <> "ORDER" AND upperJoinAlias <> "LIMIT" AND upperJoinAlias <> "JOIN" THEN
                joinTableAlias = maybeJoinAlias
                ParserAdvance()
            END IF
        END IF

        ' ON clause (required for INNER/LEFT/RIGHT, not for CROSS)
        hasJoinCondition = 0
        IF gTok.kind = TK_ON THEN
            ParserAdvance()
            LET joinCondition = ParseExpr()
            hasJoinCondition = -1
        ELSEIF joinType <> 0 THEN
            ParserSetError("Expected ON clause for JOIN")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF

        stmt.AddJoin(joinTableName, joinTableAlias, joinType, joinCondition, hasJoinCondition)
    WEND

    ' Optional WHERE
    IF gTok.kind = TK_WHERE THEN
        ParserAdvance()
        LET col = ParseExpr()
        IF gParserHasError = 0 THEN
            stmt.SetWhere(col)
        END IF
    END IF

    ' Optional GROUP BY
    IF gTok.kind = TK_GROUP THEN
        DIM groupExpr AS Expr
        ParserAdvance()
        IF ParserExpect(TK_BY) = 0 THEN
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        ' Parse GROUP BY expressions
        WHILE gParserHasError = 0
            LET groupExpr = ParseExpr()
            IF gParserHasError <> 0 THEN
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            stmt.AddGroupBy(groupExpr)

            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND
    END IF

    ' Optional HAVING clause (after GROUP BY)
    IF gTok.kind = TK_HAVING THEN
        DIM havingExpr AS Expr
        ParserAdvance()
        LET havingExpr = ParseExpr()
        IF gParserHasError = 0 THEN
            LET stmt.havingClause = havingExpr
            stmt.hasHaving = -1
        END IF
    END IF

    ' Optional ORDER BY
    IF gTok.kind = TK_ORDER THEN
        DIM orderExpr AS Expr
        DIM isDesc AS INTEGER
        ParserAdvance()
        IF ParserExpect(TK_BY) = 0 THEN
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        ' Parse ORDER BY expressions
        WHILE gParserHasError = 0
            LET orderExpr = ParseExpr()
            IF gParserHasError <> 0 THEN
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ' Check for ASC or DESC (default is ASC = 0, DESC = 1)
            isDesc = 0
            IF gTok.kind = TK_DESC THEN
                isDesc = 1
                ParserAdvance()
            ELSEIF gTok.kind = TK_ASC THEN
                ParserAdvance()
            END IF
            stmt.AddOrderBy(orderExpr, isDesc)

            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND
    END IF

    ' Optional LIMIT clause
    IF gTok.kind = TK_LIMIT THEN
        ParserAdvance()
        IF gTok.kind = TK_INTEGER THEN
            stmt.limitValue = VAL(gTok.text)
            ParserAdvance()
        ELSE
            ParserSetError("Expected integer after LIMIT")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF

        ' Optional OFFSET after LIMIT
        IF gTok.kind = TK_OFFSET THEN
            ParserAdvance()
            IF gTok.kind = TK_INTEGER THEN
                stmt.offsetValue = VAL(gTok.text)
                ParserAdvance()
            ELSE
                ParserSetError("Expected integer after OFFSET")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
        END IF
    END IF

    ParserMatch(TK_SEMICOLON)
    ParseSelectStmt = stmt
END FUNCTION

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

' Parse UPDATE statement (already past UPDATE token)
FUNCTION ParseUpdateStmt() AS UpdateStmt
    DIM stmt AS UpdateStmt
    DIM colName AS STRING
    DIM val AS Expr

    LET stmt = NEW UpdateStmt()
    stmt.Init()

    ' Get table name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseUpdateStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

    ' Expect SET
    IF ParserExpect(TK_SET) = 0 THEN
        ParseUpdateStmt = stmt
        EXIT FUNCTION
    END IF

    ' Parse SET column = value pairs
    WHILE gParserHasError = 0
        IF gTok.kind <> TK_IDENTIFIER THEN
            ParserSetError("Expected column name")
            ParseUpdateStmt = stmt
            EXIT FUNCTION
        END IF
        colName = gTok.text
        ParserAdvance()

        IF ParserExpect(TK_EQ) = 0 THEN
            ParseUpdateStmt = stmt
            EXIT FUNCTION
        END IF

        LET val = ParseExpr()
        IF gParserHasError <> 0 THEN
            ParseUpdateStmt = stmt
            EXIT FUNCTION
        END IF

        stmt.AddSet(colName, val)

        IF gTok.kind = TK_COMMA THEN
            ParserAdvance()
        ELSE
            EXIT WHILE
        END IF
    WEND

    ' Optional WHERE
    IF gTok.kind = TK_WHERE THEN
        ParserAdvance()
        LET val = ParseExpr()
        IF gParserHasError = 0 THEN
            stmt.SetWhere(val)
        END IF
    END IF

    ParserMatch(TK_SEMICOLON)
    ParseUpdateStmt = stmt
END FUNCTION

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

' Parse DELETE statement (already past DELETE token)
FUNCTION ParseDeleteStmt() AS DeleteStmt
    DIM stmt AS DeleteStmt
    DIM val AS Expr

    LET stmt = NEW DeleteStmt()
    stmt.Init()

    ' Expect FROM
    IF ParserExpect(TK_FROM) = 0 THEN
        ParseDeleteStmt = stmt
        EXIT FUNCTION
    END IF

    ' Get table name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseDeleteStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

    ' Optional WHERE
    IF gTok.kind = TK_WHERE THEN
        ParserAdvance()
        LET val = ParseExpr()
        IF gParserHasError = 0 THEN
            stmt.SetWhere(val)
        END IF
    END IF

    ParserMatch(TK_SEMICOLON)
    ParseDeleteStmt = stmt
END FUNCTION

'=============================================================================
' CREATE INDEX STATEMENT
'=============================================================================

' Parse CREATE INDEX statement (past CREATE token, now at UNIQUE or INDEX)
FUNCTION ParseCreateIndexStmt() AS CreateIndexStmt
    DIM stmt AS CreateIndexStmt
    DIM colName AS STRING

    LET stmt = NEW CreateIndexStmt()
    stmt.Init()

    ' Check for UNIQUE keyword
    IF gTok.kind = TK_UNIQUE THEN
        stmt.isUnique = -1
        ParserAdvance()
    END IF

    ' Expect INDEX keyword
    IF ParserExpect(TK_INDEX) = 0 THEN
        ParseCreateIndexStmt = stmt
        EXIT FUNCTION
    END IF

    ' Get index name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected index name")
        ParseCreateIndexStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.indexName = gTok.text
    ParserAdvance()

    ' Expect ON keyword
    IF ParserExpect(TK_ON) = 0 THEN
        ParseCreateIndexStmt = stmt
        EXIT FUNCTION
    END IF

    ' Get table name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseCreateIndexStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

    ' Expect opening parenthesis
    IF ParserExpect(TK_LPAREN) = 0 THEN
        ParseCreateIndexStmt = stmt
        EXIT FUNCTION
    END IF

    ' Parse column list
    WHILE gTok.kind <> TK_RPAREN AND gParserHasError = 0
        IF gTok.kind <> TK_IDENTIFIER THEN
            ParserSetError("Expected column name in index")
            ParseCreateIndexStmt = stmt
            EXIT FUNCTION
        END IF
        colName = gTok.text
        stmt.AddColumn(colName)
        ParserAdvance()

        ' Check for ASC/DESC (ignore for now)
        IF gTok.kind = TK_ASC OR gTok.kind = TK_DESC THEN
            ParserAdvance()
        END IF

        IF gTok.kind = TK_COMMA THEN
            ParserAdvance()
        ELSE
            EXIT WHILE
        END IF
    WEND

    ' Expect closing parenthesis
    IF ParserExpect(TK_RPAREN) = 0 THEN
        ParseCreateIndexStmt = stmt
        EXIT FUNCTION
    END IF

    ParserMatch(TK_SEMICOLON)
    ParseCreateIndexStmt = stmt
END FUNCTION

'=============================================================================
' DROP INDEX STATEMENT
'=============================================================================

' Parse DROP INDEX statement (past DROP token, now at INDEX)
FUNCTION ParseDropIndexStmt() AS DropIndexStmt
    DIM stmt AS DropIndexStmt

    LET stmt = NEW DropIndexStmt()
    stmt.Init()

    ' Already past DROP, expect INDEX
    IF ParserExpect(TK_INDEX) = 0 THEN
        ParseDropIndexStmt = stmt
        EXIT FUNCTION
    END IF

    ' Get index name
    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected index name")
        ParseDropIndexStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.indexName = gTok.text
    ParserAdvance()

    ParserMatch(TK_SEMICOLON)
    ParseDropIndexStmt = stmt
END FUNCTION

