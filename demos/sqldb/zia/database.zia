// database.viper - Database Container
// Part of SQLite Clone - ViperLang Implementation

module database;

import "./table";

//=============================================================================
// DATABASE ENTITY
//=============================================================================

entity Database {
    expose String name;
    expose List[Table] tables;

    expose func init() {
        name = "default";
        tables = [];
    }

    expose func initWithName(dbName: String) {
        name = dbName;
        tables = [];
    }

    expose func tableCount() -> Integer {
        return tables.count();
    }

    expose func getTable(index: Integer) -> Table? {
        if index < 0 || index >= tables.count() {
            return null;
        }
        return tables.get(index);
    }

    expose func findTable(tableName: String) -> Table? {
        var i = 0;
        while i < tables.count() {
            var t = tables.get(i);
            if t.name == tableName {
                return t;
            }
            i = i + 1;
        }
        return null;
    }

    expose func addTable(table: Table) {
        tables.add(table);
    }

    expose func dropTable(tableName: String) -> Boolean {
        var i = 0;
        while i < tables.count() {
            if tables.get(i).name == tableName {
                tables.remove(i);
                return true;
            }
            i = i + 1;
        }
        return false;
    }

    expose func listTables() -> String {
        var result = "";
        var i = 0;
        while i < tables.count() {
            if i > 0 {
                result = result + "\n";
            }
            result = result + tables.get(i).name;
            i = i + 1;
        }
        return result;
    }
}

// Factory function
func makeDatabase(name: String) -> Database {
    var db = new Database();
    db.initWithName(name);
    return db;
}
