module SqlTypes;

// Basic type codes for columns and values.
Integer TYPE_INT = 1;
Integer TYPE_NUM = 2;
Integer TYPE_BOOL = 3;
Integer TYPE_TEXT = 4;

Integer VALUE_NULL = 0;
Integer VALUE_INT = 1;
Integer VALUE_NUM = 2;
Integer VALUE_BOOL = 3;
Integer VALUE_TEXT = 4;
Integer VALUE_DEFAULT = 5;

func typeName(t: Integer) -> String {
    if t == TYPE_INT { return "INT"; }
    if t == TYPE_NUM { return "NUM"; }
    if t == TYPE_BOOL { return "BOOL"; }
    return "TEXT";
}

func valueKindName(k: Integer) -> String {
    if k == VALUE_NULL { return "NULL"; }
    if k == VALUE_INT { return "INT"; }
    if k == VALUE_NUM { return "NUM"; }
    if k == VALUE_BOOL { return "BOOL"; }
    if k == VALUE_DEFAULT { return "DEFAULT"; }
    return "TEXT";
}

entity SqlValue {
    expose Integer kind;
    expose Integer i;
    expose Number n;
    expose Boolean b;
    expose String s;

    expose func initNull() {
        kind = VALUE_NULL;
        i = 0;
        n = 0.0;
        b = false;
        s = "";
    }

    expose func initInt(v: Integer) {
        kind = VALUE_INT;
        i = v;
        n = 0.0;
        b = false;
        s = "";
    }

    expose func initNum(v: Number) {
        kind = VALUE_NUM;
        i = 0;
        n = v;
        b = false;
        s = "";
    }

    expose func initBool(v: Boolean) {
        kind = VALUE_BOOL;
        i = 0;
        n = 0.0;
        b = v;
        s = "";
    }

    expose func initText(v: String) {
        kind = VALUE_TEXT;
        i = 0;
        n = 0.0;
        b = false;
        s = v;
    }

    expose func initDefault() {
        kind = VALUE_DEFAULT;
        i = 0;
        n = 0.0;
        b = false;
        s = "";
    }

    expose func isNull() -> Boolean {
        return kind == VALUE_NULL;
    }

    expose func isDefault() -> Boolean {
        return kind == VALUE_DEFAULT;
    }

    expose func display() -> String {
        if kind == VALUE_NULL { return "NULL"; }
        if kind == VALUE_INT { return Viper.Fmt.Int(i); }
        if kind == VALUE_NUM { return Viper.Fmt.Num(n); }
        if kind == VALUE_BOOL { return b ? "true" : "false"; }
        if kind == VALUE_DEFAULT { return "DEFAULT"; }
        return s;
    }

    expose func key() -> String {
        if kind == VALUE_NULL { return "NULL"; }
        if kind == VALUE_INT { return Viper.Fmt.Int(i); }
        if kind == VALUE_NUM { return Viper.Fmt.Num(n); }
        if kind == VALUE_BOOL { return b ? "true" : "false"; }
        if kind == VALUE_DEFAULT { return "DEFAULT"; }
        return s;
    }

    expose func clone() -> SqlValue {
        var v = new SqlValue();
        if kind == VALUE_NULL { v.initNull(); return v; }
        if kind == VALUE_INT { v.initInt(i); return v; }
        if kind == VALUE_NUM { v.initNum(n); return v; }
        if kind == VALUE_BOOL { v.initBool(b); return v; }
        if kind == VALUE_TEXT { v.initText(s); return v; }
        v.initDefault();
        return v;
    }
}

entity Column {
    expose String name;
    expose Integer typeCode;
    expose Boolean notNull;
    expose Boolean primaryKey;
    expose Boolean autoInc;
    expose Boolean unique;
    expose Boolean hasDefault;
    expose SqlValue defaultValue;

    expose func init(n: String, t: Integer) {
        name = n;
        typeCode = t;
        notNull = false;
        primaryKey = false;
        autoInc = false;
        unique = false;
        hasDefault = false;
        var dv = new SqlValue();
        dv.initNull();
        defaultValue = dv;
    }
}

entity Row {
    expose List[SqlValue] values;
    expose Boolean deleted;

    expose func init(count: Integer) {
        values = [];
        deleted = false;
        Integer i = 0;
        while i < count {
            var v = new SqlValue();
            v.initNull();
            values.add(v);
            i = i + 1;
        }
    }
}
