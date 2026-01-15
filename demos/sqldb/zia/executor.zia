// executor.viper - SQL Executor with proper OOP design
// Part of SQLite Clone - ViperLang Implementation

module executor;

import "./types";
import "./schema";
import "./table";
import "./index";
import "./expr";
import "./stmt";
import "./parser";
import "./database";
import "./result";

//=============================================================================
// EXECUTOR ENTITY
//=============================================================================

entity Executor {
    expose Database db;
    expose IndexManager indexMgr;

    // Context for correlated subqueries
    hide Row? outerRow;
    hide Table? outerTable;
    hide String outerTableAlias;
    hide String currentTableAlias;

    expose func init() {
        db = new Database();
        db.init();
        indexMgr = new IndexManager();
        indexMgr.init();
        outerRow = null;
        outerTable = null;
        outerTableAlias = "";
        currentTableAlias = "";
    }

    //=========================================================================
    // EXPRESSION EVALUATION
    //=========================================================================

    expose func evalExpr(expr: Expr, row: Row, table: Table) -> SqlValue {
        if expr.kind == EXPR_LITERAL {
            return expr.literalValue;
        }
        if expr.kind == EXPR_COLUMN {
            return evalColumn(expr, row, table);
        }
        if expr.kind == EXPR_BINARY {
            return evalBinary(expr, row, table);
        }
        if expr.kind == EXPR_UNARY {
            return evalUnary(expr, row, table);
        }
        if expr.kind == EXPR_FUNCTION {
            return evalFunction(expr, row, table);
        }
        if expr.kind == EXPR_SUBQUERY {
            return evalSubquery(expr, row, table);
        }
        return sqlNull();
    }

    // Evaluate a scalar subquery
    expose func evalSubquery(expr: Expr, outerRowContext: Row, outerTableContext: Table) -> SqlValue {
        // Set outer context for correlated subqueries
        var savedOuterRow = outerRow;
        var savedOuterTable = outerTable;
        var savedOuterAlias = outerTableAlias;

        outerRow = outerRowContext;
        outerTable = outerTableContext;
        outerTableAlias = currentTableAlias;

        // Execute the subquery
        var result = executeSql(expr.subquerySQL);

        // Restore outer context
        outerRow = savedOuterRow;
        outerTable = savedOuterTable;
        outerTableAlias = savedOuterAlias;

        // Return scalar value from first row, first column
        if result.success && result.rowCount() > 0 {
            var firstRow = result.getRow(0);
            if firstRow != null {
                var r = firstRow;
                if r.columnCount() > 0 {
                    return r.getValue(0);
                }
            }
        }
        return sqlNull();
    }

    expose func evalColumn(expr: Expr, row: Row, table: Table) -> SqlValue {
        // Check outer context for correlated subqueries
        if expr.tableName != "" && outerTableAlias != "" {
            if expr.tableName == outerTableAlias {
                if outerTable != null && outerRow != null {
                    var ot = outerTable;
                    var orow = outerRow;
                    var outerColIdx = ot.findColumnIndex(expr.columnName);
                    if outerColIdx >= 0 {
                        return orow.getValue(outerColIdx);
                    }
                }
                return sqlNull();
            }
        }

        var colIdx = table.findColumnIndex(expr.columnName);
        if colIdx < 0 {
            // Try outer context
            if expr.tableName == "" && outerTable != null && outerRow != null {
                var ot = outerTable;
                var orow = outerRow;
                var outerColIdx = ot.findColumnIndex(expr.columnName);
                if outerColIdx >= 0 {
                    return orow.getValue(outerColIdx);
                }
            }
            return sqlNull();
        }
        return row.getValue(colIdx);
    }

    expose func evalBinary(expr: Expr, row: Row, table: Table) -> SqlValue {
        var left = evalExpr(expr.getLeft(), row, table);
        var right = evalExpr(expr.getRight(), row, table);
        var op = expr.op;

        // Arithmetic
        if op == OP_ADD {
            if left.kind == SQL_INTEGER && right.kind == SQL_INTEGER {
                return sqlInteger(left.intValue + right.intValue);
            }
        }
        if op == OP_SUB {
            if left.kind == SQL_INTEGER && right.kind == SQL_INTEGER {
                return sqlInteger(left.intValue - right.intValue);
            }
        }
        if op == OP_MUL {
            if left.kind == SQL_INTEGER && right.kind == SQL_INTEGER {
                return sqlInteger(left.intValue * right.intValue);
            }
        }
        if op == OP_DIV {
            if left.kind == SQL_INTEGER && right.kind == SQL_INTEGER {
                if right.intValue != 0 {
                    return sqlInteger(left.intValue / right.intValue);
                }
            }
        }

        // Comparison - return 1 for true, 0 for false
        if op == OP_EQ {
            if left.compare(right) == 0 {
                return sqlInteger(1);
            }
            return sqlInteger(0);
        }
        if op == OP_NE {
            if left.compare(right) != 0 {
                return sqlInteger(1);
            }
            return sqlInteger(0);
        }
        if op == OP_LT {
            if left.compare(right) < 0 {
                return sqlInteger(1);
            }
            return sqlInteger(0);
        }
        if op == OP_LE {
            if left.compare(right) <= 0 {
                return sqlInteger(1);
            }
            return sqlInteger(0);
        }
        if op == OP_GT {
            if left.compare(right) > 0 {
                return sqlInteger(1);
            }
            return sqlInteger(0);
        }
        if op == OP_GE {
            if left.compare(right) >= 0 {
                return sqlInteger(1);
            }
            return sqlInteger(0);
        }

        // Logical
        if op == OP_AND {
            if left.intValue != 0 && right.intValue != 0 {
                return sqlInteger(1);
            }
            return sqlInteger(0);
        }
        if op == OP_OR {
            if left.intValue != 0 || right.intValue != 0 {
                return sqlInteger(1);
            }
            return sqlInteger(0);
        }

        // IN operator - right side is a subquery
        if op == OP_IN {
            var rightExpr = expr.getRight();
            if rightExpr.kind == EXPR_SUBQUERY {
                // Execute subquery and check if left value is in results
                var subResult = executeSql(rightExpr.subquerySQL);
                if subResult.success {
                    var i = 0;
                    while i < subResult.rowCount() {
                        var subRow = subResult.getRow(i);
                        if subRow != null {
                            var sr = subRow;
                            if sr.columnCount() > 0 {
                                var subVal = sr.getValue(0);
                                if left.compare(subVal) == 0 {
                                    return sqlInteger(1);
                                }
                            }
                        }
                        i = i + 1;
                    }
                }
            }
            return sqlInteger(0);
        }

        // String concatenation
        if op == OP_CONCAT {
            var leftStr = "";
            var rightStr = "";
            if left.kind == SQL_TEXT {
                leftStr = left.textValue;
            } else if left.kind == SQL_INTEGER {
                leftStr = Viper.Fmt.Int(left.intValue);
            }
            if right.kind == SQL_TEXT {
                rightStr = right.textValue;
            } else if right.kind == SQL_INTEGER {
                rightStr = Viper.Fmt.Int(right.intValue);
            }
            return sqlText(leftStr + rightStr);
        }

        return sqlNull();
    }

    expose func evalUnary(expr: Expr, row: Row, table: Table) -> SqlValue {
        var operand = evalExpr(expr.getOperand(), row, table);

        if expr.op == OP_NEG {
            if operand.kind == SQL_INTEGER {
                return sqlInteger(-operand.intValue);
            }
        }
        if expr.op == OP_NOT {
            if operand.intValue == 0 {
                return sqlInteger(1);
            }
            return sqlInteger(0);
        }

        return sqlNull();
    }

    expose func evalFunction(expr: Expr, row: Row, table: Table) -> SqlValue {
        var name = Viper.String.ToUpper(expr.funcName);

        // String functions
        if name == "UPPER" && expr.args.count() > 0 {
            var arg = evalExpr(expr.args.get(0), row, table);
            if arg.kind == SQL_TEXT {
                return sqlText(Viper.String.ToUpper(arg.textValue));
            }
        }
        if name == "LOWER" && expr.args.count() > 0 {
            var arg = evalExpr(expr.args.get(0), row, table);
            if arg.kind == SQL_TEXT {
                return sqlText(Viper.String.ToLower(arg.textValue));
            }
        }
        if name == "LENGTH" && expr.args.count() > 0 {
            var arg = evalExpr(expr.args.get(0), row, table);
            if arg.kind == SQL_TEXT {
                return sqlInteger(Viper.String.Length(arg.textValue));
            }
        }

        // SUBSTR(string, start) or SUBSTR(string, start, length)
        if (name == "SUBSTR" || name == "SUBSTRING") && expr.args.count() >= 2 {
            var strArg = evalExpr(expr.args.get(0), row, table);
            var startArg = evalExpr(expr.args.get(1), row, table);
            if strArg.kind == SQL_TEXT && startArg.kind == SQL_INTEGER {
                var str = strArg.textValue;
                var start = startArg.intValue - 1;  // SQL uses 1-based indexing
                if start < 0 { start = 0; }
                var len = Viper.String.Length(str) - start;

                if expr.args.count() >= 3 {
                    var lenArg = evalExpr(expr.args.get(2), row, table);
                    if lenArg.kind == SQL_INTEGER {
                        len = lenArg.intValue;
                    }
                }

                if start >= Viper.String.Length(str) {
                    return sqlText("");
                }
                return sqlText(Viper.String.Substring(str, start, len));
            }
        }

        // TRIM(string) - removes leading and trailing spaces
        if name == "TRIM" && expr.args.count() > 0 {
            var arg = evalExpr(expr.args.get(0), row, table);
            if arg.kind == SQL_TEXT {
                var s = arg.textValue;
                // Trim leading spaces
                var start = 0;
                while start < Viper.String.Length(s) && Viper.String.Substring(s, start, 1) == " " {
                    start = start + 1;
                }
                // Trim trailing spaces
                var end = Viper.String.Length(s);
                while end > start && Viper.String.Substring(s, end - 1, 1) == " " {
                    end = end - 1;
                }
                return sqlText(Viper.String.Substring(s, start, end - start));
            }
        }

        // LTRIM(string) - removes leading spaces
        if name == "LTRIM" && expr.args.count() > 0 {
            var arg = evalExpr(expr.args.get(0), row, table);
            if arg.kind == SQL_TEXT {
                var s = arg.textValue;
                var start = 0;
                while start < Viper.String.Length(s) && Viper.String.Substring(s, start, 1) == " " {
                    start = start + 1;
                }
                return sqlText(Viper.String.Substring(s, start, Viper.String.Length(s) - start));
            }
        }

        // RTRIM(string) - removes trailing spaces
        if name == "RTRIM" && expr.args.count() > 0 {
            var arg = evalExpr(expr.args.get(0), row, table);
            if arg.kind == SQL_TEXT {
                var s = arg.textValue;
                var end = Viper.String.Length(s);
                while end > 0 && Viper.String.Substring(s, end - 1, 1) == " " {
                    end = end - 1;
                }
                return sqlText(Viper.String.Substring(s, 0, end));
            }
        }

        // REPLACE(string, from, to)
        if name == "REPLACE" && expr.args.count() >= 3 {
            var strArg = evalExpr(expr.args.get(0), row, table);
            var fromArg = evalExpr(expr.args.get(1), row, table);
            var toArg = evalExpr(expr.args.get(2), row, table);
            if strArg.kind == SQL_TEXT && fromArg.kind == SQL_TEXT && toArg.kind == SQL_TEXT {
                var str = strArg.textValue;
                var from = fromArg.textValue;
                var to = toArg.textValue;
                // Simple replacement - find and replace all occurrences
                var result = "";
                var i = 0;
                var fromLen = Viper.String.Length(from);
                var strLen = Viper.String.Length(str);
                while i < strLen {
                    if i + fromLen <= strLen && Viper.String.Substring(str, i, fromLen) == from {
                        result = result + to;
                        i = i + fromLen;
                    } else {
                        result = result + Viper.String.Substring(str, i, 1);
                        i = i + 1;
                    }
                }
                return sqlText(result);
            }
        }

        // CONCAT(str1, str2, ...) or string concatenation
        if name == "CONCAT" && expr.args.count() > 0 {
            var result = "";
            var i = 0;
            while i < expr.args.count() {
                var arg = evalExpr(expr.args.get(i), row, table);
                if arg.kind == SQL_TEXT {
                    result = result + arg.textValue;
                } else if arg.kind == SQL_INTEGER {
                    result = result + Viper.Fmt.Int(arg.intValue);
                }
                i = i + 1;
            }
            return sqlText(result);
        }

        // INSTR(string, substring) - find position of substring (1-based, 0 if not found)
        if name == "INSTR" && expr.args.count() >= 2 {
            var strArg = evalExpr(expr.args.get(0), row, table);
            var subArg = evalExpr(expr.args.get(1), row, table);
            if strArg.kind == SQL_TEXT && subArg.kind == SQL_TEXT {
                var str = strArg.textValue;
                var sub = subArg.textValue;
                var subLen = Viper.String.Length(sub);
                var strLen = Viper.String.Length(str);
                var i = 0;
                while i + subLen <= strLen {
                    if Viper.String.Substring(str, i, subLen) == sub {
                        return sqlInteger(i + 1);  // 1-based
                    }
                    i = i + 1;
                }
                return sqlInteger(0);  // Not found
            }
        }

        // Math functions
        if name == "ABS" && expr.args.count() > 0 {
            var arg = evalExpr(expr.args.get(0), row, table);
            if arg.kind == SQL_INTEGER {
                var v = arg.intValue;
                if v < 0 { v = -v; }
                return sqlInteger(v);
            }
        }

        // MOD(a, b) or a % b
        if name == "MOD" && expr.args.count() >= 2 {
            var a = evalExpr(expr.args.get(0), row, table);
            var b = evalExpr(expr.args.get(1), row, table);
            if a.kind == SQL_INTEGER && b.kind == SQL_INTEGER {
                if b.intValue != 0 {
                    return sqlInteger(a.intValue % b.intValue);
                }
            }
        }

        // ROUND(value) - rounds to nearest integer
        if name == "ROUND" && expr.args.count() > 0 {
            var arg = evalExpr(expr.args.get(0), row, table);
            if arg.kind == SQL_INTEGER {
                return arg;  // Already an integer
            }
        }

        // MIN(a, b) - minimum of two values (different from aggregate MIN)
        if name == "MIN" && expr.args.count() == 2 {
            var a = evalExpr(expr.args.get(0), row, table);
            var b = evalExpr(expr.args.get(1), row, table);
            if a.compare(b) <= 0 {
                return a;
            }
            return b;
        }

        // MAX(a, b) - maximum of two values (different from aggregate MAX)
        if name == "MAX" && expr.args.count() == 2 {
            var a = evalExpr(expr.args.get(0), row, table);
            var b = evalExpr(expr.args.get(1), row, table);
            if a.compare(b) >= 0 {
                return a;
            }
            return b;
        }

        // Null handling functions
        if name == "COALESCE" {
            var i = 0;
            while i < expr.args.count() {
                var arg = evalExpr(expr.args.get(i), row, table);
                if arg.kind != SQL_NULL {
                    return arg;
                }
                i = i + 1;
            }
        }

        // IFNULL(expr, value) - returns value if expr is NULL
        if name == "IFNULL" && expr.args.count() >= 2 {
            var arg = evalExpr(expr.args.get(0), row, table);
            if arg.kind == SQL_NULL {
                return evalExpr(expr.args.get(1), row, table);
            }
            return arg;
        }

        // NULLIF(expr1, expr2) - returns NULL if expr1 == expr2
        if name == "NULLIF" && expr.args.count() >= 2 {
            var a = evalExpr(expr.args.get(0), row, table);
            var b = evalExpr(expr.args.get(1), row, table);
            if a.compare(b) == 0 {
                return sqlNull();
            }
            return a;
        }

        // IIF(condition, true_value, false_value) - inline IF
        if name == "IIF" && expr.args.count() >= 3 {
            var cond = evalExpr(expr.args.get(0), row, table);
            if cond.intValue != 0 {
                return evalExpr(expr.args.get(1), row, table);
            }
            return evalExpr(expr.args.get(2), row, table);
        }

        // TYPEOF(expr) - returns the type name
        if name == "TYPEOF" && expr.args.count() > 0 {
            var arg = evalExpr(expr.args.get(0), row, table);
            if arg.kind == SQL_NULL { return sqlText("null"); }
            if arg.kind == SQL_INTEGER { return sqlText("integer"); }
            if arg.kind == SQL_TEXT { return sqlText("text"); }
            if arg.kind == SQL_REAL { return sqlText("real"); }
            if arg.kind == SQL_BLOB { return sqlText("blob"); }
            return sqlText("unknown");
        }

        return sqlNull();
    }

    expose func evalCondition(expr: Expr?, row: Row, table: Table) -> Boolean {
        if expr == null {
            return true;
        }
        var e = expr;
        var result = evalExpr(e, row, table);
        return result.intValue != 0;
    }

    //=========================================================================
    // INDEX-BASED LOOKUPS
    //=========================================================================

    // Check if WHERE clause is a simple equality that can use an index
    // Returns the column name and value if usable, null otherwise
    expose func canUseIndex(expr: Expr?, tableName: String) -> Boolean {
        if expr == null {
            return false;
        }
        var e = expr;

        // Must be a binary expression with OP_EQ
        if e.kind != EXPR_BINARY || e.op != OP_EQ {
            return false;
        }

        var left = e.getLeft();
        var right = e.getRight();

        // Check for column = literal pattern
        if left.kind == EXPR_COLUMN && right.kind == EXPR_LITERAL {
            var colName = left.columnName;
            var maybeIdx = indexMgr.findIndexForColumn(tableName, colName);
            return maybeIdx != null;
        }

        // Check for literal = column pattern
        if left.kind == EXPR_LITERAL && right.kind == EXPR_COLUMN {
            var colName = right.columnName;
            var maybeIdx = indexMgr.findIndexForColumn(tableName, colName);
            return maybeIdx != null;
        }

        return false;
    }

    // Get matching row indices using an index
    expose func indexLookup(expr: Expr, tableName: String, table: Table) -> List[Integer] {
        var left = expr.getLeft();
        var right = expr.getRight();

        var colName = "";
        var lookupValue = sqlNull();

        // Extract column name and lookup value
        if left.kind == EXPR_COLUMN && right.kind == EXPR_LITERAL {
            colName = left.columnName;
            lookupValue = right.literalValue;
        } else if left.kind == EXPR_LITERAL && right.kind == EXPR_COLUMN {
            colName = right.columnName;
            lookupValue = left.literalValue;
        }

        // Find the index
        var maybeIdx = indexMgr.findIndexForColumn(tableName, colName);
        if maybeIdx == null {
            // Fall back to empty list (caller should do linear scan)
            var empty: List[Integer] = [];
            return empty;
        }

        var idx = maybeIdx;
        return idx.lookupSingle(lookupValue, table);
    }

    //=========================================================================
    // AGGREGATE FUNCTIONS
    //=========================================================================

    // Check if expression is an aggregate function
    // Note: MIN/MAX with exactly 2 arguments are treated as scalar functions
    expose func isAggregateExpr(expr: Expr) -> Boolean {
        if expr.kind == EXPR_FUNCTION {
            var funcName = Viper.String.ToUpper(expr.funcName);
            if funcName == "COUNT" { return true; }
            if funcName == "SUM" { return true; }
            if funcName == "AVG" { return true; }
            // MIN/MAX with 2 args = scalar, with 1 arg = aggregate
            if funcName == "MIN" && expr.args.count() != 2 { return true; }
            if funcName == "MAX" && expr.args.count() != 2 { return true; }
        }
        return false;
    }

    // Check if SELECT has any aggregate functions
    expose func hasAggregates(stmt: SelectStmt) -> Boolean {
        var c = 0;
        while c < stmt.columns.count() {
            var colExpr = stmt.columns.get(c);
            if isAggregateExpr(colExpr) {
                return true;
            }
            c = c + 1;
        }
        return false;
    }

    // Evaluate an aggregate function over a list of row indices
    expose func evalAggregate(expr: Expr, matchingRows: List[Integer], table: Table) -> SqlValue {
        var funcName = Viper.String.ToUpper(expr.funcName);
        var hasArg = expr.args.count() > 0;

        // COUNT(*)
        if funcName == "COUNT" && hasArg {
            var arg0 = expr.args.get(0);
            if arg0.kind == EXPR_STAR {
                return sqlInteger(matchingRows.count());
            }
        }

        // COUNT(column) - count non-NULL values
        if funcName == "COUNT" {
            var count = 0;
            var i = 0;
            while i < matchingRows.count() {
                var rowIdx = matchingRows.get(i);
                var maybeRow = table.getRow(rowIdx);
                if maybeRow != null {
                    var row = maybeRow;
                    if hasArg {
                        var argExpr = expr.args.get(0);
                        var val = evalExpr(argExpr, row, table);
                        if val.kind != SQL_NULL {
                            count = count + 1;
                        }
                    }
                }
                i = i + 1;
            }
            return sqlInteger(count);
        }

        // SUM(column)
        if funcName == "SUM" {
            var sumInt = 0;
            var hasValue = false;
            var i = 0;
            while i < matchingRows.count() {
                var rowIdx = matchingRows.get(i);
                var maybeRow = table.getRow(rowIdx);
                if maybeRow != null {
                    var row = maybeRow;
                    if hasArg {
                        var argExpr = expr.args.get(0);
                        var val = evalExpr(argExpr, row, table);
                        if val.kind == SQL_INTEGER {
                            sumInt = sumInt + val.intValue;
                            hasValue = true;
                        }
                    }
                }
                i = i + 1;
            }
            if hasValue == false {
                return sqlNull();
            }
            return sqlInteger(sumInt);
        }

        // AVG(column)
        if funcName == "AVG" {
            var sumInt = 0;
            var count = 0;
            var i = 0;
            while i < matchingRows.count() {
                var rowIdx = matchingRows.get(i);
                var maybeRow = table.getRow(rowIdx);
                if maybeRow != null {
                    var row = maybeRow;
                    if hasArg {
                        var argExpr = expr.args.get(0);
                        var val = evalExpr(argExpr, row, table);
                        if val.kind == SQL_INTEGER {
                            sumInt = sumInt + val.intValue;
                            count = count + 1;
                        }
                    }
                }
                i = i + 1;
            }
            if count == 0 {
                return sqlNull();
            }
            return sqlInteger(sumInt / count);
        }

        // MIN(column)
        if funcName == "MIN" {
            var hasMin = false;
            var minVal = 0;
            var i = 0;
            while i < matchingRows.count() {
                var rowIdx = matchingRows.get(i);
                var maybeRow = table.getRow(rowIdx);
                if maybeRow != null {
                    var row = maybeRow;
                    if hasArg {
                        var argExpr = expr.args.get(0);
                        var val = evalExpr(argExpr, row, table);
                        if val.kind == SQL_INTEGER {
                            if hasMin == false || val.intValue < minVal {
                                minVal = val.intValue;
                                hasMin = true;
                            }
                        }
                    }
                }
                i = i + 1;
            }
            if hasMin == false {
                return sqlNull();
            }
            return sqlInteger(minVal);
        }

        // MAX(column)
        if funcName == "MAX" {
            var hasMax = false;
            var maxVal = 0;
            var i = 0;
            while i < matchingRows.count() {
                var rowIdx = matchingRows.get(i);
                var maybeRow = table.getRow(rowIdx);
                if maybeRow != null {
                    var row = maybeRow;
                    if hasArg {
                        var argExpr = expr.args.get(0);
                        var val = evalExpr(argExpr, row, table);
                        if val.kind == SQL_INTEGER {
                            if hasMax == false || val.intValue > maxVal {
                                maxVal = val.intValue;
                                hasMax = true;
                            }
                        }
                    }
                }
                i = i + 1;
            }
            if hasMax == false {
                return sqlNull();
            }
            return sqlInteger(maxVal);
        }

        return sqlNull();
    }

    //=========================================================================
    // STATEMENT EXECUTION
    //=========================================================================

    expose func executeCreateTable(stmt: CreateTableStmt) -> QueryResult {
        var result = new QueryResult();
        result.init();

        if db.findTable(stmt.tableName) != null {
            result.setError("Table '" + stmt.tableName + "' already exists");
            return result;
        }

        var table = new Table();
        table.initWithName(stmt.tableName);

        var i = 0;
        while i < stmt.columnCount() {
            var stmtCol = stmt.getColumn(i);
            var col = new Column();
            col.initWithName(stmtCol.name, stmtCol.typeCode);
            col.notNull = stmtCol.notNull;
            col.primaryKey = stmtCol.primaryKey;
            col.autoIncrement = stmtCol.autoIncrement;
            col.unique = stmtCol.unique;
            col.isForeignKey = stmtCol.isForeignKey;
            col.refTableName = stmtCol.refTableName;
            col.refColumnName = stmtCol.refColumnName;
            table.addColumn(col);
            i = i + 1;
        }

        db.addTable(table);
        result.message = "Table '" + stmt.tableName + "' created";
        return result;
    }

    expose func executeInsert(stmt: InsertStmt) -> QueryResult {
        var result = new QueryResult();
        result.init();

        var table = db.findTable(stmt.tableName);
        if table == null {
            result.setError("Table '" + stmt.tableName + "' not found");
            return result;
        }

        var t = table;
        var rowsInserted = 0;
        var r = 0;
        while r < stmt.rowCount() {
            var valueExprs = stmt.valueRows.get(r);
            var row = new Row();
            row.initWithCount(t.columnCount());

            var c = 0;
            while c < t.columnCount() {
                var col = t.getColumn(c);
                if col != null {
                    var column = col;

                    // Check for autoincrement
                    if column.autoIncrement && c < valueExprs.count() {
                        var valExpr = valueExprs.get(c);
                        if valExpr.kind == EXPR_LITERAL && valExpr.literalValue.kind == SQL_NULL {
                            var autoVal = sqlInteger(t.nextAutoIncrement());
                            row.setValue(c, autoVal);
                            c = c + 1;
                            continue;
                        }
                    }

                    if c < valueExprs.count() {
                        var valExpr = valueExprs.get(c);
                        var val = evalExpr(valExpr, row, t);
                        row.setValue(c, val);
                    } else if column.hasDefault {
                        row.setValue(c, column.defaultValue);
                    } else {
                        row.setValue(c, sqlNull());
                    }
                }
                c = c + 1;
            }

            // Validate constraints before inserting
            var constraintError = validateConstraints(t, row, -1);
            if constraintError != "" {
                result.setError(constraintError);
                return result;
            }

            var newRowIdx = t.rowCount();
            t.addRow(row);

            // Update all indexes on this table
            updateIndexesAfterInsert(stmt.tableName, row, newRowIdx, t);

            rowsInserted = rowsInserted + 1;
            r = r + 1;
        }

        result.message = "Inserted " + Viper.Fmt.Int(rowsInserted) + " row(s)";
        result.rowsAffected = rowsInserted;
        return result;
    }

    // Validate constraints for a row (returns error message or empty string)
    hide func validateConstraints(table: Table, row: Row, excludeRowIdx: Integer) -> String {
        var c = 0;
        while c < table.columnCount() {
            var maybeCol = table.getColumn(c);
            if maybeCol != null {
                var col = maybeCol;
                var val = row.getValue(c);

                // Check NOT NULL constraint
                if col.notNull && val.kind == SQL_NULL {
                    return "NOT NULL constraint failed: " + col.name;
                }

                // Check PRIMARY KEY constraint (implies NOT NULL and UNIQUE)
                if col.primaryKey {
                    if val.kind == SQL_NULL {
                        return "PRIMARY KEY constraint failed: " + col.name + " cannot be NULL";
                    }
                    // Check uniqueness
                    var i = 0;
                    while i < table.rowCount() {
                        if i != excludeRowIdx {
                            var maybeRow = table.getRow(i);
                            if maybeRow != null {
                                var existingRow = maybeRow;
                                if existingRow.deleted == false {
                                    var existingVal = existingRow.getValue(c);
                                    if val.compare(existingVal) == 0 {
                                        return "PRIMARY KEY constraint failed: duplicate value in " + col.name;
                                    }
                                }
                            }
                        }
                        i = i + 1;
                    }
                }

                // Check UNIQUE constraint
                if col.unique && val.kind != SQL_NULL {
                    var i = 0;
                    while i < table.rowCount() {
                        if i != excludeRowIdx {
                            var maybeRow = table.getRow(i);
                            if maybeRow != null {
                                var existingRow = maybeRow;
                                if existingRow.deleted == false {
                                    var existingVal = existingRow.getValue(c);
                                    if val.compare(existingVal) == 0 {
                                        return "UNIQUE constraint failed: duplicate value in " + col.name;
                                    }
                                }
                            }
                        }
                        i = i + 1;
                    }
                }

                // Check FOREIGN KEY constraint
                if col.isForeignKey {
                    if val.kind != SQL_NULL {
                        var refTable = db.findTable(col.refTableName);
                        if refTable != null {
                            var rt = refTable;
                            var refColIdx = rt.findColumnIndex(col.refColumnName);
                            if refColIdx >= 0 {
                                var found = false;
                                var i = 0;
                                while i < rt.rowCount() {
                                    var maybeRow = rt.getRow(i);
                                    if maybeRow != null {
                                        var refRow = maybeRow;
                                        if refRow.deleted == false {
                                            var refVal = refRow.getValue(refColIdx);
                                            if val.compare(refVal) == 0 {
                                                found = true;
                                                i = rt.rowCount();  // break
                                            }
                                        }
                                    }
                                    i = i + 1;
                                }
                                if found == false {
                                    return "FOREIGN KEY constraint failed: " + col.name + " references " + col.refTableName + "(" + col.refColumnName + ")";
                                }
                            }
                        }
                    }
                }
            }
            c = c + 1;
        }
        return "";
    }

    // Update all indexes for a table after inserting a row
    hide func updateIndexesAfterInsert(tableName: String, row: Row, rowIdx: Integer, table: Table) {
        var i = 0;
        while i < indexMgr.indexCount() {
            var idx = indexMgr.indexes.get(i);
            if idx.tableName == tableName {
                idx.addEntry(row, rowIdx, table);
            }
            i = i + 1;
        }
    }

    expose func executeSelect(stmt: SelectStmt) -> QueryResult {
        // Check for multi-table (JOIN) query
        if stmt.tableNames.count() > 1 {
            return executeCrossJoin(stmt);
        }

        var result = new QueryResult();
        result.init();

        var table = db.findTable(stmt.tableName);
        if table == null {
            result.setError("Table '" + stmt.tableName + "' not found");
            return result;
        }

        var t = table;
        currentTableAlias = stmt.tableAlias;

        // Build column names for result
        if stmt.selectAll {
            var c = 0;
            while c < t.columnCount() {
                var col = t.getColumn(c);
                if col != null {
                    var column = col;
                    result.addColumnName(column.name);
                }
                c = c + 1;
            }
        } else {
            var c = 0;
            while c < stmt.columns.count() {
                var colExpr = stmt.columns.get(c);
                if colExpr.kind == EXPR_COLUMN {
                    result.addColumnName(colExpr.columnName);
                } else if colExpr.kind == EXPR_FUNCTION {
                    result.addColumnName(colExpr.funcName);
                } else {
                    result.addColumnName("col" + Viper.Fmt.Int(c));
                }
                c = c + 1;
            }
        }

        // First pass: collect matching row indices
        // Try to use index lookup for simple equality conditions
        var matchingRows = new List[Integer]();
        var usedIndex = false;

        if stmt.whereClause != null && canUseIndex(stmt.whereClause, stmt.tableName) {
            var wc = stmt.whereClause;
            matchingRows = indexLookup(wc, stmt.tableName, t);
            usedIndex = true;
            // Filter out deleted rows from index results
            var filtered = new List[Integer]();
            var fi = 0;
            while fi < matchingRows.count() {
                var rowIdx = matchingRows.get(fi);
                var maybeRow = t.getRow(rowIdx);
                if maybeRow != null {
                    var row = maybeRow;
                    if row.deleted == false {
                        filtered.add(rowIdx);
                    }
                }
                fi = fi + 1;
            }
            matchingRows = filtered;
        }

        // Fall back to linear scan if no index was used
        if usedIndex == false {
            var r = 0;
            while r < t.rowCount() {
                var row = t.getRow(r);
                if row != null {
                    var rowData = row;
                    if rowData.deleted == false {
                        if evalCondition(stmt.whereClause, rowData, t) {
                            matchingRows.add(r);
                        }
                    }
                }
                r = r + 1;
            }
        }

        // Check if this is an aggregate query
        var isAggregate = hasAggregates(stmt);

        if isAggregate && stmt.groupByExprs.count() == 0 {
            // Aggregate query without GROUP BY: return single row
            var resultRow = new Row();
            resultRow.init();

            var c = 0;
            while c < stmt.columns.count() {
                var colExpr = stmt.columns.get(c);
                if isAggregateExpr(colExpr) {
                    var val = evalAggregate(colExpr, matchingRows, t);
                    resultRow.addValue(val);
                } else if colExpr.kind == EXPR_COLUMN {
                    // For non-aggregate columns, use first matching row value
                    if matchingRows.count() > 0 {
                        var firstRow = t.getRow(matchingRows.get(0));
                        if firstRow != null {
                            var fr = firstRow;
                            var val = evalExpr(colExpr, fr, t);
                            resultRow.addValue(val);
                        } else {
                            resultRow.addValue(sqlNull());
                        }
                    } else {
                        resultRow.addValue(sqlNull());
                    }
                } else {
                    resultRow.addValue(sqlNull());
                }
                c = c + 1;
            }
            result.addRow(resultRow);

        } else if stmt.groupByExprs.count() > 0 {
            // GROUP BY query
            executeGroupBy(stmt, matchingRows, t, result);

        } else {
            // Regular (non-aggregate) query
            var i = 0;
            while i < matchingRows.count() {
                var rowIdx = matchingRows.get(i);
                var row = t.getRow(rowIdx);
                if row != null {
                    var rowData = row;
                    var resultRow = new Row();
                    resultRow.init();

                    if stmt.selectAll {
                        var c = 0;
                        while c < rowData.columnCount() {
                            resultRow.addValue(rowData.getValue(c));
                            c = c + 1;
                        }
                    } else {
                        var c = 0;
                        while c < stmt.columns.count() {
                            var colExpr = stmt.columns.get(c);
                            var val = evalExpr(colExpr, rowData, t);
                            resultRow.addValue(val);
                            c = c + 1;
                        }
                    }

                    result.addRow(resultRow);
                }
                i = i + 1;
            }
        }

        // Apply DISTINCT
        if stmt.isDistinct {
            applyDistinct(result);
        }

        // Apply ORDER BY
        if stmt.orderByExprs.count() > 0 {
            sortResults(result, stmt.orderByExprs, stmt.orderByDir, t);
        }

        // Apply LIMIT/OFFSET
        if stmt.limitValue >= 0 || stmt.offsetValue > 0 {
            applyLimitOffset(result, stmt.limitValue, stmt.offsetValue);
        }

        currentTableAlias = "";
        return result;
    }

    // Execute GROUP BY query
    expose func executeGroupBy(stmt: SelectStmt, matchingRows: List[Integer], table: Table, result: QueryResult) {
        // Build groups based on GROUP BY columns
        var groupKeys = new List[String]();
        var groupRowLists = new List[List[Integer]]();

        var i = 0;
        while i < matchingRows.count() {
            var rowIdx = matchingRows.get(i);
            var row = table.getRow(rowIdx);
            if row != null {
                var rowData = row;
                // Build group key from GROUP BY expressions
                var key = "";
                var g = 0;
                while g < stmt.groupByExprs.count() {
                    var groupExpr = stmt.groupByExprs.get(g);
                    var val = evalExpr(groupExpr, rowData, table);
                    if g > 0 {
                        key = key + "|";
                    }
                    key = key + val.toString();
                    g = g + 1;
                }

                // Find or create group
                var groupIdx = -1;
                var k = 0;
                while k < groupKeys.count() {
                    if groupKeys.get(k) == key {
                        groupIdx = k;
                        k = groupKeys.count();  // break
                    }
                    k = k + 1;
                }

                if groupIdx < 0 {
                    groupKeys.add(key);
                    var newList = new List[Integer]();
                    newList.add(rowIdx);
                    groupRowLists.add(newList);
                } else {
                    var existingList = groupRowLists.get(groupIdx);
                    existingList.add(rowIdx);
                }
            }
            i = i + 1;
        }

        // Build result rows from groups
        var g = 0;
        while g < groupKeys.count() {
            var groupRows = groupRowLists.get(g);
            var resultRow = new Row();
            resultRow.init();

            // Get first row in group for non-aggregate columns
            var firstRowIdx = groupRows.get(0);
            var firstRow = table.getRow(firstRowIdx);

            var c = 0;
            while c < stmt.columns.count() {
                var colExpr = stmt.columns.get(c);
                if isAggregateExpr(colExpr) {
                    var val = evalAggregate(colExpr, groupRows, table);
                    resultRow.addValue(val);
                } else if firstRow != null {
                    var fr = firstRow;
                    var val = evalExpr(colExpr, fr, table);
                    resultRow.addValue(val);
                } else {
                    resultRow.addValue(sqlNull());
                }
                c = c + 1;
            }

            // Check HAVING condition before adding row
            if stmt.havingClause != null {
                var hc = stmt.havingClause;
                if evalHavingExpr(hc, groupRows, table) == false {
                    g = g + 1;
                    continue;
                }
            }

            result.addRow(resultRow);
            g = g + 1;
        }
    }

    // Evaluate HAVING expression for a group
    expose func evalHavingExpr(expr: Expr, groupRows: List[Integer], table: Table) -> Boolean {
        // Handle binary expressions (comparisons and logical operators)
        if expr.kind == EXPR_BINARY {
            var left = expr.getLeft();
            var right = expr.getRight();
            var op = expr.op;

            // Handle logical operators
            if op == OP_AND {
                return evalHavingExpr(left, groupRows, table) && evalHavingExpr(right, groupRows, table);
            }
            if op == OP_OR {
                return evalHavingExpr(left, groupRows, table) || evalHavingExpr(right, groupRows, table);
            }

            // Evaluate left and right sides for comparison
            var leftVal = evalHavingValue(left, groupRows, table);
            var rightVal = evalHavingValue(right, groupRows, table);

            // Comparison operators
            var cmp = leftVal.compare(rightVal);
            if op == OP_EQ { return cmp == 0; }
            if op == OP_NE { return cmp != 0; }
            if op == OP_LT { return cmp < 0; }
            if op == OP_LE { return cmp <= 0; }
            if op == OP_GT { return cmp > 0; }
            if op == OP_GE { return cmp >= 0; }
        }

        return false;
    }

    // Evaluate a value in HAVING context (handles aggregates)
    expose func evalHavingValue(expr: Expr, groupRows: List[Integer], table: Table) -> SqlValue {
        // If it's an aggregate function, evaluate it on the group
        if expr.kind == EXPR_FUNCTION {
            if isAggregateExpr(expr) {
                return evalAggregate(expr, groupRows, table);
            }
        }

        // If it's a literal, return its value
        if expr.kind == EXPR_LITERAL {
            return expr.literalValue;
        }

        // If it's a column ref, evaluate using first row in group
        if expr.kind == EXPR_COLUMN {
            if groupRows.count() > 0 {
                var firstRowIdx = groupRows.get(0);
                var maybeRow = table.getRow(firstRowIdx);
                if maybeRow != null {
                    var row = maybeRow;
                    return evalExpr(expr, row, table);
                }
            }
        }

        return sqlNull();
    }

    // Apply DISTINCT - remove duplicate rows
    expose func applyDistinct(result: QueryResult) {
        var seenKeys = new List[String]();
        var uniqueRows = new List[Row]();

        var i = 0;
        while i < result.rowCount() {
            var row = result.getRow(i);
            if row != null {
                var r = row;
                // Build key from all column values
                var key = "";
                var c = 0;
                while c < r.columnCount() {
                    if c > 0 {
                        key = key + "|";
                    }
                    key = key + r.getValue(c).toString();
                    c = c + 1;
                }

                // Check if already seen
                var found = false;
                var k = 0;
                while k < seenKeys.count() {
                    if seenKeys.get(k) == key {
                        found = true;
                        k = seenKeys.count();  // break
                    }
                    k = k + 1;
                }

                if found == false {
                    seenKeys.add(key);
                    uniqueRows.add(r);
                }
            }
            i = i + 1;
        }

        // Replace result rows with unique rows
        result.rows = uniqueRows;
    }

    expose func executeUpdate(stmt: UpdateStmt) -> QueryResult {
        var result = new QueryResult();
        result.init();

        var table = db.findTable(stmt.tableName);
        if table == null {
            result.setError("Table '" + stmt.tableName + "' not found");
            return result;
        }

        var t = table;
        var rowsUpdated = 0;

        var r = 0;
        while r < t.rowCount() {
            var row = t.getRow(r);
            if row != null {
                var rowData = row;
                if rowData.deleted == false {
                    if evalCondition(stmt.whereClause, rowData, t) {
                        // Create a copy of the row with updated values
                        var updatedRow = new Row();
                        updatedRow.initWithCount(t.columnCount());
                        var c = 0;
                        while c < t.columnCount() {
                            updatedRow.setValue(c, rowData.getValue(c));
                            c = c + 1;
                        }

                        // Apply updates to the copy
                        var i = 0;
                        while i < stmt.setColumns.count() {
                            var colName = stmt.setColumns.get(i);
                            var valExpr = stmt.setValues.get(i);
                            var colIdx = t.findColumnIndex(colName);
                            if colIdx >= 0 {
                                var val = evalExpr(valExpr, rowData, t);
                                updatedRow.setValue(colIdx, val);
                            }
                            i = i + 1;
                        }

                        // Validate constraints (exclude current row index for uniqueness check)
                        var constraintError = validateConstraints(t, updatedRow, r);
                        if constraintError != "" {
                            result.setError(constraintError);
                            return result;
                        }

                        // Apply the updates to the actual row
                        i = 0;
                        while i < stmt.setColumns.count() {
                            var colName = stmt.setColumns.get(i);
                            var colIdx = t.findColumnIndex(colName);
                            if colIdx >= 0 {
                                rowData.setValue(colIdx, updatedRow.getValue(colIdx));
                            }
                            i = i + 1;
                        }
                        rowsUpdated = rowsUpdated + 1;
                    }
                }
            }
            r = r + 1;
        }

        result.message = "Updated " + Viper.Fmt.Int(rowsUpdated) + " row(s)";
        result.rowsAffected = rowsUpdated;
        return result;
    }

    expose func executeDelete(stmt: DeleteStmt) -> QueryResult {
        var result = new QueryResult();
        result.init();

        var table = db.findTable(stmt.tableName);
        if table == null {
            result.setError("Table '" + stmt.tableName + "' not found");
            return result;
        }

        var t = table;
        var rowsDeleted = 0;

        var r = 0;
        while r < t.rowCount() {
            var row = t.getRow(r);
            if row != null {
                var rowData = row;
                if rowData.deleted == false {
                    if evalCondition(stmt.whereClause, rowData, t) {
                        rowData.deleted = true;
                        rowsDeleted = rowsDeleted + 1;
                    }
                }
            }
            r = r + 1;
        }

        result.message = "Deleted " + Viper.Fmt.Int(rowsDeleted) + " row(s)";
        result.rowsAffected = rowsDeleted;
        return result;
    }

    //=========================================================================
    // HELPER METHODS
    //=========================================================================

    hide func sortResults(result: QueryResult, orderExprs: List[Expr], orderDir: List[Integer], table: Table) {
        // Simple bubble sort
        var n = result.rowCount();
        var i = 0;
        while i < n - 1 {
            var j = 0;
            while j < n - i - 1 {
                var row1 = result.getRow(j);
                var row2 = result.getRow(j + 1);
                if row1 != null && row2 != null {
                    var r1 = row1;
                    var r2 = row2;
                    var shouldSwap = false;

                    var e = 0;
                    while e < orderExprs.count() {
                        var orderExpr = orderExprs.get(e);
                        var isDesc = orderDir.get(e);
                        var val1 = evalExpr(orderExpr, r1, table);
                        var val2 = evalExpr(orderExpr, r2, table);
                        var cmp = val1.compare(val2);

                        if cmp != 0 {
                            if isDesc == 1 {
                                shouldSwap = cmp < 0;
                            } else {
                                shouldSwap = cmp > 0;
                            }
                            break;
                        }
                        e = e + 1;
                    }

                    if shouldSwap {
                        result.swapRows(j, j + 1);
                    }
                }
                j = j + 1;
            }
            i = i + 1;
        }
    }

    hide func applyLimitOffset(result: QueryResult, limitVal: Integer, offsetVal: Integer) {
        var newRows: List[Row] = [];
        var i = offsetVal;
        var count = 0;
        while i < result.rowCount() {
            if limitVal >= 0 && count >= limitVal {
                break;
            }
            var row = result.getRow(i);
            if row != null {
                newRows.add(row);
                count = count + 1;
            }
            i = i + 1;
        }
        result.rows = newRows;
    }

    //=========================================================================
    // MAIN EXECUTION ENTRY POINT
    //=========================================================================

    expose func executeSql(sql: String) -> QueryResult {
        var parser = new Parser(sql);

        var kind = parser.currentKind();

        if kind == TK_CREATE {
            parser.advance();
            if parser.currentKind() == TK_TABLE {
                var stmt = parser.parseCreateTableStmt();
                if parser.hasError {
                    var result = new QueryResult();
                    result.init();
                    result.setError(parser.error);
                    return result;
                }
                return executeCreateTable(stmt);
            }
            if parser.currentKind() == TK_INDEX || parser.currentKind() == TK_UNIQUE {
                var stmt = parser.parseCreateIndexStmt();
                if parser.hasError {
                    var result = new QueryResult();
                    result.init();
                    result.setError(parser.error);
                    return result;
                }
                return executeCreateIndex(stmt);
            }
        }

        if kind == TK_INSERT {
            parser.advance();
            var stmt = parser.parseInsertStmt();
            if parser.hasError {
                var result = new QueryResult();
                result.init();
                result.setError(parser.error);
                return result;
            }
            return executeInsert(stmt);
        }

        if kind == TK_SELECT {
            parser.advance();
            var stmt = parser.parseSelectStmt();
            if parser.hasError {
                var result = new QueryResult();
                result.init();
                result.setError(parser.error);
                return result;
            }
            return executeSelect(stmt);
        }

        if kind == TK_UPDATE {
            parser.advance();
            var stmt = parser.parseUpdateStmt();
            if parser.hasError {
                var result = new QueryResult();
                result.init();
                result.setError(parser.error);
                return result;
            }
            return executeUpdate(stmt);
        }

        if kind == TK_DELETE {
            parser.advance();
            var stmt = parser.parseDeleteStmt();
            if parser.hasError {
                var result = new QueryResult();
                result.init();
                result.setError(parser.error);
                return result;
            }
            return executeDelete(stmt);
        }

        if kind == TK_DROP {
            parser.advance();
            if parser.currentKind() == TK_TABLE {
                parser.advance();
                if parser.currentKind() == TK_IDENTIFIER {
                    var tableName = parser.currentText();
                    if db.dropTable(tableName) {
                        var result = new QueryResult();
                        result.init();
                        result.message = "Table '" + tableName + "' dropped";
                        return result;
                    } else {
                        var result = new QueryResult();
                        result.init();
                        result.setError("Table '" + tableName + "' not found");
                        return result;
                    }
                }
            }
            if parser.currentKind() == TK_INDEX {
                var stmt = parser.parseDropIndexStmt();
                if parser.hasError {
                    var result = new QueryResult();
                    result.init();
                    result.setError(parser.error);
                    return result;
                }
                return executeDropIndex(stmt);
            }
        }

        if kind == TK_SHOW {
            parser.advance();
            var result = new QueryResult();
            result.init();
            result.addColumnName("table_name");
            var i = 0;
            while i < db.tableCount() {
                var t = db.getTable(i);
                if t != null {
                    var table = t;
                    var row = new Row();
                    row.init();
                    row.addValue(sqlText(table.name));
                    result.addRow(row);
                }
                i = i + 1;
            }
            return result;
        }

        var result = new QueryResult();
        result.init();
        result.setError("Unknown SQL statement");
        return result;
    }

    expose func executeCreateIndex(stmt: CreateIndexStmt) -> QueryResult {
        var result = new QueryResult();
        result.init();

        var table = db.findTable(stmt.tableName);
        if table == null {
            result.setError("Table '" + stmt.tableName + "' not found");
            return result;
        }

        var idx = new SqlIndex();
        idx.initWithNames(stmt.indexName, stmt.tableName);
        idx.isUnique = stmt.isUnique;

        var i = 0;
        while i < stmt.columnCount() {
            idx.addColumn(stmt.columnNames.get(i));
            i = i + 1;
        }

        var t = table;
        idx.rebuild(t);
        indexMgr.addIndex(idx);

        result.message = "Index '" + stmt.indexName + "' created";
        return result;
    }

    expose func executeDropIndex(stmt: DropIndexStmt) -> QueryResult {
        var result = new QueryResult();
        result.init();

        if indexMgr.dropIndex(stmt.indexName) {
            result.message = "Index '" + stmt.indexName + "' dropped";
        } else {
            result.setError("Index '" + stmt.indexName + "' not found");
        }

        return result;
    }

    //=========================================================================
    // JOIN OPERATIONS
    //=========================================================================

    // Create a row with a specific number of NULL values
    hide func makeJoinRow(numCols: Integer) -> Row {
        var row = new Row();
        row.init();
        var i = 0;
        while i < numCols {
            row.addValue(sqlNull());
            i = i + 1;
        }
        return row;
    }

    // Find column value in a combined row for JOIN queries
    hide func findJoinColumnValue(tableName: String, columnName: String,
                                   tables: List[Table], aliases: List[String],
                                   combinedRow: Row, colOffsets: List[Integer]) -> SqlValue {
        var ti = 0;
        while ti < tables.count() {
            var tbl = tables.get(ti);
            var alias = aliases.get(ti);

            // Check if this table matches the requested table name or alias
            if tableName == alias || tableName == tbl.name || tableName == "" {
                var colIdx = tbl.findColumnIndex(columnName);
                if colIdx >= 0 {
                    var offset = colOffsets.get(ti);
                    return combinedRow.getValue(offset + colIdx);
                }
            }
            ti = ti + 1;
        }
        return sqlNull();
    }

    // Evaluate expression in JOIN context
    hide func evalJoinExpr(expr: Expr, tables: List[Table], aliases: List[String],
                           combinedRow: Row, colOffsets: List[Integer]) -> SqlValue {
        if expr.kind == EXPR_LITERAL {
            return expr.literalValue;
        }

        if expr.kind == EXPR_COLUMN {
            return findJoinColumnValue(expr.tableName, expr.columnName,
                                       tables, aliases, combinedRow, colOffsets);
        }

        if expr.kind == EXPR_BINARY {
            var left = evalJoinExpr(expr.getLeft(), tables, aliases, combinedRow, colOffsets);
            var right = evalJoinExpr(expr.getRight(), tables, aliases, combinedRow, colOffsets);
            var op = expr.op;

            // Comparison operators
            if op == OP_EQ {
                if left.compare(right) == 0 { return sqlInteger(1); }
                return sqlInteger(0);
            }
            if op == OP_NE {
                if left.compare(right) != 0 { return sqlInteger(1); }
                return sqlInteger(0);
            }
            if op == OP_LT {
                if left.compare(right) < 0 { return sqlInteger(1); }
                return sqlInteger(0);
            }
            if op == OP_LE {
                if left.compare(right) <= 0 { return sqlInteger(1); }
                return sqlInteger(0);
            }
            if op == OP_GT {
                if left.compare(right) > 0 { return sqlInteger(1); }
                return sqlInteger(0);
            }
            if op == OP_GE {
                if left.compare(right) >= 0 { return sqlInteger(1); }
                return sqlInteger(0);
            }

            // Logical operators
            if op == OP_AND {
                if left.intValue != 0 && right.intValue != 0 { return sqlInteger(1); }
                return sqlInteger(0);
            }
            if op == OP_OR {
                if left.intValue != 0 || right.intValue != 0 { return sqlInteger(1); }
                return sqlInteger(0);
            }

            // Arithmetic operators
            if op == OP_ADD { return sqlInteger(left.intValue + right.intValue); }
            if op == OP_SUB { return sqlInteger(left.intValue - right.intValue); }
            if op == OP_MUL { return sqlInteger(left.intValue * right.intValue); }
            if op == OP_DIV {
                if right.intValue != 0 {
                    return sqlInteger(left.intValue / right.intValue);
                }
                return sqlNull();
            }
        }

        return sqlNull();
    }

    // Execute a multi-table JOIN query
    expose func executeCrossJoin(stmt: SelectStmt) -> QueryResult {
        var result = new QueryResult();
        result.init();

        // Load all tables
        var tables = new List[Table]();
        var aliases = new List[String]();
        var colOffsets = new List[Integer]();
        var totalCols = 0;

        var ti = 0;
        while ti < stmt.tableNames.count() {
            var tblName = stmt.tableNames.get(ti);
            var maybeTable = db.findTable(tblName);
            if maybeTable == null {
                result.setError("Table '" + tblName + "' does not exist");
                return result;
            }
            var tbl = maybeTable;
            tables.add(tbl);

            var alias = stmt.tableAliases.get(ti);
            if alias == "" {
                alias = tblName;
            }
            aliases.add(alias);

            colOffsets.add(totalCols);
            totalCols = totalCols + tbl.columnCount();
            ti = ti + 1;
        }

        // Build column names for result
        if stmt.selectAll {
            ti = 0;
            while ti < tables.count() {
                var tbl = tables.get(ti);
                var alias = aliases.get(ti);
                var c = 0;
                while c < tbl.columnCount() {
                    var maybeCol = tbl.getColumn(c);
                    if maybeCol != null {
                        var col = maybeCol;
                        result.addColumnName(alias + "." + col.name);
                    }
                    c = c + 1;
                }
                ti = ti + 1;
            }
        } else {
            var c = 0;
            while c < stmt.columns.count() {
                var colExpr = stmt.columns.get(c);
                if colExpr.kind == EXPR_COLUMN {
                    if colExpr.tableName != "" {
                        result.addColumnName(colExpr.tableName + "." + colExpr.columnName);
                    } else {
                        result.addColumnName(colExpr.columnName);
                    }
                } else {
                    result.addColumnName("expr" + Viper.Fmt.Int(c));
                }
                c = c + 1;
            }
        }

        // Build cartesian product starting with first table
        var combinedRows = new List[Row]();

        if tables.count() > 0 {
            var firstTable = tables.get(0);
            var r = 0;
            while r < firstTable.rowCount() {
                var maybeRow = firstTable.getRow(r);
                if maybeRow != null {
                    var srcRow = maybeRow;
                    if srcRow.deleted == false {
                        var newRow = makeJoinRow(totalCols);
                        var c = 0;
                        while c < firstTable.columnCount() {
                            newRow.setValue(c, srcRow.getValue(c));
                            c = c + 1;
                        }
                        combinedRows.add(newRow);
                    }
                }
                r = r + 1;
            }
        }

        // Extend with each additional table
        ti = 1;
        while ti < tables.count() {
            var tbl = tables.get(ti);
            var offset = colOffsets.get(ti);
            var newCombined = new List[Row]();

            // Get join type (0=CROSS, 1=INNER, 2=LEFT, 3=RIGHT, 4=FULL)
            var joinType = 0;
            if ti - 1 < stmt.joinTypes.count() {
                joinType = stmt.joinTypes.get(ti - 1);
            }

            // Check if we have a join condition
            var hasJoinCond = ti - 1 < stmt.joinConditions.count();

            // For RIGHT/FULL JOIN: track matched right rows
            var rightMatched = new List[Integer]();
            if joinType == 3 || joinType == 4 {
                var rInit = 0;
                while rInit < tbl.rowCount() {
                    rightMatched.add(0);
                    rInit = rInit + 1;
                }
            }

            var ci = 0;
            while ci < combinedRows.count() {
                var existing = combinedRows.get(ci);
                var foundMatch = false;

                var r = 0;
                while r < tbl.rowCount() {
                    var maybeRow = tbl.getRow(r);
                    if maybeRow != null {
                        var srcRow = maybeRow;
                        if srcRow.deleted == false {
                            // Create combined row
                            var newRow = makeJoinRow(totalCols);
                            var c = 0;
                            while c < offset {
                                newRow.setValue(c, existing.getValue(c));
                                c = c + 1;
                            }
                            c = 0;
                            while c < tbl.columnCount() {
                                newRow.setValue(offset + c, srcRow.getValue(c));
                                c = c + 1;
                            }

                            // Check join condition for non-CROSS joins
                            var includeRow = true;
                            if hasJoinCond && (joinType == 1 || joinType == 2 || joinType == 3 || joinType == 4) {
                                var joinCond = stmt.joinConditions.get(ti - 1);
                                var joinResult = evalJoinExpr(joinCond, tables, aliases, newRow, colOffsets);
                                if joinResult.intValue == 0 {
                                    includeRow = false;
                                }
                            }

                            if includeRow {
                                newCombined.add(newRow);
                                foundMatch = true;
                                if joinType == 3 || joinType == 4 {
                                    rightMatched.set(r, 1);
                                }
                            }
                        }
                    }
                    r = r + 1;
                }

                // LEFT/FULL JOIN: add NULL row if no match
                if (joinType == 2 || joinType == 4) && foundMatch == false {
                    var nullRow = makeJoinRow(totalCols);
                    var c = 0;
                    while c < offset {
                        nullRow.setValue(c, existing.getValue(c));
                        c = c + 1;
                    }
                    newCombined.add(nullRow);
                }

                ci = ci + 1;
            }

            // RIGHT/FULL JOIN: add unmatched right rows
            if joinType == 3 || joinType == 4 {
                var rCheck = 0;
                while rCheck < tbl.rowCount() {
                    if rightMatched.get(rCheck) == 0 {
                        var maybeRightRow = tbl.getRow(rCheck);
                        if maybeRightRow != null {
                            var rightRow = maybeRightRow;
                            if rightRow.deleted == false {
                                var nullRow = makeJoinRow(totalCols);
                                var c = 0;
                                while c < tbl.columnCount() {
                                    nullRow.setValue(offset + c, rightRow.getValue(c));
                                    c = c + 1;
                                }
                                newCombined.add(nullRow);
                            }
                        }
                    }
                    rCheck = rCheck + 1;
                }
            }

            combinedRows = newCombined;
            ti = ti + 1;
        }

        // Apply WHERE clause
        var filteredRows = new List[Row]();
        var ri = 0;
        while ri < combinedRows.count() {
            var row = combinedRows.get(ri);
            var includeRow = true;

            if stmt.whereClause != null {
                var wc = stmt.whereClause;
                var whereResult = evalJoinExpr(wc, tables, aliases, row, colOffsets);
                if whereResult.intValue == 0 {
                    includeRow = false;
                }
            }

            if includeRow {
                filteredRows.add(row);
            }
            ri = ri + 1;
        }

        // Build result rows
        ri = 0;
        while ri < filteredRows.count() {
            var combinedRow = filteredRows.get(ri);
            var resultRow = new Row();
            resultRow.init();

            if stmt.selectAll {
                var c = 0;
                while c < totalCols {
                    resultRow.addValue(combinedRow.getValue(c));
                    c = c + 1;
                }
            } else {
                var c = 0;
                while c < stmt.columns.count() {
                    var colExpr = stmt.columns.get(c);
                    var val = evalJoinExpr(colExpr, tables, aliases, combinedRow, colOffsets);
                    resultRow.addValue(val);
                    c = c + 1;
                }
            }

            result.addRow(resultRow);
            ri = ri + 1;
        }

        return result;
    }
}
