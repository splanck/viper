module SqlEngine;

import "./sql_lexer";
import "./sql_storage";
import "./sql_types";

Integer EXPR_COMPARE = 1;
Integer EXPR_AND = 2;
Integer EXPR_OR = 3;
Integer EXPR_NOT = 4;
Integer EXPR_IS_NULL = 5;
Integer EXPR_IS_NOT_NULL = 6;
Integer EXPR_VALUE = 7;
Integer EXPR_IN = 8;

Integer VAL_LITERAL = 1;
Integer VAL_COLUMN = 2;
Integer VAL_ADD = 3;
Integer VAL_SUB = 4;
Integer VAL_MUL = 5;
Integer VAL_DIV = 6;
Integer VAL_NEG = 7;
Integer VAL_FUNC = 8;
Integer VAL_CONCAT = 9;

Integer SEL_STAR = 0;
Integer SEL_VALUE = 1;
Integer SEL_AGG = 2;

entity Expr {
    expose Integer kind;
    expose Expr? left;
    expose Expr? right;
    expose ValueExpr? leftValue;
    expose ValueExpr? rightValue;
    expose List[SqlValue] inList;
    expose String op;
}

entity ValueExpr {
    expose Integer kind;
    expose ValueExpr? left;
    expose ValueExpr? right;
    expose Integer colIndex;
    expose String name;
    expose SqlValue literal;
    expose List[ValueExpr] args;
    expose Boolean countStar;
    expose Boolean distinct;
}

entity SelectItem {
    expose Integer kind;
    expose ValueExpr? expr;
    expose String alias;
    expose String aggFunc;
    expose Boolean distinct;
    expose Boolean countStar;
}

entity OrderSpec {
    expose Integer index;
    expose String name;
    expose Boolean desc;
}

entity ResultSet {
    expose List[String] headers;
    expose List[Row] rows;
}

entity TokenStream {
    expose List[Token] tokens;
    expose Integer pos;

    expose func init(list: List[Token]) {
        tokens = list;
        pos = 0;
    }

    expose func atEnd() -> Boolean {
        return peek().kind == TOK_EOF;
    }

    expose func peek() -> Token {
        return tokens.get(pos);
    }

    expose func peekNext() -> Token {
        if pos + 1 >= tokens.count() { return tokens.get(tokens.count() - 1); }
        return tokens.get(pos + 1);
    }

    expose func advance() -> Token {
        Token t = tokens.get(pos);
        pos = pos + 1;
        return t;
    }

    expose func matchSymbol(sym: String) -> Boolean {
        Token t = peek();
        if t.kind == TOK_SYMBOL && t.text == sym {
            advance();
            return true;
        }
        return false;
    }

    expose func isKeyword(word: String) -> Boolean {
        Token t = peek();
        if t.kind != TOK_IDENT { return false; }
        return Viper.String.ToUpper(t.text) == word;
    }

    expose func matchKeyword(word: String) -> Boolean {
        if isKeyword(word) {
            advance();
            return true;
        }
        return false;
    }

    expose func expectIdent() -> String {
        Token t = peek();
        if t.kind == TOK_IDENT {
            advance();
            return t.text;
        }
        return "";
    }
}

func isIntText(text: String) -> Boolean {
    Integer len = Viper.String.Length(text);
    if len == 0 { return false; }
    Integer i = 0;
    if Viper.String.Substring(text, 0, 1) == "-" { i = 1; }
    if i >= len { return false; }
    while i < len {
        String c = Viper.String.Substring(text, i, 1);
        Integer code = Viper.String.Asc(c);
        if code < 48 || code > 57 { return false; }
        i = i + 1;
    }
    return true;
}

func isNumText(text: String) -> Boolean {
    Integer len = Viper.String.Length(text);
    if len == 0 { return false; }
    Integer i = 0;
    Boolean seenDot = false;
    if Viper.String.Substring(text, 0, 1) == "-" { i = 1; }
    if i >= len { return false; }
    while i < len {
        String c = Viper.String.Substring(text, i, 1);
        if c == "." && !seenDot {
            seenDot = true;
            i = i + 1;
            continue;
        }
        Integer code = Viper.String.Asc(c);
        if code < 48 || code > 57 { return false; }
        i = i + 1;
    }
    return true;
}

func parseIntText(text: String) -> Integer {
    Integer len = Viper.String.Length(text);
    if len == 0 { return 0; }
    Integer i = 0;
    Boolean neg = false;
    if Viper.String.Substring(text, 0, 1) == "-" {
        neg = true;
        i = 1;
    }
    Integer value = 0;
    while i < len {
        String c = Viper.String.Substring(text, i, 1);
        Integer code = Viper.String.Asc(c);
        value = value * 10 + (code - 48);
        i = i + 1;
    }
    if neg { value = 0 - value; }
    return value;
}

func digitToNum(code: Integer) -> Number {
    if code == 48 { return 0.0; }
    if code == 49 { return 1.0; }
    if code == 50 { return 2.0; }
    if code == 51 { return 3.0; }
    if code == 52 { return 4.0; }
    if code == 53 { return 5.0; }
    if code == 54 { return 6.0; }
    if code == 55 { return 7.0; }
    if code == 56 { return 8.0; }
    if code == 57 { return 9.0; }
    return 0.0;
}

func parseNumText(text: String) -> Number {
    Integer len = Viper.String.Length(text);
    if len == 0 { return 0.0; }
    Integer i = 0;
    Boolean neg = false;
    if Viper.String.Substring(text, 0, 1) == "-" {
        neg = true;
        i = 1;
    }
    Number value = 0.0;
    while i < len && Viper.String.Substring(text, i, 1) != "." {
        String c = Viper.String.Substring(text, i, 1);
        Integer code = Viper.String.Asc(c);
        Number digit = digitToNum(code);
        value = value * 10.0 + digit;
        i = i + 1;
    }
    if i < len && Viper.String.Substring(text, i, 1) == "." {
        i = i + 1;
        Number divisor = 1.0;
        while i < len {
            String c2 = Viper.String.Substring(text, i, 1);
            Integer code2 = Viper.String.Asc(c2);
            divisor = divisor * 10.0;
            Number digit = digitToNum(code2);
            value = value + digit / divisor;
            i = i + 1;
        }
    }
    if neg { value = 0.0 - value; }
    return value;
}

func intToNum(value: Integer) -> Number {
    return parseNumText(Viper.Fmt.Int(value));
}

func parseLiteral(ts: TokenStream, allowNull: Boolean, allowDefault: Boolean) -> SqlValue? {
    Boolean neg = false;
    if ts.matchSymbol("-") {
        neg = true;
    }
    Token t = ts.peek();
    var v = new SqlValue();
    if t.kind == TOK_NUMBER {
        ts.advance();
        String text = t.text;
        if Viper.String.IndexOf(text, ".") >= 0 {
            Number n = parseNumText(text);
            if neg { n = 0.0 - n; }
            v.initNum(n);
            return v;
        }
        Integer i = parseIntText(text);
        if neg { i = 0 - i; }
        v.initInt(i);
        return v;
    }
    if neg {
        return null;
    }
    if t.kind == TOK_STRING {
        ts.advance();
        v.initText(t.text);
        return v;
    }
    if t.kind == TOK_IDENT {
        String upper = Viper.String.ToUpper(t.text);
        if upper == "NULL" {
            ts.advance();
            if allowNull {
                v.initNull();
                return v;
            }
            return null;
        }
        if upper == "DEFAULT" {
            ts.advance();
            if allowDefault {
                v.initDefault();
                return v;
            }
            return null;
        }
        if upper == "TRUE" {
            ts.advance();
            v.initBool(true);
            return v;
        }
        if upper == "FALSE" {
            ts.advance();
            v.initBool(false);
            return v;
        }
        if upper == "CURRENT_TIMESTAMP" || upper == "CURRENT_DATETIME" {
            ts.advance();
            v.initText(Viper.DateTime.ToISO(Viper.DateTime.Now()));
            return v;
        }
        if upper == "CURRENT_DATE" {
            ts.advance();
            String iso = Viper.DateTime.ToISO(Viper.DateTime.Now());
            if Viper.String.Length(iso) >= 10 {
                v.initText(Viper.String.Substring(iso, 0, 10));
            } else {
                v.initText(iso);
            }
            return v;
        }
        if upper == "CURRENT_TIME" {
            ts.advance();
            String iso2 = Viper.DateTime.ToISO(Viper.DateTime.Now());
            Integer idx = Viper.String.IndexOf(iso2, "T");
            if idx >= 0 && Viper.String.Length(iso2) >= idx + 9 {
                v.initText(Viper.String.Substring(iso2, idx + 1, 8));
            } else {
                v.initText(iso2);
            }
            return v;
        }
    }
    return null;
}

func coerceValue(val: SqlValue, col: Column, allowNull: Boolean) -> SqlValue? {
    if val.kind == VALUE_DEFAULT { return null; }
    if val.kind == VALUE_NULL {
        if allowNull { return val; }
        return null;
    }
    if col.typeCode == TYPE_INT {
        if val.kind == VALUE_INT { return val; }
        if val.kind == VALUE_NUM {
            var out = new SqlValue();
            out.initInt(Viper.Convert.NumToInt(val.n));
            return out;
        }
        if val.kind == VALUE_BOOL {
            var out = new SqlValue();
            out.initInt(val.b ? 1 : 0);
            return out;
        }
        if val.kind == VALUE_TEXT {
            if !isIntText(val.s) { return null; }
            var out = new SqlValue();
            out.initInt(parseIntText(val.s));
            return out;
        }
        return null;
    }
    if col.typeCode == TYPE_NUM {
        if val.kind == VALUE_NUM { return val; }
        if val.kind == VALUE_INT {
            var out = new SqlValue();
            Number n = intToNum(val.i);
            out.initNum(n);
            return out;
        }
        if val.kind == VALUE_BOOL {
            var out = new SqlValue();
            out.initNum(val.b ? 1.0 : 0.0);
            return out;
        }
        if val.kind == VALUE_TEXT {
            if !isNumText(val.s) { return null; }
            var out = new SqlValue();
            out.initNum(parseNumText(val.s));
            return out;
        }
        return null;
    }
    if col.typeCode == TYPE_BOOL {
        if val.kind == VALUE_BOOL { return val; }
        if val.kind == VALUE_INT {
            var out = new SqlValue();
            out.initBool(val.i != 0);
            return out;
        }
        if val.kind == VALUE_TEXT {
            String lower = Viper.String.ToLower(val.s);
            if lower == "true" || lower == "1" || lower == "yes" {
                var out = new SqlValue();
                out.initBool(true);
                return out;
            }
            if lower == "false" || lower == "0" || lower == "no" {
                var out2 = new SqlValue();
                out2.initBool(false);
                return out2;
            }
            return null;
        }
        return null;
    }
    if col.typeCode == TYPE_TEXT {
        if val.kind == VALUE_TEXT { return val; }
        var outText = new SqlValue();
        if val.kind == VALUE_INT {
            outText.initText(Viper.Fmt.Int(val.i));
        } else if val.kind == VALUE_NUM {
            outText.initText(Viper.Fmt.Num(val.n));
        } else if val.kind == VALUE_BOOL {
            outText.initText(val.b ? "true" : "false");
        } else {
            outText.initText(val.display());
        }
        return outText;
    }
    return null;
}

func makeNull() -> SqlValue {
    var v = new SqlValue();
    v.initNull();
    return v;
}

func makeInt(i: Integer) -> SqlValue {
    var v = new SqlValue();
    v.initInt(i);
    return v;
}

func makeNum(n: Number) -> SqlValue {
    var v = new SqlValue();
    v.initNum(n);
    return v;
}

func makeBool(b: Boolean) -> SqlValue {
    var v = new SqlValue();
    v.initBool(b);
    return v;
}

func makeText(s: String) -> SqlValue {
    var v = new SqlValue();
    v.initText(s);
    return v;
}

func toNumberValue(val: SqlValue) -> SqlValue? {
    if val.kind == VALUE_NUM { return val; }
    if val.kind == VALUE_INT { return makeNum(intToNum(val.i)); }
    if val.kind == VALUE_BOOL { return makeNum(val.b ? 1.0 : 0.0); }
    if val.kind == VALUE_TEXT {
        if !isNumText(val.s) { return null; }
        return makeNum(parseNumText(val.s));
    }
    return null;
}

func toIntValue(val: SqlValue) -> SqlValue? {
    if val.kind == VALUE_INT { return val; }
    if val.kind == VALUE_NUM { return makeInt(Viper.Convert.NumToInt(val.n)); }
    if val.kind == VALUE_BOOL { return makeInt(val.b ? 1 : 0); }
    if val.kind == VALUE_TEXT {
        if !isIntText(val.s) { return null; }
        return makeInt(parseIntText(val.s));
    }
    return null;
}

func toTextValue(val: SqlValue) -> SqlValue {
    if val.kind == VALUE_TEXT { return val; }
    return makeText(val.display());
}

func compareValues(a: SqlValue, b: SqlValue, op: String) -> Boolean {
    if op == "<>" { op = "!="; }
    if op == "==" { op = "="; }
    if op == "=" {
        SqlValue? na = toNumberValue(a);
        SqlValue? nb = toNumberValue(b);
        if na != null && nb != null { return na.n == nb.n; }
        return a.key() == b.key();
    }
    if op == "!=" {
        SqlValue? na2 = toNumberValue(a);
        SqlValue? nb2 = toNumberValue(b);
        if na2 != null && nb2 != null { return na2.n != nb2.n; }
        return a.key() != b.key();
    }
    if a.kind == VALUE_NULL || b.kind == VALUE_NULL { return false; }
    if a.kind == VALUE_DEFAULT || b.kind == VALUE_DEFAULT { return false; }
    SqlValue? na3 = toNumberValue(a);
    SqlValue? nb3 = toNumberValue(b);
    if na3 != null && nb3 != null {
        if op == "<" { return na3.n < nb3.n; }
        if op == "<=" { return na3.n <= nb3.n; }
        if op == ">" { return na3.n > nb3.n; }
        if op == ">=" { return na3.n >= nb3.n; }
    }
    SqlValue ta = toTextValue(a);
    SqlValue tb = toTextValue(b);
    Integer cmp = Viper.String.Cmp(ta.s, tb.s);
    if op == "<" { return cmp < 0; }
    if op == "<=" { return cmp <= 0; }
    if op == ">" { return cmp > 0; }
    if op == ">=" { return cmp >= 0; }
    return false;
}

func compareValueForSort(a: SqlValue, b: SqlValue) -> Integer {
    if a.kind == VALUE_NULL || a.kind == VALUE_DEFAULT {
        if b.kind == VALUE_NULL || b.kind == VALUE_DEFAULT { return 0; }
        return 1;
    }
    if b.kind == VALUE_NULL || b.kind == VALUE_DEFAULT { return -1; }
    SqlValue? na = toNumberValue(a);
    SqlValue? nb = toNumberValue(b);
    if na != null && nb != null {
        if na.n < nb.n { return -1; }
        if na.n > nb.n { return 1; }
        return 0;
    }
    String sa = a.display();
    String sb = b.display();
    return Viper.String.Cmp(sa, sb);
}

func valueTruthy(val: SqlValue) -> Boolean {
    if val.kind == VALUE_NULL { return false; }
    if val.kind == VALUE_DEFAULT { return false; }
    if val.kind == VALUE_BOOL { return val.b; }
    if val.kind == VALUE_INT { return val.i != 0; }
    if val.kind == VALUE_NUM { return val.n != 0.0; }
    if val.kind == VALUE_TEXT { return Viper.String.Length(val.s) > 0; }
    return false;
}

func evalValueExpr(expr: ValueExpr?, table: Table, row: Row) -> SqlValue {
    if expr == null { return makeNull(); }
    ValueExpr e = expr;
    if e.kind == VAL_LITERAL { return e.literal.clone(); }
    if e.kind == VAL_COLUMN {
        if e.colIndex >= 0 && e.colIndex < row.values.count() {
            return row.values.get(e.colIndex).clone();
        }
        return makeNull();
    }
    if e.kind == VAL_NEG {
        SqlValue inner = evalValueExpr(e.left, table, row);
        SqlValue? num = toNumberValue(inner);
        if num == null { return makeNull(); }
        return makeNum(0.0 - num.n);
    }
    if e.kind == VAL_CONCAT {
        SqlValue left = evalValueExpr(e.left, table, row);
        SqlValue right = evalValueExpr(e.right, table, row);
        if left.kind == VALUE_NULL || right.kind == VALUE_NULL { return makeNull(); }
        return makeText(Viper.String.Concat(left.display(), right.display()));
    }
    if e.kind == VAL_ADD || e.kind == VAL_SUB || e.kind == VAL_MUL || e.kind == VAL_DIV {
        SqlValue left2 = evalValueExpr(e.left, table, row);
        SqlValue right2 = evalValueExpr(e.right, table, row);
        SqlValue? nl = toNumberValue(left2);
        SqlValue? nr = toNumberValue(right2);
        if nl == null || nr == null { return makeNull(); }
        if e.kind == VAL_ADD { return makeNum(nl.n + nr.n); }
        if e.kind == VAL_SUB { return makeNum(nl.n - nr.n); }
        if e.kind == VAL_MUL { return makeNum(nl.n * nr.n); }
        if nr.n == 0.0 { return makeNull(); }
        return makeNum(nl.n / nr.n);
    }
    if e.kind == VAL_FUNC {
        return evalFunction(e, table, row);
    }
    return makeNull();
}

func evalFunction(expr: ValueExpr, table: Table, row: Row) -> SqlValue {
    String name = Viper.String.ToUpper(expr.name);
    List[SqlValue] args = [];
    Integer i = 0;
    while i < expr.args.count() {
        args.add(evalValueExpr(expr.args.get(i), table, row));
        i = i + 1;
    }
    return evalFunctionArgs(name, args);
}

func evalFunctionArgs(name: String, args: List[SqlValue]) -> SqlValue {
    if name == "LOWER" && args.count() == 1 {
        return makeText(Viper.String.ToLower(args.get(0).display()));
    }
    if name == "UPPER" && args.count() == 1 {
        return makeText(Viper.String.ToUpper(args.get(0).display()));
    }
    if name == "LENGTH" && args.count() == 1 {
        return makeInt(Viper.String.Length(args.get(0).display()));
    }
    if name == "TRIM" && args.count() == 1 {
        return makeText(Viper.String.Trim(args.get(0).display()));
    }
    if name == "ABS" && args.count() == 1 {
        SqlValue? n = toNumberValue(args.get(0));
        if n == null { return makeNull(); }
        return makeNum(Viper.Math.Abs(n.n));
    }
    if (name == "COALESCE" || name == "IFNULL") && args.count() >= 1 {
        Integer i2 = 0;
        while i2 < args.count() {
            SqlValue v = args.get(i2);
            if v.kind != VALUE_NULL { return v; }
            i2 = i2 + 1;
        }
        return makeNull();
    }
    if name == "ROUND" && args.count() >= 1 {
        SqlValue? n2 = toNumberValue(args.get(0));
        if n2 == null { return makeNull(); }
        if args.count() == 1 {
            return makeNum(Viper.Math.Round(n2.n));
        }
        SqlValue? p = toIntValue(args.get(1));
        if p == null { return makeNull(); }
        Number factor = 1.0;
        Integer i3 = 0;
        while i3 < p.i {
            factor = factor * 10.0;
            i3 = i3 + 1;
        }
        Number out = Viper.Math.Round(n2.n * factor) / factor;
        return makeNum(out);
    }
    if name == "SUBSTR" && args.count() >= 2 {
        String text = args.get(0).display();
        SqlValue? startInt = toIntValue(args.get(1));
        if startInt == null { return makeNull(); }
        Integer start = startInt.i;
        if start < 1 { start = 1; }
        Integer len = Viper.String.Length(text);
        if args.count() >= 3 {
            SqlValue? lenInt = toIntValue(args.get(2));
            if lenInt == null { return makeNull(); }
            Integer count = lenInt.i;
            if count < 0 { count = 0; }
            return makeText(Viper.String.Substring(text, start - 1, count));
        }
        return makeText(Viper.String.Substring(text, start - 1, len - (start - 1)));
    }
    if name == "REPLACE" && args.count() == 3 {
        String src = args.get(0).display();
        String find = args.get(1).display();
        String rep = args.get(2).display();
        return makeText(Viper.String.Replace(src, find, rep));
    }
    if name == "LEFT" && args.count() == 2 {
        String src2 = args.get(0).display();
        SqlValue? n3 = toIntValue(args.get(1));
        if n3 == null { return makeNull(); }
        Integer count2 = n3.i;
        if count2 < 0 { count2 = 0; }
        Integer len2 = Viper.String.Length(src2);
        if count2 > len2 { count2 = len2; }
        return makeText(Viper.String.Substring(src2, 0, count2));
    }
    if name == "RIGHT" && args.count() == 2 {
        String src3 = args.get(0).display();
        SqlValue? n4 = toIntValue(args.get(1));
        if n4 == null { return makeNull(); }
        Integer count3 = n4.i;
        if count3 < 0 { count3 = 0; }
        Integer len3 = Viper.String.Length(src3);
        if count3 > len3 { count3 = len3; }
        return makeText(Viper.String.Substring(src3, len3 - count3, count3));
    }
    if name == "INT" && args.count() == 1 {
        SqlValue? iVal = toIntValue(args.get(0));
        if iVal == null { return makeNull(); }
        return iVal;
    }
    if name == "NUM" && args.count() == 1 {
        SqlValue? nVal = toNumberValue(args.get(0));
        if nVal == null { return makeNull(); }
        return nVal;
    }
    if name == "TEXT" && args.count() == 1 {
        return makeText(args.get(0).display());
    }
    if name == "BOOL" && args.count() == 1 {
        SqlValue v2 = args.get(0);
        if v2.kind == VALUE_BOOL { return v2; }
        if v2.kind == VALUE_INT { return makeBool(v2.i != 0); }
        if v2.kind == VALUE_NUM { return makeBool(v2.n != 0.0); }
        if v2.kind == VALUE_TEXT {
            String lower = Viper.String.ToLower(v2.s);
            return makeBool(lower == "true" || lower == "1" || lower == "yes");
        }
        return makeBool(false);
    }
    if name == "NOW" && args.count() == 0 {
        return makeText(Viper.DateTime.ToISO(Viper.DateTime.Now()));
    }
    if name == "CAST" && args.count() == 2 {
        String typeName = Viper.String.ToUpper(args.get(1).display());
        Column temp = new Column();
        temp.init("", parseTypeName(typeName));
        SqlValue? coerced = coerceValue(args.get(0), temp, true);
        if coerced == null { return makeNull(); }
        return coerced;
    }
    return makeNull();
}

func isAggregateFunc(name: String) -> Boolean {
    String upper = Viper.String.ToUpper(name);
    return upper == "COUNT" || upper == "SUM" || upper == "AVG" || upper == "MIN" || upper == "MAX";
}

func evalAggregate(expr: ValueExpr, table: Table, rowIdx: List[Integer]) -> SqlValue {
    String name = Viper.String.ToUpper(expr.name);
    if name == "COUNT" && expr.countStar {
        return makeInt(rowIdx.count());
    }
    if rowIdx.count() == 0 {
        if name == "COUNT" { return makeInt(0); }
        return makeNull();
    }
    if expr.args.count() == 0 {
        if name == "COUNT" { return makeInt(0); }
        return makeNull();
    }
    Map[String, Boolean] seen = new Map[String, Boolean]();
    Number sum = 0.0;
    Integer count = 0;
    SqlValue? best = null;
    Integer i = 0;
    while i < rowIdx.count() {
        Row r = table.rows.get(rowIdx.get(i));
        SqlValue v = evalValueExpr(expr.args.get(0), table, r);
        if expr.distinct {
            String key = v.key();
            if seen.has(key) { i = i + 1; continue; }
            seen.set(key, true);
        }
        if name == "COUNT" {
            if v.kind != VALUE_NULL { count = count + 1; }
            i = i + 1;
            continue;
        }
        if v.kind == VALUE_NULL {
            i = i + 1;
            continue;
        }
        if name == "SUM" || name == "AVG" {
            SqlValue? n = toNumberValue(v);
            if n != null {
                sum = sum + n.n;
                count = count + 1;
            }
        } else if name == "MIN" || name == "MAX" {
            if best == null {
                best = v;
            } else {
                SqlValue b = best;
                Integer cmp = compareValueForSort(v, b);
                if name == "MIN" && cmp < 0 { best = v; }
                if name == "MAX" && cmp > 0 { best = v; }
            }
        }
        i = i + 1;
    }
    if name == "COUNT" { return makeInt(count); }
    if name == "SUM" {
        if count == 0 { return makeNull(); }
        return makeNum(sum);
    }
    if name == "AVG" {
        if count == 0 { return makeNull(); }
        return makeNum(sum / count);
    }
    if name == "MIN" || name == "MAX" {
        if best == null { return makeNull(); }
        return best;
    }
    return makeNull();
}

func evalValueExprGroup(expr: ValueExpr?, table: Table, rowIdx: List[Integer]) -> SqlValue {
    if expr == null { return makeNull(); }
    if expr.kind == VAL_FUNC && isAggregateFunc(expr.name) {
        return evalAggregate(expr, table, rowIdx);
    }
    if expr.kind == VAL_LITERAL { return expr.literal.clone(); }
    if expr.kind == VAL_COLUMN {
        if rowIdx.count() == 0 { return makeNull(); }
        Row r = table.rows.get(rowIdx.get(0));
        if expr.colIndex >= 0 && expr.colIndex < r.values.count() {
            return r.values.get(expr.colIndex).clone();
        }
        return makeNull();
    }
    if expr.kind == VAL_NEG {
        SqlValue inner = evalValueExprGroup(expr.left, table, rowIdx);
        SqlValue? num = toNumberValue(inner);
        if num == null { return makeNull(); }
        return makeNum(0.0 - num.n);
    }
    if expr.kind == VAL_CONCAT {
        SqlValue left = evalValueExprGroup(expr.left, table, rowIdx);
        SqlValue right = evalValueExprGroup(expr.right, table, rowIdx);
        if left.kind == VALUE_NULL || right.kind == VALUE_NULL { return makeNull(); }
        return makeText(Viper.String.Concat(left.display(), right.display()));
    }
    if expr.kind == VAL_ADD || expr.kind == VAL_SUB || expr.kind == VAL_MUL || expr.kind == VAL_DIV {
        SqlValue left2 = evalValueExprGroup(expr.left, table, rowIdx);
        SqlValue right2 = evalValueExprGroup(expr.right, table, rowIdx);
        SqlValue? nl = toNumberValue(left2);
        SqlValue? nr = toNumberValue(right2);
        if nl == null || nr == null { return makeNull(); }
        if expr.kind == VAL_ADD { return makeNum(nl.n + nr.n); }
        if expr.kind == VAL_SUB { return makeNum(nl.n - nr.n); }
        if expr.kind == VAL_MUL { return makeNum(nl.n * nr.n); }
        if nr.n == 0.0 { return makeNull(); }
        return makeNum(nl.n / nr.n);
    }
    if expr.kind == VAL_FUNC {
        List[SqlValue] args = [];
        Integer i = 0;
        while i < expr.args.count() {
            args.add(evalValueExprGroup(expr.args.get(i), table, rowIdx));
            i = i + 1;
        }
        return evalFunctionArgs(Viper.String.ToUpper(expr.name), args);
    }
    return makeNull();
}

func evalExprGroup(expr: Expr?, table: Table, rowIdx: List[Integer]) -> Boolean {
    if expr == null { return true; }
    Expr e = expr;
    if e.kind == EXPR_AND {
        return evalExprGroup(e.left, table, rowIdx) && evalExprGroup(e.right, table, rowIdx);
    }
    if e.kind == EXPR_OR {
        return evalExprGroup(e.left, table, rowIdx) || evalExprGroup(e.right, table, rowIdx);
    }
    if e.kind == EXPR_NOT {
        return !evalExprGroup(e.left, table, rowIdx);
    }
    if e.kind == EXPR_IS_NULL {
        SqlValue v = evalValueExprGroup(e.leftValue, table, rowIdx);
        return v.kind == VALUE_NULL;
    }
    if e.kind == EXPR_IS_NOT_NULL {
        SqlValue v2 = evalValueExprGroup(e.leftValue, table, rowIdx);
        return v2.kind != VALUE_NULL;
    }
    if e.kind == EXPR_VALUE {
        SqlValue v3 = evalValueExprGroup(e.leftValue, table, rowIdx);
        return valueTruthy(v3);
    }
    if e.kind == EXPR_IN {
        SqlValue v4 = evalValueExprGroup(e.leftValue, table, rowIdx);
        if v4.kind == VALUE_NULL { return false; }
        Integer i = 0;
        while i < e.inList.count() {
            SqlValue lit = e.inList.get(i);
            if compareValues(v4, lit, "=") { return true; }
            i = i + 1;
        }
        return false;
    }
    if e.kind == EXPR_COMPARE {
        SqlValue lv = evalValueExprGroup(e.leftValue, table, rowIdx);
        SqlValue rv = evalValueExprGroup(e.rightValue, table, rowIdx);
        String op = Viper.String.ToUpper(e.op);
        if op == "LIKE" {
            return matchLike(lv.display(), rv.display());
        }
        if op == "NOT LIKE" {
            return !matchLike(lv.display(), rv.display());
        }
        return compareValues(lv, rv, e.op);
    }
    return false;
}

func cloneColumn(c: Column) -> Column {
    var col = new Column();
    col.init(c.name, c.typeCode);
    col.notNull = c.notNull;
    col.primaryKey = c.primaryKey;
    col.autoInc = c.autoInc;
    col.unique = c.unique;
    col.hasDefault = c.hasDefault;
    if c.hasDefault {
        col.defaultValue = c.defaultValue.clone();
    }
    return col;
}

func cloneRow(r: Row) -> Row {
    var row = new Row();
    row.values = [];
    row.deleted = r.deleted;
    Integer i = 0;
    while i < r.values.count() {
        row.values.add(r.values.get(i).clone());
        i = i + 1;
    }
    return row;
}

func cloneTable(t: Table) -> Table {
    var nt = new Table();
    nt.init(t.name);
    Integer i = 0;
    while i < t.columns.count() {
        nt.addColumn(cloneColumn(t.columns.get(i)));
        i = i + 1;
    }
    nt.finalizeSchema();
    nt.autoInc = t.autoInc;
    nt.dirty = t.dirty;
    i = 0;
    while i < t.rows.count() {
        nt.rows.add(cloneRow(t.rows.get(i)));
        i = i + 1;
    }
    nt.indexes = [];
    nt.indexByName = new Map[String, Integer]();
    i = 0;
    while i < t.indexes.count() {
        Index idx = t.indexes.get(i);
        if !idx.system {
            var newIdx = new Index();
            newIdx.init(idx.name, idx.colIndex, idx.unique, false);
            nt.indexes.add(newIdx);
        }
        i = i + 1;
    }
    ensureSystemIndexes(nt);
    rebuildPrimaryIndex(nt);
    rebuildIndexes(nt);
    return nt;
}

func cloneDatabase(src: Database) -> Database {
    var d = new Database();
    d.init(src.path);
    Integer i = 0;
    while i < src.tables.count() {
        Table t = src.tables.get(i);
        Table nt = cloneTable(t);
        d.tableIndex.set(Viper.String.ToUpper(nt.name), d.tables.count());
        d.tables.add(nt);
        i = i + 1;
    }
    return d;
}

func matchLike(text: String, pattern: String) -> Boolean {
    Integer ti = 0;
    Integer pi = 0;
    Integer tlen = Viper.String.Length(text);
    Integer plen = Viper.String.Length(pattern);
    Integer starText = -1;
    Integer starPat = -1;
    while ti < tlen {
        String tc = Viper.String.Substring(text, ti, 1);
        String pc = pi < plen ? Viper.String.Substring(pattern, pi, 1) : "";
        if pc == "%" {
            starPat = pi;
            starText = ti;
            pi = pi + 1;
            continue;
        }
        if pc == "_" || pc == tc {
            ti = ti + 1;
            pi = pi + 1;
            continue;
        }
        if starPat >= 0 {
            starText = starText + 1;
            ti = starText;
            pi = starPat + 1;
            continue;
        }
        return false;
    }
    while pi < plen && Viper.String.Substring(pattern, pi, 1) == "%" {
        pi = pi + 1;
    }
    return pi >= plen;
}

func evalExpr(expr: Expr?, table: Table, row: Row) -> Boolean {
    if expr == null { return true; }
    Expr e = expr;
    if e.kind == EXPR_AND {
        return evalExpr(e.left, table, row) && evalExpr(e.right, table, row);
    }
    if e.kind == EXPR_OR {
        return evalExpr(e.left, table, row) || evalExpr(e.right, table, row);
    }
    if e.kind == EXPR_NOT {
        return !evalExpr(e.left, table, row);
    }
    if e.kind == EXPR_IS_NULL {
        SqlValue v = evalValueExpr(e.leftValue, table, row);
        return v.kind == VALUE_NULL;
    }
    if e.kind == EXPR_IS_NOT_NULL {
        SqlValue v2 = evalValueExpr(e.leftValue, table, row);
        return v2.kind != VALUE_NULL;
    }
    if e.kind == EXPR_VALUE {
        SqlValue v3 = evalValueExpr(e.leftValue, table, row);
        return valueTruthy(v3);
    }
    if e.kind == EXPR_IN {
        SqlValue v4 = evalValueExpr(e.leftValue, table, row);
        if v4.kind == VALUE_NULL { return false; }
        Integer i = 0;
        while i < e.inList.count() {
            SqlValue lit = e.inList.get(i);
            if compareValues(v4, lit, "=") { return true; }
            i = i + 1;
        }
        return false;
    }
    if e.kind == EXPR_COMPARE {
        SqlValue lv = evalValueExpr(e.leftValue, table, row);
        SqlValue rv = evalValueExpr(e.rightValue, table, row);
        String op = Viper.String.ToUpper(e.op);
        if op == "LIKE" {
            return matchLike(lv.display(), rv.display());
        }
        if op == "NOT LIKE" {
            return !matchLike(lv.display(), rv.display());
        }
        return compareValues(lv, rv, e.op);
    }
    return false;
}

entity SqlEngine {
    expose Database db;
    expose Boolean running;
    expose String lastError;
    expose Boolean inTransaction;
    expose Database? txnSnapshot;

    expose func init(path: String) {
        var d = new Database();
        d.init(path);
        db = d;
        lastError = "";
        running = true;
        inTransaction = false;
        txnSnapshot = null;
        if !loadDatabase(db) {
            lastError = db.lastError;
        }
    }

    expose func clearError() {
        lastError = "";
    }

    expose func fail(msg: String) -> Boolean {
        lastError = msg;
        return false;
    }

    expose func maybeSave() -> Boolean {
        if inTransaction { return true; }
        if !saveDatabase(db) { return fail(db.lastError); }
        return true;
    }

    expose func reportError() {
        if lastError != "" {
            Viper.Terminal.Say(Viper.String.Concat("error: ", lastError));
        }
    }

    expose func exec(input: String) -> Boolean {
        clearError();
        List[Token] tokens = lex(input);
        var ts = new TokenStream();
        ts.init(tokens);
        while !ts.atEnd() {
            if !parseStatement(ts) {
                reportError();
                return false;
            }
            ts.matchSymbol(";");
        }
        return true;
    }

    expose func parseStatement(ts: TokenStream) -> Boolean {
        if ts.atEnd() { return true; }
        if ts.matchKeyword("CREATE") { return parseCreate(ts); }
        if ts.matchKeyword("DROP") { return parseDrop(ts); }
        if ts.matchKeyword("INSERT") { return parseInsert(ts); }
        if ts.matchKeyword("SELECT") { return parseSelect(ts); }
        if ts.matchKeyword("UPDATE") { return parseUpdate(ts); }
        if ts.matchKeyword("DELETE") { return parseDelete(ts); }
        if ts.matchKeyword("ALTER") { return parseAlter(ts); }
        if ts.matchKeyword("BEGIN") || ts.matchKeyword("START") { return parseBegin(ts); }
        if ts.matchKeyword("COMMIT") || ts.matchKeyword("END") { return parseCommit(ts); }
        if ts.matchKeyword("ROLLBACK") { return parseRollback(ts); }
        if ts.matchKeyword("SHOW") { return parseShow(ts); }
        if ts.matchKeyword("DESCRIBE") { return parseDescribe(ts); }
        if ts.matchKeyword("DESC") { return parseDescribe(ts); }
        if ts.matchKeyword("VACUUM") { return parseVacuum(ts); }
        if ts.matchKeyword("EXPORT") { return parseExport(ts); }
        if ts.matchKeyword("IMPORT") { return parseImport(ts); }
        if ts.matchKeyword("SAVE") {
            if !saveDatabase(db) { return fail(db.lastError); }
            Viper.Terminal.Say("ok");
            return true;
        }
        if ts.matchKeyword("OPEN") { return parseOpen(ts); }
        if ts.matchKeyword("HELP") { printHelp(); return true; }
        if ts.matchKeyword("EXIT") || ts.matchKeyword("QUIT") {
            running = false;
            return true;
        }
        return fail("Unknown statement");
    }

    expose func expectKeyword(ts: TokenStream, word: String) -> Boolean {
        if ts.matchKeyword(word) { return true; }
        return fail(Viper.String.Concat("Expected ", word));
    }

    expose func expectSymbol(ts: TokenStream, sym: String) -> Boolean {
        if ts.matchSymbol(sym) { return true; }
        return fail(Viper.String.Concat("Expected symbol ", sym));
    }

    expose func parseCreate(ts: TokenStream) -> Boolean {
        if ts.matchKeyword("TABLE") { return parseCreateTable(ts); }
        if ts.matchKeyword("UNIQUE") {
            if !expectKeyword(ts, "INDEX") { return false; }
            return parseCreateIndex(ts, true);
        }
        if ts.matchKeyword("INDEX") { return parseCreateIndex(ts, false); }
        return fail("Expected TABLE or INDEX");
    }

    expose func parseCreateTable(ts: TokenStream) -> Boolean {
        Boolean ifNotExists = false;
        if ts.matchKeyword("IF") {
            if !expectKeyword(ts, "NOT") { return false; }
            if !expectKeyword(ts, "EXISTS") { return false; }
            ifNotExists = true;
        }
        String name = ts.expectIdent();
        if name == "" { return fail("Expected table name"); }
        if db.tableIndexByName(name) >= 0 {
            if ifNotExists { return true; }
            return fail("Table already exists");
        }
        if !expectSymbol(ts, "(") { return false; }
        var table = new Table();
        table.init(name);
        Boolean hasPk = false;
        while true {
            if ts.isKeyword("PRIMARY") {
                ts.advance();
                if !expectKeyword(ts, "KEY") { return false; }
                if !expectSymbol(ts, "(") { return false; }
                String pkName = ts.expectIdent();
                if pkName == "" { return fail("Expected column name"); }
                if !expectSymbol(ts, ")") { return false; }
                Integer pkIndex = findColumnIndex(table, pkName);
                if pkIndex < 0 { return fail("Unknown column in PRIMARY KEY"); }
                if hasPk { return fail("Multiple primary keys not supported"); }
                table.columns.get(pkIndex).primaryKey = true;
                hasPk = true;
            } else if ts.isKeyword("UNIQUE") {
                ts.advance();
                ts.matchKeyword("KEY");
                if !expectSymbol(ts, "(") { return false; }
                String uqName = ts.expectIdent();
                if uqName == "" { return fail("Expected column name"); }
                if !expectSymbol(ts, ")") { return false; }
                Integer uqIndex = findColumnIndex(table, uqName);
                if uqIndex < 0 { return fail("Unknown column in UNIQUE"); }
                table.columns.get(uqIndex).unique = true;
            } else {
                String colName = ts.expectIdent();
                if colName == "" { return fail("Expected column name"); }
                String typeNameText = ts.expectIdent();
                if typeNameText == "" { return fail("Expected column type"); }
                var col = new Column();
                col.init(colName, parseTypeName(typeNameText));
                while true {
                    if ts.matchKeyword("PRIMARY") {
                        if !expectKeyword(ts, "KEY") { return false; }
                        if hasPk { return fail("Multiple primary keys not supported"); }
                        col.primaryKey = true;
                        hasPk = true;
                        continue;
                    }
                    if ts.matchKeyword("NOT") {
                        if !expectKeyword(ts, "NULL") { return false; }
                        col.notNull = true;
                        continue;
                    }
                    if ts.matchKeyword("UNIQUE") {
                        col.unique = true;
                        continue;
                    }
                    if ts.matchKeyword("DEFAULT") {
                        SqlValue? lit = parseLiteral(ts, true, false);
                        if lit == null { return fail("Expected default literal"); }
                        SqlValue dv = lit ?? makeNull();
                        col.defaultValue = dv;
                        col.hasDefault = true;
                        continue;
                    }
                    if ts.matchKeyword("AUTOINCREMENT") || ts.matchKeyword("AUTO") {
                        col.autoInc = true;
                        continue;
                    }
                    break;
                }
                if col.notNull && col.hasDefault && col.defaultValue.kind == VALUE_NULL {
                    return fail("NOT NULL column cannot default to NULL");
                }
                table.addColumn(col);
            }
            if ts.matchSymbol(",") { continue; }
            break;
        }
        if !expectSymbol(ts, ")") { return false; }
        table.finalizeSchema();
        ensureSystemIndexes(table);
        if !rebuildPrimaryIndex(table) { return fail("Primary key conflict"); }
        if !rebuildIndexes(table) { return fail("Unique index conflict"); }
        db.tableIndex.set(Viper.String.ToUpper(name), db.tables.count());
        db.tables.add(table);
        if !maybeSave() { return false; }
        Viper.Terminal.Say("ok");
        return true;
    }

    expose func parseCreateIndex(ts: TokenStream, unique: Boolean) -> Boolean {
        Boolean ifNotExists = false;
        if ts.matchKeyword("IF") {
            if !expectKeyword(ts, "NOT") { return false; }
            if !expectKeyword(ts, "EXISTS") { return false; }
            ifNotExists = true;
        }
        String idxName = ts.expectIdent();
        if idxName == "" { return fail("Expected index name"); }
        if !expectKeyword(ts, "ON") { return false; }
        String tableName = ts.expectIdent();
        if tableName == "" { return fail("Expected table name"); }
        Integer tidx = db.tableIndexByName(tableName);
        if tidx < 0 { return fail("Table not found"); }
        Table table = db.tables.get(tidx);
        String key = Viper.String.ToUpper(idxName);
        if table.indexByName.has(key) {
            if ifNotExists { return true; }
            return fail("Index already exists");
        }
        if !expectSymbol(ts, "(") { return false; }
        String colName = ts.expectIdent();
        if colName == "" { return fail("Expected column name"); }
        if !expectSymbol(ts, ")") { return false; }
        Integer colIndex = table.columnIndexByName(colName);
        if colIndex < 0 { return fail("Unknown column in index"); }
        var idx = new Index();
        idx.init(idxName, colIndex, unique, false);
        table.indexes.add(idx);
        rebuildIndexMap(table);
        if !rebuildIndexes(table) { return fail("Unique index conflict"); }
        if !maybeSave() { return false; }
        Viper.Terminal.Say("ok");
        return true;
    }

    expose func parseDrop(ts: TokenStream) -> Boolean {
        if ts.matchKeyword("TABLE") { return parseDropTable(ts); }
        if ts.matchKeyword("INDEX") { return parseDropIndex(ts); }
        return fail("Expected TABLE or INDEX");
    }

    expose func parseDropTable(ts: TokenStream) -> Boolean {
        Boolean ifExists = false;
        if ts.matchKeyword("IF") {
            if !expectKeyword(ts, "EXISTS") { return false; }
            ifExists = true;
        }
        String name = ts.expectIdent();
        if name == "" { return fail("Expected table name"); }
        Integer idx = db.tableIndexByName(name);
        if idx < 0 {
            if ifExists { return true; }
            return fail("Table not found");
        }
        db.tables.set(idx, db.tables.get(db.tables.count() - 1));
        db.tables.removeAt(db.tables.count() - 1);
        db.tableIndex = new Map[String, Integer]();
        Integer i = 0;
        while i < db.tables.count() {
            Table t = db.tables.get(i);
            db.tableIndex.set(Viper.String.ToUpper(t.name), i);
            i = i + 1;
        }
        String path = tablePath(db, name);
        if Viper.IO.File.Exists(path) { Viper.IO.File.Delete(path); }
        if !maybeSave() { return false; }
        Viper.Terminal.Say("ok");
        return true;
    }

    expose func parseDropIndex(ts: TokenStream) -> Boolean {
        Boolean ifExists = false;
        if ts.matchKeyword("IF") {
            if !expectKeyword(ts, "EXISTS") { return false; }
            ifExists = true;
        }
        String idxName = ts.expectIdent();
        if idxName == "" { return fail("Expected index name"); }
        String key = Viper.String.ToUpper(idxName);
        Integer t = 0;
        while t < db.tables.count() {
            Table table = db.tables.get(t);
            if table.indexByName.has(key) {
                Integer i2 = table.indexByName.get(key);
                table.indexes.set(i2, table.indexes.get(table.indexes.count() - 1));
                table.indexes.removeAt(table.indexes.count() - 1);
                rebuildIndexMap(table);
                rebuildIndexes(table);
                if !maybeSave() { return false; }
                Viper.Terminal.Say("ok");
                return true;
            }
            t = t + 1;
        }
        if ifExists { return true; }
        return fail("Index not found");
    }

    expose func parseBegin(ts: TokenStream) -> Boolean {
        if inTransaction { return fail("Transaction already active"); }
        txnSnapshot = cloneDatabase(db);
        inTransaction = true;
        Viper.Terminal.Say("ok");
        return true;
    }

    expose func parseCommit(ts: TokenStream) -> Boolean {
        if !inTransaction { return fail("No active transaction"); }
        inTransaction = false;
        txnSnapshot = null;
        if !saveDatabase(db) { return fail(db.lastError); }
        Viper.Terminal.Say("ok");
        return true;
    }

    expose func parseRollback(ts: TokenStream) -> Boolean {
        if !inTransaction { return fail("No active transaction"); }
        if txnSnapshot == null { return fail("Missing transaction snapshot"); }
        Database snap = txnSnapshot;
        db = snap;
        inTransaction = false;
        txnSnapshot = null;
        Viper.Terminal.Say("ok");
        return true;
    }

    expose func parseAlter(ts: TokenStream) -> Boolean {
        if !expectKeyword(ts, "TABLE") { return false; }
        String name = ts.expectIdent();
        if name == "" { return fail("Expected table name"); }
        Integer idx = db.tableIndexByName(name);
        if idx < 0 { return fail("Table not found"); }
        Table table = db.tables.get(idx);
        if ts.matchKeyword("RENAME") {
            if ts.matchKeyword("TO") {
                String newName = ts.expectIdent();
                if newName == "" { return fail("Expected new table name"); }
                if db.tableIndexByName(newName) >= 0 { return fail("Table already exists"); }
                String oldPath = tablePath(db, table.name);
                String newPath = tablePath(db, newName);
                if Viper.IO.File.Exists(oldPath) {
                    String content = Viper.IO.File.ReadAllText(oldPath);
                    Viper.IO.File.WriteAllText(newPath, content);
                    Viper.IO.File.Delete(oldPath);
                }
                table.name = newName;
                db.tableIndex = new Map[String, Integer]();
                Integer i = 0;
                while i < db.tables.count() {
                    Table t = db.tables.get(i);
                    db.tableIndex.set(Viper.String.ToUpper(t.name), i);
                    i = i + 1;
                }
                if !maybeSave() { return false; }
                Viper.Terminal.Say("ok");
                return true;
            }
            if ts.matchKeyword("COLUMN") {
                String oldCol = ts.expectIdent();
                if oldCol == "" { return fail("Expected column name"); }
                if !expectKeyword(ts, "TO") { return false; }
                String newCol = ts.expectIdent();
                if newCol == "" { return fail("Expected column name"); }
                Integer cidx = table.columnIndexByName(oldCol);
                if cidx < 0 { return fail("Unknown column"); }
                table.columns.get(cidx).name = newCol;
                table.finalizeSchema();
                ensureSystemIndexes(table);
                if !rebuildIndexes(table) { return fail("Unique index conflict"); }
                if !maybeSave() { return false; }
                Viper.Terminal.Say("ok");
                return true;
            }
            return fail("Expected TO or COLUMN");
        }
        if ts.matchKeyword("ADD") {
            ts.matchKeyword("COLUMN");
            String colName = ts.expectIdent();
            if colName == "" { return fail("Expected column name"); }
            String typeNameText = ts.expectIdent();
            if typeNameText == "" { return fail("Expected column type"); }
            var col = new Column();
            col.init(colName, parseTypeName(typeNameText));
            while true {
                if ts.matchKeyword("NOT") {
                    if !expectKeyword(ts, "NULL") { return false; }
                    col.notNull = true;
                    continue;
                }
                if ts.matchKeyword("UNIQUE") {
                    col.unique = true;
                    continue;
                }
                if ts.matchKeyword("DEFAULT") {
                    SqlValue? lit = parseLiteral(ts, true, false);
                    if lit == null { return fail("Expected default literal"); }
                    SqlValue dv = lit ?? makeNull();
                    col.defaultValue = dv;
                    col.hasDefault = true;
                    continue;
                }
                break;
            }
            if col.notNull && (!col.hasDefault || col.defaultValue.kind == VALUE_NULL) {
                return fail("NOT NULL column requires DEFAULT");
            }
            table.addColumn(col);
            table.finalizeSchema();
            ensureSystemIndexes(table);
            Integer i2 = 0;
            while i2 < table.rows.count() {
                Row r = table.rows.get(i2);
                SqlValue v2 = makeNull();
                if col.hasDefault { v2 = col.defaultValue.clone(); }
                r.values.add(v2);
                i2 = i2 + 1;
            }
            rebuildIndexes(table);
            if !maybeSave() { return false; }
            Viper.Terminal.Say("ok");
            return true;
        }
        return fail("Unsupported ALTER TABLE");
    }

    expose func parseInsert(ts: TokenStream) -> Boolean {
        if !expectKeyword(ts, "INTO") { return false; }
        String name = ts.expectIdent();
        if name == "" { return fail("Expected table name"); }
        Integer idx = db.tableIndexByName(name);
        if idx < 0 { return fail("Table not found"); }
        Table table = db.tables.get(idx);
        List[Integer] columnOrder = [];
        if ts.matchSymbol("(") {
            while true {
                String colName = ts.expectIdent();
                if colName == "" { return fail("Expected column name"); }
                Integer colIndex = table.columnIndexByName(colName);
                if colIndex < 0 { return fail("Unknown column in insert"); }
                columnOrder.add(colIndex);
                if ts.matchSymbol(",") { continue; }
                break;
            }
            if !expectSymbol(ts, ")") { return false; }
        }
        if ts.matchKeyword("DEFAULT") {
            if !expectKeyword(ts, "VALUES") { return false; }
            List[SqlValue] rowVals = buildDefaultRow(table);
            if !insertRow(table, rowVals) { return false; }
            if !maybeSave() { return false; }
            Viper.Terminal.Say("ok");
            return true;
        }
        if !expectKeyword(ts, "VALUES") { return false; }
        while true {
            if !expectSymbol(ts, "(") { return false; }
            List[SqlValue] inputVals = [];
            while true {
                SqlValue? lit = parseLiteral(ts, true, true);
                if lit == null { return fail("Expected literal value"); }
                inputVals.add(lit);
                if ts.matchSymbol(",") { continue; }
                break;
            }
            if !expectSymbol(ts, ")") { return false; }
            List[SqlValue] rowVals = buildDefaultRow(table);
            Integer colCount = table.columns.count();
            Integer i = 0;
            if columnOrder.count() == 0 {
                if inputVals.count() != colCount { return fail("Column count mismatch"); }
                i = 0;
                while i < colCount {
                    rowVals.set(i, inputVals.get(i));
                    i = i + 1;
                }
            } else {
                if inputVals.count() != columnOrder.count() { return fail("Column count mismatch"); }
                i = 0;
                while i < columnOrder.count() {
                    Integer cidx = columnOrder.get(i);
                    rowVals.set(cidx, inputVals.get(i));
                    i = i + 1;
                }
            }
            if !insertRow(table, rowVals) { return false; }
            if ts.matchSymbol(",") { continue; }
            break;
        }
        if !maybeSave() { return false; }
        Viper.Terminal.Say("ok");
        return true;
    }

    expose func insertRow(table: Table, values: List[SqlValue]) -> Boolean {
        Integer colCount = table.columns.count();
        if values.count() != colCount { return fail("Insert values count mismatch"); }
        var row = new Row();
        row.values = [];
        row.deleted = false;
        Integer i = 0;
        while i < colCount {
            Column col = table.columns.get(i);
            SqlValue val = values.get(i);
            SqlValue? resolved = resolveDefaultValue(table, col, val);
            if resolved == null { return false; }
            SqlValue? coerced = coerceValue(resolved, col, !col.notNull);
            if coerced == null { return fail(Viper.String.Concat("Type mismatch for column ", col.name)); }
            if col.notNull && coerced.kind == VALUE_NULL {
                return fail(Viper.String.Concat("NULL not allowed for column ", col.name));
            }
            if col.autoInc && coerced.kind == VALUE_INT && coerced.i >= table.autoInc {
                table.autoInc = coerced.i + 1;
            }
            row.values.add(coerced);
            i = i + 1;
        }
        if !validateUniqueRow(table, row, table.rows.count()) { return false; }
        table.rows.add(row);
        if !updateIndexesForInsert(table, row, table.rows.count() - 1) { return false; }
        table.dirty = true;
        return true;
    }

    expose func buildDefaultRow(table: Table) -> List[SqlValue] {
        List[SqlValue] rowVals = [];
        Integer i = 0;
        while i < table.columns.count() {
            var v = new SqlValue();
            v.initDefault();
            rowVals.add(v);
            i = i + 1;
        }
        return rowVals;
    }

    expose func resolveDefaultValue(table: Table, col: Column, val: SqlValue) -> SqlValue? {
        SqlValue out = val;
        if out.kind == VALUE_DEFAULT {
            if col.autoInc {
                out = makeInt(table.autoInc);
                table.autoInc = table.autoInc + 1;
                return out;
            }
            if col.hasDefault {
                return col.defaultValue.clone();
            }
            return makeNull();
        }
        if col.autoInc && out.kind == VALUE_NULL {
            out = makeInt(table.autoInc);
            table.autoInc = table.autoInc + 1;
        }
        return out;
    }

    expose func validateUniqueRow(table: Table, row: Row, rowIndex: Integer) -> Boolean {
        ensureSystemIndexes(table);
        if table.hasPrimary {
            SqlValue pk = row.values.get(table.primaryIndex);
            if pk.kind == VALUE_NULL { return fail("Primary key cannot be NULL"); }
            String key = pk.key();
            if table.pkLookup.has(key) { return fail("Primary key conflict"); }
        }
        Integer i = 0;
        while i < table.indexes.count() {
            Index idx = table.indexes.get(i);
            if idx.unique {
                SqlValue v = row.values.get(idx.colIndex);
                if v.kind != VALUE_NULL {
                    String key2 = v.key();
                    if idx.buckets.has(key2) { return fail("Unique index conflict"); }
                }
            }
            i = i + 1;
        }
        return true;
    }

    expose func updateIndexesForInsert(table: Table, row: Row, rowIndex: Integer) -> Boolean {
        if table.hasPrimary {
            SqlValue pk = row.values.get(table.primaryIndex);
            if pk.kind == VALUE_NULL { return fail("Primary key cannot be NULL"); }
            table.pkLookup.set(pk.key(), rowIndex);
        }
        Integer i = 0;
        while i < table.indexes.count() {
            Index idx = table.indexes.get(i);
            SqlValue v = row.values.get(idx.colIndex);
            String key = v.key();
            List[Integer] bucket = [];
            if idx.buckets.has(key) { bucket = idx.buckets.get(key); }
            bucket.add(rowIndex);
            idx.buckets.set(key, bucket);
            i = i + 1;
        }
        return true;
    }

    expose func validateUpdate(table: Table, targets: List[Integer], newValues: List[List[SqlValue]]) -> Boolean {
        Map[String, Integer] updates = new Map[String, Integer]();
        Integer i = 0;
        while i < targets.count() {
            updates.set(Viper.Fmt.Int(targets.get(i)), i);
            i = i + 1;
        }
        Map[String, Integer] pkMap = new Map[String, Integer]();
        Integer r = 0;
        while r < table.rows.count() {
            Row row = table.rows.get(r);
            if !row.deleted {
                List[SqlValue] vals = row.values;
                String key = Viper.Fmt.Int(r);
                if updates.has(key) {
                    vals = newValues.get(updates.get(key));
                }
                if table.hasPrimary {
                    SqlValue pk = vals.get(table.primaryIndex);
                    if pk.kind == VALUE_NULL { return fail("Primary key cannot be NULL"); }
                    String pkKey = pk.key();
                    if pkMap.has(pkKey) { return fail("Primary key conflict"); }
                    pkMap.set(pkKey, r);
                }
            }
            r = r + 1;
        }
        Integer idx = 0;
        while idx < table.indexes.count() {
            Index index = table.indexes.get(idx);
            if index.unique {
                Map[String, Integer] seen = new Map[String, Integer]();
                r = 0;
                while r < table.rows.count() {
                    Row row2 = table.rows.get(r);
                    if !row2.deleted {
                        List[SqlValue] vals2 = row2.values;
                        String key2 = Viper.Fmt.Int(r);
                        if updates.has(key2) {
                            vals2 = newValues.get(updates.get(key2));
                        }
                        SqlValue v = vals2.get(index.colIndex);
                        if v.kind != VALUE_NULL {
                            String k = v.key();
                            if seen.has(k) { return fail("Unique index conflict"); }
                            seen.set(k, r);
                        }
                    }
                    r = r + 1;
                }
            }
            idx = idx + 1;
        }
        return true;
    }

    expose func isClauseStart(ts: TokenStream) -> Boolean {
        if ts.isKeyword("FROM") { return true; }
        if ts.isKeyword("WHERE") { return true; }
        if ts.isKeyword("GROUP") { return true; }
        if ts.isKeyword("HAVING") { return true; }
        if ts.isKeyword("ORDER") { return true; }
        if ts.isKeyword("LIMIT") { return true; }
        if ts.isKeyword("OFFSET") { return true; }
        if ts.isKeyword("UNION") { return true; }
        return false;
    }

    expose func exprHasColumn(expr: ValueExpr?) -> Boolean {
        if expr == null { return false; }
        if expr.kind == VAL_COLUMN { return true; }
        if exprHasColumn(expr.left) { return true; }
        if exprHasColumn(expr.right) { return true; }
        Integer i = 0;
        while i < expr.args.count() {
            if exprHasColumn(expr.args.get(i)) { return true; }
            i = i + 1;
        }
        return false;
    }

    expose func makeSelectAlias(expr: ValueExpr?, idx: Integer, table: Table?) -> String {
        if expr == null { return Viper.String.Concat("expr", Viper.Fmt.Int(idx)); }
        if expr.kind == VAL_COLUMN {
            if table != null && expr.colIndex >= 0 {
                return table.columns.get(expr.colIndex).name;
            }
            return expr.name != "" ? expr.name : Viper.String.Concat("col", Viper.Fmt.Int(idx));
        }
        if expr.kind == VAL_FUNC {
            return Viper.String.Concat(expr.name, Viper.String.Concat("_", Viper.Fmt.Int(idx)));
        }
        return Viper.String.Concat("expr", Viper.Fmt.Int(idx));
    }

    expose func buildResultSet(table: Table?, items: List[SelectItem], whereExpr: Expr?, groupBy: List[ValueExpr], havingExpr: Expr?, distinct: Boolean, orderSpecs: List[OrderSpec], limit: Integer, offset: Integer) -> ResultSet? {
        var rs = new ResultSet();
        rs.headers = [];
        rs.rows = [];
        Integer i = 0;
        while i < items.count() {
            rs.headers.add(items.get(i).alias);
            i = i + 1;
        }

        if table == null {
            var dummy = new Table();
            dummy.init("__dual");
            var base = new Row();
            base.values = [];
            base.deleted = false;
            dummy.rows.add(base);
            List[Integer] rowIdx0 = [];
            rowIdx0.add(0);
            var row = new Row();
            row.values = [];
            row.deleted = false;
            Integer c = 0;
            while c < items.count() {
                SqlValue v = evalValueExprGroup(items.get(c).expr, dummy, rowIdx0);
                row.values.add(v);
                c = c + 1;
            }
            rs.rows.add(row);
            return finalizeResultSet(rs, orderSpecs, limit, offset, distinct);
        }

        List[Integer] rowIdx = [];
        i = 0;
        while i < table.rows.count() {
            Row r = table.rows.get(i);
            if !r.deleted {
                if evalExpr(whereExpr, table, r) {
                    rowIdx.add(i);
                }
            }
            i = i + 1;
        }

        Boolean hasAgg = false;
        Boolean hasGroup = groupBy.count() > 0;
        i = 0;
        while i < items.count() {
            if exprHasAggregate(items.get(i).expr) { hasAgg = true; }
            i = i + 1;
        }
        if exprHasAggregateExpr(havingExpr) { hasAgg = true; }
        if havingExpr != null && !hasGroup && !hasAgg {
            return failResult("HAVING requires GROUP BY or aggregate");
        }

        if hasGroup || hasAgg {
            Map[String, List[Integer]] groups = new Map[String, List[Integer]]();
            List[String] groupKeys = [];
            i = 0;
            while i < rowIdx.count() {
                Row r2 = table.rows.get(rowIdx.get(i));
                String key = "";
                if groupBy.count() == 0 {
                    key = "__all__";
                } else {
                    Integer g = 0;
                    while g < groupBy.count() {
                        SqlValue v2 = evalValueExpr(groupBy.get(g), table, r2);
                        String part = encodeValue(v2);
                        key = g == 0 ? part : Viper.String.Concat(Viper.String.Concat(key, "|"), part);
                        g = g + 1;
                    }
                }
                List[Integer] bucket = [];
                if !groups.has(key) { groupKeys.add(key); }
                if groups.has(key) { bucket = groups.get(key); }
                bucket.add(rowIdx.get(i));
                groups.set(key, bucket);
                i = i + 1;
            }
            i = 0;
            while i < groupKeys.count() {
                String key2 = groupKeys.get(i);
                List[Integer] bucket2 = groups.get(key2);
                if havingExpr != null {
                    if !evalExprGroup(havingExpr, table, bucket2) {
                        i = i + 1;
                        continue;
                    }
                }
                var row3 = new Row();
                row3.values = [];
                row3.deleted = false;
                Integer c2 = 0;
                while c2 < items.count() {
                    SqlValue v3 = evalValueExprGroup(items.get(c2).expr, table, bucket2);
                    row3.values.add(v3);
                    c2 = c2 + 1;
                }
                rs.rows.add(row3);
                i = i + 1;
            }
        } else {
            i = 0;
            while i < rowIdx.count() {
                Row r3 = table.rows.get(rowIdx.get(i));
                var out = new Row();
                out.values = [];
                out.deleted = false;
                Integer c3 = 0;
                while c3 < items.count() {
                    SqlValue v4 = evalValueExpr(items.get(c3).expr, table, r3);
                    out.values.add(v4);
                    c3 = c3 + 1;
                }
                rs.rows.add(out);
                i = i + 1;
            }
        }
        return finalizeResultSet(rs, orderSpecs, limit, offset, distinct);
    }

    expose func finalizeResultSet(rs: ResultSet, orderSpecs: List[OrderSpec], limit: Integer, offset: Integer, distinct: Boolean) -> ResultSet {
        if orderSpecs.count() > 0 {
            sortResultRows(rs, orderSpecs);
        }
        if distinct {
            rs = distinctRows(rs);
        }
        if offset > 0 || limit >= 0 {
            rs = applyLimitOffset(rs, limit, offset);
        }
        return rs;
    }

    expose func distinctRows(rs: ResultSet) -> ResultSet {
        var out = new ResultSet();
        out.headers = rs.headers;
        out.rows = [];
        Map[String, Boolean] seen = new Map[String, Boolean]();
        Integer i = 0;
        while i < rs.rows.count() {
            Row r = rs.rows.get(i);
            String key = "";
            Integer c = 0;
            while c < r.values.count() {
                String part = encodeValue(r.values.get(c));
                key = c == 0 ? part : Viper.String.Concat(Viper.String.Concat(key, "|"), part);
                c = c + 1;
            }
            if !seen.has(key) {
                seen.set(key, true);
                out.rows.add(r);
            }
            i = i + 1;
        }
        return out;
    }

    expose func applyLimitOffset(rs: ResultSet, limit: Integer, offset: Integer) -> ResultSet {
        var out = new ResultSet();
        out.headers = rs.headers;
        out.rows = [];
        Integer start = offset;
        if start < 0 { start = 0; }
        Integer end = rs.rows.count();
        if limit >= 0 && start + limit < end { end = start + limit; }
        Integer i = start;
        while i < end {
            out.rows.add(rs.rows.get(i));
            i = i + 1;
        }
        return out;
    }

    expose func sortResultRows(rs: ResultSet, orderSpecs: List[OrderSpec]) {
        Integer n = rs.rows.count();
        Integer i = 0;
        while i < n {
            Integer j = 0;
            while j + 1 < n {
                Row a = rs.rows.get(j);
                Row b = rs.rows.get(j + 1);
                Integer cmp = compareResultRow(rs, a, b, orderSpecs);
                if cmp > 0 {
                    Row temp = rs.rows.get(j);
                    rs.rows.set(j, rs.rows.get(j + 1));
                    rs.rows.set(j + 1, temp);
                }
                j = j + 1;
            }
            i = i + 1;
        }
    }

    expose func compareResultRow(rs: ResultSet, a: Row, b: Row, orderSpecs: List[OrderSpec]) -> Integer {
        Integer i = 0;
        while i < orderSpecs.count() {
            OrderSpec spec = orderSpecs.get(i);
            Integer idx = spec.index;
            if idx < 0 {
                idx = resolveOrderIndex(rs, spec.name);
            }
            if idx < 0 || idx >= a.values.count() { i = i + 1; continue; }
            Integer cmp = compareValueForSort(a.values.get(idx), b.values.get(idx));
            if spec.desc { cmp = 0 - cmp; }
            if cmp != 0 { return cmp; }
            i = i + 1;
        }
        return 0;
    }

    expose func resolveOrderIndex(rs: ResultSet, name: String) -> Integer {
        String key = Viper.String.ToUpper(name);
        Integer i = 0;
        while i < rs.headers.count() {
            if Viper.String.ToUpper(rs.headers.get(i)) == key { return i; }
            i = i + 1;
        }
        return -1;
    }

    expose func printResultSet(rs: ResultSet) {
        Integer colCount = rs.headers.count();
        List[Integer] widths = [];
        Integer i = 0;
        while i < colCount {
            widths.add(Viper.String.Length(rs.headers.get(i)));
            i = i + 1;
        }
        i = 0;
        while i < rs.rows.count() {
            Row r = rs.rows.get(i);
            Integer j = 0;
            while j < colCount {
                Integer w = Viper.String.Length(r.values.get(j).display());
                if w > widths.get(j) { widths.set(j, w); }
                j = j + 1;
            }
            i = i + 1;
        }
        i = 0;
        while i < colCount {
            String cell = Viper.Fmt.PadRight(rs.headers.get(i), widths.get(i) + 2);
            Viper.Terminal.Print(cell);
            i = i + 1;
        }
        Viper.Terminal.Print("\n");
        i = 0;
        while i < rs.rows.count() {
            Row r2 = rs.rows.get(i);
            Integer j2 = 0;
            while j2 < colCount {
                String cell2 = Viper.Fmt.PadRight(r2.values.get(j2).display(), widths.get(j2) + 2);
                Viper.Terminal.Print(cell2);
                j2 = j2 + 1;
            }
            Viper.Terminal.Print("\n");
            i = i + 1;
        }
    }

    expose func parseSelect(ts: TokenStream) -> Boolean {
        Boolean distinct = false;
        if ts.matchKeyword("DISTINCT") { distinct = true; }
        List[SelectItem] items = [];
        while true {
            if ts.matchSymbol("*") {
                var star = new SelectItem();
                star.kind = SEL_STAR;
                star.alias = "*";
                star.expr = null;
                star.aggFunc = "";
                star.distinct = false;
                star.countStar = false;
                items.add(star);
            } else {
                ValueExpr? expr = parseValueExpr(ts, null, true);
                if expr == null { return false; }
                var item = new SelectItem();
                item.kind = SEL_VALUE;
                item.expr = expr;
                item.alias = "";
                item.aggFunc = "";
                item.distinct = false;
                item.countStar = false;
                if ts.matchKeyword("AS") {
                    String alias = ts.expectIdent();
                    if alias == "" { return fail("Expected alias"); }
                    item.alias = alias;
            } else if ts.peek().kind == TOK_IDENT && !isClauseStart(ts) {
                item.alias = ts.expectIdent();
            }
                items.add(item);
            }
            if ts.matchSymbol(",") { continue; }
            break;
        }

        Table? table = null;
        if ts.matchKeyword("FROM") {
            String name = ts.expectIdent();
            if name == "" { return fail("Expected table name"); }
            Integer idx = db.tableIndexByName(name);
            if idx < 0 { return fail("Table not found"); }
            table = db.tables.get(idx);
        }

        List[SelectItem] resolved = [];
        Integer exprIdx = 1;
        Integer i = 0;
        while i < items.count() {
            SelectItem it = items.get(i);
            if it.kind == SEL_STAR {
                if table == null { return fail("'*' requires FROM"); }
                Integer c = 0;
                while c < table.columns.count() {
                    Column col = table.columns.get(c);
                    var expr = new ValueExpr();
                    expr.kind = VAL_COLUMN;
                    expr.colIndex = c;
                    expr.name = col.name;
                    expr.args = [];
                    expr.countStar = false;
                    expr.distinct = false;
                    var item2 = new SelectItem();
                    item2.kind = SEL_VALUE;
                    item2.expr = expr;
                    item2.alias = col.name;
                    item2.aggFunc = "";
                    item2.distinct = false;
                    item2.countStar = false;
                    resolved.add(item2);
                    c = c + 1;
                }
            } else {
                if table != null {
                    if !resolveValueExpr(it.expr, table) {
                        return fail("Unknown column in SELECT");
                    }
                } else {
                    if exprHasColumn(it.expr) {
                        return fail("SELECT column requires FROM");
                    }
                }
                if it.alias == "" {
                    it.alias = makeSelectAlias(it.expr, exprIdx, table);
                }
                resolved.add(it);
                exprIdx = exprIdx + 1;
            }
            i = i + 1;
        }
        items = resolved;

        Expr? whereExpr = null;
        if ts.matchKeyword("WHERE") {
            if table == null { return fail("WHERE requires FROM"); }
            whereExpr = parseExpression(ts, table);
            if whereExpr == null { return false; }
        }
        if whereExpr != null && exprHasAggregateExpr(whereExpr) {
            return fail("Aggregate functions not allowed in WHERE");
        }

        List[ValueExpr] groupBy = [];
        if ts.matchKeyword("GROUP") {
            if !expectKeyword(ts, "BY") { return false; }
            if table == null { return fail("GROUP BY requires FROM"); }
            while true {
                ValueExpr? gexpr = parseValueExpr(ts, table, false);
                if gexpr == null { return false; }
                if exprHasAggregate(gexpr) { return fail("Aggregate not allowed in GROUP BY"); }
                groupBy.add(gexpr);
                if ts.matchSymbol(",") { continue; }
                break;
            }
        }

        Expr? havingExpr = null;
        if ts.matchKeyword("HAVING") {
            if table == null { return fail("HAVING requires FROM"); }
            havingExpr = parseExpression(ts, table);
            if havingExpr == null { return false; }
        }

        List[OrderSpec] orderSpecs = [];
        if ts.matchKeyword("ORDER") {
            if !expectKeyword(ts, "BY") { return false; }
            while true {
                var spec = new OrderSpec();
                spec.index = -1;
                spec.name = "";
                spec.desc = false;
                Token t = ts.peek();
                if t.kind == TOK_NUMBER && isIntText(t.text) {
                    ts.advance();
                    spec.index = parseIntText(t.text) - 1;
                } else {
                    String name2 = ts.expectIdent();
                    if name2 == "" { return fail("Expected column in ORDER BY"); }
                    spec.name = name2;
                }
                if ts.matchKeyword("DESC") { spec.desc = true; }
                else if ts.matchKeyword("ASC") { spec.desc = false; }
                orderSpecs.add(spec);
                if ts.matchSymbol(",") { continue; }
                break;
            }
        }

        Integer limit = -1;
        Integer offset = 0;
        if ts.matchKeyword("LIMIT") {
            SqlValue? lit = parseLiteral(ts, false, false);
            if lit == null || lit.kind != VALUE_INT { return fail("LIMIT expects integer"); }
            limit = lit.i;
            if ts.matchSymbol(",") {
                SqlValue? lit2 = parseLiteral(ts, false, false);
                if lit2 == null || lit2.kind != VALUE_INT { return fail("LIMIT expects integer"); }
                offset = limit;
                limit = lit2.i;
            } else if ts.matchKeyword("OFFSET") {
                SqlValue? lit3 = parseLiteral(ts, false, false);
                if lit3 == null || lit3.kind != VALUE_INT { return fail("OFFSET expects integer"); }
                offset = lit3.i;
            }
        }

        ResultSet? rs = buildResultSet(table, items, whereExpr, groupBy, havingExpr, distinct, orderSpecs, limit, offset);
        if rs == null { return false; }
        ResultSet out = rs;
        printResultSet(out);
        Viper.Terminal.Say(Viper.String.Concat("rows: ", Viper.Fmt.Int(out.rows.count())));
        return true;
    }

    expose func parseUpdate(ts: TokenStream) -> Boolean {
        String name = ts.expectIdent();
        if name == "" { return fail("Expected table name"); }
        Integer idx = db.tableIndexByName(name);
        if idx < 0 { return fail("Table not found"); }
        Table table = db.tables.get(idx);
        if !expectKeyword(ts, "SET") { return false; }
        List[Integer] setCols = [];
        List[ValueExpr] setExprs = [];
        while true {
            String colName = ts.expectIdent();
            if colName == "" { return fail("Expected column name"); }
            Integer colIndex = table.columnIndexByName(colName);
            if colIndex < 0 { return fail("Unknown column in SET"); }
            if !expectSymbol(ts, "=") { return false; }
            ValueExpr? expr = null;
            if ts.matchKeyword("DEFAULT") {
                var v = new SqlValue();
                v.initDefault();
                var e = new ValueExpr();
                e.kind = VAL_LITERAL;
                e.literal = v;
                e.args = [];
                e.countStar = false;
                e.distinct = false;
                expr = e;
            } else {
                expr = parseValueExpr(ts, table, false);
            }
            if expr == null { return fail("Expected value expression"); }
            setCols.add(colIndex);
            setExprs.add(expr);
            if ts.matchSymbol(",") { continue; }
            break;
        }
        Expr? whereExpr = null;
        if ts.matchKeyword("WHERE") {
            whereExpr = parseExpression(ts, table);
            if whereExpr == null { return false; }
        }
        List[Integer] targets = [];
        List[List[SqlValue]] newValues = [];
        Integer i = 0;
        while i < table.rows.count() {
            Row r = table.rows.get(i);
            if !r.deleted && evalExpr(whereExpr, table, r) {
                List[SqlValue] vals = [];
                Integer c = 0;
                while c < r.values.count() {
                    vals.add(r.values.get(c).clone());
                    c = c + 1;
                }
                Integer s = 0;
                while s < setCols.count() {
                    Integer cidx = setCols.get(s);
                    Column col = table.columns.get(cidx);
                    SqlValue val = evalValueExpr(setExprs.get(s), table, r);
                    SqlValue? resolved = resolveDefaultValue(table, col, val);
                    if resolved == null { return false; }
                    SqlValue? coerced = coerceValue(resolved, col, !col.notNull);
                    if coerced == null { return fail("Type mismatch in UPDATE"); }
                    if col.notNull && coerced.kind == VALUE_NULL { return fail("NULL not allowed in UPDATE"); }
                    vals.set(cidx, coerced);
                    s = s + 1;
                }
                targets.add(i);
                newValues.add(vals);
            }
            i = i + 1;
        }
        if !validateUpdate(table, targets, newValues) { return false; }
        Integer updated = 0;
        i = 0;
        while i < targets.count() {
            Integer rowIndex = targets.get(i);
            Row r2 = table.rows.get(rowIndex);
            List[SqlValue] vals2 = newValues.get(i);
            r2.values = vals2;
            updated = updated + 1;
            i = i + 1;
        }
        rebuildPrimaryIndex(table);
        rebuildIndexes(table);
        if !maybeSave() { return false; }
        Viper.Terminal.Say(Viper.String.Concat("updated: ", Viper.Fmt.Int(updated)));
        return true;
    }

    expose func parseDelete(ts: TokenStream) -> Boolean {
        if !expectKeyword(ts, "FROM") { return false; }
        String name = ts.expectIdent();
        if name == "" { return fail("Expected table name"); }
        Integer idx = db.tableIndexByName(name);
        if idx < 0 { return fail("Table not found"); }
        Table table = db.tables.get(idx);
        Expr? whereExpr = null;
        if ts.matchKeyword("WHERE") {
            whereExpr = parseExpression(ts, table);
            if whereExpr == null { return false; }
        }
        Integer deleted = 0;
        Integer i = 0;
        while i < table.rows.count() {
            Row r = table.rows.get(i);
            if !r.deleted && evalExpr(whereExpr, table, r) {
                r.deleted = true;
                deleted = deleted + 1;
            }
            i = i + 1;
        }
        rebuildPrimaryIndex(table);
        rebuildIndexes(table);
        if !maybeSave() { return false; }
        Viper.Terminal.Say(Viper.String.Concat("deleted: ", Viper.Fmt.Int(deleted)));
        return true;
    }

    expose func parseShow(ts: TokenStream) -> Boolean {
        if ts.matchKeyword("TABLES") {
            Integer i = 0;
            while i < db.tables.count() {
                Viper.Terminal.Say(db.tables.get(i).name);
                i = i + 1;
            }
            return true;
        }
        if ts.matchKeyword("INDEXES") || ts.matchKeyword("INDEX") {
            return showIndexes(ts);
        }
        if ts.matchKeyword("SCHEMA") {
            return showSchema(ts);
        }
        if ts.matchKeyword("CREATE") {
            if !expectKeyword(ts, "TABLE") { return false; }
            return showSchema(ts);
        }
        return fail("Expected TABLES, INDEXES, or SCHEMA");
    }

    expose func parseDescribe(ts: TokenStream) -> Boolean {
        String name = ts.expectIdent();
        if name == "" { return fail("Expected table name"); }
        Integer idx = db.tableIndexByName(name);
        if idx < 0 { return fail("Table not found"); }
        Table table = db.tables.get(idx);
        Integer i = 0;
        while i < table.columns.count() {
            Column c = table.columns.get(i);
            String line = Viper.String.Concat(c.name, " ");
            line = Viper.String.Concat(line, typeName(c.typeCode));
            if c.primaryKey { line = Viper.String.Concat(line, " PRIMARY KEY"); }
            if c.notNull { line = Viper.String.Concat(line, " NOT NULL"); }
            if c.autoInc { line = Viper.String.Concat(line, " AUTOINCREMENT"); }
            if c.unique { line = Viper.String.Concat(line, " UNIQUE"); }
            if c.hasDefault {
                line = Viper.String.Concat(line, " DEFAULT ");
                line = Viper.String.Concat(line, c.defaultValue.display());
            }
            Viper.Terminal.Say(line);
            i = i + 1;
        }
        return true;
    }

    expose func showIndexes(ts: TokenStream) -> Boolean {
        String name = "";
        if ts.matchKeyword("FROM") || ts.matchKeyword("ON") {
            name = ts.expectIdent();
            if name == "" { return fail("Expected table name"); }
        } else if ts.peek().kind == TOK_IDENT {
            name = ts.expectIdent();
        }
        if name != "" {
            Integer idx = db.tableIndexByName(name);
            if idx < 0 { return fail("Table not found"); }
            showTableIndexes(db.tables.get(idx));
            return true;
        }
        Integer t = 0;
        while t < db.tables.count() {
            showTableIndexes(db.tables.get(t));
            t = t + 1;
        }
        return true;
    }

    expose func showTableIndexes(table: Table) {
        Integer i = 0;
        while i < table.indexes.count() {
            Index idx = table.indexes.get(i);
            String colName = "?";
            if idx.colIndex >= 0 && idx.colIndex < table.columns.count() {
                colName = table.columns.get(idx.colIndex).name;
            }
            String line = Viper.String.Concat(table.name, " ");
            line = Viper.String.Concat(line, idx.name);
            line = Viper.String.Concat(line, " (");
            line = Viper.String.Concat(line, colName);
            line = Viper.String.Concat(line, ")");
            if idx.unique { line = Viper.String.Concat(line, " UNIQUE"); }
            if idx.system { line = Viper.String.Concat(line, " [system]"); }
            Viper.Terminal.Say(line);
            i = i + 1;
        }
    }

    expose func showSchema(ts: TokenStream) -> Boolean {
        String name = "";
        if ts.peek().kind == TOK_IDENT {
            name = ts.expectIdent();
        }
        if name != "" {
            Integer idx = db.tableIndexByName(name);
            if idx < 0 { return fail("Table not found"); }
            Table table = db.tables.get(idx);
            Viper.Terminal.Say(renderCreateTable(table));
            Integer j = 0;
            while j < table.indexes.count() {
                Index idx2 = table.indexes.get(j);
                if !idx2.system {
                    Viper.Terminal.Say(renderCreateIndex(table, idx2));
                }
                j = j + 1;
            }
            return true;
        }
        Integer t = 0;
        while t < db.tables.count() {
            Table table2 = db.tables.get(t);
            Viper.Terminal.Say(renderCreateTable(table2));
            Integer k = 0;
            while k < table2.indexes.count() {
                Index idx3 = table2.indexes.get(k);
                if !idx3.system {
                    Viper.Terminal.Say(renderCreateIndex(table2, idx3));
                }
                k = k + 1;
            }
            if t + 1 < db.tables.count() { Viper.Terminal.Say(""); }
            t = t + 1;
        }
        return true;
    }

    expose func sqlValueLiteral(v: SqlValue) -> String {
        if v.kind == VALUE_NULL { return "NULL"; }
        if v.kind == VALUE_INT { return Viper.Fmt.Int(v.i); }
        if v.kind == VALUE_NUM { return Viper.Fmt.Num(v.n); }
        if v.kind == VALUE_BOOL { return v.b ? "TRUE" : "FALSE"; }
        if v.kind == VALUE_DEFAULT { return "DEFAULT"; }
        String escaped = Viper.String.Replace(v.s, "'", "''");
        return Viper.String.Concat(Viper.String.Concat("'", escaped), "'");
    }

    expose func renderCreateTable(table: Table) -> String {
        var sb = Viper.Text.StringBuilder.New();
        Viper.Text.StringBuilder.Append(sb, "CREATE TABLE ");
        Viper.Text.StringBuilder.Append(sb, table.name);
        Viper.Text.StringBuilder.Append(sb, " (");
        Integer i = 0;
        while i < table.columns.count() {
            Column c = table.columns.get(i);
            if i > 0 { Viper.Text.StringBuilder.Append(sb, ", "); }
            Viper.Text.StringBuilder.Append(sb, c.name);
            Viper.Text.StringBuilder.Append(sb, " ");
            Viper.Text.StringBuilder.Append(sb, typeName(c.typeCode));
            if c.primaryKey { Viper.Text.StringBuilder.Append(sb, " PRIMARY KEY"); }
            if c.notNull { Viper.Text.StringBuilder.Append(sb, " NOT NULL"); }
            if c.autoInc { Viper.Text.StringBuilder.Append(sb, " AUTOINCREMENT"); }
            if c.unique { Viper.Text.StringBuilder.Append(sb, " UNIQUE"); }
            if c.hasDefault {
                Viper.Text.StringBuilder.Append(sb, " DEFAULT ");
                Viper.Text.StringBuilder.Append(sb, sqlValueLiteral(c.defaultValue));
            }
            i = i + 1;
        }
        Viper.Text.StringBuilder.Append(sb, ")");
        return Viper.Text.StringBuilder.ToString(sb);
    }

    expose func renderCreateIndex(table: Table, idx: Index) -> String {
        String colName = "?";
        if idx.colIndex >= 0 && idx.colIndex < table.columns.count() {
            colName = table.columns.get(idx.colIndex).name;
        }
        String line = idx.unique ? "CREATE UNIQUE INDEX " : "CREATE INDEX ";
        line = Viper.String.Concat(line, idx.name);
        line = Viper.String.Concat(line, " ON ");
        line = Viper.String.Concat(line, table.name);
        line = Viper.String.Concat(line, " (");
        line = Viper.String.Concat(line, colName);
        line = Viper.String.Concat(line, ")");
        return line;
    }

    expose func parseVacuum(ts: TokenStream) -> Boolean {
        if ts.atEnd() || ts.peek().kind == TOK_EOF {
            Integer i = 0;
            while i < db.tables.count() {
                vacuumTable(db.tables.get(i));
                i = i + 1;
            }
            if !maybeSave() { return false; }
            Viper.Terminal.Say("ok");
            return true;
        }
        String name = ts.expectIdent();
        if name == "" { return fail("Expected table name"); }
        Integer idx = db.tableIndexByName(name);
        if idx < 0 { return fail("Table not found"); }
        vacuumTable(db.tables.get(idx));
        if !maybeSave() { return false; }
        Viper.Terminal.Say("ok");
        return true;
    }

    expose func parseOpen(ts: TokenStream) -> Boolean {
        Token t = ts.peek();
        if t.kind == TOK_STRING || t.kind == TOK_IDENT {
            ts.advance();
            String path = t.text;
            var d = new Database();
            d.init(path);
            if !loadDatabase(d) { return fail(d.lastError); }
            db = d;
            Viper.Terminal.Say(Viper.String.Concat("opened: ", path));
            return true;
        }
        return fail("Expected path");
    }

    expose func parseExport(ts: TokenStream) -> Boolean {
        String name = ts.expectIdent();
        if name == "" { return fail("Expected table name"); }
        if !expectKeyword(ts, "TO") { return false; }
        Token t = ts.peek();
        if t.kind != TOK_STRING && t.kind != TOK_IDENT { return fail("Expected output path"); }
        ts.advance();
        String outPath = t.text;
        Integer idx = db.tableIndexByName(name);
        if idx < 0 { return fail("Table not found"); }
        Table table = db.tables.get(idx);
        String csv = exportCsv(table);
        Viper.IO.File.WriteAllText(outPath, csv);
        Viper.Terminal.Say("ok");
        return true;
    }

    expose func parseImport(ts: TokenStream) -> Boolean {
        String name = ts.expectIdent();
        if name == "" { return fail("Expected table name"); }
        if !expectKeyword(ts, "FROM") { return false; }
        Token t = ts.peek();
        if t.kind != TOK_STRING && t.kind != TOK_IDENT { return fail("Expected input path"); }
        ts.advance();
        String inPath = t.text;
        Integer idx = db.tableIndexByName(name);
        if idx < 0 { return fail("Table not found"); }
        Table table = db.tables.get(idx);
        if !Viper.IO.File.Exists(inPath) { return fail("File not found"); }
        String content = Viper.IO.File.ReadAllText(inPath);
        if !importCsv(table, content) { return false; }
        if !maybeSave() { return false; }
        Viper.Terminal.Say("ok");
        return true;
    }

    expose func parseExpression(ts: TokenStream, table: Table) -> Expr? {
        return parseOr(ts, table);
    }

    expose func parseOr(ts: TokenStream, table: Table) -> Expr? {
        Expr? left = parseAnd(ts, table);
        if left == null { return null; }
        while ts.matchKeyword("OR") {
            Expr? right = parseAnd(ts, table);
            if right == null { return null; }
            var expr = new Expr();
            expr.kind = EXPR_OR;
            expr.left = left;
            expr.right = right;
            left = expr;
        }
        return left;
    }

    expose func parseAnd(ts: TokenStream, table: Table) -> Expr? {
        Expr? left = parseNot(ts, table);
        if left == null { return null; }
        while ts.matchKeyword("AND") {
            Expr? right = parseNot(ts, table);
            if right == null { return null; }
            var expr = new Expr();
            expr.kind = EXPR_AND;
            expr.left = left;
            expr.right = right;
            left = expr;
        }
        return left;
    }

    expose func parseNot(ts: TokenStream, table: Table) -> Expr? {
        if ts.matchKeyword("NOT") {
            Expr? inner = parseNot(ts, table);
            if inner == null { return null; }
            var expr = new Expr();
            expr.kind = EXPR_NOT;
            expr.left = inner;
            return expr;
        }
        if ts.matchSymbol("(") {
            Expr? inner2 = parseExpression(ts, table);
            if inner2 == null { return null; }
            if !expectSymbol(ts, ")") { return null; }
            return inner2;
        }
        return parseComparison(ts, table);
    }

    expose func parseComparison(ts: TokenStream, table: Table) -> Expr? {
        ValueExpr? left = parseValueExpr(ts, table, false);
        if left == null { return null; }

        if ts.matchKeyword("IS") {
            Boolean notFlag = ts.matchKeyword("NOT");
            if ts.matchKeyword("NULL") {
                var expr = new Expr();
                expr.kind = notFlag ? EXPR_IS_NOT_NULL : EXPR_IS_NULL;
                expr.leftValue = left;
                return expr;
            }
            if ts.matchKeyword("TRUE") {
                var lit = new SqlValue();
                lit.initBool(true);
                var rhs = new ValueExpr();
                rhs.kind = VAL_LITERAL;
                rhs.literal = lit;
                rhs.args = [];
                rhs.countStar = false;
                rhs.distinct = false;
                var expr2 = new Expr();
                expr2.kind = EXPR_COMPARE;
                expr2.leftValue = left;
                expr2.rightValue = rhs;
                expr2.op = notFlag ? "!=" : "=";
                return expr2;
            }
            if ts.matchKeyword("FALSE") {
                var lit2 = new SqlValue();
                lit2.initBool(false);
                var rhs2 = new ValueExpr();
                rhs2.kind = VAL_LITERAL;
                rhs2.literal = lit2;
                rhs2.args = [];
                rhs2.countStar = false;
                rhs2.distinct = false;
                var expr3 = new Expr();
                expr3.kind = EXPR_COMPARE;
                expr3.leftValue = left;
                expr3.rightValue = rhs2;
                expr3.op = notFlag ? "!=" : "=";
                return expr3;
            }
            return failExpr("Expected NULL or boolean");
        }

        Boolean notFlag2 = false;
        if ts.matchKeyword("NOT") { notFlag2 = true; }

        if ts.matchKeyword("BETWEEN") {
            ValueExpr? low = parseValueExpr(ts, table, false);
            if low == null { return null; }
            if !expectKeyword(ts, "AND") { return null; }
            ValueExpr? high = parseValueExpr(ts, table, false);
            if high == null { return null; }
            var ge = new Expr();
            ge.kind = EXPR_COMPARE;
            ge.leftValue = left;
            ge.rightValue = low;
            ge.op = ">=";
            var le = new Expr();
            le.kind = EXPR_COMPARE;
            le.leftValue = left;
            le.rightValue = high;
            le.op = "<=";
            var andExpr = new Expr();
            andExpr.kind = EXPR_AND;
            andExpr.left = ge;
            andExpr.right = le;
            if notFlag2 {
                var notExpr = new Expr();
                notExpr.kind = EXPR_NOT;
                notExpr.left = andExpr;
                return notExpr;
            }
            return andExpr;
        }

        if ts.matchKeyword("IN") {
            Expr? inExpr = parseInList(ts, left);
            if inExpr == null { return null; }
            if notFlag2 {
                var notExpr2 = new Expr();
                notExpr2.kind = EXPR_NOT;
                notExpr2.left = inExpr;
                return notExpr2;
            }
            return inExpr;
        }

        if ts.matchKeyword("LIKE") {
            ValueExpr? right = parseValueExpr(ts, table, false);
            if right == null { return null; }
            var expr3 = new Expr();
            expr3.kind = EXPR_COMPARE;
            expr3.leftValue = left;
            expr3.rightValue = right;
            expr3.op = notFlag2 ? "NOT LIKE" : "LIKE";
            return expr3;
        }

        if notFlag2 { return failExpr("Expected IN, BETWEEN, or LIKE"); }

        Token opTok = ts.peek();
        if opTok.kind == TOK_SYMBOL {
            ts.advance();
            ValueExpr? right2 = parseValueExpr(ts, table, false);
            if right2 == null { return null; }
            var expr4 = new Expr();
            expr4.kind = EXPR_COMPARE;
            expr4.leftValue = left;
            expr4.rightValue = right2;
            expr4.op = opTok.text;
            return expr4;
        }

        var expr5 = new Expr();
        expr5.kind = EXPR_VALUE;
        expr5.leftValue = left;
        return expr5;
    }

    expose func parseInList(ts: TokenStream, left: ValueExpr) -> Expr? {
        if !expectSymbol(ts, "(") { return null; }
        List[SqlValue] vals = [];
        if !ts.matchSymbol(")") {
            while true {
                SqlValue? lit = parseLiteral(ts, true, false);
                if lit == null { return failExpr("Expected literal in IN list"); }
                vals.add(lit);
                if ts.matchSymbol(",") { continue; }
                break;
            }
            if !expectSymbol(ts, ")") { return null; }
        }
        var expr = new Expr();
        expr.kind = EXPR_IN;
        expr.leftValue = left;
        expr.inList = vals;
        return expr;
    }

    expose func parseValueExpr(ts: TokenStream, table: Table?, allowUnknown: Boolean) -> ValueExpr? {
        return parseConcat(ts, table, allowUnknown);
    }

    expose func parseConcat(ts: TokenStream, table: Table?, allowUnknown: Boolean) -> ValueExpr? {
        ValueExpr? left = parseAdd(ts, table, allowUnknown);
        if left == null { return null; }
        while ts.matchSymbol("||") {
            ValueExpr? right = parseAdd(ts, table, allowUnknown);
            if right == null { return null; }
            var expr = new ValueExpr();
            expr.kind = VAL_CONCAT;
            expr.left = left;
            expr.right = right;
            expr.args = [];
            expr.countStar = false;
            expr.distinct = false;
            left = expr;
        }
        return left;
    }

    expose func parseAdd(ts: TokenStream, table: Table?, allowUnknown: Boolean) -> ValueExpr? {
        ValueExpr? left = parseMul(ts, table, allowUnknown);
        if left == null { return null; }
        while true {
            if ts.matchSymbol("+") {
                ValueExpr? right = parseMul(ts, table, allowUnknown);
                if right == null { return null; }
                var expr = new ValueExpr();
                expr.kind = VAL_ADD;
                expr.left = left;
                expr.right = right;
                expr.args = [];
                expr.countStar = false;
                expr.distinct = false;
                left = expr;
                continue;
            }
            if ts.matchSymbol("-") {
                ValueExpr? right2 = parseMul(ts, table, allowUnknown);
                if right2 == null { return null; }
                var expr2 = new ValueExpr();
                expr2.kind = VAL_SUB;
                expr2.left = left;
                expr2.right = right2;
                expr2.args = [];
                expr2.countStar = false;
                expr2.distinct = false;
                left = expr2;
                continue;
            }
            break;
        }
        return left;
    }

    expose func parseMul(ts: TokenStream, table: Table?, allowUnknown: Boolean) -> ValueExpr? {
        ValueExpr? left = parseUnary(ts, table, allowUnknown);
        if left == null { return null; }
        while true {
            if ts.matchSymbol("*") {
                ValueExpr? right = parseUnary(ts, table, allowUnknown);
                if right == null { return null; }
                var expr = new ValueExpr();
                expr.kind = VAL_MUL;
                expr.left = left;
                expr.right = right;
                expr.args = [];
                expr.countStar = false;
                expr.distinct = false;
                left = expr;
                continue;
            }
            if ts.matchSymbol("/") {
                ValueExpr? right2 = parseUnary(ts, table, allowUnknown);
                if right2 == null { return null; }
                var expr2 = new ValueExpr();
                expr2.kind = VAL_DIV;
                expr2.left = left;
                expr2.right = right2;
                expr2.args = [];
                expr2.countStar = false;
                expr2.distinct = false;
                left = expr2;
                continue;
            }
            break;
        }
        return left;
    }

    expose func parseUnary(ts: TokenStream, table: Table?, allowUnknown: Boolean) -> ValueExpr? {
        if ts.matchSymbol("-") {
            ValueExpr? inner = parseUnary(ts, table, allowUnknown);
            if inner == null { return null; }
            var expr = new ValueExpr();
            expr.kind = VAL_NEG;
            expr.left = inner;
            expr.args = [];
            expr.countStar = false;
            expr.distinct = false;
            return expr;
        }
        return parseValuePrimary(ts, table, allowUnknown);
    }

    expose func parseValuePrimary(ts: TokenStream, table: Table?, allowUnknown: Boolean) -> ValueExpr? {
        if ts.matchSymbol("(") {
            ValueExpr? inner = parseValueExpr(ts, table, allowUnknown);
            if inner == null { return null; }
            if !expectSymbol(ts, ")") { return null; }
            return inner;
        }
        SqlValue? lit = parseLiteral(ts, true, false);
        if lit != null {
            var expr = new ValueExpr();
            expr.kind = VAL_LITERAL;
            expr.literal = lit;
            expr.args = [];
            expr.countStar = false;
            expr.distinct = false;
            return expr;
        }
        Token t = ts.peek();
        if t.kind == TOK_IDENT {
            Token next = ts.peekNext();
            if next.kind == TOK_SYMBOL && next.text == "(" {
                String funcName = t.text;
                ts.advance();
                if !expectSymbol(ts, "(") { return null; }
                var func = new ValueExpr();
                func.kind = VAL_FUNC;
                func.name = funcName;
                func.args = [];
                func.countStar = false;
                func.distinct = false;
                if ts.matchSymbol(")") { return func; }
                if Viper.String.ToUpper(funcName) == "COUNT" && ts.matchSymbol("*") {
                    func.countStar = true;
                    if !expectSymbol(ts, ")") { return null; }
                    return func;
                }
                if ts.matchKeyword("DISTINCT") { func.distinct = true; }
                while true {
                    ValueExpr? arg = parseValueExpr(ts, table, allowUnknown);
                    if arg == null { return null; }
                    func.args.add(arg);
                    if ts.matchSymbol(",") { continue; }
                    break;
                }
                if !expectSymbol(ts, ")") { return null; }
                return func;
            }
            Integer colIndex = -1;
            if table != null {
                colIndex = table.columnIndexByName(t.text);
            }
            if colIndex >= 0 || allowUnknown {
                ts.advance();
                var expr2 = new ValueExpr();
                expr2.kind = VAL_COLUMN;
                expr2.colIndex = colIndex;
                expr2.name = t.text;
                expr2.args = [];
                expr2.countStar = false;
                expr2.distinct = false;
                return expr2;
            }
            return failValueExpr(Viper.String.Concat("Unknown column: ", t.text));
        }
        return failValueExpr("Expected value");
    }

    expose func resolveValueExpr(expr: ValueExpr?, table: Table) -> Boolean {
        if expr == null { return true; }
        if expr.kind == VAL_COLUMN {
            if expr.colIndex >= 0 { return true; }
            Integer idx = table.columnIndexByName(expr.name);
            if idx < 0 { return false; }
            expr.colIndex = idx;
            return true;
        }
        if !resolveValueExpr(expr.left, table) { return false; }
        if !resolveValueExpr(expr.right, table) { return false; }
        Integer i = 0;
        while i < expr.args.count() {
            if !resolveValueExpr(expr.args.get(i), table) { return false; }
            i = i + 1;
        }
        return true;
    }

    expose func exprHasAggregate(expr: ValueExpr?) -> Boolean {
        if expr == null { return false; }
        if expr.kind == VAL_FUNC {
            String name = Viper.String.ToUpper(expr.name);
            if name == "COUNT" || name == "SUM" || name == "AVG" || name == "MIN" || name == "MAX" {
                return true;
            }
        }
        if exprHasAggregate(expr.left) { return true; }
        if exprHasAggregate(expr.right) { return true; }
        Integer i = 0;
        while i < expr.args.count() {
            if exprHasAggregate(expr.args.get(i)) { return true; }
            i = i + 1;
        }
        return false;
    }

    expose func exprHasAggregateExpr(expr: Expr?) -> Boolean {
        if expr == null { return false; }
        if exprHasAggregate(expr.leftValue) { return true; }
        if exprHasAggregate(expr.rightValue) { return true; }
        if exprHasAggregateExpr(expr.left) { return true; }
        if exprHasAggregateExpr(expr.right) { return true; }
        return false;
    }

    expose func failExpr(msg: String) -> Expr? {
        lastError = msg;
        return null;
    }

    expose func failValueExpr(msg: String) -> ValueExpr? {
        lastError = msg;
        return null;
    }

    expose func failResult(msg: String) -> ResultSet? {
        lastError = msg;
        return null;
    }

    expose func findColumnIndex(table: Table, name: String) -> Integer {
        String key = Viper.String.ToUpper(name);
        Integer i = 0;
        while i < table.columns.count() {
            Column c = table.columns.get(i);
            if Viper.String.ToUpper(c.name) == key { return i; }
            i = i + 1;
        }
        return -1;
    }

    expose func sortRows(table: Table, rows: List[Integer], colIndex: Integer, desc: Boolean) {
        Integer n = rows.count();
        Integer i = 0;
        while i < n {
            Integer j = 0;
            while j + 1 < n {
                Row a = table.rows.get(rows.get(j));
                Row b = table.rows.get(rows.get(j + 1));
                SqlValue va = a.values.get(colIndex);
                SqlValue vb = b.values.get(colIndex);
                Integer cmp = compareForSort(va, vb);
                if desc { cmp = 0 - cmp; }
                if cmp > 0 {
                    Integer temp = rows.get(j);
                    rows.set(j, rows.get(j + 1));
                    rows.set(j + 1, temp);
                }
                j = j + 1;
            }
            i = i + 1;
        }
    }

    expose func compareForSort(a: SqlValue, b: SqlValue) -> Integer {
        if a.kind == VALUE_TEXT && b.kind == VALUE_TEXT {
            return Viper.String.Cmp(a.s, b.s);
        }
        if a.kind == VALUE_INT && b.kind == VALUE_INT {
            if a.i < b.i { return -1; }
            if a.i > b.i { return 1; }
            return 0;
        }
        if a.kind == VALUE_NUM && b.kind == VALUE_NUM {
            if a.n < b.n { return -1; }
            if a.n > b.n { return 1; }
            return 0;
        }
        return Viper.String.Cmp(a.key(), b.key());
    }

    expose func printRows(table: Table, rowIdx: List[Integer], cols: List[Integer]) {
        Integer colCount = cols.count();
        List[Integer] widths = [];
        Integer i = 0;
        while i < colCount {
            Column c = table.columns.get(cols.get(i));
            widths.add(Viper.String.Length(c.name));
            i = i + 1;
        }
        i = 0;
        while i < rowIdx.count() {
            Row r = table.rows.get(rowIdx.get(i));
            Integer j = 0;
            while j < colCount {
                SqlValue v = r.values.get(cols.get(j));
                Integer w = Viper.String.Length(v.display());
                if w > widths.get(j) { widths.set(j, w); }
                j = j + 1;
            }
            i = i + 1;
        }
        // Header
        i = 0;
        while i < colCount {
            Column c = table.columns.get(cols.get(i));
            String cell = Viper.Fmt.PadRight(c.name, widths.get(i) + 2);
            Viper.Terminal.Print(cell);
            i = i + 1;
        }
        Viper.Terminal.Print("\n");
        // Rows
        i = 0;
        while i < rowIdx.count() {
            Row r = table.rows.get(rowIdx.get(i));
            Integer j = 0;
            while j < colCount {
                SqlValue v = r.values.get(cols.get(j));
                String cell2 = Viper.Fmt.PadRight(v.display(), widths.get(j) + 2);
                Viper.Terminal.Print(cell2);
                j = j + 1;
            }
            Viper.Terminal.Print("\n");
            i = i + 1;
        }
    }

    expose func vacuumTable(table: Table) {
        List[Row] fresh = [];
        Integer i = 0;
        while i < table.rows.count() {
            Row r = table.rows.get(i);
            if !r.deleted { fresh.add(r); }
            i = i + 1;
        }
        table.rows = fresh;
        rebuildPrimaryIndex(table);
        rebuildIndexes(table);
        table.dirty = true;
    }

    expose func exportCsv(table: Table) -> String {
        var sb = Viper.Text.StringBuilder.New();
        // header
        Integer i = 0;
        while i < table.columns.count() {
            Column c = table.columns.get(i);
            Viper.Text.StringBuilder.Append(sb, csvEscape(c.name));
            if i + 1 < table.columns.count() { Viper.Text.StringBuilder.Append(sb, ","); }
            i = i + 1;
        }
        Viper.Text.StringBuilder.AppendLine(sb, "");
        i = 0;
        while i < table.rows.count() {
            Row r = table.rows.get(i);
            if !r.deleted {
                Integer j = 0;
                while j < r.values.count() {
                    SqlValue v = r.values.get(j);
                    Viper.Text.StringBuilder.Append(sb, csvEscape(v.display()));
                    if j + 1 < r.values.count() { Viper.Text.StringBuilder.Append(sb, ","); }
                    j = j + 1;
                }
                Viper.Text.StringBuilder.AppendLine(sb, "");
            }
            i = i + 1;
        }
        return Viper.Text.StringBuilder.ToString(sb);
    }

    expose func csvEscape(text: String) -> String {
        Boolean needs = Viper.String.IndexOf(text, ",") >= 0 || Viper.String.IndexOf(text, "\"") >= 0 || Viper.String.IndexOf(text, "\n") >= 0;
        if !needs { return text; }
        String out = Viper.String.Replace(text, "\"", "\"\"");
        return Viper.String.Concat(Viper.String.Concat("\"", out), "\"");
    }

    expose func importCsv(table: Table, content: String) -> Boolean {
        List[String] lines = splitLines(content);
        Integer lineIdx = 0;
        if lines.count() == 0 { return true; }
        List[String] header = parseCsvLine(lines.get(0));
        Boolean hasHeader = false;
        if header.count() == table.columns.count() {
            hasHeader = true;
            Integer i = 0;
            while i < header.count() {
                String h = Viper.String.ToUpper(header.get(i));
                String c = Viper.String.ToUpper(table.columns.get(i).name);
                if h != c { hasHeader = false; }
                i = i + 1;
            }
        }
        if hasHeader { lineIdx = 1; }
        while lineIdx < lines.count() {
            String line = lines.get(lineIdx);
            if Viper.String.Trim(line) == "" { lineIdx = lineIdx + 1; continue; }
            List[String] fields = parseCsvLine(line);
            if fields.count() != table.columns.count() { return fail("CSV column count mismatch"); }
            List[SqlValue] vals = [];
            Integer i2 = 0;
            while i2 < fields.count() {
                var v = new SqlValue();
                v.initText(fields.get(i2));
                vals.add(v);
                i2 = i2 + 1;
            }
            if !insertRow(table, vals) { return false; }
            lineIdx = lineIdx + 1;
        }
        return true;
    }

    expose func parseCsvLine(line: String) -> List[String] {
        List[String] fields = [];
        Integer len = Viper.String.Length(line);
        Integer i = 0;
        String current = "";
        Boolean inQuote = false;
        while i < len {
            String c = Viper.String.Substring(line, i, 1);
            if inQuote {
                if c == "\"" {
                    if i + 1 < len && Viper.String.Substring(line, i + 1, 1) == "\"" {
                        current = Viper.String.Concat(current, "\"");
                        i = i + 2;
                        continue;
                    }
                    inQuote = false;
                    i = i + 1;
                    continue;
                }
                current = Viper.String.Concat(current, c);
                i = i + 1;
                continue;
            }
            if c == "\"" {
                inQuote = true;
                i = i + 1;
                continue;
            }
            if c == "," {
                fields.add(current);
                current = "";
                i = i + 1;
                continue;
            }
            current = Viper.String.Concat(current, c);
            i = i + 1;
        }
        fields.add(current);
        return fields;
    }

    expose func printHelp() {
        Viper.Terminal.Say("Commands:");
        Viper.Terminal.Say("  CREATE TABLE name (col TYPE [PRIMARY KEY] [NOT NULL] [AUTOINCREMENT], ...)");
        Viper.Terminal.Say("  CREATE [UNIQUE] INDEX idx ON table (col)");
        Viper.Terminal.Say("  DROP TABLE name | DROP INDEX name");
        Viper.Terminal.Say("  ALTER TABLE name RENAME TO new | RENAME COLUMN old TO new | ADD COLUMN col TYPE");
        Viper.Terminal.Say("  INSERT INTO name [(col,...)] VALUES (val,...) [, ...] | DEFAULT VALUES");
        Viper.Terminal.Say("  SELECT [DISTINCT] expr[, ...] FROM name [WHERE expr] [GROUP BY expr] [HAVING expr]");
        Viper.Terminal.Say("    [ORDER BY col|index [ASC|DESC]] [LIMIT n [OFFSET m]]");
        Viper.Terminal.Say("  UPDATE name SET col=val[, ...] [WHERE expr]");
        Viper.Terminal.Say("  DELETE FROM name [WHERE expr]");
        Viper.Terminal.Say("  SHOW TABLES | SHOW INDEXES [table] | SHOW SCHEMA [table]");
        Viper.Terminal.Say("  DESCRIBE name");
        Viper.Terminal.Say("  BEGIN | COMMIT | ROLLBACK");
        Viper.Terminal.Say("  EXPORT name TO path");
        Viper.Terminal.Say("  IMPORT name FROM path");
        Viper.Terminal.Say("  VACUUM [name]");
        Viper.Terminal.Say("  SAVE | OPEN path | HELP | EXIT");
    }
}
