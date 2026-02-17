# SQL Reference

## Table of Contents

- [Data Types](#data-types)
- [DDL Statements](#ddl-statements)
- [DML Statements](#dml-statements)
- [Query Clauses](#query-clauses)
- [Expressions and Operators](#expressions-and-operators)
- [Built-in Functions](#built-in-functions)
- [Date/Time Functions](#datetime-functions)
- [Aggregate Functions](#aggregate-functions)
- [Extended Date/Time Functions](#extended-datetime-functions)
- [Regular Expression Functions](#regular-expression-functions)
- [JSON Functions](#json-functions)
- [Joins](#joins)
- [Multi-Table UPDATE/DELETE](#multi-table-updatedelete)
- [Subqueries](#subqueries)
- [Set Operations](#set-operations)
- [Indexes](#indexes)
- [Constraints](#constraints)

---

## Data Types

| Type | Description | Examples |
|------|-------------|----------|
| `INTEGER` | Signed integer | `42`, `-17`, `0` |
| `REAL` | Floating-point number | `3.14`, `-0.5`, `100.0` |
| `TEXT` | Character string | `'hello'`, `'O''Brien'` |
| `BOOLEAN` | True/false value | `TRUE`, `FALSE` |
| `DATE` | Calendar date | `DATE('2025-06-15')` |
| `TIMESTAMP` | Date and time | `TIMESTAMP('2025-06-15T10:30:00')` |
| `JSON` | JSON document | `'{"key": "value"}'` |
| `NULL` | Absence of a value | `NULL` |

String literals use single quotes. To include a literal single quote, double it: `'O''Brien'`.

**BOOLEAN** values can be tested with `IS TRUE`, `IS FALSE`, `IS NOT TRUE`, `IS NOT FALSE`. Booleans are cross-compatible with INTEGER (TRUE=1, FALSE=0).

**DATE** supports arithmetic: `DATE + INTEGER` adds days, `DATE - DATE` returns the day difference. Use `CAST(expr AS DATE)` or the `DATE()` function to create date values.

**TIMESTAMP** stores date and time as epoch seconds. `NOW()` and `CURRENT_TIMESTAMP` return native TIMESTAMP values. `CURRENT_DATE` returns a native DATE.

**Type coercion**: INSERT into typed columns auto-converts values (e.g., INTEGER 1 → BOOLEAN TRUE, TEXT '2025-06-15' → DATE).

NULL follows SQL three-valued logic: comparisons with NULL return unknown (treated as false), use `IS NULL` / `IS NOT NULL` to test for NULL values.

---

## DDL Statements

### CREATE TABLE

```sql
CREATE TABLE table_name (
    column1 TYPE [constraints],
    column2 TYPE [constraints],
    ...
);
```

Example with all constraint types:

```sql
CREATE TABLE products (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    price REAL DEFAULT 0.0,
    category TEXT DEFAULT 'general',
    supplier_id INTEGER REFERENCES suppliers(id)
);
```

### DROP TABLE

```sql
DROP TABLE table_name;
```

### ALTER TABLE

Add a column (existing rows get NULL for the new column):

```sql
ALTER TABLE users ADD COLUMN city TEXT;
ALTER TABLE users ADD COLUMN score INTEGER;
```

Drop a column:

```sql
ALTER TABLE users DROP COLUMN city;
```

Rename a table:

```sql
ALTER TABLE users RENAME TO customers;
```

Rename a column:

```sql
ALTER TABLE users RENAME COLUMN email TO contact_email;
```

### Table Partitioning

Partition tables by range, list, or hash for logical data distribution:

```sql
-- Range partitioning
CREATE TABLE orders (id INTEGER, amount INTEGER) PARTITION BY RANGE (id);
CREATE TABLE orders_low PARTITION OF orders FOR VALUES FROM (MINVALUE) TO (100);
CREATE TABLE orders_mid PARTITION OF orders FOR VALUES FROM (100) TO (1000);
CREATE TABLE orders_high PARTITION OF orders FOR VALUES FROM (1000) TO (MAXVALUE);

-- List partitioning
CREATE TABLE regions (id INTEGER, region TEXT) PARTITION BY LIST (region);
CREATE TABLE regions_us PARTITION OF regions FOR VALUES IN ('US', 'CA');
CREATE TABLE regions_eu PARTITION OF regions FOR VALUES IN ('UK', 'DE', 'FR');

-- Hash partitioning
CREATE TABLE data (id INTEGER, val TEXT) PARTITION BY HASH (id);
CREATE TABLE data_p0 PARTITION OF data FOR VALUES WITH (MODULUS 3, REMAINDER 0);
CREATE TABLE data_p1 PARTITION OF data FOR VALUES WITH (MODULUS 3, REMAINDER 1);
CREATE TABLE data_p2 PARTITION OF data FOR VALUES WITH (MODULUS 3, REMAINDER 2);
```

INSERT into the parent table auto-routes rows to the correct child partition. SELECT from the parent merges data from all partitions with automatic pruning on equality predicates. Aggregates (COUNT, SUM, MIN, MAX, AVG) work across partitions.

### Table Inheritance (INHERITS)

Create child tables that inherit columns from a parent:

```sql
-- Child inherits parent columns (id, name) and adds its own (salary)
CREATE TABLE people (id INTEGER, name TEXT);
CREATE TABLE employees (salary REAL) INHERITS (people);

-- Polymorphic query: parent + children
SELECT id, name FROM people;

-- Exclude children with ONLY
SELECT id, name FROM ONLY people;
```

### Generated Columns (GENERATED ALWAYS AS)

Define computed columns that are automatically calculated on INSERT and UPDATE:

```sql
CREATE TABLE products (
    price REAL,
    tax_rate REAL,
    total REAL GENERATED ALWAYS AS (price + price * tax_rate) STORED
);

INSERT INTO products (price, tax_rate) VALUES (100.0, 0.1);
-- total is automatically computed as 110
```

### CREATE VIEW / DROP VIEW

Views are named queries that act as virtual tables:

```sql
-- Create a view
CREATE VIEW active_users AS SELECT * FROM users WHERE status = 'active';

-- Query the view like a table
SELECT name FROM active_users WHERE age > 30;

-- Views support WHERE, ORDER BY, LIMIT on the outer query
SELECT * FROM active_users ORDER BY name LIMIT 10;

-- Drop a view
DROP VIEW active_users;
```

---

## DML Statements

### INSERT

Insert with named columns (recommended):

```sql
INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30);
```

Insert multiple rows:

```sql
INSERT INTO users (name, age) VALUES ('Bob', 25), ('Charlie', 35), ('Diana', 28);
```

Insert with expressions:

```sql
INSERT INTO stats (name, value) VALUES ('total', 100 + 50);
```

Insert with DEFAULT VALUES (all columns get defaults or NULL):

```sql
INSERT INTO logs DEFAULT VALUES;
```

Insert from a SELECT query (INSERT...SELECT):

```sql
-- Copy all rows from one table to another
INSERT INTO archive SELECT * FROM users;

-- Copy specific columns with a filter
INSERT INTO vip_users (name, age) SELECT name, age FROM users WHERE age > 30;

-- Insert aggregated data
INSERT INTO summary SELECT COUNT(*), AVG(age) FROM users;
```

Standalone VALUES as a query:

```sql
VALUES (1, 'Alice'), (2, 'Bob'), (3, 'Carol');
```

### SELECT

Basic query:

```sql
SELECT * FROM users;
SELECT name, email FROM users;
```

With all clauses:

```sql
SELECT DISTINCT department, COUNT(*) AS cnt
FROM employees
WHERE salary > 50000
GROUP BY department
HAVING COUNT(*) >= 2
ORDER BY cnt DESC
LIMIT 10 OFFSET 5;
```

Column aliases:

```sql
SELECT name AS employee_name, salary * 12 AS annual_salary FROM employees;
```

SELECT without FROM (expression evaluation):

```sql
SELECT 1 + 2;
SELECT CAST('42' AS INTEGER);
SELECT EXISTS (SELECT 1 FROM users);
```

Derived tables (subqueries in FROM):

```sql
-- Use a subquery as a virtual table
SELECT name FROM (SELECT * FROM users WHERE age > 25) AS adults ORDER BY name;

-- Aggregate in subquery, filter in outer
SELECT cnt FROM (SELECT COUNT(*) AS cnt FROM users) AS stats;
```

### UPDATE

```sql
UPDATE products SET price = 19.99 WHERE name = 'Widget';
UPDATE products SET price = price * 1.1, category = 'premium' WHERE price > 50;
```

### DELETE

```sql
DELETE FROM orders WHERE status = 'cancelled';
DELETE FROM logs;  -- deletes all rows
```

### RETURNING Clause

All DML statements support `RETURNING` to get back affected rows:

```sql
-- INSERT RETURNING: see the inserted row (including defaults and auto-increment)
INSERT INTO users (name) VALUES ('Alice') RETURNING *;
INSERT INTO users (name) VALUES ('Bob') RETURNING id, name;

-- UPDATE RETURNING: see the updated rows
UPDATE users SET score = score + 10 WHERE name = 'Alice' RETURNING id, score;

-- DELETE RETURNING: see what was deleted
DELETE FROM users WHERE id = 5 RETURNING *;
```

### INSERT ON CONFLICT (UPSERT)

Handle duplicate key violations gracefully:

```sql
-- Skip duplicate inserts
INSERT INTO users VALUES (1, 'Alice') ON CONFLICT (id) DO NOTHING;

-- Update on conflict (upsert)
INSERT INTO users VALUES (1, 'Alice', 95)
  ON CONFLICT (id) DO UPDATE SET name = 'Alice', score = 95;
```

### TRUNCATE TABLE

Fast table truncation (removes all rows):

```sql
TRUNCATE TABLE users;
TRUNCATE users;  -- TABLE keyword is optional
```

### GENERATE_SERIES

Generate a series of integers as a virtual table:

```sql
SELECT * FROM generate_series(1, 10);            -- 1 to 10
SELECT * FROM generate_series(0, 100, 10);       -- 0, 10, 20, ..., 100
SELECT * FROM generate_series(5, 1, -1);         -- 5, 4, 3, 2, 1
SELECT * FROM generate_series(1, 1000) LIMIT 5;  -- first 5 values
```

### Transactions

ViperSQL supports explicit transactions with `BEGIN`, `COMMIT`, and `ROLLBACK`:

```sql
BEGIN;
INSERT INTO accounts VALUES (1, 'Alice', 1000);
UPDATE accounts SET balance = balance - 100 WHERE id = 1;
INSERT INTO transfers VALUES (1, 1, 2, 100);
COMMIT;  -- all changes become permanent

BEGIN;
DELETE FROM accounts WHERE id = 1;
ROLLBACK;  -- all changes within the transaction are undone
```

**Statement-level atomicity**: Multi-row INSERT and UPDATE statements are atomic — if any row fails a constraint check, all changes from that statement are rolled back automatically.

**MVCC snapshot isolation**: Each `BEGIN` assigns a unique transaction ID and takes a snapshot. Rows are versioned with xmin/xmax — readers see a consistent snapshot without blocking writers.

**Transaction rules**:
- Nested `BEGIN` is not allowed (returns an error)
- `COMMIT`/`ROLLBACK` without an active transaction returns an error
- DELETE compaction is deferred during transactions and applied on COMMIT
- WAL-based crash recovery with full redo/undo (ARIES-style) for persistent databases

### Savepoints

Savepoints allow partial rollback within a transaction:

```sql
BEGIN;
INSERT INTO orders VALUES (1, 'Widget', 100);
SAVEPOINT before_discount;
UPDATE orders SET price = 80 WHERE id = 1;
-- Oops, wrong discount — rollback just the update
ROLLBACK TO SAVEPOINT before_discount;
-- Continue with correct discount
UPDATE orders SET price = 90 WHERE id = 1;
COMMIT;
```

- `SAVEPOINT name` — create a savepoint at the current journal position
- `ROLLBACK TO [SAVEPOINT] name` — undo all changes since the savepoint
- `RELEASE [SAVEPOINT] name` — discard a savepoint (changes become permanent within the transaction)
- Nested savepoints are supported (rollback to an outer savepoint removes inner ones)
- Re-creating a savepoint with the same name overwrites the previous position

### Prepared Statements

Prepare and execute parameterized queries:

```sql
PREPARE find_user AS SELECT * FROM users WHERE id = $1;
EXECUTE find_user (42);
EXECUTE find_user (99);

PREPARE insert_user AS INSERT INTO users VALUES ($1, $2, $3);
EXECUTE insert_user (1, 'Alice', 30);

DEALLOCATE find_user;     -- remove a specific prepared statement
DEALLOCATE ALL;           -- remove all prepared statements
```

### EXPLAIN ANALYZE

Run a query and report actual execution statistics:

```sql
EXPLAIN ANALYZE SELECT * FROM users WHERE age > 25;
-- Returns: plan info, Execution Time, Rows Returned
```

### Cursors

Declare cursors to iterate over result sets with bidirectional navigation:

```sql
DECLARE my_cursor CURSOR FOR SELECT * FROM large_table ORDER BY id;

FETCH NEXT FROM my_cursor;           -- get one row
FETCH FORWARD 5 FROM my_cursor;     -- get next 5 rows
FETCH ALL FROM my_cursor;           -- get all remaining rows
FETCH FIRST FROM my_cursor;         -- jump to first row
FETCH LAST FROM my_cursor;          -- jump to last row
FETCH PRIOR FROM my_cursor;         -- go backward one row
FETCH ABSOLUTE 10 FROM my_cursor;   -- jump to row 10
FETCH RELATIVE -3 FROM my_cursor;   -- go back 3 rows

MOVE FORWARD 5 IN my_cursor;        -- skip 5 rows without returning data

CLOSE my_cursor;                     -- release cursor resources
CLOSE ALL;                           -- close all open cursors
```

### COPY TO/FROM

Bulk data import and export in CSV format:

```sql
-- Export a table to CSV file
COPY users TO '/tmp/users.csv';

-- Export query results to CSV
COPY (SELECT name, score FROM users WHERE score > 80) TO '/tmp/top_users.csv';

-- Import CSV data into a table
COPY users FROM '/tmp/users.csv';

-- Export to STDOUT (returns rows with CSV lines)
COPY users TO STDOUT;

-- With options
COPY users TO '/tmp/users.tsv' WITH (FORMAT CSV, DELIMITER '\t', HEADER);
```

### CALL Statement

Execute stored functions using CALL:

```sql
CREATE FUNCTION add_user(p_id INTEGER, p_name TEXT) RETURNS VOID AS
  'INSERT INTO users VALUES ($1, $2)';

CALL add_user(1, 'Alice');
CALL add_user(2, 'Bob');

CREATE FUNCTION get_score(p_id INTEGER) RETURNS INTEGER AS
  'SELECT score FROM users WHERE id = $1';

CALL get_score(1);  -- returns the query result
```

### Array Type & Functions

PostgreSQL-compatible arrays with constructor syntax and utility functions:

```sql
-- Array constructor
SELECT ARRAY[1, 2, 3];           -- {1,2,3}
SELECT ARRAY['a', 'b', 'c'];    -- {a,b,c}
SELECT ARRAY[1 + 1, 2 * 3];     -- {2,6}

-- Array functions
SELECT ARRAY_LENGTH(ARRAY[10, 20, 30], 1);          -- 3
SELECT ARRAY_UPPER(ARRAY[10, 20, 30], 1);           -- 3
SELECT ARRAY_LOWER(ARRAY[10, 20, 30], 1);           -- 1
SELECT ARRAY_TO_STRING(ARRAY[1, 2, 3], ', ');        -- 1, 2, 3
SELECT ARRAY_APPEND(ARRAY[1, 2], 3);                -- {1,2,3}
SELECT ARRAY_PREPEND(0, ARRAY[1, 2]);               -- {0,1,2}
SELECT ARRAY_CAT(ARRAY[1, 2], ARRAY[3, 4]);         -- {1,2,3,4}
SELECT ARRAY_REMOVE(ARRAY[1, 2, 3, 2], 2);          -- {1,3}
SELECT ARRAY_POSITION(ARRAY[10, 20, 30], 20);       -- 2
SELECT TYPEOF(ARRAY[1, 2, 3]);                      -- array
```

### Materialized Views

Materialized views store query results as physical data for fast reads:

```sql
-- Create materialized view (executes query and stores result)
CREATE MATERIALIZED VIEW monthly_totals AS
  SELECT region, SUM(amount) FROM sales GROUP BY region;

-- Query reads from stored snapshot (no re-execution)
SELECT * FROM monthly_totals WHERE region = 'North';

-- Refresh to pick up changes to source tables
REFRESH MATERIALIZED VIEW monthly_totals;

-- Drop materialized view
DROP MATERIALIZED VIEW monthly_totals;
```

### CREATE TABLE AS SELECT (CTAS)

Create tables from query results:

```sql
-- Create table from query result
CREATE TABLE high_scores AS
  SELECT name, score FROM students WHERE score >= 85;

-- Create with aggregation
CREATE TABLE region_summary AS
  SELECT region, SUM(amount), COUNT(*) FROM orders GROUP BY region;

-- Create temporary table
CREATE TEMPORARY TABLE temp_copy AS SELECT * FROM source_table;
```

---

## Query Clauses

### WHERE

Supports all comparison, logical, and special operators:

```sql
SELECT * FROM users WHERE age >= 18 AND age <= 65;
SELECT * FROM users WHERE name = 'Alice' OR name = 'Bob';
SELECT * FROM users WHERE NOT (age < 18);
```

### ORDER BY

Sort results ascending (default) or descending:

```sql
SELECT * FROM users ORDER BY age ASC;
SELECT * FROM users ORDER BY age DESC;
SELECT * FROM users ORDER BY department ASC, salary DESC;
```

Order by expressions and aggregates:

```sql
SELECT name, price * quantity AS total FROM orders ORDER BY price * quantity DESC;
SELECT category, SUM(price) FROM products GROUP BY category ORDER BY SUM(price) DESC;
```

### GROUP BY

Group rows by one or more columns for aggregate calculations:

```sql
SELECT department, COUNT(*), AVG(salary) FROM employees GROUP BY department;
SELECT city, state, COUNT(*) FROM customers GROUP BY city, state;
```

### HAVING

Filter groups after aggregation (like WHERE but for groups):

```sql
SELECT department, AVG(salary) AS avg_sal
FROM employees
GROUP BY department
HAVING AVG(salary) > 60000;
```

### LIMIT and OFFSET

Limit the number of returned rows and skip rows:

```sql
SELECT * FROM products ORDER BY price LIMIT 10;           -- first 10
SELECT * FROM products ORDER BY price LIMIT 10 OFFSET 20; -- rows 21-30
```

### DISTINCT

Remove duplicate rows from results:

```sql
SELECT DISTINCT category FROM products;
SELECT DISTINCT city, state FROM customers;
```

---

## Expressions and Operators

### Arithmetic

| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition | `price + tax` |
| `-` | Subtraction | `total - discount` |
| `*` | Multiplication | `quantity * price` |
| `/` | Division | `total / count` |
| `-` (unary) | Negation | `-amount` |

Integer arithmetic stays integer. Mixed integer/real operations produce real results.

### Comparison

| Operator | Description |
|----------|-------------|
| `=` | Equal |
| `!=` or `<>` | Not equal |
| `<` | Less than |
| `<=` | Less than or equal |
| `>` | Greater than |
| `>=` | Greater than or equal |

### Logical

| Operator | Description |
|----------|-------------|
| `AND` | Both conditions must be true |
| `OR` | Either condition must be true |
| `NOT` | Inverts a condition |

### Special Operators

**BETWEEN** — Range check (inclusive). Also supports `BETWEEN SYMMETRIC` (auto-swaps bounds):

```sql
SELECT * FROM products WHERE price BETWEEN 10 AND 50;
SELECT * FROM products WHERE price NOT BETWEEN 10 AND 50;
SELECT * FROM products WHERE price BETWEEN SYMMETRIC 50 AND 10;  -- same as 10 AND 50
```

**LIKE** — Pattern matching (`%` = any characters, `_` = single character):

```sql
SELECT * FROM users WHERE name LIKE 'A%';        -- starts with A
SELECT * FROM users WHERE email LIKE '%@gmail%';  -- contains @gmail
SELECT * FROM users WHERE name LIKE '_ob';        -- 3 chars ending in "ob"
SELECT * FROM users WHERE name NOT LIKE '%test%'; -- doesn't contain "test"
```

**IN** — Check membership in a list or subquery:

```sql
SELECT * FROM users WHERE age IN (25, 30, 35);
SELECT * FROM users WHERE dept IN (SELECT id FROM departments WHERE active = 1);
SELECT * FROM users WHERE age NOT IN (25, 30);
```

**IS NULL / IS NOT NULL** — Test for NULL values:

```sql
SELECT * FROM users WHERE email IS NULL;
SELECT * FROM users WHERE email IS NOT NULL;
```

**String concatenation** with `||`:

```sql
SELECT first_name || ' ' || last_name AS full_name FROM users;
```

### CASE Expressions

Conditional logic within queries:

```sql
SELECT name, score,
    CASE
        WHEN score >= 90 THEN 'A'
        WHEN score >= 80 THEN 'B'
        WHEN score >= 70 THEN 'C'
        ELSE 'F'
    END AS grade
FROM students;
```

CASE can be used anywhere an expression is valid: SELECT lists, WHERE clauses, ORDER BY, etc.

### CAST

Explicitly convert values between types:

```sql
SELECT CAST('42' AS INTEGER);     -- 42 (integer)
SELECT CAST(3.7 AS INTEGER);      -- 3 (truncated)
SELECT CAST(42 AS TEXT);           -- '42' (text)
SELECT CAST('3.14' AS REAL);       -- 3.14 (real)
SELECT CAST(NULL AS INTEGER);     -- NULL (preserved)

-- CAST in expressions
SELECT CAST('10' AS INTEGER) + 5;  -- 15
```

Supported target types: `INTEGER`, `INT`, `REAL`, `FLOAT`, `TEXT`, `VARCHAR`, `BOOLEAN`, `DATE`, `TIMESTAMP`, `JSON`, `JSONB`.

---

## Built-in Functions

### String Functions

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `UPPER(s)` | Convert to uppercase | `UPPER('hello')` | `HELLO` |
| `LOWER(s)` | Convert to lowercase | `LOWER('HELLO')` | `hello` |
| `LENGTH(s)` | String length | `LENGTH('hello')` | `5` |
| `SUBSTR(s, start, len)` | Substring (1-based) | `SUBSTR('hello', 1, 3)` | `hel` |
| `TRIM(s)` | Remove leading/trailing spaces | `TRIM('  hi  ')` | `hi` |
| `LTRIM(s)` | Remove leading spaces | `LTRIM('  hi')` | `hi` |
| `RTRIM(s)` | Remove trailing spaces | `RTRIM('hi  ')` | `hi` |
| `REPLACE(s, from, to)` | Replace all occurrences | `REPLACE('hello', 'l', 'r')` | `herro` |
| `CONCAT(a, b, ...)` | Concatenate strings | `CONCAT('a', 'b', 'c')` | `abc` |
| `INSTR(s, sub)` | Find substring position (1-based, 0 if not found) | `INSTR('hello', 'lo')` | `4` |
| `POSITION(sub IN s)` | SQL-standard substring position | `POSITION('lo' IN 'hello')` | `4` |
| `SUBSTRING(s FROM n FOR len)` | SQL-standard substring | `SUBSTRING('hello' FROM 2 FOR 3)` | `ell` |
| `ILIKE` | Case-insensitive LIKE | `name ILIKE '%alice%'` | — |
| `INITCAP(s)` | Capitalize first letter of each word | `INITCAP('hello world')` | `Hello World` |
| `REPEAT(s, n)` | Repeat string n times | `REPEAT('ab', 3)` | `ababab` |
| `TRANSLATE(s, from, to)` | Character-by-character replacement | `TRANSLATE('abc', 'ac', 'xz')` | `xbz` |
| `OVERLAY(s PLACING r FROM n FOR len)` | Replace substring at position | `OVERLAY('hello' PLACING 'XX' FROM 2 FOR 3)` | `hXXlo` |
| `MD5(s)` | MD5 hash | `MD5('hello')` | `5d41402a...` |
| `SHA256(s)` | SHA-256 hash | `SHA256('hello')` | `2cf24dba...` |

SQL-standard TRIM with directional options:

```sql
SELECT TRIM(LEADING ' ' FROM '  hello  ');   -- 'hello  '
SELECT TRIM(TRAILING ' ' FROM '  hello  ');  -- '  hello'
SELECT TRIM(BOTH ' ' FROM '  hello  ');      -- 'hello'
```

### Math Functions

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `ABS(x)` | Absolute value | `ABS(-5)` | `5` |
| `MOD(a, b)` | Modulo (remainder) | `MOD(10, 3)` | `1` |
| `MIN(a, b)` | Minimum of two values | `MIN(5, 3)` | `3` |
| `MAX(a, b)` | Maximum of two values | `MAX(5, 3)` | `5` |
| `ROUND(x)` | Round to nearest integer | `ROUND(3.7)` | `4` |
| `POWER(x, y)` / `POW(x, y)` | Raise x to the power y | `POWER(2, 10)` | `1024` |
| `SQRT(x)` | Square root | `SQRT(144)` | `12` |
| `CEIL(x)` / `CEILING(x)` | Round up to integer | `CEIL(3.2)` | `4` |
| `FLOOR(x)` | Round down to integer | `FLOOR(3.9)` | `3` |
| `SIGN(x)` | Sign (-1, 0, or 1) | `SIGN(-42)` | `-1` |
| `LOG(x)` / `LN(x)` | Natural logarithm | `LOG(2.718)` | `~1` |
| `LOG10(x)` | Base-10 logarithm | `LOG10(100)` | `2` |
| `LOG2(x)` | Base-2 logarithm | `LOG2(8)` | `3` |
| `EXP(x)` | e raised to x | `EXP(1)` | `~2.718` |
| `PI()` | Pi constant | `PI()` | `3.14159...` |
| `RANDOM()` / `RAND()` | Random integer | `RANDOM()` | varies |
| `SIN(x)` | Sine (radians) | `SIN(0)` | `0` |
| `COS(x)` | Cosine (radians) | `COS(0)` | `1` |
| `TAN(x)` | Tangent (radians) | `TAN(0)` | `0` |
| `ASIN(x)` | Arc sine | `ASIN(1)` | `~1.5708` |
| `ACOS(x)` | Arc cosine | `ACOS(1)` | `0` |
| `ATAN(x)` | Arc tangent | `ATAN(1)` | `~0.7854` |
| `ATAN2(y, x)` | Two-argument arc tangent | `ATAN2(1, 1)` | `~0.7854` |
| `DEGREES(x)` | Radians to degrees | `DEGREES(3.14159)` | `~180` |
| `RADIANS(x)` | Degrees to radians | `RADIANS(180)` | `~3.14159` |
| `TRUNC(x)` / `TRUNCATE(x)` | Truncate to integer | `TRUNC(3.9)` | `3` |
| `CBRT(x)` | Cube root | `CBRT(27)` | `3` |
| `GCD(a, b)` | Greatest common divisor | `GCD(12, 8)` | `4` |
| `LCM(a, b)` | Least common multiple | `LCM(4, 6)` | `12` |
| `DIV(a, b)` | Integer division | `DIV(7, 2)` | `3` |

### Extended String Functions

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `LEFT(s, n)` | First n characters | `LEFT('hello', 3)` | `hel` |
| `RIGHT(s, n)` | Last n characters | `RIGHT('hello', 3)` | `llo` |
| `LPAD(s, len, pad)` | Left-pad to length | `LPAD('42', 5, '0')` | `00042` |
| `RPAD(s, len, pad)` | Right-pad to length | `RPAD('hi', 5, '.')` | `hi...` |
| `REVERSE(s)` | Reverse a string | `REVERSE('hello')` | `olleh` |
| `HEX(n)` | Integer to hex | `HEX(255)` | `FF` |
| `CHAR_LENGTH(s)` / `CHARACTER_LENGTH(s)` | Character count | `CHAR_LENGTH('hello')` | `5` |
| `OCTET_LENGTH(s)` | Byte count | `OCTET_LENGTH('hello')` | `5` |
| `BIT_LENGTH(s)` | Bit count (8 * bytes) | `BIT_LENGTH('hello')` | `40` |
| `TRANSLATE(s, from, to)` | Character substitution | `TRANSLATE('hello', 'el', 'ip')` | `hippo` |
| `OVERLAY(s, new, start [, count])` | Substring replacement | `OVERLAY('hello', 'XX', 3, 2)` | `heXXo` |
| `STARTS_WITH(s, prefix)` | Test prefix | `STARTS_WITH('hello', 'he')` | `1` |
| `ENDS_WITH(s, suffix)` | Test suffix | `ENDS_WITH('hello', 'lo')` | `1` |
| `QUOTE_LITERAL(s)` | SQL-quote a string | `QUOTE_LITERAL('O''Brien')` | `'O''Brien'` |
| `QUOTE_IDENT(s)` | SQL-quote an identifier | `QUOTE_IDENT('my col')` | `"my col"` |
| `MD5(s)` | MD5 hash | `MD5('hello')` | `5d41402a...` |
| `SHA256(s)` | SHA-256 hash | `SHA256('hello')` | `2cf24dba...` |
| `TO_HEX(n)` | Integer to lowercase hex | `TO_HEX(255)` | `ff` |

### Conditional Functions

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `GREATEST(a, b, ...)` | Largest value | `GREATEST(1, 5, 3)` | `5` |
| `LEAST(a, b, ...)` | Smallest value | `LEAST(1, 5, 3)` | `1` |

### NULL Handling Functions

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `COALESCE(a, b, ...)` | First non-NULL argument | `COALESCE(NULL, NULL, 'found')` | `found` |
| `IFNULL(expr, default)` | Default if NULL | `IFNULL(NULL, 'default')` | `default` |
| `NULLIF(a, b)` | NULL if a equals b | `NULLIF(5, 5)` | `NULL` |
| `IIF(cond, true_val, false_val)` | Inline conditional | `IIF(age > 18, 'adult', 'minor')` | depends |
| `TYPEOF(expr)` | Returns type name as string | `TYPEOF(42)` | `integer` |

---

## Date/Time Functions

ViperSQL provides date/time functions powered by the Viper runtime's `DateTime` library. Dates are stored as TEXT in ISO format (`YYYY-MM-DDTHH:MM:SS`) or as INTEGER Unix timestamps.

### Current Date/Time

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `NOW()` | Current local datetime | `SELECT NOW()` | `2025-06-15T14:30:00` |
| `CURRENT_DATE` | Current date (no parens) | `SELECT CURRENT_DATE` | `2025-06-15` |
| `CURRENT_TIME` | Current time (no parens) | `SELECT CURRENT_TIME` | `14:30:00` |
| `CURRENT_TIMESTAMP` | Current datetime (no parens) | `SELECT CURRENT_TIMESTAMP` | `2025-06-15T14:30:00` |

### Construction

| Function | Description | Example |
|----------|-------------|---------|
| `DATETIME(y, m, d [, h, min, s])` | Build datetime string | `DATETIME(2025, 6, 15, 10, 30, 0)` |
| `FROM_EPOCH(seconds)` | Unix epoch to ISO string | `FROM_EPOCH(1750000000)` |

### Extraction

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `DATE(dt)` | Extract date portion | `DATE('2025-06-15T10:30:00')` | `2025-06-15` |
| `TIME(dt)` | Extract time portion | `TIME('2025-06-15T10:30:00')` | `10:30:00` |
| `YEAR(dt)` | Extract year | `YEAR('2025-06-15T10:30:00')` | `2025` |
| `MONTH(dt)` | Extract month (1-12) | `MONTH('2025-06-15T10:30:00')` | `6` |
| `DAY(dt)` | Extract day (1-31) | `DAY('2025-06-15T10:30:00')` | `15` |
| `HOUR(dt)` | Extract hour (0-23) | `HOUR('2025-06-15T10:30:00')` | `10` |
| `MINUTE(dt)` | Extract minute (0-59) | `MINUTE('2025-06-15T10:30:00')` | `30` |
| `SECOND(dt)` | Extract second (0-59) | `SECOND('2025-06-15T10:30:45')` | `45` |
| `DAYOFWEEK(dt)` | Day of week (0=Sun, 6=Sat) | `DAYOFWEEK('2025-06-15T00:00:00')` | `0` |
| `EPOCH(dt)` | Convert to Unix epoch | `EPOCH('2025-01-01T00:00:00')` | `1735707600` |

### Arithmetic

| Function | Description | Example |
|----------|-------------|---------|
| `DATE_ADD(dt, days)` | Add days to datetime | `DATE_ADD('2025-01-01T00:00:00', 10)` |
| `DATE_SUB(dt, days)` | Subtract days from datetime | `DATE_SUB('2025-01-15T00:00:00', 5)` |
| `DATEDIFF(dt1, dt2)` | Difference in days | `DATEDIFF('2025-06-15T00:00:00', '2025-01-15T00:00:00')` -> `151` |
| `TIMEDIFF(dt1, dt2)` | Difference in seconds | `TIMEDIFF('2025-01-01T01:00:00', '2025-01-01T00:00:00')` -> `3600` |

### Formatting

| Function | Description | Example |
|----------|-------------|---------|
| `STRFTIME(format, dt)` | Format with strftime codes | `STRFTIME('%Y-%m-%d', '2025-06-15T10:30:00')` |

Date/time usage examples:

```sql
-- Store events with timestamps
CREATE TABLE events (id INTEGER PRIMARY KEY, name TEXT, event_date TEXT);
INSERT INTO events VALUES (1, 'Launch', DATETIME(2025, 1, 15, 9, 0, 0));
INSERT INTO events VALUES (2, 'Release', DATETIME(2025, 6, 15, 18, 0, 0));

-- Query by date components
SELECT name FROM events WHERE MONTH(event_date) = 6;
SELECT name, YEAR(event_date) AS yr FROM events;

-- Date arithmetic
SELECT name, DATE(DATE_ADD(event_date, 30)) AS plus_30_days FROM events;
SELECT DATEDIFF('2025-06-15T00:00:00', '2025-01-15T00:00:00') AS days_between;

-- Insert with current timestamp
INSERT INTO logs VALUES (1, 'startup', NOW());
```

---

## Aggregate Functions

Used with `GROUP BY` or over entire result sets:

| Function | Description | Example |
|----------|-------------|---------|
| `COUNT(*)` | Count all rows | `SELECT COUNT(*) FROM users` |
| `COUNT(col)` | Count non-NULL values | `SELECT COUNT(email) FROM users` |
| `COUNT(DISTINCT col)` | Count distinct non-NULL values | `SELECT COUNT(DISTINCT dept) FROM employees` |
| `SUM(col)` | Sum of values | `SELECT SUM(salary) FROM employees` |
| `AVG(col)` | Average of values | `SELECT AVG(score) FROM tests` |
| `MIN(col)` | Minimum value | `SELECT MIN(price) FROM products` |
| `MAX(col)` | Maximum value | `SELECT MAX(price) FROM products` |
| `STRING_AGG(col, sep)` | Concatenate values with separator | `SELECT STRING_AGG(name, ', ') FROM users` |
| `GROUP_CONCAT(col, sep)` | MySQL alias for STRING_AGG | `SELECT GROUP_CONCAT(tag, ',') FROM tags` |
| `ARRAY_AGG(col)` | Collect values into array | `SELECT ARRAY_AGG(score) FROM tests` |
| `BOOL_AND(col)` | Logical AND of all values | `SELECT BOOL_AND(active) FROM users` |
| `BOOL_OR(col)` | Logical OR of all values | `SELECT BOOL_OR(premium) FROM users` |

All aggregate functions support the `FILTER (WHERE ...)` clause — see [advanced-sql.md](advanced-sql.md#filter-clause).

```sql
-- STRING_AGG: concatenate values with separator
SELECT dept, STRING_AGG(name, ', ') AS members FROM employees GROUP BY dept;
-- Result: 'Engineering' | 'Alice, Bob, Carol'

-- ARRAY_AGG: collect values into an array
SELECT dept, ARRAY_AGG(salary) AS salaries FROM employees GROUP BY dept;

-- BOOL_AND / BOOL_OR: logical aggregation
SELECT dept, BOOL_AND(active) AS all_active, BOOL_OR(manager) AS has_manager
FROM employees GROUP BY dept;
```

Full example:

```sql
SELECT
    department,
    COUNT(*) AS headcount,
    SUM(salary) AS total_salary,
    AVG(salary) AS avg_salary,
    MIN(salary) AS min_salary,
    MAX(salary) AS max_salary
FROM employees
GROUP BY department
HAVING COUNT(*) >= 2
ORDER BY avg_salary DESC;
```

---

## Extended Date/Time Functions

Beyond the core date/time functions above, ViperSQL provides PostgreSQL-compatible extended date/time functions:

### Date Component Functions

| Function | Description | Example |
|----------|-------------|---------|
| `DATE_PART(field, source)` | Extract component (year, month, day, hour, minute, second, dow, epoch) | `DATE_PART('year', NOW())` |
| `DATE_TRUNC(field, source)` | Truncate to precision (year, month, day, hour, minute) | `DATE_TRUNC('month', NOW())` |
| `AGE(ts1, ts2)` | Human-readable interval between timestamps | `AGE(NOW(), '2020-01-01T00:00:00')` |

### Date/Time Construction

| Function | Description | Example |
|----------|-------------|---------|
| `MAKE_DATE(y, m, d)` | Construct a date | `MAKE_DATE(2025, 6, 15)` |
| `MAKE_TIMESTAMP(y, mo, d, h, mi, s)` | Construct a timestamp | `MAKE_TIMESTAMP(2025, 6, 15, 10, 30, 0)` |
| `MAKE_INTERVAL(years, months, days)` | Construct an interval string | `MAKE_INTERVAL(1, 2, 3)` |
| `TO_TIMESTAMP(epoch)` | Epoch seconds to timestamp | `TO_TIMESTAMP(1750000000)` |
| `TO_DATE(str, format)` | Parse date string (YYYY-MM-DD) | `TO_DATE('2025-06-15', 'YYYY-MM-DD')` |

### Timing and Formatting

| Function | Description | Example |
|----------|-------------|---------|
| `TO_CHAR(ts, format)` | Format timestamp as string | `TO_CHAR(NOW(), 'YYYY-MM-DD HH24:MI:SS')` |
| `ISFINITE(ts)` | Check if timestamp is finite | `ISFINITE(NOW())` → `1` |
| `CLOCK_TIMESTAMP()` | Current wall-clock time | `SELECT CLOCK_TIMESTAMP()` |
| `STATEMENT_TIMESTAMP()` | Time of current statement | `SELECT STATEMENT_TIMESTAMP()` |
| `TRANSACTION_TIMESTAMP()` | Time of current transaction | `SELECT TRANSACTION_TIMESTAMP()` |
| `TIMEOFDAY()` | Current time as text string | `SELECT TIMEOFDAY()` |

---

## Regular Expression Functions

ViperSQL provides PostgreSQL-compatible regular expression operators and functions powered by the Viper runtime's `Pattern` library.

### Regex Operators

```sql
-- ~ operator: regex match (case-sensitive)
SELECT 'hello' ~ 'hel.*';          -- 1 (true)
SELECT 'Hello' ~ '^hello$';         -- 0 (false, case-sensitive)

-- ~* operator: regex match (case-insensitive)
SELECT 'Hello' ~* '^hello$';        -- 1 (true)

-- SIMILAR TO: SQL-standard pattern matching (% and _ wildcards)
SELECT 'hello' SIMILAR TO 'hel%';   -- 1
SELECT 'hello' NOT SIMILAR TO 'xyz%'; -- 1
```

### Regex Functions

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `REGEXP_MATCH(text, pattern)` | Test for match (1/0) | `REGEXP_MATCH('hello', 'hel')` | `1` |
| `REGEXP_MATCHES(text, pattern)` | Return all matches as array | `REGEXP_MATCHES('ab1cd2', '[0-9]+')` | `{1,2}` |
| `REGEXP_REPLACE(text, pattern, repl)` | Regex substitution | `REGEXP_REPLACE('hello', '[aeiou]', '*')` | `h*ll*` |
| `REGEXP_COUNT(text, pattern)` | Count non-overlapping matches | `REGEXP_COUNT('abcabc', 'abc')` | `2` |
| `REGEXP_SPLIT_TO_ARRAY(text, pattern)` | Split into array by pattern | `REGEXP_SPLIT_TO_ARRAY('a,b,,c', ',')` | `{a,b,,c}` |
| `REGEXP_SUBSTR(text, pattern)` | Extract first match | `REGEXP_SUBSTR('abc123', '[0-9]+')` | `123` |
| `REGEXP_INSTR(text, pattern)` | Position of first match (1-based) | `REGEXP_INSTR('abc123', '[0-9]+')` | `4` |
| `REGEXP_LIKE(text, pattern)` | Boolean match test (1/0) | `REGEXP_LIKE('hello', '^h')` | `1` |

---

## JSON Functions

ViperSQL supports JSON and JSONB column types with a comprehensive set of JSON functions for inspection, extraction, and construction. Internally, JSON and JSONB are stored identically as validated JSON text.

### JSON Column Type

```sql
-- Create a table with JSON columns
CREATE TABLE events (
    id INTEGER PRIMARY KEY,
    data JSON,
    metadata JSONB
);

-- Insert JSON data (text is auto-coerced to JSON)
INSERT INTO events VALUES (1, '{"type": "click", "x": 100}', '{"source": "web"}');

-- TYPEOF returns 'json' for JSON values
SELECT TYPEOF(data) FROM events;  -- 'json'
```

### JSON Inspection

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `JSON_VALID(s)` | Check if string is valid JSON | `JSON_VALID('{"a":1}')` | `1` |
| `JSON_TYPE(s)` | JSON value type | `JSON_TYPE('{"a":1}')` | `object` |
| `JSON_TYPEOF(s)` | Alias for JSON_TYPE | `JSON_TYPEOF('[1,2]')` | `array` |

### JSON Extraction

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `JSON_EXTRACT(json, path)` | Extract value by JSONPath | `JSON_EXTRACT('{"a":1}', '$.a')` | `1` |
| `JSON_EXTRACT_TEXT(json, key)` | Extract and unwrap string | `JSON_EXTRACT_TEXT('{"a":"hi"}', 'a')` | `hi` |
| `JSON_ARRAY_LENGTH(json)` | Number of array elements | `JSON_ARRAY_LENGTH('[1,2,3]')` | `3` |
| `JSON_OBJECT_KEYS(json)` | Comma-separated key list | `JSON_OBJECT_KEYS('{"a":1,"b":2}')` | `a,b` |

JSONPath supports dot notation (`$.key.subkey`), array indexing (`$[0]`), and nested paths (`$.items[0].name`):

```sql
SELECT JSON_EXTRACT('{"user": {"name": "Alice", "scores": [10, 20]}}', '$.user.name');
-- Result: "Alice"

SELECT JSON_EXTRACT('{"items": [{"id": 1}, {"id": 2}]}', '$.items[1].id');
-- Result: 2
```

### JSON Construction

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `JSON_BUILD_OBJECT(k1, v1, ...)` | Build JSON object from key-value pairs | `JSON_BUILD_OBJECT('a', 1, 'b', 'hi')` | `{"a":1,"b":"hi"}` |
| `JSON_BUILD_ARRAY(v1, v2, ...)` | Build JSON array from values | `JSON_BUILD_ARRAY(1, 'two', 3)` | `[1,"two",3]` |
| `JSON_QUOTE(s)` | Wrap value in JSON quotes | `JSON_QUOTE('hello')` | `"hello"` |
| `JSON(s)` | Validate and return JSON | `JSON('{"a":1}')` | `{"a":1}` |

### CAST to JSON

```sql
SELECT CAST('{"key": "value"}' AS JSON);
SELECT CAST('[1, 2, 3]' AS JSONB);
```

---

## Joins

### INNER JOIN

Returns rows that have matching values in both tables:

```sql
SELECT e.name, d.name AS department
FROM employees e
INNER JOIN departments d ON e.dept_id = d.id;
```

### LEFT JOIN

Returns all rows from the left table, with NULLs for unmatched right table columns:

```sql
SELECT e.name, d.name AS department
FROM employees e
LEFT JOIN departments d ON e.dept_id = d.id;
```

### RIGHT JOIN

Returns all rows from the right table, with NULLs for unmatched left table columns:

```sql
SELECT e.name, d.name AS department
FROM employees e
RIGHT JOIN departments d ON e.dept_id = d.id;
```

### FULL OUTER JOIN

Returns all rows from both tables, with NULLs where there is no match:

```sql
SELECT e.name, d.name AS department
FROM employees e
FULL JOIN departments d ON e.dept_id = d.id;
```

### CROSS JOIN

Cartesian product of two tables. Two equivalent syntaxes:

```sql
-- Explicit CROSS JOIN
SELECT c.color, s.size FROM colors c CROSS JOIN sizes s;

-- Implicit (comma) syntax
SELECT c.color, s.size FROM colors c, sizes s;
```

### Multi-Table Joins

Join three or more tables:

```sql
SELECT o.id, c.name, p.name
FROM orders o
JOIN customers c ON o.customer_id = c.id
JOIN products p ON o.product_id = p.id
WHERE o.total > 100;
```

Implicit cross join with filtering:

```sql
SELECT a.name, b.name, c.name
FROM authors a, books b, reviews r
WHERE b.author_id = a.id AND r.book_id = b.id;
```

Also supports `NATURAL JOIN` (auto-detect common columns), `JOIN ... USING (col1, col2)`, and `LATERAL` subqueries — see [advanced-sql.md](advanced-sql.md).

---

## Multi-Table UPDATE/DELETE

### UPDATE...FROM

Update rows in one table based on values from another table:

```sql
-- Update prices from a lookup table
UPDATE products SET price = pu.new_price
FROM price_updates pu
WHERE products.id = pu.product_id;

-- Update with calculation
UPDATE products SET price = products.price - d.discount
FROM discounts d
WHERE products.category = d.category;
```

### DELETE...USING

Delete rows in one table based on matches in another table:

```sql
-- Delete orders for discontinued products
DELETE FROM orders
USING discontinued d
WHERE orders.product_id = d.product_id;

-- Without alias
DELETE FROM orders
USING to_remove
WHERE orders.product_id = to_remove.product_id;
```

---

## Subqueries

### Scalar Subqueries

A subquery that returns a single value, used in WHERE clauses:

```sql
SELECT name, score
FROM students
WHERE score > (SELECT AVG(score) FROM students);
```

### IN Subqueries

Check if a value exists in a subquery result:

```sql
SELECT name
FROM employees
WHERE dept_id IN (SELECT id FROM departments WHERE name = 'Engineering');

SELECT name
FROM employees
WHERE dept_id NOT IN (SELECT id FROM departments WHERE location = 'Remote');
```

### EXISTS / NOT EXISTS

Test whether a subquery returns any rows:

```sql
-- Find customers who have placed orders
SELECT name FROM customers
WHERE EXISTS (SELECT 1 FROM orders WHERE orders.customer_id = customers.id);

-- Find customers without orders
SELECT name FROM customers
WHERE NOT EXISTS (SELECT 1 FROM orders WHERE orders.customer_id = customers.id);

-- EXISTS in a SELECT expression (returns 1 or 0)
SELECT EXISTS (SELECT 1 FROM users) AS has_users;
```

---

## Set Operations

Combine results from multiple SELECT statements:

### UNION (removes duplicates)

```sql
SELECT name FROM team_a
UNION
SELECT name FROM team_b;
```

### UNION ALL (keeps duplicates)

```sql
SELECT name FROM team_a
UNION ALL
SELECT name FROM team_b;
```

### EXCEPT (rows in first but not second)

```sql
SELECT name FROM all_students
EXCEPT
SELECT name FROM graduated_students;
```

### INTERSECT (rows in both)

```sql
SELECT name FROM team_a
INTERSECT
SELECT name FROM team_b;
```

### EXCEPT ALL / INTERSECT ALL (preserves duplicates)

```sql
-- EXCEPT ALL: if row appears N times in first and M in second, result has max(N-M, 0) copies
SELECT x FROM t1 EXCEPT ALL SELECT x FROM t2;

-- INTERSECT ALL: result has min(N, M) copies of each row
SELECT x FROM t1 INTERSECT ALL SELECT x FROM t2;
```

### Chained Set Operations

Multiple set operations can be chained in a single query:

```sql
-- 3-way UNION ALL
SELECT x FROM t1 UNION ALL SELECT x FROM t2 UNION ALL SELECT x FROM t3;

-- Mixed operations (left-to-right evaluation)
SELECT x FROM t1 UNION ALL SELECT x FROM t2 EXCEPT SELECT x FROM t3;
```

---

## Indexes

Create indexes to speed up queries on frequently searched columns:

```sql
-- Standard index
CREATE INDEX idx_users_email ON users (email);

-- Unique index (also enforces uniqueness)
CREATE UNIQUE INDEX idx_users_email ON users (email);

-- Multi-column index
CREATE INDEX idx_orders_date_status ON orders (order_date, status);

-- Partial index (only rows matching WHERE predicate)
CREATE INDEX idx_active ON users (email) WHERE active = 1;

-- Drop an index
DROP INDEX idx_users_email;
```

Indexes are automatically used by the query optimizer when filtering with `=` on indexed columns. Unique indexes enforce uniqueness at INSERT time. Indexes are automatically maintained during INSERT, UPDATE, and DELETE operations.

### Composite Indexes

Multi-column indexes support efficient lookups when filtering on multiple columns:

```sql
-- Create a composite index on two columns
CREATE INDEX idx_orders ON orders (customer_id, order_date);

-- Full composite equality lookup (uses the composite index)
SELECT * FROM orders WHERE customer_id = 5 AND order_date = '2025-01-15';

-- Prefix lookup (uses leading column of composite index)
SELECT * FROM orders WHERE customer_id = 5;

-- Three-column composite index
CREATE UNIQUE INDEX idx_event ON events (year, month, day);
```

Composite indexes build keys from concatenated column values with a `|` separator to prevent cross-value collisions. The optimizer automatically detects AND chains of equality conditions and matches them against available composite indexes. EXPLAIN shows composite index usage:

```sql
EXPLAIN SELECT * FROM orders WHERE customer_id = 5 AND order_date = '2025-01-15';
-- Access: COMPOSITE INDEX SCAN via idx_orders (customer_id, order_date)
```

### Index Persistence

For persistent databases (`.vdb` files), indexes are backed by on-disk B-tree structures that survive database restarts:

```sql
-- Open a persistent database
OPEN 'mydata.vdb';

-- Create a table and index
CREATE TABLE users (id INTEGER PRIMARY KEY, email TEXT, name TEXT);
INSERT INTO users VALUES (1, 'alice@test.com', 'Alice');
INSERT INTO users VALUES (2, 'bob@test.com', 'Bob');
CREATE INDEX idx_email ON users (email);

-- Close and reopen — index is preserved
CLOSE;
OPEN 'mydata.vdb';

-- Index is still active and used by the optimizer
SELECT * FROM users WHERE email = 'alice@test.com';
```

**How it works:**
- **In-memory databases**: Use 64-bucket hash indexes for fast equality lookups (lost on exit)
- **Persistent databases (.vdb)**: Hash indexes are paired with disk-based B-tree indexes. Index metadata is stored in schema pages alongside table metadata. On OPEN, both hash indexes and B-trees are rebuilt/restored automatically.
- **B-tree structure**: Order-50 B-tree with ~99 keys per 4KB page, supporting search, insert, delete, and range scan operations via the BufferPool page cache.

---

## Constraints

Constraints enforce data integrity rules on columns:

| Constraint | Description | Example |
|------------|-------------|---------|
| `PRIMARY KEY` | Unique identifier for each row | `id INTEGER PRIMARY KEY` |
| `AUTOINCREMENT` | Auto-assign incrementing values | `id INTEGER PRIMARY KEY AUTOINCREMENT` |
| `NOT NULL` | Column cannot contain NULL | `name TEXT NOT NULL` |
| `UNIQUE` | All values must be distinct | `email TEXT UNIQUE` |
| `DEFAULT value` | Default value when not specified | `status TEXT DEFAULT 'active'` |
| `REFERENCES table(col)` | Foreign key reference | `dept_id INTEGER REFERENCES departments(id)` |
| `CHECK(expr)` | Enforce arbitrary condition | `age INTEGER CHECK(age >= 0)` |

Multiple constraints can be combined:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    email TEXT NOT NULL UNIQUE,
    role TEXT DEFAULT 'user',
    age INTEGER CHECK(age >= 0),
    manager_id INTEGER REFERENCES users(id)
);
```

CHECK constraints are evaluated on INSERT and UPDATE. NULL values always pass CHECK (per SQL standard):

```sql
CREATE TABLE scores (id INTEGER PRIMARY KEY, score INTEGER CHECK(score >= 0));
INSERT INTO scores VALUES (1, 100);     -- OK
INSERT INTO scores VALUES (2, -5);      -- Error: CHECK constraint failed
INSERT INTO scores VALUES (3, NULL);    -- OK (NULL passes CHECK)
UPDATE scores SET score = -1 WHERE id = 1;  -- Error: CHECK constraint failed
```

### Foreign Key Actions

Foreign keys support `ON DELETE` and `ON UPDATE` actions:

| Action | Description |
|--------|-------------|
| `CASCADE` | Delete/update referencing rows automatically |
| `SET NULL` | Set the FK column to NULL in referencing rows |
| `RESTRICT` | Prevent the delete/update if references exist |
| `NO ACTION` | Default — error if references exist (same as RESTRICT) |

```sql
CREATE TABLE departments (id INTEGER PRIMARY KEY, name TEXT);
CREATE TABLE employees (
    id INTEGER PRIMARY KEY,
    name TEXT,
    dept_id INTEGER REFERENCES departments(id) ON DELETE CASCADE ON UPDATE CASCADE
);

-- Deleting a department automatically deletes its employees
DELETE FROM departments WHERE id = 1;

-- Updating a department PK automatically updates employee FK values
UPDATE departments SET id = 10 WHERE id = 2;
```
