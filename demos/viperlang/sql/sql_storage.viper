module SqlStorage;

import "./sql_types";

func strLen(text: String) -> Integer {
    return Viper.String.Length(text);
}

func charAt(text: String, idx: Integer) -> String {
    return Viper.String.Substring(text, idx, 1);
}

func startsWith(text: String, prefix: String) -> Boolean {
    return Viper.String.StartsWith(text, prefix);
}

func splitLines(text: String) -> List[String] {
    List[String] lines = [];
    Integer len = strLen(text);
    Integer start = 0;
    Integer i = 0;
    while i < len {
        if charAt(text, i) == "\n" {
            lines.add(Viper.String.Substring(text, start, i - start));
            start = i + 1;
        }
        i = i + 1;
    }
    if start <= len {
        lines.add(Viper.String.Substring(text, start, len - start));
    }
    return lines;
}

func encodeText(text: String) -> String {
    Integer len = strLen(text);
    Integer i = 0;
    String out = "";
    while i < len {
        String c = charAt(text, i);
        if c == "\\" { out = Viper.String.Concat(out, "\\\\"); i = i + 1; continue; }
        if c == "\n" { out = Viper.String.Concat(out, "\\n"); i = i + 1; continue; }
        if c == "\r" { out = Viper.String.Concat(out, "\\r"); i = i + 1; continue; }
        if c == "\t" { out = Viper.String.Concat(out, "\\t"); i = i + 1; continue; }
        if c == "|" { out = Viper.String.Concat(out, "\\|"); i = i + 1; continue; }
        out = Viper.String.Concat(out, c);
        i = i + 1;
    }
    return out;
}

func decodeText(text: String) -> String {
    Integer len = strLen(text);
    Integer i = 0;
    String out = "";
    while i < len {
        String c = charAt(text, i);
        if c == "\\" && i + 1 < len {
            String esc = charAt(text, i + 1);
            if esc == "n" { out = Viper.String.Concat(out, "\n"); i = i + 2; continue; }
            if esc == "r" { out = Viper.String.Concat(out, "\r"); i = i + 2; continue; }
            if esc == "t" { out = Viper.String.Concat(out, "\t"); i = i + 2; continue; }
            if esc == "|" { out = Viper.String.Concat(out, "|"); i = i + 2; continue; }
            if esc == "\\" { out = Viper.String.Concat(out, "\\"); i = i + 2; continue; }
            out = Viper.String.Concat(out, esc);
            i = i + 2;
            continue;
        }
        out = Viper.String.Concat(out, c);
        i = i + 1;
    }
    return out;
}

func splitEscaped(text: String, delim: String) -> List[String] {
    List[String] parts = [];
    Integer len = strLen(text);
    Integer i = 0;
    String current = "";
    while i < len {
        String c = charAt(text, i);
        if c == "\\" && i + 1 < len {
            String esc = charAt(text, i + 1);
            current = Viper.String.Concat(current, "\\");
            current = Viper.String.Concat(current, esc);
            i = i + 2;
            continue;
        }
        if c == delim {
            parts.add(current);
            current = "";
            i = i + 1;
            continue;
        }
        current = Viper.String.Concat(current, c);
        i = i + 1;
    }
    parts.add(current);
    return parts;
}

func parseIntText(text: String) -> Integer {
    Integer len = strLen(text);
    if len == 0 { return 0; }
    Integer i = 0;
    Boolean neg = false;
    if charAt(text, 0) == "-" {
        neg = true;
        i = 1;
    }
    Integer value = 0;
    while i < len {
        Integer code = Viper.String.Asc(charAt(text, i));
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
    Integer len = strLen(text);
    if len == 0 { return 0.0; }
    Integer i = 0;
    Boolean neg = false;
    if charAt(text, 0) == "-" {
        neg = true;
        i = 1;
    }
    Number value = 0.0;
    while i < len && charAt(text, i) != "." {
        Integer code = Viper.String.Asc(charAt(text, i));
        Number digit = digitToNum(code);
        value = value * 10.0 + digit;
        i = i + 1;
    }
    if i < len && charAt(text, i) == "." {
        i = i + 1;
        Number divisor = 1.0;
        while i < len {
            Integer code2 = Viper.String.Asc(charAt(text, i));
            divisor = divisor * 10.0;
            Number digit = digitToNum(code2);
            value = value + digit / divisor;
            i = i + 1;
        }
    }
    if neg { value = 0.0 - value; }
    return value;
}

func encodeValue(v: SqlValue) -> String {
    if v.kind == VALUE_NULL { return "~"; }
    if v.kind == VALUE_INT { return Viper.String.Concat("i:", Viper.Fmt.Int(v.i)); }
    if v.kind == VALUE_NUM { return Viper.String.Concat("n:", Viper.Fmt.Num(v.n)); }
    if v.kind == VALUE_BOOL { return Viper.String.Concat("b:", v.b ? "1" : "0"); }
    if v.kind == VALUE_DEFAULT { return "~"; }
    return Viper.String.Concat("s:", encodeText(v.s));
}

func decodeValue(text: String) -> SqlValue {
    var v = new SqlValue();
    if text == "~" {
        v.initNull();
        return v;
    }
    if startsWith(text, "i:") {
        String payload = Viper.String.Substring(text, 2, strLen(text) - 2);
        v.initInt(parseIntText(payload));
        return v;
    }
    if startsWith(text, "n:") {
        String payload = Viper.String.Substring(text, 2, strLen(text) - 2);
        v.initNum(parseNumText(payload));
        return v;
    }
    if startsWith(text, "b:") {
        String payload = Viper.String.Substring(text, 2, strLen(text) - 2);
        v.initBool(payload == "1");
        return v;
    }
    if startsWith(text, "s:") {
        String payload = Viper.String.Substring(text, 2, strLen(text) - 2);
        v.initText(decodeText(payload));
        return v;
    }
    v.initText(text);
    return v;
}


func parseTypeName(typeNameText: String) -> Integer {
    String upper = Viper.String.ToUpper(typeNameText);
    if upper == "INT" || upper == "INTEGER" { return TYPE_INT; }
    if upper == "NUM" || upper == "NUMBER" || upper == "REAL" || upper == "FLOAT" || upper == "DOUBLE" { return TYPE_NUM; }
    if upper == "BOOL" || upper == "BOOLEAN" { return TYPE_BOOL; }
    if upper == "TEXT" || upper == "STRING" || upper == "VARCHAR" || upper == "CHAR" || upper == "DATE" || upper == "DATETIME" { return TYPE_TEXT; }
    return TYPE_TEXT;
}

entity Table {
    expose String name;
    expose List[Column] columns;
    expose Map[String, Integer] colIndex;
    expose List[Row] rows;
    expose Boolean hasPrimary;
    expose Integer primaryIndex;
    expose Map[String, Integer] pkLookup;
    expose Integer autoInc;
    expose Boolean dirty;
    expose List[Index] indexes;
    expose Map[String, Integer] indexByName;

    expose func init(n: String) {
        name = n;
        columns = [];
        colIndex = new Map[String, Integer]();
        rows = [];
        hasPrimary = false;
        primaryIndex = -1;
        pkLookup = new Map[String, Integer]();
        autoInc = 1;
        dirty = false;
        indexes = [];
        indexByName = new Map[String, Integer]();
    }

    expose func addColumn(c: Column) {
        columns.add(c);
    }

    expose func finalizeSchema() {
        colIndex = new Map[String, Integer]();
        Integer i = 0;
        while i < columns.count() {
            Column col = columns.get(i);
            colIndex.set(Viper.String.ToUpper(col.name), i);
            if col.primaryKey {
                hasPrimary = true;
                primaryIndex = i;
            }
            i = i + 1;
        }
    }

    expose func columnIndexByName(nameText: String) -> Integer {
        String key = Viper.String.ToUpper(nameText);
        if colIndex.has(key) {
            return colIndex.get(key);
        }
        return -1;
    }
}

entity Index {
    expose String name;
    expose Integer colIndex;
    expose Boolean unique;
    expose Boolean system;
    expose Map[String, List[Integer]] buckets;

    expose func init(n: String, col: Integer, uniq: Boolean, sys: Boolean) {
        name = n;
        colIndex = col;
        unique = uniq;
        system = sys;
        buckets = new Map[String, List[Integer]]();
    }
}

entity IndexDef {
    expose String name;
    expose String colName;
    expose Boolean unique;

    expose func init(n: String, c: String, uniq: Boolean) {
        name = n;
        colName = c;
        unique = uniq;
    }
}

func flagHas(flags: String, flag: String) -> Boolean {
    if flags == "" { return false; }
    List[String] parts = splitEscaped(flags, ",");
    Integer i = 0;
    while i < parts.count() {
        if parts.get(i) == flag { return true; }
        i = i + 1;
    }
    return false;
}

func rebuildIndexMap(table: Table) {
    table.indexByName = new Map[String, Integer]();
    Integer i = 0;
    while i < table.indexes.count() {
        Index idx = table.indexes.get(i);
        table.indexByName.set(Viper.String.ToUpper(idx.name), i);
        i = i + 1;
    }
}

func ensureSystemIndexes(table: Table) {
    rebuildIndexMap(table);
    Integer i = 0;
    while i < table.columns.count() {
        Column c = table.columns.get(i);
            if c.unique {
                String idxName = Viper.String.Concat("__uniq_", c.name);
                String key = Viper.String.ToUpper(idxName);
                if table.indexByName.has(key) {
                } else {
                    var idx = new Index();
                    idx.init(idxName, i, true, true);
                    table.indexes.add(idx);
                    table.indexByName.set(key, table.indexes.count() - 1);
                }
        }
        i = i + 1;
    }
}

func normalizeRows(table: Table) {
    Integer i = 0;
    Integer colCount = table.columns.count();
    while i < table.rows.count() {
        Row r = table.rows.get(i);
        while r.values.count() < colCount {
            Column c = table.columns.get(r.values.count());
            if c.hasDefault {
                SqlValue dv = c.defaultValue;
                r.values.add(dv.clone());
            } else {
                var v = new SqlValue();
                v.initNull();
                r.values.add(v);
            }
        }
        while r.values.count() > colCount {
            r.values.removeAt(r.values.count() - 1);
        }
        i = i + 1;
    }
}

func rebuildIndexes(table: Table) -> Boolean {
    Integer i = 0;
    while i < table.indexes.count() {
        Index idx = table.indexes.get(i);
        idx.buckets = new Map[String, List[Integer]]();
        i = i + 1;
    }
    i = 0;
    while i < table.rows.count() {
        Row r = table.rows.get(i);
        if !r.deleted {
            Integer j = 0;
            while j < table.indexes.count() {
                Index idx2 = table.indexes.get(j);
                if idx2.colIndex >= 0 && idx2.colIndex < r.values.count() {
                    SqlValue v = r.values.get(idx2.colIndex);
                    String key = v.key();
                    if idx2.unique && key != "NULL" {
                        if idx2.buckets.has(key) { return false; }
                    }
                    List[Integer] bucket = [];
                    if idx2.buckets.has(key) {
                        bucket = idx2.buckets.get(key);
                    }
                    bucket.add(i);
                    idx2.buckets.set(key, bucket);
                }
                j = j + 1;
            }
        }
        i = i + 1;
    }
    return true;
}

entity Database {
    expose String path;
    expose List[Table] tables;
    expose Map[String, Integer] tableIndex;
    expose String lastError;

    expose func init(dbPath: String) {
        path = dbPath;
        tables = [];
        tableIndex = new Map[String, Integer]();
        lastError = "";
    }

    expose func clearError() {
        lastError = "";
    }

    expose func fail(msg: String) -> Boolean {
        lastError = msg;
        return false;
    }

    expose func tableCount() -> Integer {
        return tables.count();
    }

    expose func tableIndexByName(nameText: String) -> Integer {
        String key = Viper.String.ToUpper(nameText);
        if tableIndex.has(key) {
            return tableIndex.get(key);
        }
        return -1;
    }
}

func catalogPath(db: Database) -> String {
    return Viper.IO.Path.Join(db.path, "catalog.vdb");
}

func tablePath(db: Database, tableName: String) -> String {
    return Viper.IO.Path.Join(db.path, Viper.String.Concat(tableName, ".vdb"));
}

func ensureDir(db: Database) -> Boolean {
    if Viper.IO.Dir.Exists(db.path) { return true; }
    Viper.IO.Dir.Make(db.path);
    return Viper.IO.Dir.Exists(db.path);
}

func loadDatabase(db: Database) -> Boolean {
    db.clearError();
    if !ensureDir(db) {
        return db.fail("Failed to create database directory");
    }
    String catPath = catalogPath(db);
    if !Viper.IO.File.Exists(catPath) {
        Viper.IO.File.WriteAllText(catPath, "");
        return true;
    }
    String content = Viper.IO.File.ReadAllText(catPath);
    List[String] lines = splitLines(content);
    Integer i = 0;
    while i < lines.count() {
        String line = Viper.String.Trim(lines.get(i));
        if line != "" {
            var t = new Table();
            t.init(line);
            if !loadTable(db, t) { return false; }
            db.tableIndex.set(Viper.String.ToUpper(t.name), db.tables.count());
            db.tables.add(t);
        }
        i = i + 1;
    }
    return true;
}

func saveDatabase(db: Database) -> Boolean {
    db.clearError();
    if !ensureDir(db) {
        return db.fail("Failed to create database directory");
    }
    Integer i = 0;
    var sb = Viper.Text.StringBuilder.New();
    while i < db.tables.count() {
        Table t = db.tables.get(i);
        Viper.Text.StringBuilder.AppendLine(sb, t.name);
        i = i + 1;
    }
    Viper.IO.File.WriteAllText(catalogPath(db), Viper.Text.StringBuilder.ToString(sb));
    i = 0;
    while i < db.tables.count() {
        Table t = db.tables.get(i);
        if !saveTable(db, t) { return false; }
        i = i + 1;
    }
    return true;
}

func saveTable(db: Database, table: Table) -> Boolean {
    var sb = Viper.Text.StringBuilder.New();
    Viper.Text.StringBuilder.AppendLine(sb, "schema|" + table.name);
    Integer i = 0;
    while i < table.columns.count() {
        Column c = table.columns.get(i);
        String flags = "";
        if c.primaryKey { flags = flags == "" ? "pk" : Viper.String.Concat(flags, ",pk"); }
        if c.notNull { flags = flags == "" ? "nn" : Viper.String.Concat(flags, ",nn"); }
        if c.autoInc { flags = flags == "" ? "ai" : Viper.String.Concat(flags, ",ai"); }
        if c.unique { flags = flags == "" ? "uq" : Viper.String.Concat(flags, ",uq"); }
        String defText = "";
        if c.hasDefault {
            defText = encodeValue(c.defaultValue);
        }
        String line = "col|";
        line = Viper.String.Concat(line, encodeText(c.name));
        line = Viper.String.Concat(line, "|");
        line = Viper.String.Concat(line, typeName(c.typeCode));
        line = Viper.String.Concat(line, "|");
        line = Viper.String.Concat(line, flags);
        line = Viper.String.Concat(line, "|");
        line = Viper.String.Concat(line, defText);
        Viper.Text.StringBuilder.AppendLine(sb, line);
        i = i + 1;
    }
    i = 0;
    while i < table.indexes.count() {
        Index idx = table.indexes.get(i);
        if !idx.system {
            String colName = table.columns.get(idx.colIndex).name;
            String line2 = "index|";
            line2 = Viper.String.Concat(line2, encodeText(idx.name));
            line2 = Viper.String.Concat(line2, "|");
            line2 = Viper.String.Concat(line2, encodeText(colName));
            line2 = Viper.String.Concat(line2, "|");
            line2 = Viper.String.Concat(line2, idx.unique ? "1" : "0");
            Viper.Text.StringBuilder.AppendLine(sb, line2);
        }
        i = i + 1;
    }
    Viper.Text.StringBuilder.AppendLine(sb, Viper.String.Concat("auto|", Viper.Fmt.Int(table.autoInc)));

    i = 0;
    while i < table.rows.count() {
        Row r = table.rows.get(i);
        if !r.deleted {
            String rowLine = "row";
            Integer j = 0;
            while j < r.values.count() {
                SqlValue v = r.values.get(j);
                rowLine = Viper.String.Concat(rowLine, "|");
                rowLine = Viper.String.Concat(rowLine, encodeValue(v));
                j = j + 1;
            }
            Viper.Text.StringBuilder.AppendLine(sb, rowLine);
        }
        i = i + 1;
    }
    Viper.IO.File.WriteAllText(tablePath(db, table.name), Viper.Text.StringBuilder.ToString(sb));
    table.dirty = false;
    return true;
}

func loadTable(db: Database, table: Table) -> Boolean {
    String path = tablePath(db, table.name);
    if !Viper.IO.File.Exists(path) {
        return db.fail(Viper.String.Concat("Missing table file: ", path));
    }
    String content = Viper.IO.File.ReadAllText(path);
    List[String] lines = splitLines(content);
    List[IndexDef] pending = [];
    Integer i = 0;
    while i < lines.count() {
        String line = lines.get(i);
        if startsWith(line, "schema|") {
            String restName = Viper.String.Substring(line, 7, strLen(line) - 7);
            if restName != "" {
                table.name = decodeText(restName);
            }
        } else if startsWith(line, "col|") {
            List[String] parts = splitEscaped(line, "|");
            if parts.count() >= 4 {
                String colName2 = decodeText(parts.get(1));
                String typeName2 = parts.get(2);
                String flags2 = parts.get(3);
                var col2 = new Column();
                col2.init(colName2, parseTypeName(typeName2));
                if flagHas(flags2, "pk") { col2.primaryKey = true; }
                if flagHas(flags2, "nn") { col2.notNull = true; }
                if flagHas(flags2, "ai") { col2.autoInc = true; }
                if flagHas(flags2, "uq") { col2.unique = true; }
                if parts.count() >= 5 && parts.get(4) != "" {
                    col2.defaultValue = decodeValue(parts.get(4));
                    col2.hasDefault = true;
                }
                table.addColumn(col2);
            }
        } else if startsWith(line, "columns|") {
            String rest = Viper.String.Substring(line, 8, strLen(line) - 8);
            List[String] colParts = splitEscaped(rest, "|");
            Integer c = 0;
            while c < colParts.count() {
                String part = colParts.get(c);
                List[String] segs = splitEscaped(part, ":");
                if segs.count() >= 2 {
                    String colName = decodeText(segs.get(0));
                    String typeNameText = segs.get(1);
                    var col = new Column();
                    col.init(colName, parseTypeName(typeNameText));
                    if segs.count() >= 3 {
                        String flags = segs.get(2);
                        if flagHas(flags, "pk") { col.primaryKey = true; }
                        if flagHas(flags, "nn") { col.notNull = true; }
                        if flagHas(flags, "ai") { col.autoInc = true; }
                        if flagHas(flags, "uq") { col.unique = true; }
                    }
                    table.addColumn(col);
                }
                c = c + 1;
            }
        } else if startsWith(line, "index|") {
            List[String] parts2 = splitEscaped(line, "|");
            if parts2.count() >= 4 {
                String idxName = decodeText(parts2.get(1));
                String colName3 = decodeText(parts2.get(2));
                Boolean uniq = parts2.get(3) == "1";
                var def = new IndexDef();
                def.init(idxName, colName3, uniq);
                pending.add(def);
            }
        } else if startsWith(line, "auto|") {
            String rest = Viper.String.Substring(line, 5, strLen(line) - 5);
    table.autoInc = parseIntText(rest);
        } else if startsWith(line, "row|") {
            String rest = Viper.String.Substring(line, 4, strLen(line) - 4);
            List[String] vals = splitEscaped(rest, "|");
            var row = new Row();
            row.values = [];
            row.deleted = false;
            Integer v = 0;
            while v < vals.count() {
                row.values.add(decodeValue(vals.get(v)));
                v = v + 1;
            }
            table.rows.add(row);
        }
        i = i + 1;
    }
    table.finalizeSchema();
    normalizeRows(table);
    i = 0;
    while i < pending.count() {
        IndexDef def2 = pending.get(i);
        Integer colIndex = table.columnIndexByName(def2.colName);
        if colIndex < 0 {
            return db.fail(Viper.String.Concat("Index column not found: ", def2.colName));
        }
        var idx2 = new Index();
        idx2.init(def2.name, colIndex, def2.unique, false);
        table.indexes.add(idx2);
        i = i + 1;
    }
    ensureSystemIndexes(table);
    if !rebuildPrimaryIndex(table) { return db.fail("Primary key conflict"); }
    if !rebuildIndexes(table) { return db.fail("Unique index conflict"); }
    return true;
}

func rebuildPrimaryIndex(table: Table) -> Boolean {
    table.pkLookup = new Map[String, Integer]();
    if !table.hasPrimary { return true; }
    Integer i = 0;
    while i < table.rows.count() {
        Row r = table.rows.get(i);
        if !r.deleted {
            SqlValue v = r.values.get(table.primaryIndex);
            if v.kind == VALUE_NULL { return false; }
            String key = v.key();
            if table.pkLookup.has(key) { return false; }
            table.pkLookup.set(key, i);
        }
        i = i + 1;
    }
    return true;
}
