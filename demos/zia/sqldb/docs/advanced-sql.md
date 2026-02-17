# Advanced SQL Features

## Table of Contents

- [Window Functions](#window-functions)
- [Common Table Expressions (CTEs)](#common-table-expressions-ctes)
- [Recursive CTEs](#recursive-ctes)
- [Triggers](#triggers)
- [Sequences](#sequences)
- [Stored Functions](#stored-functions)
- [Table Inheritance](#table-inheritance)
- [Generated Columns](#generated-columns)
- [FILTER Clause](#filter-clause)
- [LATERAL Joins](#lateral-joins)
- [NATURAL JOIN and USING](#natural-join-and-using)
- [FETCH FIRST / OFFSET-FETCH](#fetch-first--offset-fetch)
- [GROUPING SETS / ROLLUP / CUBE](#grouping-sets--rollup--cube)
- [DISTINCT ON](#distinct-on)
- [Window Frame Specifications](#window-frame-specifications)
- [NULLS FIRST / NULLS LAST](#nulls-first--nulls-last)
- [IS DISTINCT FROM](#is-distinct-from)
- [IF EXISTS / IF NOT EXISTS / CREATE OR REPLACE](#if-exists--if-not-exists--create-or-replace)
- [SHOW CREATE TABLE](#show-create-table)
- [SHOW INDEXES / SHOW COLUMNS](#show-indexes--show-columns)
- [CREATE TABLE LIKE](#create-table-like)
- [TABLE Expression](#table-expression)
- [Composite PRIMARY KEY](#composite-primary-key)
- [LIMIT PERCENT](#limit-percent)
- [SELECT INTO](#select-into)
- [Temporary Tables](#temporary-tables)
- [Row-Level Locking](#row-level-locking)
- [MVCC (Snapshot Isolation)](#mvcc-snapshot-isolation)
- [BETWEEN SYMMETRIC](#between-symmetric)
- [TABLESAMPLE](#tablesample)
- [ANY / ALL / SOME](#any--all--some)
- [MERGE INTO](#merge-into)
- [Partial Indexes](#partial-indexes)
- [Named WINDOW Clause](#named-window-clause)
- [Row Value Constructors](#row-value-constructors)

---

## Window Functions

Window functions compute values across a set of rows related to the current row, without collapsing the result set like aggregate functions do.

### Ranking Functions

```sql
-- ROW_NUMBER: unique sequential number per partition
SELECT name, dept, salary,
    ROW_NUMBER() OVER (PARTITION BY dept ORDER BY salary DESC) AS rn
FROM employees;

-- RANK: same rank for ties, gaps after ties
SELECT name, score,
    RANK() OVER (ORDER BY score DESC) AS rnk
FROM scores;
-- A=100->1, B=90->2, C=90->2, D=80->4

-- DENSE_RANK: same rank for ties, no gaps
SELECT name, score,
    DENSE_RANK() OVER (ORDER BY score DESC) AS drnk
FROM scores;
-- A=100->1, B=90->2, C=90->2, D=80->3
```

### Aggregate Window Functions

```sql
-- Running sum within a partition
SELECT name, dept, salary,
    SUM(salary) OVER (PARTITION BY dept ORDER BY salary) AS running_sum
FROM employees;

-- Total count per partition (no ORDER BY = full partition)
SELECT name, dept,
    COUNT(*) OVER (PARTITION BY dept) AS dept_count
FROM employees;

-- Multiple window functions in one query
SELECT name, salary,
    ROW_NUMBER() OVER (ORDER BY salary DESC) AS rn,
    RANK() OVER (ORDER BY salary DESC) AS rnk
FROM employees;
```

### Navigation & Distribution Functions

```sql
-- LAG/LEAD: access previous/next rows
SELECT day, amount,
    LAG(amount) OVER (ORDER BY day) AS prev_amount,
    LEAD(amount) OVER (ORDER BY day) AS next_amount
FROM sales;

-- LAG with custom offset and default
SELECT day, LAG(amount, 2, 0) OVER (ORDER BY day) FROM sales;

-- FIRST_VALUE / LAST_VALUE
SELECT day, amount,
    FIRST_VALUE(amount) OVER (ORDER BY day) AS first_amt,
    LAST_VALUE(amount) OVER (ORDER BY day) AS last_amt
FROM sales;

-- NTH_VALUE: value from nth row in partition
SELECT day, NTH_VALUE(amount, 3) OVER (ORDER BY day) FROM sales;

-- NTILE: divide into equal buckets
SELECT name, score, NTILE(4) OVER (ORDER BY score) AS quartile FROM students;

-- PERCENT_RANK / CUME_DIST: statistical distribution
SELECT name, score,
    PERCENT_RANK() OVER (ORDER BY score) AS pct_rank,
    CUME_DIST() OVER (ORDER BY score) AS cume_dist
FROM students;
```

Supported window functions: `ROW_NUMBER()`, `RANK()`, `DENSE_RANK()`, `LAG()`, `LEAD()`, `FIRST_VALUE()`, `LAST_VALUE()`, `NTH_VALUE()`, `NTILE()`, `PERCENT_RANK()`, `CUME_DIST()`, `SUM()`, `COUNT()`, `AVG()`, `MIN()`, `MAX()`.

Named windows can be defined with `WINDOW w AS (...)` and referenced with `OVER w`. See [Named WINDOW Clause](#named-window-clause).

---

## Common Table Expressions (CTEs)

CTEs define temporary named result sets using the `WITH` clause, making complex queries more readable:

```sql
-- Simple CTE
WITH active AS (
    SELECT * FROM users WHERE status = 'active'
)
SELECT name, email FROM active ORDER BY name;

-- CTE with aggregation
WITH dept_stats AS (
    SELECT dept, AVG(salary) AS avg_salary, COUNT(*) AS cnt
    FROM employees GROUP BY dept
)
SELECT dept, avg_salary FROM dept_stats WHERE cnt >= 3;

-- Multiple CTEs
WITH
    engineers AS (SELECT * FROM employees WHERE dept = 'Engineering'),
    high_paid AS (SELECT * FROM engineers WHERE salary > 80000)
SELECT name, salary FROM high_paid ORDER BY salary DESC;

-- CTE with INSERT...SELECT
WITH new_data AS (
    SELECT name, salary FROM employees WHERE dept = 'Sales'
)
INSERT INTO archive SELECT * FROM new_data;

-- CTE with UPDATE...FROM
WITH total_shipped AS (
    SELECT item_id, SUM(quantity) AS total FROM shipments GROUP BY item_id
)
UPDATE inventory SET stock = inventory.stock - ts.total
FROM total_shipped ts WHERE inventory.id = ts.item_id;
```

---

## Recursive CTEs

Recursive CTEs allow iterative queries using `WITH RECURSIVE` for hierarchical data traversal, graph walks, and series generation:

```sql
-- Generate a number series
WITH RECURSIVE nums(n) AS (
    SELECT 1
    UNION ALL
    SELECT n + 1 FROM nums WHERE n < 10
)
SELECT n FROM nums;

-- Employee hierarchy traversal
WITH RECURSIVE org(id, name, manager_id, lvl) AS (
    SELECT id, name, manager_id, 0 FROM employees WHERE manager_id IS NULL
    UNION ALL
    SELECT e.id, e.name, e.manager_id, o.lvl + 1
    FROM employees e JOIN org o ON e.manager_id = o.id
)
SELECT name, lvl FROM org ORDER BY lvl;

-- Fibonacci sequence
WITH RECURSIVE fib(a, b) AS (
    SELECT 0, 1
    UNION ALL
    SELECT b, a + b FROM fib WHERE b < 100
)
SELECT a FROM fib;

-- Recursive CTE with UNION (dedup)
WITH RECURSIVE paths(node) AS (
    SELECT 'A'
    UNION
    SELECT CASE WHEN node = 'A' THEN 'B' WHEN node = 'B' THEN 'C' ELSE 'done' END
    FROM paths WHERE node != 'done'
)
SELECT node FROM paths;
```

**Recursive CTE features**:
- `UNION ALL` accumulates all rows; `UNION` deduplicates across iterations
- Fixpoint termination: stops when the recursive member produces no new rows
- Maximum recursion depth: 1000 iterations (prevents infinite loops)
- Optional column list: `WITH RECURSIVE name(col1, col2)` or inferred from anchor
- Supports JOINs, aggregates, and string operations in recursive member

---

## Triggers

ViperSQL supports SQL triggers that fire automatically on INSERT, UPDATE, or DELETE operations.

### CREATE TRIGGER

```sql
-- AFTER INSERT trigger: log every new order
CREATE TRIGGER log_order AFTER INSERT ON orders
    FOR EACH ROW EXECUTE 'INSERT INTO audit_log VALUES (NEW.id, ''insert'')';

-- BEFORE DELETE trigger: archive before deletion
CREATE TRIGGER archive_user BEFORE DELETE ON users
    FOR EACH ROW EXECUTE 'INSERT INTO archive VALUES (OLD.id, OLD.name)';

-- AFTER UPDATE trigger
CREATE TRIGGER track_change AFTER UPDATE ON products
    FOR EACH ROW EXECUTE 'INSERT INTO changes VALUES (OLD.price, NEW.price)';

-- FOR EACH STATEMENT trigger (fires once per statement)
CREATE TRIGGER notify_bulk AFTER INSERT ON orders
    FOR EACH STATEMENT EXECUTE 'INSERT INTO notifications VALUES (''bulk insert done'')';
```

**Trigger features**:
- `BEFORE` and `AFTER` timing for all DML operations (INSERT, UPDATE, DELETE)
- `FOR EACH ROW` triggers fire once per affected row
- `FOR EACH STATEMENT` triggers fire once per SQL statement
- `OLD` and `NEW` pseudo-tables provide access to row values (via temporary tables)
- Triggers fire in alphabetical order by name (per PostgreSQL convention)
- Recursive trigger depth is limited to 16 levels

### DROP TRIGGER / SHOW TRIGGERS

```sql
DROP TRIGGER log_order;
SHOW TRIGGERS;
```

Triggers are automatically cleaned up when the associated table is dropped.

---

## Sequences

ViperSQL supports PostgreSQL-style sequences for generating unique auto-incrementing values.

### CREATE SEQUENCE

```sql
-- Basic sequence (starts at 1, increments by 1)
CREATE SEQUENCE user_id_seq;

-- Sequence with options
CREATE SEQUENCE order_seq START WITH 1000 INCREMENT BY 1;

-- Descending sequence with bounds
CREATE SEQUENCE countdown START WITH 10 INCREMENT BY -1 MINVALUE 1 MAXVALUE 10;

-- Cycling sequence (wraps around when bounds are reached)
CREATE SEQUENCE round_robin START WITH 1 MAXVALUE 3 CYCLE;
```

### Using Sequences

```sql
-- Get next value (advances the sequence)
SELECT NEXTVAL('user_id_seq');

-- Get current value (does not advance; error if NEXTVAL not called yet)
SELECT CURRVAL('user_id_seq');

-- Set sequence to a specific value
SELECT SETVAL('user_id_seq', 100);

-- Use in INSERT statements
INSERT INTO users (id, name) VALUES (NEXTVAL('user_id_seq'), 'Alice');
```

### ALTER / DROP / SHOW

```sql
-- Modify sequence properties
ALTER SEQUENCE order_seq RESTART WITH 1;
ALTER SEQUENCE order_seq INCREMENT BY 5;

-- Drop a sequence
DROP SEQUENCE order_seq;

-- List all sequences
SHOW SEQUENCES;
```

**Sequence semantics**:
- Sequences survive ROLLBACK (per PostgreSQL — once NEXTVAL is called, the value is consumed)
- NO CYCLE sequences return NULL when the bound is reached
- CYCLE sequences wrap: ascending wraps to MINVALUE, descending wraps to MAXVALUE

---

## Stored Functions

ViperSQL supports user-defined SQL functions with parameter substitution.

### CREATE FUNCTION

```sql
-- Simple function returning a scalar value
CREATE FUNCTION double(x INTEGER) RETURNS INTEGER AS 'SELECT $1 * 2';

-- Function with multiple parameters
CREATE FUNCTION full_name(first TEXT, last TEXT) RETURNS TEXT
    AS 'SELECT $1 || '' '' || $2';

-- Function querying a table
CREATE FUNCTION get_email(uid INTEGER) RETURNS TEXT
    AS 'SELECT email FROM users WHERE id = $1';

-- VOID function (no return value)
CREATE FUNCTION log_event(msg TEXT) RETURNS VOID
    AS 'INSERT INTO audit_log VALUES ($1)';

-- Optional LANGUAGE SQL clause (for PostgreSQL compatibility)
CREATE FUNCTION add(a INTEGER, b INTEGER) RETURNS INTEGER
    LANGUAGE SQL AS 'SELECT $1 + $2';
```

### Using Functions

```sql
-- In SELECT expressions
SELECT double(21);  -- 42
SELECT full_name('John', 'Doe');  -- 'John Doe'

-- In WHERE clauses
SELECT * FROM users WHERE id = double(5);

-- In INSERT statements
INSERT INTO results VALUES (1, double(50));

-- Function overloading (same name, different parameter counts)
CREATE FUNCTION greet(name TEXT) RETURNS TEXT AS 'SELECT ''Hello '' || $1';
CREATE FUNCTION greet(first TEXT, last TEXT) RETURNS TEXT AS 'SELECT ''Hello '' || $1 || '' '' || $2';
SELECT greet('World');         -- 'Hello World'
SELECT greet('John', 'Doe');   -- 'Hello John Doe'
```

### DROP FUNCTION / SHOW FUNCTIONS

```sql
DROP FUNCTION double;
SHOW FUNCTIONS;
```

**Function features**:
- Parameter types: INTEGER, REAL, TEXT, BOOLEAN, DATE, TIMESTAMP
- Return types: INTEGER, REAL, TEXT, BOOLEAN, VOID
- Positional parameter substitution ($1, $2, ...) in SQL body
- Function overloading by parameter count
- Functions can query tables and return computed results

---

## Table Inheritance

ViperSQL supports PostgreSQL-style table inheritance, where child tables inherit columns from a parent table:

```sql
-- Create parent table
CREATE TABLE shapes (id INTEGER, color TEXT);

-- Create child that inherits parent columns and adds its own
CREATE TABLE circles (radius REAL) INHERITS (shapes);
-- circles has columns: id, color (inherited), radius (own)

-- Insert into parent and child
INSERT INTO shapes VALUES (1, 'green');
INSERT INTO circles VALUES (2, 'red', 3.14);

-- Polymorphic query: SELECT from parent returns parent + all child rows
SELECT id, color FROM shapes ORDER BY id;
-- Returns: (1, green), (2, red) — includes circle row

-- ONLY modifier: exclude child rows
SELECT id, color FROM ONLY shapes ORDER BY id;
-- Returns: (1, green) — only parent rows

-- Multiple children per parent
CREATE TABLE rectangles (width REAL, height REAL) INHERITS (shapes);
INSERT INTO rectangles VALUES (3, 'blue', 10.0, 5.0);
SELECT id, color FROM shapes ORDER BY id;
-- Returns all 3 rows from shapes, circles, and rectangles
```

**Inheritance features**:
- Child tables inherit all parent columns (prepended before child's own columns)
- `SELECT` from parent includes all child rows (polymorphic queries)
- `SELECT * FROM ONLY parent` excludes child rows
- WHERE, ORDER BY, LIMIT/OFFSET work across inherited queries
- Multiple children can inherit from the same parent
- Error if parent table doesn't exist

---

## Generated Columns

ViperSQL supports SQL-standard generated (computed) columns with `GENERATED ALWAYS AS ... STORED` syntax:

```sql
-- Create table with a generated column
CREATE TABLE orders (
    quantity INTEGER,
    unit_price REAL,
    total REAL GENERATED ALWAYS AS (quantity * unit_price) STORED
);

-- Generated columns are computed automatically on INSERT
INSERT INTO orders (quantity, unit_price) VALUES (5, 19.99);
SELECT quantity, unit_price, total FROM orders;
-- Returns: 5, 19.99, 99.95

-- Generated columns are recomputed on UPDATE
UPDATE orders SET quantity = 10 WHERE quantity = 5;
SELECT total FROM orders;
-- Returns: 199.9

-- Positional INSERT skips generated columns
INSERT INTO orders VALUES (3, 9.99);
SELECT total FROM orders WHERE quantity = 3;
-- Returns: 29.97
```

**Generated column features**:
- `GENERATED ALWAYS AS (expr) STORED` — expression evaluated at INSERT/UPDATE time
- Expressions can use column references, arithmetic, string operations, and functions (UPPER, CASE, etc.)
- Positional INSERT (no column list) automatically skips generated columns
- UPDATE recomputes generated columns after applying SET values
- Direct UPDATE of generated columns is blocked with an error
- Multiple generated columns per table supported
- WHERE clauses can filter on generated columns
- DESCRIBE shows `GENERATED ALWAYS AS (...) STORED`

---

## FILTER Clause

The SQL-standard `FILTER (WHERE ...)` clause restricts which rows an aggregate function processes:

```sql
-- Count sales by region using FILTER
SELECT
    COUNT(*) FILTER (WHERE region = 'North') AS north_count,
    COUNT(*) FILTER (WHERE region = 'South') AS south_count,
    COUNT(*) AS total_count
FROM sales;

-- SUM with FILTER
SELECT
    SUM(amount) FILTER (WHERE region = 'North') AS north_total,
    SUM(amount) FILTER (WHERE region = 'South') AS south_total
FROM sales;

-- FILTER with GROUP BY
SELECT
    product,
    COUNT(*) FILTER (WHERE region = 'North') AS north_sales
FROM sales
GROUP BY product
ORDER BY product;

-- Complex FILTER conditions
SELECT SUM(amount) FILTER (WHERE region = 'North' AND product = 'Widget') FROM sales;
```

**FILTER clause features**:
- Works with all aggregate functions: COUNT, SUM, AVG, MIN, MAX, STRING_AGG, ARRAY_AGG, etc.
- Multiple aggregates can have different FILTER clauses in the same SELECT
- FILTER works alongside GROUP BY
- Mixed filtered and unfiltered aggregates in the same query
- Returns 0 for COUNT or NULL for other aggregates when no rows match the filter
- FILTER with complex conditions (AND, OR, comparisons)

---

## LATERAL Joins

LATERAL subqueries allow each row of the outer table to drive a correlated subquery:

```sql
-- Top-2 highest-paid employees per department
SELECT d.name, e.name, e.salary
FROM departments d,
     LATERAL (SELECT name, salary FROM employees
              WHERE dept_id = d.id ORDER BY salary DESC LIMIT 2) e;

-- Department statistics with LATERAL
SELECT d.name, sub.cnt, sub.total
FROM departments d,
     LATERAL (SELECT COUNT(*) AS cnt, SUM(salary) AS total
              FROM employees WHERE dept_id = d.id) sub;

-- LEFT JOIN LATERAL (preserves outer rows with no match)
SELECT d.name, sub.cnt
FROM departments d
LEFT JOIN LATERAL (SELECT COUNT(*) AS cnt FROM employees WHERE dept_id = d.id) sub ON true;

-- CROSS JOIN LATERAL
SELECT d.name, e.name
FROM departments d
CROSS JOIN LATERAL (SELECT name FROM employees WHERE dept_id = d.id LIMIT 1) e;
```

**LATERAL join features**:
- Comma syntax: `FROM t1, LATERAL (SELECT ... WHERE ... = t1.col)`
- LEFT JOIN LATERAL, CROSS JOIN LATERAL
- Outer column references automatically substituted per row
- Supports aggregates, LIMIT, ORDER BY in LATERAL subquery
- Empty subquery results: CROSS produces no row, LEFT produces NULL row

---

## NATURAL JOIN and USING

NATURAL JOIN automatically joins on all columns with matching names. USING specifies explicit column names:

```sql
-- NATURAL JOIN: auto-detects common columns
SELECT t1.id, t1.val, t2.score FROM t1 NATURAL JOIN t2;

-- NATURAL LEFT JOIN (preserves unmatched left rows)
SELECT * FROM t1 NATURAL LEFT JOIN t2;

-- NATURAL RIGHT JOIN, NATURAL FULL JOIN
SELECT * FROM t1 NATURAL RIGHT JOIN t2;
SELECT * FROM t1 NATURAL FULL JOIN t2;

-- JOIN ... USING (single column)
SELECT e.name, d.name FROM employees e JOIN departments d USING (dept_id);

-- JOIN ... USING (multiple columns)
SELECT o.qty, r.reason
FROM orders o JOIN refunds r USING (customer_id, product_id);

-- LEFT JOIN ... USING
SELECT t1.id, t1.val, t2.score FROM t1 LEFT JOIN t2 USING (id);
```

**NATURAL/USING features**:
- NATURAL JOIN, NATURAL LEFT/RIGHT/FULL/INNER JOIN
- JOIN ... USING with single or multiple columns
- LEFT/RIGHT/FULL JOIN ... USING
- No common columns: NATURAL JOIN degrades to CROSS JOIN
- Condition synthesis at execution time (converts to equi-join internally)

---

## FETCH FIRST / OFFSET-FETCH

SQL-standard paging syntax as an alternative to PostgreSQL `LIMIT`/`OFFSET`:

```sql
-- Basic: first N rows
SELECT * FROM items ORDER BY id FETCH FIRST 10 ROWS ONLY;

-- NEXT is a synonym for FIRST
SELECT * FROM items ORDER BY id FETCH NEXT 5 ROWS ONLY;

-- Singular ROW keyword
SELECT * FROM items ORDER BY price DESC FETCH FIRST 1 ROW ONLY;

-- OFFSET N ROWS with FETCH FIRST
SELECT * FROM items ORDER BY id OFFSET 20 ROWS FETCH FIRST 10 ROWS ONLY;

-- OFFSET only (no FETCH, returns remaining rows)
SELECT * FROM items ORDER BY id OFFSET 5 ROWS;

-- Works with WHERE, GROUP BY, ORDER BY
SELECT category, SUM(amount)
FROM sales
WHERE status = 'completed'
GROUP BY category
ORDER BY category
OFFSET 2 ROWS FETCH FIRST 5 ROWS ONLY;
```

Both `LIMIT`/`OFFSET` (PostgreSQL) and `FETCH FIRST`/`OFFSET ROWS` (SQL standard) syntaxes are supported — use whichever you prefer.

---

## GROUPING SETS / ROLLUP / CUBE

Advanced GROUP BY features for multi-level aggregation and subtotals:

```sql
-- ROLLUP: hierarchical subtotals + grand total
-- Generates: (region, product), (region), ()
SELECT region, product, SUM(amount)
FROM sales
GROUP BY ROLLUP (region, product);

-- CUBE: all possible subtotal combinations
-- Generates: (region, product), (region), (product), ()
SELECT region, product, SUM(amount)
FROM sales
GROUP BY CUBE (region, product);

-- GROUPING SETS: explicit grouping specifications
SELECT region, product, SUM(amount)
FROM sales
GROUP BY GROUPING SETS ((region), (product), ());

-- Grand total only
SELECT SUM(amount) FROM sales GROUP BY GROUPING SETS (());

-- ROLLUP with HAVING
SELECT region, SUM(amount) FROM sales
GROUP BY ROLLUP (region)
HAVING SUM(amount) > 1000;
```

Columns not in the active grouping set produce NULL values (standard SQL behavior). For example, in a ROLLUP region subtotal row, the product column is NULL.

---

## DISTINCT ON

PostgreSQL's `DISTINCT ON` returns the first row for each unique value of the specified expression(s). Use with `ORDER BY` to control which row is "first":

```sql
-- Highest salary per department
SELECT DISTINCT ON (dept) dept, name, salary
FROM employees
ORDER BY dept, salary DESC;

-- Most recent order per customer
SELECT DISTINCT ON (customer_id) customer_id, order_date, total
FROM orders
ORDER BY customer_id, order_date DESC;

-- DISTINCT ON with multiple columns
SELECT DISTINCT ON (day, category) day, category, event, priority
FROM log
ORDER BY day, category, priority DESC;

-- With LIMIT
SELECT DISTINCT ON (dept) dept, name, salary
FROM employees
ORDER BY dept, salary DESC
LIMIT 2;
```

---

## Window Frame Specifications

Window functions support explicit `ROWS BETWEEN` frame specifications for sliding window calculations:

```sql
-- Running sum (UNBOUNDED PRECEDING to CURRENT ROW)
SELECT day, amount,
       SUM(amount) OVER (ORDER BY day ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) AS running_total
FROM sales;

-- 3-day moving average
SELECT day, amount,
       AVG(amount) OVER (ORDER BY day ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING) AS moving_avg
FROM sales;

-- Sliding window MIN/MAX
SELECT day, amount,
       MIN(amount) OVER (ORDER BY day ROWS BETWEEN 2 PRECEDING AND CURRENT ROW) AS rolling_min
FROM sales;

-- Reverse running sum (current to end)
SELECT day, amount,
       SUM(amount) OVER (ORDER BY day ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING) AS remaining
FROM sales;

-- Full partition sum (every row sees total)
SELECT day, amount,
       SUM(amount) OVER (ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING) AS total
FROM sales;

-- Shorthand: ROWS N PRECEDING = BETWEEN N PRECEDING AND CURRENT ROW
SELECT day, amount,
       SUM(amount) OVER (ORDER BY day ROWS 2 PRECEDING) AS last_3_sum
FROM sales;
```

Supported frame bounds: `UNBOUNDED PRECEDING`, `N PRECEDING`, `CURRENT ROW`, `N FOLLOWING`, `UNBOUNDED FOLLOWING`. Works with SUM, COUNT, AVG, MIN, MAX and PARTITION BY.

### EXCLUDE Clause

Exclude specific rows from the window frame:

```sql
-- Exclude the current row from the frame
SELECT day, amount,
       SUM(amount) OVER (ORDER BY day ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING
                         EXCLUDE CURRENT ROW) AS neighbors_sum
FROM sales;

-- Other EXCLUDE options
-- EXCLUDE NO OTHERS   (default — include all frame rows)
-- EXCLUDE GROUP       (exclude current row and its peers)
-- EXCLUDE TIES        (exclude peers of current row, keep current row)
```

---

## NULLS FIRST / NULLS LAST

ORDER BY columns can specify explicit NULL positioning with `NULLS FIRST` or `NULLS LAST` modifiers:

```sql
-- Default: ASC puts NULLs last, DESC puts NULLs first
SELECT name, price FROM items ORDER BY price ASC;       -- NULLs at bottom
SELECT name, price FROM items ORDER BY price DESC;      -- NULLs at top

-- Override the default NULL positioning
SELECT name, price FROM items ORDER BY price ASC NULLS FIRST;   -- NULLs at top
SELECT name, price FROM items ORDER BY price DESC NULLS LAST;   -- NULLs at bottom

-- Multi-column with per-column NULLS mode
SELECT name, category, price FROM items
ORDER BY category ASC NULLS FIRST, price ASC NULLS LAST;
```

Works with WHERE, LIMIT/OFFSET, GROUP BY, aggregate queries, and JOINs.

---

## IS DISTINCT FROM

NULL-safe comparison operators that treat NULL values as equal rather than unknown:

```sql
-- IS DISTINCT FROM: true when values are different (NULL-safe !=)
SELECT * FROM t WHERE a IS DISTINCT FROM b;
-- NULL IS DISTINCT FROM NULL → false (NULLs considered equal)
-- NULL IS DISTINCT FROM 1   → true
-- 1 IS DISTINCT FROM 2      → true
-- 1 IS DISTINCT FROM 1      → false

-- IS NOT DISTINCT FROM: true when values are the same (NULL-safe =)
SELECT * FROM t WHERE val IS NOT DISTINCT FROM NULL;
-- NULL IS NOT DISTINCT FROM NULL → true (unlike val = NULL which is unknown)

-- Useful for finding NULL rows (alternative to IS NULL)
SELECT * FROM t WHERE val IS NOT DISTINCT FROM NULL;

-- Column vs column comparison
SELECT * FROM pairs WHERE a IS NOT DISTINCT FROM b;
```

---

## IF EXISTS / IF NOT EXISTS / CREATE OR REPLACE

Defensive DDL operations that prevent errors when objects already exist or don't exist, enabling idempotent migration scripts:

```sql
-- DROP IF EXISTS: no error if object doesn't exist
DROP TABLE IF EXISTS old_users;
DROP INDEX IF EXISTS idx_name;
DROP VIEW IF EXISTS v_summary;
DROP DATABASE IF EXISTS testdb;
DROP TRIGGER IF EXISTS trg_audit;
DROP SEQUENCE IF EXISTS seq_id;
DROP FUNCTION IF EXISTS calc_total;
DROP USER IF EXISTS temp_user;

-- CREATE IF NOT EXISTS: skip if object already exists
CREATE TABLE IF NOT EXISTS users (id INTEGER, name TEXT);
CREATE INDEX IF NOT EXISTS idx_users_name ON users (name);

-- CREATE OR REPLACE: replace existing or create new
CREATE OR REPLACE VIEW active_users AS SELECT * FROM users WHERE active = 1;
CREATE OR REPLACE FUNCTION double(x INTEGER) RETURNS INTEGER AS 'SELECT x * 2';

-- Idempotent migration script pattern
DROP TABLE IF EXISTS app_config;
CREATE TABLE IF NOT EXISTS app_config (key TEXT, value TEXT);
DROP INDEX IF EXISTS idx_config_key;
CREATE INDEX IF NOT EXISTS idx_config_key ON app_config (key);
```

---

## SHOW CREATE TABLE

DDL introspection: generate the CREATE TABLE statement that would recreate a table from its current metadata. Includes column types, constraints (PRIMARY KEY, NOT NULL, UNIQUE, AUTOINCREMENT), DEFAULT values, foreign key references, CHECK constraints, generated columns, and index definitions.

```sql
-- Basic SHOW CREATE TABLE
SHOW CREATE TABLE users;
-- Returns:
-- Table | Create Table
-- users | CREATE TABLE users (
--       |   id INTEGER PRIMARY KEY,
--       |   name TEXT NOT NULL,
--       |   email TEXT UNIQUE
--       | );

-- Tables with indexes return additional rows for each index
SHOW CREATE TABLE orders;
-- Row 1: CREATE TABLE orders (...)
-- Row 2: CREATE INDEX idx_customer ON orders (customer);
-- Row 3: CREATE UNIQUE INDEX idx_id ON orders (id);

-- Round-trip: output is valid SQL
DROP TABLE users;
-- Execute the DDL from SHOW CREATE TABLE to recreate
```

---

## SHOW INDEXES / SHOW COLUMNS

Schema introspection commands for exploring table structure and index metadata.

```sql
-- Show all indexes in the database
SHOW INDEXES;

-- Show indexes for a specific table
SHOW INDEXES FROM orders;
-- Returns: index_name | table_name | columns          | is_unique
--          idx_cust   | orders     | customer         | NO
--          idx_date   | orders     | year, month, day | NO

-- Show column metadata for a table
SHOW COLUMNS FROM employees;
-- Returns: column_name | data_type | is_nullable | column_default | is_primary_key
--          id          | INTEGER   | NO          | NULL           | YES
--          name        | TEXT      | NO          | NULL           | NO
--          salary      | INTEGER   | YES         | 50000          | NO
```

---

## CREATE TABLE LIKE

Clone a table's structure (columns, types, constraints, defaults) without copying data.

```sql
-- Clone table structure
CREATE TABLE users_backup (LIKE users);

-- Clone preserves column types, PK, NOT NULL, UNIQUE, defaults
-- Does NOT copy data, indexes, or triggers
INSERT INTO users_backup SELECT * FROM users;
```

---

## TABLE Expression

Shorthand syntax for `SELECT * FROM tablename`:

```sql
TABLE users;  -- equivalent to SELECT * FROM users
```

---

## Composite PRIMARY KEY

Table-level composite primary key constraints with optional constraint naming:

```sql
-- Composite primary key (table-level syntax)
CREATE TABLE enrollments (
    student_id INTEGER,
    course_id INTEGER,
    grade TEXT,
    PRIMARY KEY (student_id, course_id)
);

-- With named constraint
CREATE TABLE assignments (
    employee_id INTEGER,
    project_id INTEGER,
    role TEXT,
    CONSTRAINT pk_assign PRIMARY KEY (employee_id, project_id)
);

-- Named CHECK constraints
CREATE TABLE products (
    id INTEGER,
    price INTEGER CONSTRAINT chk_positive CHECK (price > 0),
    name TEXT
);
```

---

## LIMIT PERCENT

Return a percentage of total rows instead of a fixed count:

```sql
-- Top 10% of scores
SELECT * FROM scores ORDER BY score DESC LIMIT 10 PERCENT;

-- 50% sample
SELECT * FROM large_table LIMIT 50 PERCENT;
```

---

## SELECT INTO

Create a new table from query results (alternative to CREATE TABLE AS SELECT):

```sql
SELECT name, salary INTO high_earners FROM employees WHERE salary > 100000;

-- Multi-table TRUNCATE
TRUNCATE TABLE t1, t2, t3;
```

---

## Temporary Tables

ViperSQL supports session-scoped temporary tables that are automatically dropped when the session ends.

```sql
-- Create a temporary table
CREATE TEMPORARY TABLE temp_results (id INTEGER, value TEXT);
CREATE TEMP TABLE staging (name TEXT, score INTEGER);

-- Use like a regular table
INSERT INTO temp_results VALUES (1, 'hello');
SELECT * FROM temp_results;

-- Temporary tables are isolated per session
-- Other connections cannot see your temp tables

-- Temporary tables shadow regular tables with the same name
CREATE TABLE data (id INTEGER);
CREATE TEMP TABLE data (id INTEGER, extra TEXT);
SELECT * FROM data;  -- queries the temp table
```

---

## Row-Level Locking

ViperSQL supports row-level locking for fine-grained concurrent access control within transactions:

```sql
-- Exclusive row locks (prevent other transactions from modifying or locking these rows)
SELECT * FROM accounts WHERE balance > 1000 FOR UPDATE;

-- Shared row locks (prevent modifications but allow other shared locks)
SELECT * FROM accounts WHERE id = 1 FOR SHARE;

-- Non-blocking: return error immediately if lock unavailable
SELECT * FROM accounts WHERE id = 1 FOR UPDATE NOWAIT;

-- Skip locked rows instead of waiting
SELECT * FROM accounts WHERE status = 'pending' FOR UPDATE SKIP LOCKED;
```

**Lock compatibility**:
- `FOR SHARE` + `FOR SHARE` = compatible (concurrent readers)
- `FOR SHARE` + `FOR UPDATE` = conflict (blocks or errors with NOWAIT)
- `FOR UPDATE` + `FOR UPDATE` = conflict
- Locks are released on `COMMIT` or `ROLLBACK`
- Lock timeout: 5 seconds with 10ms retry interval

The `RowLockManager` (in `storage/txn.zia`) implements thread-safe row-level locks with monitor-based concurrency.

---

## MVCC (Snapshot Isolation)

ViperSQL implements Multi-Version Concurrency Control (MVCC) for snapshot isolation. Each transaction sees a consistent view of the database as of its start time — readers never block writers and writers never block readers.

**How it works**:
- Each `BEGIN` assigns a monotonically increasing transaction ID and takes a snapshot
- `INSERT` stamps the new row's `xmin` with the current transaction ID
- `DELETE` stamps the row's `xmax` with the current transaction ID (soft-delete)
- `UPDATE` stamps the modified row's `xmin` with the current transaction ID
- A row is visible to a transaction if:
  - `xmin` was committed before the snapshot (or created by the current transaction)
  - `xmax` is 0 (not deleted), or `xmax` is from a transaction not yet visible

```sql
-- Transaction 1 inserts data
BEGIN;
INSERT INTO accounts VALUES (1, 'Alice', 1000);
COMMIT;

-- Transaction 2 sees committed data
BEGIN;
SELECT * FROM accounts;  -- sees Alice's row (xmin committed before snapshot)
COMMIT;
```

Outside of explicit transactions (`snapshotId=0`), MVCC is not active and the legacy `deleted` flag is used for visibility.

---

## BETWEEN SYMMETRIC

Standard `BETWEEN` requires `low AND high` in order, but `BETWEEN SYMMETRIC` accepts operands in any order:

```sql
-- Standard BETWEEN: low must be <= high
SELECT * FROM t WHERE x BETWEEN 1 AND 10;

-- BETWEEN SYMMETRIC: automatically swaps if needed
SELECT * FROM t WHERE x BETWEEN SYMMETRIC 10 AND 1;   -- same as BETWEEN 1 AND 10
SELECT * FROM t WHERE x BETWEEN SYMMETRIC 1 AND 10;    -- works either way
SELECT * FROM t WHERE x NOT BETWEEN SYMMETRIC 10 AND 1; -- negated
```

---

## TABLESAMPLE

Random sampling of table rows without reading the entire table:

```sql
-- SYSTEM sampling: fast, block-level randomness (percentage of rows)
SELECT * FROM large_table TABLESAMPLE SYSTEM (10);   -- ~10% of rows

-- BERNOULLI sampling: row-level, more uniform distribution
SELECT * FROM large_table TABLESAMPLE BERNOULLI (25); -- ~25% of rows

-- Use with WHERE and other clauses
SELECT * FROM orders TABLESAMPLE BERNOULLI (50) WHERE status = 'active';
```

---

## ANY / ALL / SOME

Quantified comparisons against subquery result sets:

```sql
-- ANY/SOME: true if comparison holds for at least one subquery row
SELECT * FROM products WHERE price > ANY (SELECT price FROM discounted);
SELECT * FROM products WHERE price = SOME (SELECT price FROM featured);

-- ALL: true if comparison holds for every subquery row
SELECT * FROM products WHERE price > ALL (SELECT price FROM budget_items);

-- Works with all comparison operators: =, <>, <, <=, >, >=
SELECT name FROM employees WHERE salary >= ALL (SELECT min_salary FROM grades);
```

---

## MERGE INTO

SQL-standard upsert combining INSERT, UPDATE, and DELETE in a single statement:

```sql
-- MERGE with UPDATE on match, INSERT on no match
MERGE INTO target AS t
USING source AS s
ON t.id = s.id
WHEN MATCHED THEN UPDATE SET t.name = s.name, t.value = s.value
WHEN NOT MATCHED THEN INSERT (id, name, value) VALUES (s.id, s.name, s.value);

-- MERGE with DELETE on match
MERGE INTO inventory AS i
USING returns AS r
ON i.product_id = r.product_id
WHEN MATCHED THEN DELETE;

-- MERGE with both WHEN MATCHED and WHEN NOT MATCHED
MERGE INTO employees AS e
USING new_hires AS h
ON e.emp_id = h.emp_id
WHEN MATCHED THEN UPDATE SET e.dept = h.dept
WHEN NOT MATCHED THEN INSERT (emp_id, name, dept) VALUES (h.emp_id, h.name, h.dept);
```

---

## Partial Indexes

Create indexes that only cover rows matching a WHERE predicate, saving space and improving performance for selective queries:

```sql
-- Index only active users
CREATE INDEX idx_active ON users (email) WHERE active = 1;

-- Partial unique index: enforce uniqueness only for non-null values
CREATE UNIQUE INDEX idx_unique_email ON users (email) WHERE email IS NOT NULL;

-- Index only recent orders
CREATE INDEX idx_recent ON orders (created_at) WHERE status = 'pending';

-- Show partial status in SHOW INDEXES
SHOW INDEXES ON users;
-- Displays "partial" column indicating partial index predicate
```

Partial indexes are automatically maintained: rows not matching the WHERE predicate are excluded from the index during INSERT/UPDATE.

---

## Named WINDOW Clause

Define reusable window specifications to avoid repeating PARTITION BY / ORDER BY:

```sql
-- Define a named window and reference it
SELECT dept, name, salary,
    SUM(salary) OVER w AS dept_total,
    ROW_NUMBER() OVER w AS rn,
    AVG(salary) OVER w AS dept_avg
FROM employees
WINDOW w AS (PARTITION BY dept ORDER BY salary);

-- Multiple named windows
SELECT name, dept, salary,
    RANK() OVER by_dept AS dept_rank,
    RANK() OVER by_salary AS overall_rank
FROM employees
WINDOW by_dept AS (PARTITION BY dept ORDER BY salary DESC),
       by_salary AS (ORDER BY salary DESC);
```

---

## Row Value Constructors

Compare multiple columns as a tuple using row value constructor syntax:

```sql
-- Equality comparison
SELECT * FROM t WHERE (a, b) = (1, 2);

-- Inequality
SELECT * FROM t WHERE (a, b) <> (1, 2);

-- Lexicographic ordering (compares element by element)
SELECT * FROM t WHERE (a, b) < (10, 5);
SELECT * FROM t WHERE (a, b) >= (3, 7);

-- Row value IN list — match against multiple tuples
SELECT * FROM t WHERE (x, y) IN ((1, 2), (3, 4), (5, 6));

-- Three or more elements
SELECT * FROM t WHERE (a, b, c) = (1, 2, 3);

-- Expressions inside row values
SELECT * FROM t WHERE (a + 1, b * 2) = (4, 8);
```

Lexicographic ordering compares elements left to right: `(1, 5) < (2, 1)` because `1 < 2`; `(1, 3) < (1, 5)` because the first elements are equal and `3 < 5`.
