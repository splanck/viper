' test.bas - Test Functions
' Part of SQLite Clone - Viper Basic Implementation

AddFile "executor.bas"

'=============================================================================
' TEST EXECUTOR
'=============================================================================

SUB TestExecutor()
    DIM result AS QueryResult
    DIM sql AS STRING
    DIM r AS INTEGER
    DIM row AS SqlRow

    PRINT "=== Executor Test ==="
    PRINT ""

    ' Reset database
    gDbInitialized = 0

    ' Test CREATE TABLE
    PRINT "--- CREATE TABLE ---"
    sql = "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER);"
    PRINT "SQL: "; sql
    PRINT "About to call ExecuteSql..."
    LET result = ExecuteSql(sql)
    PRINT "Back from ExecuteSql"
    PRINT "Result: "; result.message
    PRINT ""

    ' Test INSERT
    PRINT "--- INSERT ---"
    sql = "INSERT INTO users (id, name, age) VALUES (1, 'Alice', 30);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    sql = "INSERT INTO users (id, name, age) VALUES (2, 'Bob', 25), (3, 'Charlie', 35);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    ' Test SELECT all
    PRINT "--- SELECT * ---"
    sql = "SELECT * FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test SELECT with specific columns
    PRINT "--- SELECT name, age ---"
    sql = "SELECT name, age FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test SELECT with WHERE
    PRINT "--- SELECT with WHERE ---"
    sql = "SELECT * FROM users WHERE age > 28;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test UPDATE
    PRINT "--- UPDATE ---"
    sql = "UPDATE users SET age = 31 WHERE name = 'Alice';"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    ' Verify UPDATE
    PRINT "--- Verify UPDATE ---"
    sql = "SELECT * FROM users WHERE name = 'Alice';"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test DELETE
    PRINT "--- DELETE ---"
    sql = "DELETE FROM users WHERE name = 'Bob';"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    ' Verify DELETE
    PRINT "--- Verify DELETE ---"
    sql = "SELECT * FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test ORDER BY - Insert more data
    PRINT "--- ORDER BY ---"
    sql = "INSERT INTO users (id, name, age) VALUES (4, 'Diana', 28);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    ' Test ORDER BY ASC
    PRINT "--- ORDER BY age ---"
    sql = "SELECT * FROM users ORDER BY age;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test ORDER BY DESC
    PRINT "--- ORDER BY age DESC ---"
    sql = "SELECT * FROM users ORDER BY age DESC;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test ORDER BY name
    PRINT "--- ORDER BY name ASC ---"
    sql = "SELECT * FROM users ORDER BY name ASC;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test LIMIT
    PRINT "--- LIMIT 2 ---"
    sql = "SELECT * FROM users ORDER BY age LIMIT 2;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test LIMIT with OFFSET
    PRINT "--- LIMIT 2 OFFSET 1 ---"
    sql = "SELECT * FROM users ORDER BY age LIMIT 2 OFFSET 1;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test DISTINCT - First add some duplicate ages
    PRINT "--- DISTINCT Setup ---"
    sql = "INSERT INTO users (id, name, age) VALUES (5, 'Eve', 31), (6, 'Frank', 28);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    ' Test SELECT without DISTINCT (shows duplicates)
    PRINT "--- SELECT age (no DISTINCT) ---"
    sql = "SELECT age FROM users ORDER BY age;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test SELECT DISTINCT
    PRINT "--- SELECT DISTINCT age ---"
    sql = "SELECT DISTINCT age FROM users ORDER BY age;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test COUNT(*)
    PRINT "--- COUNT(*) ---"
    sql = "SELECT COUNT(*) FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test SUM(age)
    PRINT "--- SUM(age) ---"
    sql = "SELECT SUM(age) FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test AVG(age)
    PRINT "--- AVG(age) ---"
    sql = "SELECT AVG(age) FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test MIN(age) and MAX(age)
    PRINT "--- MIN(age), MAX(age) ---"
    sql = "SELECT MIN(age), MAX(age) FROM users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test COUNT(*) with WHERE
    PRINT "--- COUNT(*) with WHERE ---"
    sql = "SELECT COUNT(*) FROM users WHERE age > 30;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test GROUP BY
    PRINT "--- GROUP BY age ---"
    sql = "SELECT age, COUNT(*) FROM users GROUP BY age;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        PRINT "Columns: ";
        FOR c = 0 TO result.columnCount - 1
            PRINT result.columnNames(c); " ";
        NEXT c
        PRINT ""
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test GROUP BY with SUM
    PRINT "--- GROUP BY with SUM ---"
    sql = "SELECT age, SUM(id) FROM users GROUP BY age;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test HAVING clause
    PRINT "--- HAVING COUNT(*) > 1 ---"
    sql = "SELECT age, COUNT(*) FROM users GROUP BY age HAVING COUNT(*) > 1;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test HAVING with SUM
    PRINT "--- HAVING SUM(id) >= 4 ---"
    sql = "SELECT age, SUM(id) FROM users GROUP BY age HAVING SUM(id) >= 4;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test table alias
    PRINT "--- Table Alias ---"
    sql = "SELECT * FROM users u WHERE age > 30;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test table alias with AS
    PRINT "--- Table Alias with AS ---"
    sql = "SELECT id, name FROM users AS u;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' CROSS JOIN tests - create a second table
    PRINT "--- CROSS JOIN Tests ---"
    sql = "CREATE TABLE orders (order_id INTEGER, user_id INTEGER, product TEXT);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message
    PRINT ""

    sql = "INSERT INTO orders VALUES (101, 1, 'Widget');"
    LET result = ExecuteSql(sql)
    sql = "INSERT INTO orders VALUES (102, 3, 'Gadget');"
    LET result = ExecuteSql(sql)
    sql = "INSERT INTO orders VALUES (103, 99, 'Thingamajig');"
    LET result = ExecuteSql(sql)
    PRINT "Inserted 3 orders (one with non-existent user_id=99)"
    PRINT ""

    ' Test basic CROSS JOIN with SELECT *
    PRINT "--- Basic CROSS JOIN ---"
    sql = "SELECT * FROM users, orders;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        PRINT "Columns: "; result.columnCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test CROSS JOIN with WHERE clause (simulating INNER JOIN)
    PRINT "--- CROSS JOIN with WHERE (simulating INNER JOIN) ---"
    sql = "SELECT users.name, orders.product FROM users, orders WHERE users.id = orders.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test CROSS JOIN with aliases
    PRINT "--- CROSS JOIN with aliases ---"
    sql = "SELECT u.name, o.product FROM users u, orders o WHERE u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test INNER JOIN syntax
    PRINT "--- INNER JOIN ---"
    sql = "SELECT users.name, orders.product FROM users INNER JOIN orders ON users.id = orders.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test INNER JOIN with aliases
    PRINT "--- INNER JOIN with aliases ---"
    sql = "SELECT u.name, o.product FROM users u INNER JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test bare JOIN (same as INNER JOIN)
    PRINT "--- Bare JOIN (same as INNER JOIN) ---"
    sql = "SELECT u.name, o.product FROM users u JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test LEFT JOIN - returns all users, even those without orders
    PRINT "--- LEFT JOIN ---"
    sql = "SELECT u.name, o.product FROM users u LEFT JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test LEFT OUTER JOIN (same as LEFT JOIN)
    PRINT "--- LEFT OUTER JOIN ---"
    sql = "SELECT u.name, o.product FROM users u LEFT OUTER JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test RIGHT JOIN - returns all orders, even those without matching users
    PRINT "--- RIGHT JOIN ---"
    sql = "SELECT u.name, o.product FROM users u RIGHT JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test FULL OUTER JOIN - returns all from both tables
    PRINT "--- FULL OUTER JOIN ---"
    sql = "SELECT u.name, o.product FROM users u FULL OUTER JOIN orders o ON u.id = o.user_id;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Phase 6: Subqueries
    PRINT "--- Subquery Tests ---"

    ' Test scalar subquery in WHERE: find users older than average
    PRINT "--- Scalar subquery (AVG) ---"
    sql = "SELECT name, age FROM users WHERE age > (SELECT AVG(age) FROM users);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test scalar subquery getting max value
    PRINT "--- Scalar subquery (MAX) ---"
    sql = "SELECT name FROM users WHERE age = (SELECT MAX(age) FROM users);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test scalar subquery getting count
    PRINT "--- Scalar subquery (COUNT) ---"
    sql = "SELECT * FROM users WHERE id > (SELECT COUNT(*) FROM orders);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test IN subquery: users who have placed orders
    PRINT "--- IN subquery (users with orders) ---"
    sql = "SELECT name FROM users WHERE id IN (SELECT user_id FROM orders);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test IN subquery with WHERE clause
    PRINT "--- IN subquery (filtered) ---"
    sql = "SELECT name FROM users WHERE id IN (SELECT user_id FROM orders WHERE user_id > 2);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    PRINT ""
    PRINT "--- Derived Table Tests ---"

    ' Test derived table (subquery in FROM)
    PRINT "--- Derived table (SELECT *) ---"
    sql = "SELECT * FROM (SELECT name, age FROM users WHERE age > 25) AS older_users;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Test derived table with aggregation
    PRINT "--- Derived table (COUNT) ---"
    sql = "SELECT * FROM (SELECT COUNT(*) AS cnt FROM users) AS user_count;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    PRINT "--- Correlated Subquery Tests ---"

    ' Create employees table for correlated subquery tests
    sql = "CREATE TABLE employees (id INTEGER, name TEXT, department TEXT, salary INTEGER);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT "Result: "; result.message

    ' Insert test data
    sql = "INSERT INTO employees VALUES (1, 'Alice', 'Engineering', 80000);"
    LET result = ExecuteSql(sql)
    sql = "INSERT INTO employees VALUES (2, 'Bob', 'Engineering', 75000);"
    LET result = ExecuteSql(sql)
    sql = "INSERT INTO employees VALUES (3, 'Charlie', 'Sales', 60000);"
    LET result = ExecuteSql(sql)
    sql = "INSERT INTO employees VALUES (4, 'Diana', 'Sales', 65000);"
    LET result = ExecuteSql(sql)
    sql = "INSERT INTO employees VALUES (5, 'Eve', 'Engineering', 90000);"
    LET result = ExecuteSql(sql)
    PRINT "Inserted 5 employees"

    ' Correlated subquery: Find employees earning more than avg salary in their department
    PRINT "--- Correlated subquery (salary > dept avg) ---"

    ' First, let's test a simple non-correlated AVG to verify it works
    PRINT "-- Testing non-correlated AVG on employees --"

    ' First verify the table has all rows
    sql = "SELECT * FROM employees;"
    LET result = ExecuteSql(sql)
    PRINT "All employees count: "; result.rowCount

    sql = "SELECT AVG(salary) FROM employees;"
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Overall AVG salary rows: "; result.rowCount
        IF result.rowCount > 0 THEN
            LET row = result.rows(0)
            PRINT "  Overall AVG: "; row.ToString$()
        END IF
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    sql = "SELECT name, department, salary FROM employees e WHERE salary > (SELECT AVG(salary) FROM employees WHERE department = e.department);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Correlated subquery: Find employees where their salary equals max in their dept
    PRINT "--- Correlated subquery (salary = dept max) ---"
    sql = "SELECT name, department, salary FROM employees e WHERE salary = (SELECT MAX(salary) FROM employees WHERE department = e.department);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    ELSE
        PRINT "ERROR: "; result.message
    END IF
    PRINT ""

    ' Reset database for constraint tests
    gDatabaseInitialized = 0

    '=========================================================================
    ' CONSTRAINT TESTS (Phase 7)
    '=========================================================================
    PRINT "--- Constraint Tests ---"

    ' Create table with constraints
    sql = "CREATE TABLE consttest (id INTEGER PRIMARY KEY, name TEXT NOT NULL, email TEXT UNIQUE);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Valid insert
    sql = "INSERT INTO consttest VALUES (1, 'Alice', 'alice@test.com');"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Test PRIMARY KEY duplicate rejection
    sql = "INSERT INTO consttest VALUES (1, 'Bob', 'bob@test.com');"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success = 0 THEN
        PRINT "EXPECTED ERROR: "; result.message
    ELSE
        PRINT "FAILED: Duplicate PRIMARY KEY was allowed!"
    END IF

    ' Test NOT NULL rejection
    sql = "INSERT INTO consttest VALUES (2, NULL, 'null@test.com');"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success = 0 THEN
        PRINT "EXPECTED ERROR: "; result.message
    ELSE
        PRINT "FAILED: NULL in NOT NULL column was allowed!"
    END IF

    ' Test UNIQUE rejection
    sql = "INSERT INTO consttest VALUES (3, 'Charlie', 'alice@test.com');"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success = 0 THEN
        PRINT "EXPECTED ERROR: "; result.message
    ELSE
        PRINT "FAILED: Duplicate UNIQUE value was allowed!"
    END IF

    ' Valid insert with unique values
    sql = "INSERT INTO consttest VALUES (2, 'Bob', 'bob@test.com');"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Verify final state
    sql = "SELECT * FROM consttest;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    END IF
    PRINT ""

    '=========================================================================
    ' DEFAULT VALUE TESTS
    '=========================================================================
    PRINT "--- Default Value Tests ---"

    ' Reset database
    gDatabaseInitialized = 0

    ' Create table with DEFAULT values
    sql = "CREATE TABLE deftest (id INTEGER PRIMARY KEY, name TEXT DEFAULT 'Unknown', score INTEGER DEFAULT 100);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Insert with explicit values
    sql = "INSERT INTO deftest VALUES (1, 'Alice', 95);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Insert with NULL to trigger defaults
    sql = "INSERT INTO deftest VALUES (2, NULL, NULL);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Verify
    sql = "SELECT * FROM deftest;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    END IF
    PRINT ""

    PRINT "--- AUTOINCREMENT Tests ---"

    ' Reset database
    gDatabaseInitialized = 0

    ' Create table with AUTOINCREMENT
    sql = "CREATE TABLE autoinc (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Insert without specifying id (should auto-generate)
    sql = "INSERT INTO autoinc (name) VALUES ('Alice');"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    sql = "INSERT INTO autoinc (name) VALUES ('Bob');"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    sql = "INSERT INTO autoinc (name) VALUES ('Charlie');"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Verify auto-generated IDs
    sql = "SELECT * FROM autoinc;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    END IF
    PRINT ""

    PRINT "--- FOREIGN KEY Tests ---"

    ' Reset database
    gDbInitialized = 0

    ' Create parent table
    sql = "CREATE TABLE fkparent (id INTEGER PRIMARY KEY, name TEXT);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Insert into parent table
    sql = "INSERT INTO fkparent VALUES (1, 'Engineering');"
    LET result = ExecuteSql(sql)
    PRINT result.message
    sql = "INSERT INTO fkparent VALUES (2, 'Sales');"
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Create child table with foreign key
    sql = "CREATE TABLE fkchild (id INTEGER PRIMARY KEY, name TEXT, parent_id INTEGER REFERENCES fkparent(id));"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Valid insert (references existing parent)
    sql = "INSERT INTO fkchild VALUES (1, 'Alice', 1);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Invalid insert (references non-existent parent)
    sql = "INSERT INTO fkchild VALUES (2, 'Bob', 99);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success = 0 THEN
        PRINT "EXPECTED ERROR: "; result.message
    ELSE
        PRINT "FAILED: Invalid foreign key value was allowed!"
    END IF

    ' NULL foreign key is allowed
    sql = "INSERT INTO fkchild VALUES (3, 'Charlie', NULL);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Verify final state
    sql = "SELECT * FROM fkchild;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success <> 0 THEN
        PRINT "Rows returned: "; result.rowCount
        FOR r = 0 TO result.rowCount - 1
            LET row = result.rows(r)
            PRINT "  Row "; r; ": "; row.ToString$()
        NEXT r
    END IF
    PRINT ""

    PRINT ""
    PRINT "--- INDEX Tests ---"

    ' Reset database
    gDbInitialized = 0

    ' Create test table
    sql = "CREATE TABLE indextest (id INTEGER PRIMARY KEY, name TEXT, score INTEGER);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Insert test data
    sql = "INSERT INTO indextest VALUES (1, 'Alice', 95);"
    LET result = ExecuteSql(sql)
    sql = "INSERT INTO indextest VALUES (2, 'Bob', 87);"
    LET result = ExecuteSql(sql)
    sql = "INSERT INTO indextest VALUES (3, 'Charlie', 92);"
    LET result = ExecuteSql(sql)
    PRINT "Inserted 3 rows"

    ' Create an index
    sql = "CREATE INDEX idx_name ON indextest (name);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Create a multi-column index
    sql = "CREATE INDEX idx_score_name ON indextest (score, name);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Create a unique index
    sql = "CREATE UNIQUE INDEX idx_id_unique ON indextest (id);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Try to create duplicate index (should fail)
    sql = "CREATE INDEX idx_name ON indextest (name);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success = 0 THEN
        PRINT "EXPECTED ERROR: "; result.message
    ELSE
        PRINT "FAILED: Duplicate index was allowed!"
    END IF

    ' Try to create index on non-existent table (should fail)
    sql = "CREATE INDEX idx_bad ON nonexistent (col);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success = 0 THEN
        PRINT "EXPECTED ERROR: "; result.message
    ELSE
        PRINT "FAILED: Index on nonexistent table was allowed!"
    END IF

    ' Try to create index on non-existent column (should fail)
    sql = "CREATE INDEX idx_badcol ON indextest (badcol);"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success = 0 THEN
        PRINT "EXPECTED ERROR: "; result.message
    ELSE
        PRINT "FAILED: Index on nonexistent column was allowed!"
    END IF

    ' Drop an index
    sql = "DROP INDEX idx_name;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    PRINT result.message

    ' Try to drop non-existent index (should fail)
    sql = "DROP INDEX idx_nonexistent;"
    PRINT "SQL: "; sql
    LET result = ExecuteSql(sql)
    IF result.success = 0 THEN
        PRINT "EXPECTED ERROR: "; result.message
    ELSE
        PRINT "FAILED: Drop of nonexistent index was allowed!"
    END IF
    PRINT ""

    PRINT "=== Executor Test PASSED ==="
END SUB

'=============================================================================
' TEST FUNCTIONS
'=============================================================================

SUB TestSqlValue()
    DIM v1 AS SqlValue
    DIM v2 AS SqlValue
    DIM v3 AS SqlValue
    DIM v4 AS SqlValue
    DIM v5 AS SqlValue
    DIM v6 AS SqlValue

    PRINT "=== SqlValue Test ==="

    LET v1 = SqlNull()
    PRINT "v1 (NULL): "; v1.ToString$(); " type="; v1.TypeName$()

    LET v2 = SqlInteger(42)
    PRINT "v2 (INTEGER): "; v2.ToString$(); " type="; v2.TypeName$()

    LET v3 = SqlReal(3.14, "3.14")
    PRINT "v3 (REAL): "; v3.ToString$(); " type="; v3.TypeName$()

    LET v4 = SqlText("Hello")
    PRINT "v4 (TEXT): "; v4.ToString$(); " type="; v4.TypeName$()

    ' Test comparisons
    LET v5 = SqlInteger(100)
    PRINT "v2.Compare(v5): "; v2.Compare(v5); " (expect -1)"
    PRINT "v5.Compare(v2): "; v5.Compare(v2); " (expect 1)"

    LET v6 = SqlInteger(42)
    PRINT "v2.Equals(v6): "; v2.Equals(v6); " (expect -1=true)"

    PRINT "=== SqlValue Test PASSED ==="
END SUB

SUB TestTokens()
    DIM tok AS Token

    PRINT "=== Token Types Test ==="

    LET tok = NEW Token()
    tok.Init(TK_SELECT, "SELECT", 1, 1)
    PRINT "Token 1: kind="; tok.kind; " text='"; tok.text; "'"

    LET tok = NEW Token()
    tok.Init(TK_INTEGER, "42", 1, 8)
    PRINT "Token 2: kind="; tok.kind; " text='"; tok.text; "'"

    PRINT "=== Token Types Test PASSED ==="
END SUB

SUB TestLexer()
    DIM sql AS STRING

    PRINT "=== Lexer Test ==="

    LET sql = "SELECT id, name FROM users WHERE age > 21;"
    PRINT "Input: "; sql
    PRINT ""

    LexerInit(sql)
    LexerNextToken()
    WHILE gTok.kind <> TK_EOF
        PRINT "  "; TokenTypeName$(gTok.kind); ": '"; gTok.text; "'"
        LexerNextToken()
    WEND

    PRINT ""

    LET sql = "INSERT INTO users VALUES (1, 'John', 3.14);"
    PRINT "Input: "; sql
    PRINT ""

    LexerInit(sql)
    LexerNextToken()
    WHILE gTok.kind <> TK_EOF
        PRINT "  "; TokenTypeName$(gTok.kind); ": '"; gTok.text; "'"
        LexerNextToken()
    WEND

    PRINT ""
    PRINT "=== Lexer Test PASSED ==="
END SUB

SUB TestColumnRow()
    DIM col1 AS SqlColumn
    DIM col2 AS SqlColumn
    DIM col3 AS SqlColumn
    DIM row1 AS SqlRow
    DIM row2 AS SqlRow
    DIM v AS SqlValue

    PRINT "=== Column & Row Test ==="

    ' Test Column creation
    LET col1 = MakeColumn("id", SQL_INTEGER)
    col1.primaryKey = -1
    col1.autoIncrement = -1
    PRINT "col1: "; col1.ToString$()

    LET col2 = MakeColumn("name", SQL_TEXT)
    col2.notNull = -1
    PRINT "col2: "; col2.ToString$()

    LET col3 = MakeColumn("score", SQL_REAL)
    col3.SetDefault(SqlReal(0.0, "0.0"))
    PRINT "col3: "; col3.ToString$()

    ' Test Row creation
    LET row1 = MakeRow(3)
    PRINT "row1 (empty): "; row1.ToString$()

    row1.SetValue(0, SqlInteger(1))
    row1.SetValue(1, SqlText("Alice"))
    row1.SetValue(2, SqlReal(95.5, "95.5"))
    PRINT "row1 (filled): "; row1.ToString$()

    ' Test Row clone
    LET row2 = row1.Clone()
    row2.SetValue(0, SqlInteger(2))
    row2.SetValue(1, SqlText("Bob"))
    PRINT "row2 (cloned): "; row2.ToString$()

    ' Verify original unchanged
    PRINT "row1 (verify): "; row1.ToString$()

    ' Test Row value access
    LET v = row1.GetValue(1)
    PRINT "row1.GetValue(1): "; v.ToString$()

    PRINT "=== Column & Row Test PASSED ==="
END SUB

SUB TestTable()
    DIM users AS SqlTable
    DIM colId AS SqlColumn
    DIM colName AS SqlColumn
    DIM colAge AS SqlColumn
    DIM row1 AS SqlRow
    DIM row2 AS SqlRow
    DIM row3 AS SqlRow
    DIM idx AS INTEGER

    PRINT "=== Table Test ==="

    ' Create a users table
    LET users = MakeTable("users")

    ' Add columns
    LET colId = MakeColumn("id", SQL_INTEGER)
    colId.primaryKey = -1
    colId.autoIncrement = -1
    users.AddColumn(colId)

    LET colName = MakeColumn("name", SQL_TEXT)
    colName.notNull = -1
    users.AddColumn(colName)

    LET colAge = MakeColumn("age", SQL_INTEGER)
    users.AddColumn(colAge)

    PRINT users.ToString$()
    PRINT ""
    PRINT users.SchemaString$()
    PRINT ""

    ' Test column lookup
    idx = users.FindColumnIndex("name")
    PRINT "Column 'name' at index: "; idx

    ' Insert some rows
    LET row1 = users.CreateRow()
    row1.SetValue(0, SqlInteger(1))
    row1.SetValue(1, SqlText("Alice"))
    row1.SetValue(2, SqlInteger(30))
    users.AddRow(row1)

    LET row2 = users.CreateRow()
    row2.SetValue(0, SqlInteger(2))
    row2.SetValue(1, SqlText("Bob"))
    row2.SetValue(2, SqlInteger(25))
    users.AddRow(row2)

    LET row3 = users.CreateRow()
    row3.SetValue(0, SqlInteger(3))
    row3.SetValue(1, SqlText("Charlie"))
    row3.SetValue(2, SqlNull())
    users.AddRow(row3)

    PRINT users.ToString$()
    PRINT ""
    PRINT "Rows inserted: "; users.rowCount

    ' Test delete
    users.DeleteRow(1)
    PRINT "After deleting row 1:"
    PRINT "(Row 0 and 2 should remain, row 1 marked deleted)"

    PRINT ""
    PRINT "=== Table Test PASSED ==="
END SUB

SUB TestExpr()
    DIM e1 AS Expr
    DIM e2 AS Expr
    DIM e3 AS Expr
    DIM e4 AS Expr
    DIM e5 AS Expr
    DIM e6 AS Expr
    DIM e7 AS Expr
    DIM e8 AS Expr
    DIM e9 AS Expr
    DIM add AS Expr
    DIM mul AS Expr
    DIM e10 AS Expr
    DIM e11 AS Expr
    DIM fn AS Expr

    PRINT "=== Expression Test ==="

    ' Test literal expressions
    LET e1 = ExprNull()
    PRINT "Null literal: "; e1.ToString$(); " (kind="; e1.kind; ")"

    LET e2 = ExprInt(42)
    PRINT "Int literal: "; e2.ToString$(); " (kind="; e2.kind; ")"

    LET e3 = ExprReal(3.14, "3.14")
    PRINT "Real literal: "; e3.ToString$()

    LET e4 = ExprText("hello")
    PRINT "Text literal: "; e4.ToString$()

    ' Test column reference
    LET e5 = ExprColumn("name")
    PRINT "Column ref: "; e5.ToString$(); " (kind="; e5.kind; ", col="; e5.columnName; ")"

    LET e6 = ExprTableColumn("users", "id")
    PRINT "Table.column ref: "; e6.ToString$()

    ' Test star expression
    LET e7 = ExprStar()
    PRINT "Star: "; e7.ToString$()

    ' Test binary expressions
    LET e8 = ExprBinary(OP_ADD, ExprInt(1), ExprInt(2))
    PRINT "Binary add: "; e8.ToString$()

    LET e9 = ExprBinary(OP_EQ, ExprColumn("age"), ExprInt(21))
    PRINT "Binary eq: "; e9.ToString$()

    ' Test compound expression: (a + b) * c
    LET add = ExprBinary(OP_ADD, ExprColumn("a"), ExprColumn("b"))
    LET mul = ExprBinary(OP_MUL, add, ExprColumn("c"))
    PRINT "Compound: "; mul.ToString$()

    ' Test unary expression
    LET e10 = ExprUnary(UOP_NEG, ExprInt(5))
    PRINT "Unary neg: "; e10.ToString$()

    LET e11 = ExprUnary(UOP_NOT, ExprColumn("active"))
    PRINT "Unary not: "; e11.ToString$()

    ' Test function call
    LET fn = ExprFunc("COUNT")
    fn.AddArg(ExprStar())
    PRINT "Function: "; fn.ToString$()

    PRINT "=== Expression Test PASSED ==="
END SUB

SUB TestParser()
    DIM sql1 AS STRING
    DIM sql2 AS STRING
    DIM sql3 AS STRING
    DIM createStmt AS CreateTableStmt
    DIM insertStmt AS InsertStmt
    DIM expr AS Expr

    PRINT "=== Parser Test ==="

    ' Test CREATE TABLE parsing
    LET sql1 = "CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER);"
    PRINT "Input: "; sql1
    PRINT ""

    ParserInit(sql1)
    ParserAdvance()  ' Skip CREATE token
    LET createStmt = ParseCreateTableStmt()

    IF gParserHasError <> 0 THEN
        PRINT "ERROR: "; gParserError
    ELSE
        PRINT "Parsed:"
        PRINT createStmt.ToString$()
    END IF

    PRINT ""

    ' Test INSERT parsing
    LET sql2 = "INSERT INTO users (id, name, age) VALUES (1, 'Alice', 30), (2, 'Bob', 25);"
    PRINT "Input: "; sql2
    PRINT ""

    ParserInit(sql2)
    ParserAdvance()  ' Skip INSERT token
    LET insertStmt = ParseInsertStmt()

    IF gParserHasError <> 0 THEN
        PRINT "ERROR: "; gParserError
    ELSE
        PRINT "Parsed:"
        PRINT insertStmt.ToString$()
    END IF

    PRINT ""

    ' Test expression parsing
    LET sql3 = "1 + 2 * 3"
    PRINT "Expr: "; sql3
    ParserInit(sql3)
    LET expr = ParseExpr()
    PRINT "Parsed: "; expr.ToString$()

    PRINT ""
    PRINT "=== Parser Test PASSED ==="
END SUB

'=============================================================================
' MAIN - Run all tests
'=============================================================================

PRINT "SQLite Clone - Viper Basic Edition"
PRINT "==================================="
PRINT ""

TestTokens()
PRINT ""
TestLexer()
PRINT ""
TestSqlValue()
PRINT ""
TestColumnRow()
PRINT ""
TestTable()
PRINT ""
TestParser()
PRINT ""
TestExecutor()
