' expr.bas - Expression Types
' Part of SQLite Clone - Viper Basic Implementation

AddFile "schema.bas"

'=============================================================================
' EXPRESSION TYPES
'=============================================================================

' Expression kind constants
CONST EXPR_LITERAL = 1
CONST EXPR_COLUMN = 2
CONST EXPR_BINARY = 3
CONST EXPR_UNARY = 4
CONST EXPR_FUNCTION = 5
CONST EXPR_STAR = 6
CONST EXPR_SUBQUERY = 7

' Binary operators
CONST OP_ADD = 1
CONST OP_SUB = 2
CONST OP_MUL = 3
CONST OP_DIV = 4
CONST OP_MOD = 5
CONST OP_EQ = 10
CONST OP_NE = 11
CONST OP_LT = 12
CONST OP_LE = 13
CONST OP_GT = 14
CONST OP_GE = 15
CONST OP_AND = 20
CONST OP_OR = 21
CONST OP_LIKE = 22
CONST OP_IN = 23
CONST OP_IS = 24
CONST OP_CONCAT = 25

' Unary operators
CONST UOP_NEG = 1
CONST UOP_NOT = 2

' Maximum args for function calls
CONST MAX_ARGS = 16

CLASS Expr
    PUBLIC kind AS INTEGER         ' EXPR_LITERAL, EXPR_COLUMN, etc.

    ' For EXPR_LITERAL
    PUBLIC literalValue AS SqlValue

    ' For EXPR_COLUMN
    PUBLIC tableName AS STRING     ' Optional table alias
    PUBLIC columnName AS STRING

    ' For EXPR_BINARY and EXPR_UNARY
    PUBLIC op AS INTEGER           ' Operator constant

    ' For EXPR_FUNCTION
    PUBLIC funcName AS STRING

    ' Children (binary: left=args(0), right=args(1); unary: operand=args(0); func: arguments)
    PUBLIC args(MAX_ARGS) AS Expr
    PUBLIC argCount AS INTEGER

    ' For EXPR_SUBQUERY
    PUBLIC subquerySQL AS STRING

    PUBLIC SUB Init()
        kind = EXPR_LITERAL
        LET literalValue = NEW SqlValue()
        literalValue.InitNull()
        tableName = ""
        columnName = ""
        op = 0
        funcName = ""
        argCount = 0
        subquerySQL = ""
    END SUB

    PUBLIC SUB InitLiteral(val AS SqlValue)
        kind = EXPR_LITERAL
        LET literalValue = val
    END SUB

    PUBLIC SUB InitColumn(tbl AS STRING, col AS STRING)
        kind = EXPR_COLUMN
        tableName = tbl
        columnName = col
    END SUB

    PUBLIC SUB InitBinary(operator AS INTEGER, left AS Expr, right AS Expr)
        kind = EXPR_BINARY
        op = operator
        LET args(0) = left
        LET args(1) = right
        argCount = 2
    END SUB

    PUBLIC SUB InitUnary(operator AS INTEGER, operand AS Expr)
        kind = EXPR_UNARY
        op = operator
        LET args(0) = operand
        argCount = 1
    END SUB

    PUBLIC SUB InitFunction(name AS STRING)
        kind = EXPR_FUNCTION
        funcName = name
        argCount = 0
    END SUB

    PUBLIC SUB InitStar()
        kind = EXPR_STAR
    END SUB

    PUBLIC SUB InitSubquery(sql AS STRING)
        kind = EXPR_SUBQUERY
        subquerySQL = sql
    END SUB

    PUBLIC SUB AddArg(arg AS Expr)
        IF argCount < MAX_ARGS THEN
            LET args(argCount) = arg
            argCount = argCount + 1
        END IF
    END SUB

    PUBLIC FUNCTION GetLeft() AS Expr
        GetLeft = args(0)
    END FUNCTION

    PUBLIC FUNCTION GetRight() AS Expr
        GetRight = args(1)
    END FUNCTION

    PUBLIC FUNCTION GetOperand() AS Expr
        GetOperand = args(0)
    END FUNCTION

    PUBLIC FUNCTION IsLiteral() AS INTEGER
        IF kind = EXPR_LITERAL THEN
            IsLiteral = -1
        ELSE
            IsLiteral = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsColumn() AS INTEGER
        IF kind = EXPR_COLUMN THEN
            IsColumn = -1
        ELSE
            IsColumn = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsBinary() AS INTEGER
        IF kind = EXPR_BINARY THEN
            IsBinary = -1
        ELSE
            IsBinary = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsUnary() AS INTEGER
        IF kind = EXPR_UNARY THEN
            IsUnary = -1
        ELSE
            IsUnary = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsFunction() AS INTEGER
        IF kind = EXPR_FUNCTION THEN
            IsFunction = -1
        ELSE
            IsFunction = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION IsStar() AS INTEGER
        IF kind = EXPR_STAR THEN
            IsStar = -1
        ELSE
            IsStar = 0
        END IF
    END FUNCTION

    PUBLIC FUNCTION ToString$()
        DIM result AS STRING
        DIM i AS INTEGER

        IF kind = EXPR_LITERAL THEN
            ToString$ = literalValue.ToString$()
        ELSEIF kind = EXPR_COLUMN THEN
            IF tableName <> "" THEN
                ToString$ = tableName + "." + columnName
            ELSE
                ToString$ = columnName
            END IF
        ELSEIF kind = EXPR_STAR THEN
            ToString$ = "*"
        ELSEIF kind = EXPR_BINARY THEN
            result = "(" + args(0).ToString$() + " " + OpToString$(op) + " " + args(1).ToString$() + ")"
            ToString$ = result
        ELSEIF kind = EXPR_UNARY THEN
            IF op = UOP_NEG THEN
                ToString$ = "-" + args(0).ToString$()
            ELSEIF op = UOP_NOT THEN
                ToString$ = "NOT " + args(0).ToString$()
            ELSE
                ToString$ = "?" + args(0).ToString$()
            END IF
        ELSEIF kind = EXPR_FUNCTION THEN
            result = funcName + "("
            FOR i = 0 TO argCount - 1
                IF i > 0 THEN
                    result = result + ", "
                END IF
                result = result + args(i).ToString$()
            NEXT i
            result = result + ")"
            ToString$ = result
        ELSEIF kind = EXPR_SUBQUERY THEN
            ToString$ = "(" + subquerySQL + ")"
        ELSE
            ToString$ = "<?>"
        END IF
    END FUNCTION
END CLASS

' Helper function to convert operator to string
FUNCTION OpToString$(op AS INTEGER)
    IF op = OP_ADD THEN
        OpToString$ = "+"
    ELSEIF op = OP_SUB THEN
        OpToString$ = "-"
    ELSEIF op = OP_MUL THEN
        OpToString$ = "*"
    ELSEIF op = OP_DIV THEN
        OpToString$ = "/"
    ELSEIF op = OP_MOD THEN
        OpToString$ = "%"
    ELSEIF op = OP_EQ THEN
        OpToString$ = "="
    ELSEIF op = OP_NE THEN
        OpToString$ = "<>"
    ELSEIF op = OP_LT THEN
        OpToString$ = "<"
    ELSEIF op = OP_LE THEN
        OpToString$ = "<="
    ELSEIF op = OP_GT THEN
        OpToString$ = ">"
    ELSEIF op = OP_GE THEN
        OpToString$ = ">="
    ELSEIF op = OP_AND THEN
        OpToString$ = "AND"
    ELSEIF op = OP_OR THEN
        OpToString$ = "OR"
    ELSEIF op = OP_LIKE THEN
        OpToString$ = "LIKE"
    ELSEIF op = OP_CONCAT THEN
        OpToString$ = "||"
    ELSE
        OpToString$ = "?"
    END IF
END FUNCTION

' Factory functions for creating expressions
FUNCTION ExprNull() AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitLiteral(SqlNull())
    ExprNull = e
END FUNCTION

FUNCTION ExprInt(val AS INTEGER) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitLiteral(SqlInteger(val))
    ExprInt = e
END FUNCTION

FUNCTION ExprReal(val AS SINGLE, txt AS STRING) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitLiteral(SqlReal(val, txt))
    ExprReal = e
END FUNCTION

FUNCTION ExprText(val AS STRING) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitLiteral(SqlText(val))
    ExprText = e
END FUNCTION

FUNCTION ExprColumn(col AS STRING) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitColumn("", col)
    ExprColumn = e
END FUNCTION

FUNCTION ExprTableColumn(tbl AS STRING, col AS STRING) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitColumn(tbl, col)
    ExprTableColumn = e
END FUNCTION

FUNCTION ExprStar() AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitStar()
    ExprStar = e
END FUNCTION

FUNCTION ExprBinary(operator AS INTEGER, left AS Expr, right AS Expr) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitBinary(operator, left, right)
    ExprBinary = e
END FUNCTION

FUNCTION ExprUnary(operator AS INTEGER, operand AS Expr) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitUnary(operator, operand)
    ExprUnary = e
END FUNCTION

FUNCTION ExprFunc(name AS STRING) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitFunction(name)
    ExprFunc = e
END FUNCTION

FUNCTION ExprSubquery(sql AS STRING) AS Expr
    DIM e AS Expr
    LET e = NEW Expr()
    e.Init()
    e.InitSubquery(sql)
    ExprSubquery = e
END FUNCTION

