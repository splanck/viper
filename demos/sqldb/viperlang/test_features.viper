// test_features.viper - Test various SQL features
module test_features;

import "./executor";

Integer passed = 0;
Integer failed = 0;

func assert(condition: Boolean, msg: String) {
    if condition {
        passed = passed + 1;
        Viper.Terminal.Say("  PASS: " + msg);
    } else {
        failed = failed + 1;
        Viper.Terminal.Say("  FAIL: " + msg);
    }
}

func main() {
    var exec = new Executor();
    exec.init();

    Viper.Terminal.Say("=== Extended SQL Feature Tests ===");
    Viper.Terminal.Say("");

    // Test CREATE INDEX and index lookups
    Viper.Terminal.Say("Testing INDEX...");
    exec.executeSql("CREATE TABLE products (id INTEGER, name TEXT, price INTEGER)");
    exec.executeSql("INSERT INTO products VALUES (1, 'Apple', 100)");
    exec.executeSql("INSERT INTO products VALUES (2, 'Banana', 50)");
    exec.executeSql("INSERT INTO products VALUES (3, 'Cherry', 75)");
    exec.executeSql("INSERT INTO products VALUES (4, 'Date', 120)");
    exec.executeSql("INSERT INTO products VALUES (5, 'Elderberry', 200)");

    var r1 = exec.executeSql("CREATE INDEX idx_name ON products(name)");
    assert(r1.success, "CREATE INDEX should succeed");

    // Test string functions
    Viper.Terminal.Say("Testing String Functions...");
    var r2 = exec.executeSql("SELECT UPPER(name) FROM products WHERE id = 1");
    assert(r2.success, "UPPER function should work");
    assert(r2.rowCount() == 1, "Should return 1 row");

    var r3 = exec.executeSql("SELECT LOWER(name) FROM products WHERE id = 1");
    assert(r3.success, "LOWER function should work");

    var r4 = exec.executeSql("SELECT LENGTH(name) FROM products WHERE id = 1");
    assert(r4.success, "LENGTH function should work");

    // Test ABS function
    Viper.Terminal.Say("Testing Math Functions...");
    exec.executeSql("CREATE TABLE temps (id INTEGER, temp INTEGER)");
    exec.executeSql("INSERT INTO temps VALUES (1, -10)");
    exec.executeSql("INSERT INTO temps VALUES (2, 25)");

    var r5 = exec.executeSql("SELECT ABS(temp) FROM temps WHERE id = 1");
    assert(r5.success, "ABS function should work");

    // Test COALESCE
    Viper.Terminal.Say("Testing COALESCE...");
    exec.executeSql("CREATE TABLE nullable (id INTEGER, val INTEGER)");
    exec.executeSql("INSERT INTO nullable VALUES (1, 100)");
    // Can't easily insert NULL, but test with existing value
    var r6 = exec.executeSql("SELECT COALESCE(val, 0) FROM nullable");
    assert(r6.success, "COALESCE should work");

    // Test complex WHERE with AND/OR
    Viper.Terminal.Say("Testing Complex WHERE...");
    var r7 = exec.executeSql("SELECT * FROM products WHERE price > 50 AND price < 150");
    assert(r7.success, "AND in WHERE should work");
    assert(r7.rowCount() == 3, "Should return 3 products (Apple, Cherry, Date)");

    var r8 = exec.executeSql("SELECT * FROM products WHERE price < 60 OR price > 150");
    assert(r8.success, "OR in WHERE should work");
    assert(r8.rowCount() == 2, "Should return 2 products (Banana, Elderberry)");

    // Test nested expressions
    Viper.Terminal.Say("Testing Arithmetic...");
    var r9 = exec.executeSql("SELECT price * 2 FROM products WHERE id = 1");
    assert(r9.success, "Arithmetic in SELECT should work");

    // Test multiple ORDER BY
    Viper.Terminal.Say("Testing ORDER BY...");
    var r10 = exec.executeSql("SELECT * FROM products ORDER BY price ASC");
    assert(r10.success, "ORDER BY ASC should work");

    var r11 = exec.executeSql("SELECT * FROM products ORDER BY price DESC");
    assert(r11.success, "ORDER BY DESC should work");

    // Test LIMIT with OFFSET
    Viper.Terminal.Say("Testing LIMIT/OFFSET...");
    var r12 = exec.executeSql("SELECT * FROM products LIMIT 2 OFFSET 1");
    assert(r12.success, "LIMIT with OFFSET should work");
    assert(r12.rowCount() == 2, "Should return 2 rows");

    // Test combined features
    Viper.Terminal.Say("Testing Combined Features...");
    var r13 = exec.executeSql("SELECT name, price FROM products WHERE price > 50 ORDER BY price DESC LIMIT 3");
    assert(r13.success, "Combined WHERE/ORDER/LIMIT should work");
    assert(r13.rowCount() == 3, "Should return 3 rows");

    // Test aggregate with WHERE
    Viper.Terminal.Say("Testing Aggregates with WHERE...");
    var r14 = exec.executeSql("SELECT COUNT(*) FROM products WHERE price > 100");
    assert(r14.success, "COUNT with WHERE should work");
    assert(r14.rowCount() == 1, "Should return 1 row");

    var r15 = exec.executeSql("SELECT SUM(price) FROM products WHERE price <= 100");
    assert(r15.success, "SUM with WHERE should work");

    var r16 = exec.executeSql("SELECT AVG(price) FROM products");
    assert(r16.success, "AVG should work");

    // Print results
    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Results ===");
    Viper.Terminal.Say("Passed: " + Viper.Fmt.Int(passed));
    Viper.Terminal.Say("Failed: " + Viper.Fmt.Int(failed));
}
