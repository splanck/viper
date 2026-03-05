' test_split.bas - Test that AddFile split works
' Part of SQLite Clone - Viper Basic Implementation

AddFile "token.bas"
AddFile "lexer.bas"
AddFile "types.bas"
AddFile "schema.bas"
AddFile "table.bas"
AddFile "expr.bas"
AddFile "stmt.bas"
AddFile "parser.bas"

PRINT "=== Testing Module Split ==="

' Test token constants
PRINT "TK_SELECT ="; TK_SELECT
PRINT "TK_FROM ="; TK_FROM

' Test lexer
LexerInit("SELECT * FROM users")
LexerNextToken()
PRINT "First token kind:"; gTok.kind; " text: "; gTok.text

' Test SqlValue
DIM val AS SqlValue
LET val = SqlInteger(42)
PRINT "SqlValue:"; val.ToString$()

' Test SqlColumn
DIM col AS SqlColumn
LET col = MakeColumn("id", SQL_INTEGER)
PRINT "Column:"; col.ToString$()

' Test SqlRow
DIM row AS SqlRow
LET row = MakeRow(2)
row.SetValue(0, SqlInteger(1))
row.SetValue(1, SqlText("hello"))
PRINT "Row:"; row.ToString$()

' Test SqlTable
DIM tbl AS SqlTable
LET tbl = MakeTable("test")
tbl.AddColumn(col)
PRINT "Table:"; tbl.ToString$()

' Test Expr
DIM e AS Expr
LET e = ExprInt(123)
PRINT "Expr:"; e.ToString$()

' Test Parser
PRINT ""
PRINT "Testing Parser..."
ParserInit("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)")
IF gTok.kind = TK_CREATE THEN
    ParserAdvance()  ' Move past CREATE
    DIM createStmt AS CreateTableStmt
    LET createStmt = ParseCreateTableStmt()
    IF gParserHasError = 0 THEN
        PRINT "Parsed CREATE TABLE:"; createStmt.tableName
        PRINT "Column count:"; createStmt.columnCount
    ELSE
        PRINT "Parse error:"; gParserError
    END IF
END IF

PRINT ""
PRINT "Testing INSERT parsing..."
ParserInit("INSERT INTO users (id, name) VALUES (1, 'Alice')")
IF gTok.kind = TK_INSERT THEN
    ParserAdvance()  ' Move past INSERT
    DIM insertStmt AS InsertStmt
    LET insertStmt = ParseInsertStmt()
    IF gParserHasError = 0 THEN
        PRINT "Parsed INSERT INTO:"; insertStmt.tableName
        PRINT "Row count:"; insertStmt.rowCount
    ELSE
        PRINT "Parse error:"; gParserError
    END IF
END IF

PRINT "=== All tests passed! ==="
