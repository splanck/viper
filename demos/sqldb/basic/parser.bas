' parser.bas - SQL Parser
' Part of SQLite Clone - Viper Basic Implementation
' Requires: lexer.bas, stmt.bas (AddFile before this)

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

'=============================================================================
' EXPRESSION PARSING
'=============================================================================

FUNCTION ParsePrimaryExpr() AS Expr
    DIM kind AS INTEGER
    DIM text AS STRING
    DIM name AS STRING
    DIM colName AS STRING
    DIM sv AS SqlValue
    DIM e AS Expr

    LET kind = gTok.kind

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

    IF kind = TK_NUMBER THEN
        LET text = gTok.text
        ParserAdvance()
        LET e = ExprReal(0.0, text)
        ParsePrimaryExpr = e
        EXIT FUNCTION
    END IF

    IF kind = TK_STRING THEN
        LET text = gTok.text
        ParserAdvance()
        ParsePrimaryExpr = ExprText(text)
        EXIT FUNCTION
    END IF

    IF kind = TK_NULL THEN
        ParserAdvance()
        ParsePrimaryExpr = ExprNull()
        EXIT FUNCTION
    END IF

    IF kind = TK_IDENTIFIER THEN
        LET name = gTok.text
        ParserAdvance()

        IF gTok.kind = TK_LPAREN THEN
            DIM funcExpr AS Expr
            DIM argExpr AS Expr
            DIM argText AS STRING
            DIM argInt AS INTEGER

            ParserAdvance()
            LET funcExpr = NEW Expr()
            funcExpr.Init()
            funcExpr.InitFunction(name)

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

                IF gTok.kind = TK_COMMA THEN
                    ParserAdvance()
                END IF
            WEND

            IF gTok.kind = TK_RPAREN THEN
                ParserAdvance()
            END IF

            ParsePrimaryExpr = funcExpr
            EXIT FUNCTION
        END IF

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

    IF kind = TK_STAR THEN
        ParserAdvance()
        ParsePrimaryExpr = ExprStar()
        EXIT FUNCTION
    END IF

    IF kind = TK_LPAREN THEN
        DIM subquerySql AS STRING
        DIM depth AS INTEGER
        DIM innerExpr AS Expr

        ParserAdvance()

        IF gTok.kind = TK_SELECT THEN
            subquerySql = "SELECT"
            ParserAdvance()
            depth = 1

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
                    subquerySql = subquerySql + " " + gTok.text
                END IF
                ParserAdvance()
            WEND

            ParsePrimaryExpr = ExprSubquery(subquerySql)
            EXIT FUNCTION
        END IF

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
    IF kind = TK_IN THEN
        DIM right AS Expr
        ParserAdvance()
        IF gTok.kind <> TK_LPAREN THEN
            ParserSetError("Expected '(' after IN")
            ParseCompExpr = left
            EXIT FUNCTION
        END IF
        LET right = ParsePrimaryExpr()
        ParseCompExpr = ExprBinary(OP_IN, left, right)
        EXIT FUNCTION
    END IF
    ParseCompExpr = left
END FUNCTION

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

FUNCTION ParseExpr() AS Expr
    ParseExpr = ParseOrExpr()
END FUNCTION

'=============================================================================
' COLUMN AND CREATE TABLE PARSING
'=============================================================================

FUNCTION ParseColumnDef() AS SqlColumn
    DIM col AS SqlColumn

    LET col = NEW SqlColumn()
    col.Init("", SQL_TEXT)

    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected column name")
        ParseColumnDef = col
        EXIT FUNCTION
    END IF
    col.name = gTok.text
    ParserAdvance()

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
        ELSE
            EXIT WHILE
        END IF
    WEND

    ParseColumnDef = col
END FUNCTION

FUNCTION ParseCreateTableStmt() AS CreateTableStmt
    DIM stmt AS CreateTableStmt
    DIM col AS SqlColumn

    LET stmt = NEW CreateTableStmt()
    stmt.Init()

    IF ParserExpect(TK_TABLE) = 0 THEN
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF

    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

    IF ParserExpect(TK_LPAREN) = 0 THEN
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF

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

    IF ParserExpect(TK_RPAREN) = 0 THEN
        ParseCreateTableStmt = stmt
        EXIT FUNCTION
    END IF

    ParserMatch(TK_SEMICOLON)
    ParseCreateTableStmt = stmt
END FUNCTION

'=============================================================================
' INSERT PARSING
'=============================================================================

FUNCTION ParseInsertStmt() AS InsertStmt
    DIM stmt AS InsertStmt
    DIM rowIdx AS INTEGER
    DIM val AS Expr

    LET stmt = NEW InsertStmt()
    stmt.Init()

    IF ParserExpect(TK_INTO) = 0 THEN
        ParseInsertStmt = stmt
        EXIT FUNCTION
    END IF

    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseInsertStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

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

    IF ParserExpect(TK_VALUES) = 0 THEN
        ParseInsertStmt = stmt
        EXIT FUNCTION
    END IF

    WHILE gParserHasError = 0
        IF ParserExpect(TK_LPAREN) = 0 THEN
            ParseInsertStmt = stmt
            EXIT FUNCTION
        END IF

        stmt.AddValueRow()
        rowIdx = stmt.rowCount - 1

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
' SELECT PARSING (Large function - extracted from original)
'=============================================================================

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

            ' Handle column alias
            IF gTok.kind = TK_AS THEN
                ParserAdvance()
                IF gTok.kind = TK_IDENTIFIER THEN
                    ParserAdvance()
                END IF
            ELSEIF gTok.kind = TK_IDENTIFIER THEN
                maybeColAlias = gTok.text
                upperColAlias = UCASE$(maybeColAlias)
                IF upperColAlias <> "FROM" AND upperColAlias <> "WHERE" AND upperColAlias <> "GROUP" AND upperColAlias <> "ORDER" AND upperColAlias <> "LIMIT" AND upperColAlias <> "HAVING" THEN
                    ParserAdvance()
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

    ' Check for derived table (subquery in FROM)
    IF gTok.kind = TK_LPAREN THEN
        ParserAdvance()
        IF gTok.kind = TK_SELECT THEN
            DIM parenDepth AS INTEGER
            DIM subquerySQL AS STRING
            parenDepth = 1
            subquerySQL = "SELECT"
            ParserAdvance()

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
        ELSE
            ParserSetError("Expected SELECT in derived table")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
    ELSE
        ' Get first table name
        IF gTok.kind <> TK_IDENTIFIER THEN
            ParserSetError("Expected table name")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        DIM firstTableName AS STRING
        DIM firstTableAlias AS STRING
        firstTableName = gTok.text
        firstTableAlias = ""
        stmt.tableName = firstTableName
        ParserAdvance()

        ' Optional table alias
        IF gTok.kind = TK_AS THEN
            ParserAdvance()
            IF gTok.kind = TK_IDENTIFIER THEN
                firstTableAlias = gTok.text
                stmt.tableAlias = firstTableAlias
                ParserAdvance()
            END IF
        ELSEIF gTok.kind = TK_IDENTIFIER THEN
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

        stmt.AddTable(firstTableName, firstTableAlias)
    END IF

    ' Parse additional tables (comma-separated for CROSS JOIN)
    DIM extraTableName AS STRING
    DIM extraTableAlias AS STRING
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

        IF gTok.kind = TK_AS THEN
            ParserAdvance()
            IF gTok.kind = TK_IDENTIFIER THEN
                extraTableAlias = gTok.text
                ParserAdvance()
            END IF
        ELSEIF gTok.kind = TK_IDENTIFIER THEN
            DIM maybeAlias AS STRING
            DIM upperMaybe AS STRING
            maybeAlias = gTok.text
            upperMaybe = UCASE$(maybeAlias)
            IF upperMaybe <> "WHERE" AND upperMaybe <> "GROUP" AND upperMaybe <> "ORDER" AND upperMaybe <> "LIMIT" AND upperMaybe <> "HAVING" AND upperMaybe <> "JOIN" THEN
                extraTableAlias = maybeAlias
                ParserAdvance()
            END IF
        END IF

        stmt.AddTable(extraTableName, extraTableAlias)
    WEND

    ' Parse JOIN clauses
    DIM joinType AS INTEGER
    DIM joinTableName AS STRING
    DIM joinTableAlias AS STRING
    DIM joinCondition AS Expr
    DIM hasJoinCondition AS INTEGER

    WHILE gTok.kind = TK_JOIN OR gTok.kind = TK_INNER OR gTok.kind = TK_LEFT OR gTok.kind = TK_RIGHT OR gTok.kind = TK_FULL OR gTok.kind = TK_CROSS
        joinType = 1

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
            IF gTok.kind = TK_OUTER THEN ParserAdvance()
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after LEFT")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_RIGHT THEN
            joinType = 3
            ParserAdvance()
            IF gTok.kind = TK_OUTER THEN ParserAdvance()
            IF gTok.kind <> TK_JOIN THEN
                ParserSetError("Expected JOIN after RIGHT")
                ParseSelectStmt = stmt
                EXIT FUNCTION
            END IF
            ParserAdvance()
        ELSEIF gTok.kind = TK_FULL THEN
            joinType = 4
            ParserAdvance()
            IF gTok.kind = TK_OUTER THEN ParserAdvance()
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
            joinType = 1
            ParserAdvance()
        END IF

        IF gTok.kind <> TK_IDENTIFIER THEN
            ParserSetError("Expected table name after JOIN")
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        joinTableName = gTok.text
        joinTableAlias = ""
        ParserAdvance()

        IF gTok.kind = TK_AS THEN
            ParserAdvance()
            IF gTok.kind = TK_IDENTIFIER THEN
                joinTableAlias = gTok.text
                ParserAdvance()
            END IF
        ELSEIF gTok.kind = TK_IDENTIFIER THEN
            DIM maybeJoinAlias AS STRING
            DIM upperJoinAlias AS STRING
            maybeJoinAlias = gTok.text
            upperJoinAlias = UCASE$(maybeJoinAlias)
            IF upperJoinAlias <> "ON" AND upperJoinAlias <> "WHERE" AND upperJoinAlias <> "GROUP" AND upperJoinAlias <> "ORDER" AND upperJoinAlias <> "LIMIT" AND upperJoinAlias <> "JOIN" THEN
                joinTableAlias = maybeJoinAlias
                ParserAdvance()
            END IF
        END IF

        hasJoinCondition = 0
        IF gTok.kind = TK_ON THEN
            ParserAdvance()
            LET joinCondition = ParseExpr()
            hasJoinCondition = -1
        END IF

        stmt.AddJoin(joinTableName, joinTableAlias, joinType, joinCondition, hasJoinCondition)
    WEND

    ' WHERE clause
    IF gTok.kind = TK_WHERE THEN
        ParserAdvance()
        LET col = ParseExpr()
        IF gParserHasError = 0 THEN
            stmt.SetWhere(col)
        END IF
    END IF

    ' GROUP BY
    IF gTok.kind = TK_GROUP THEN
        ParserAdvance()
        IF ParserExpect(TK_BY) = 0 THEN
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        WHILE gParserHasError = 0
            LET col = ParseExpr()
            IF gParserHasError <> 0 THEN EXIT WHILE
            stmt.AddGroupBy(col)
            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND
    END IF

    ' HAVING
    IF gTok.kind = TK_HAVING THEN
        ParserAdvance()
        LET col = ParseExpr()
        IF gParserHasError = 0 THEN
            stmt.havingClause = col
            stmt.hasHaving = -1
        END IF
    END IF

    ' ORDER BY
    IF gTok.kind = TK_ORDER THEN
        ParserAdvance()
        IF ParserExpect(TK_BY) = 0 THEN
            ParseSelectStmt = stmt
            EXIT FUNCTION
        END IF
        WHILE gParserHasError = 0
            LET col = ParseExpr()
            IF gParserHasError <> 0 THEN EXIT WHILE
            DIM isDesc AS INTEGER
            isDesc = 0
            IF gTok.kind = TK_ASC THEN
                ParserAdvance()
            ELSEIF gTok.kind = TK_DESC THEN
                isDesc = -1
                ParserAdvance()
            END IF
            stmt.AddOrderBy(col, isDesc)
            IF gTok.kind = TK_COMMA THEN
                ParserAdvance()
            ELSE
                EXIT WHILE
            END IF
        WEND
    END IF

    ' LIMIT
    IF gTok.kind = TK_LIMIT THEN
        ParserAdvance()
        IF gTok.kind = TK_INTEGER THEN
            stmt.limitValue = VAL(gTok.text)
            ParserAdvance()
        END IF
    END IF

    ' OFFSET
    IF gTok.kind = TK_OFFSET THEN
        ParserAdvance()
        IF gTok.kind = TK_INTEGER THEN
            stmt.offsetValue = VAL(gTok.text)
            ParserAdvance()
        END IF
    END IF

    ParserMatch(TK_SEMICOLON)
    ParseSelectStmt = stmt
END FUNCTION

'=============================================================================
' UPDATE PARSING
'=============================================================================

FUNCTION ParseUpdateStmt() AS UpdateStmt
    DIM stmt AS UpdateStmt
    DIM colName AS STRING
    DIM val AS Expr

    LET stmt = NEW UpdateStmt()
    stmt.Init()

    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseUpdateStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

    IF ParserExpect(TK_SET) = 0 THEN
        ParseUpdateStmt = stmt
        EXIT FUNCTION
    END IF

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
' DELETE PARSING
'=============================================================================

FUNCTION ParseDeleteStmt() AS DeleteStmt
    DIM stmt AS DeleteStmt
    DIM val AS Expr

    LET stmt = NEW DeleteStmt()
    stmt.Init()

    IF ParserExpect(TK_FROM) = 0 THEN
        ParseDeleteStmt = stmt
        EXIT FUNCTION
    END IF

    IF gTok.kind <> TK_IDENTIFIER THEN
        ParserSetError("Expected table name")
        ParseDeleteStmt = stmt
        EXIT FUNCTION
    END IF
    stmt.tableName = gTok.text
    ParserAdvance()

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
