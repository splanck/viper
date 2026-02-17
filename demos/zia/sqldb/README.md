# ViperSQL

A complete SQL database engine written entirely in [Zia](../../../docs/zia-guide.md), the high-level language frontend for the Viper compiler toolchain. ViperSQL implements a substantial subset of SQL including DDL, DML, joins, subqueries, aggregations, views, CHECK constraints, CAST expressions, EXISTS/NOT EXISTS, derived tables, INSERT...SELECT, transactions (BEGIN/COMMIT/ROLLBACK) with savepoints (SAVEPOINT/ROLLBACK TO/RELEASE), ON DELETE/UPDATE CASCADE, window functions, Common Table Expressions (CTEs) including recursive CTEs (WITH RECURSIVE), multi-table UPDATE/DELETE, date/time functions (60+ functions including DATE_PART, DATE_TRUNC, AGE, TO_CHAR, MAKE_DATE), regular expressions (~ operator, SIMILAR TO, REGEXP_MATCH/REPLACE/COUNT/SPLIT), table inheritance (INHERITS, ONLY, polymorphic queries), generated columns (GENERATED ALWAYS AS ... STORED), aggregate FILTER clause, LATERAL joins, NATURAL JOIN and USING clause, EXCEPT ALL/INTERSECT ALL with chained set operations, SQL-standard FETCH FIRST/OFFSET-FETCH paging syntax, GROUPING SETS/ROLLUP/CUBE for multi-level aggregation, DISTINCT ON for first-row-per-group, window frame specifications (ROWS BETWEEN), LAG/LEAD/FIRST_VALUE/LAST_VALUE/NTH_VALUE/NTILE/PERCENT_RANK/CUME_DIST, NULLS FIRST/NULLS LAST ordering, IS DISTINCT FROM/IS NOT DISTINCT FROM, IF EXISTS/IF NOT EXISTS/CREATE OR REPLACE for defensive DDL, SHOW CREATE TABLE for DDL introspection, SHOW INDEXES/COLUMNS for schema introspection, CREATE TABLE ... LIKE for structure cloning, TABLE expression shorthand, composite PRIMARY KEY constraints, named CONSTRAINT syntax, LIMIT PERCENT, SELECT INTO, multi-table TRUNCATE, BETWEEN SYMMETRIC, TABLESAMPLE SYSTEM/BERNOULLI for random sampling, column aliases in ORDER BY, ANY/ALL/SOME quantified subquery comparisons, EXCLUDE CURRENT ROW window frame clause, MERGE INTO (upsert with WHEN MATCHED/NOT MATCHED), partial indexes (CREATE INDEX ... WHERE), named WINDOW clause (WINDOW w AS ... / OVER w), row value constructors ((a,b) comparisons and IN tuples), trigonometric and logarithmic math functions, extended string functions (TRANSLATE, OVERLAY, MD5, SHA256), disk-based B-tree indexes with persistence, persistent storage, CSV import/export, COPY TO/FROM for bulk data transfer, multi-database support, a thread-safe storage layer, per-connection sessions, table-level concurrency control (S/X locking), row-level locking (FOR UPDATE/FOR SHARE with NOWAIT and SKIP LOCKED), MVCC snapshot isolation (row versioning with xmin/xmax), WAL-based crash recovery with full redo/undo (ARIES-style), triggers (BEFORE/AFTER INSERT/UPDATE/DELETE), sequences (NEXTVAL/CURRVAL/SETVAL), stored functions (CREATE FUNCTION with parameter substitution and CALL), SQL cursors (DECLARE/FETCH/CLOSE with bidirectional navigation), composite multi-column indexes, JSON/JSONB data type with extraction and construction functions, pg_catalog system views, prepared statements (PREPARE/EXECUTE/DEALLOCATE) and EXPLAIN ANALYZE, a multi-user TCP server with PostgreSQL wire protocol (v3) including the extended query protocol (Parse/Bind/Describe/Execute) for prepared statements and parameterized queries, system views (INFORMATION_SCHEMA, sys.*, and pg_catalog), temporary tables, user authentication, and GRANT/REVOKE privilege management with table ownership.

**60,100+ lines of Zia** across 109 source and test files, with **4,985+ automated test assertions** across 104 test files.

Runs both interpreted (via the Viper VM) and compiled to native ARM64/x86-64 machine code.

Supports standard PostgreSQL client tools: connect via **psql**, **ODBC** (psqlODBC), or any PostgreSQL-compatible driver.

---

## Table of Contents

- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Running the Interactive Shell](#running-the-interactive-shell)
  - [Running from a Script](#running-from-a-script)
  - [Compiling to Native Code](#compiling-to-native-code)
- [Connecting to the Server](#connecting-to-the-server)
  - [Starting the Server](#starting-the-server)
  - [Connecting via psql](#connecting-via-psql)
  - [Connecting via ODBC](#connecting-via-odbc)
- [Tutorial](#tutorial)
  - [Step 1: Create a Table](#step-1-create-a-table)
  - [Step 2: Insert Data](#step-2-insert-data)
  - [Step 3: Query Data](#step-3-query-data)
  - [Step 4: Update and Delete](#step-4-update-and-delete)
  - [Step 5: Aggregations](#step-5-aggregations)
  - [Step 6: Joins](#step-6-joins)
  - [Step 7: Save Your Work](#step-7-save-your-work)
- [SQL Reference](#sql-reference)
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
  - [Window Functions](#window-functions)
  - [Common Table Expressions (CTEs)](#common-table-expressions-ctes)
  - [Recursive CTEs](#recursive-ctes)
  - [Joins](#joins)
  - [Multi-Table UPDATE/DELETE](#multi-table-updatedelete)
  - [Subqueries](#subqueries)
  - [Set Operations](#set-operations)
  - [Indexes](#indexes)
  - [Constraints](#constraints)
  - [Triggers](#triggers)
  - [Sequences](#sequences)
  - [Stored Functions](#stored-functions)
  - [Multi-Database](#multi-database)
  - [Persistence and Import/Export](#persistence-and-importexport)
  - [User Management](#user-management)
  - [Privileges (GRANT/REVOKE)](#privileges-grantrevoke)
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
  - [System Views](#system-views)
  - [Utility Commands](#utility-commands)
  - [VACUUM and ANALYZE](#vacuum-and-analyze)
  - [pg_stat Views](#pg_stat-views)
  - [DO Blocks](#do-blocks)
  - [Session Variables](#session-variables)
  - [System Functions](#system-functions)
  - [BETWEEN SYMMETRIC](#between-symmetric)
  - [TABLESAMPLE](#tablesample)
  - [ANY / ALL / SOME](#any--all--some)
  - [MERGE INTO](#merge-into)
  - [Partial Indexes](#partial-indexes)
  - [Named WINDOW Clause](#named-window-clause)
  - [Row Value Constructors](#row-value-constructors)
- [Architecture](#architecture)
- [Testing](#testing)
- [Directory Structure](#directory-structure)

---

## Getting Started

### Prerequisites

Build the Viper toolchain from the repository root:

```bash
# macOS / Linux
./scripts/build_viper.sh

# Windows
scripts\build_viper.cmd
```

This builds the toolchain, runs all tests, and installs the `viper` CLI to `/usr/local/bin`.

### Running the Interactive Shell

Launch the ViperSQL REPL:

```bash
viper run demos/zia/sqldb/main.zia
```

You'll see:

```
ViperSQL - SQL Database Server Written in Zia
Type SQL commands or 'exit' to quit.
Type .help for meta-commands.

sql>
```

Type SQL statements at the `sql>` prompt. Type `exit` or `quit` to leave.

### Running from a Script

You can embed ViperSQL in any Zia program. Create a file `my_app.zia`:

```zia
module main;

bind Terminal = Viper.Terminal;
bind "./executor";

func main() {
    var db = new Executor();
    db.init();

    db.executeSql("CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, email TEXT)");
    db.executeSql("INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com')");
    db.executeSql("INSERT INTO users (name, email) VALUES ('Bob', 'bob@example.com')");

    var result = db.executeSql("SELECT * FROM users");
    Terminal.Say(result.toString());
}
```

Run it:

```bash
viper run my_app.zia
```

### Compiling to Native Code

Compile to a standalone native binary for maximum performance:

```bash
viper build demos/zia/sqldb/main.zia -o vipersql
./vipersql
```

---

## Connecting to the Server

ViperSQL includes a multi-user TCP server that speaks the PostgreSQL wire protocol (v3). This allows standard PostgreSQL clients to connect directly.

### Starting the Server

```bash
viper run demos/zia/sqldb/server/tcp_server.zia
```

```
========================================
  ViperSQL Server v0.2
  PostgreSQL Wire Protocol v3
========================================
Database server initialized (database: main)
Thread model: thread-per-connection

Text protocol listening on port 5433
PG wire protocol listening on port 5432

Connect via: psql -h localhost -p 5432 -U admin -d main
Press Ctrl+C to stop.
```

The server listens on two ports:
- **Port 5432** — PostgreSQL wire protocol (for psql, ODBC, pgAdmin, etc.)
- **Port 5433** — Simple text protocol (for lightweight clients)

Default credentials: username `admin`, password `admin`.

### Connecting via psql

```bash
psql -h localhost -p 5432 -U admin -d main
```

Enter the password (`admin`) when prompted. You can then use standard SQL:

```
main=> CREATE TABLE employees (id INTEGER PRIMARY KEY, name TEXT, salary INTEGER);
CREATE TABLE
main=> INSERT INTO employees VALUES (1, 'Alice', 85000);
INSERT 0 1
main=> INSERT INTO employees VALUES (2, 'Bob', 92000);
INSERT 0 1
main=> SELECT * FROM employees;
 id | name  | salary
----+-------+--------
 1  | Alice | 85000
 2  | Bob   | 92000
(2 rows)

main=> SHOW USERS;
 username
----------
 admin
(1 row)
```

### Connecting via ODBC

ViperSQL works with the **psqlODBC** driver via unixODBC.

#### 1. Install the ODBC driver

macOS (Homebrew):

```bash
brew install unixodbc psqlodbc
```

#### 2. Register the PostgreSQL ODBC driver

```bash
cat > /tmp/psqlodbc_template.ini << 'EOF'
[PostgreSQL Unicode]
Description = PostgreSQL ODBC Driver (Unicode)
Driver = /opt/homebrew/lib/psqlodbcw.so
EOF
sudo odbcinst -i -d -f /tmp/psqlodbc_template.ini
```

#### 3. Create a DSN in `~/.odbc.ini`

```ini
[ViperSQL]
Description = ViperSQL Database
Driver = PostgreSQL Unicode
Servername = localhost
Port = 5432
Database = main
Username = admin
Password = admin
Protocol = 7.4
UseServerSidePrepare = 0
UseDeclareFetch = 0
```

`UseServerSidePrepare = 0` disables server-side prepared statements. ViperSQL supports both the Simple Query protocol and the Extended Query protocol (Parse/Bind/Describe/Execute), but setting this to 0 can improve compatibility with older ODBC driver versions.

#### 4. Connect with isql

```bash
isql -v ViperSQL admin admin
```

```
+---------------------------------------+
| Connected!                            |
| sql-statement                         |
| help [tablename]                      |
| quit                                  |
+---------------------------------------+
SQL> SELECT 1 + 2 AS result;
+-----------+
| result    |
+-----------+
| 3         |
+-----------+
SQL> CREATE TABLE test (id INTEGER, name TEXT);
SQL> INSERT INTO test VALUES (1, 'hello');
SQL> SELECT * FROM test;
+-----------+-----------+
| id        | name      |
+-----------+-----------+
| 1         | hello     |
+-----------+-----------+
```

#### Compatible Clients

Any PostgreSQL-compatible client should work, including:
- **psql** (PostgreSQL interactive terminal)
- **psqlODBC** (via unixODBC)
- **pgAdmin** (GUI tool)
- Programming language drivers (Python `psycopg2`/`psycopg3`, Node.js `pg`, Go `pgx`, Java JDBC)

ViperSQL supports both the **Simple Query** protocol (Q messages) and the **Extended Query** protocol (Parse/Bind/Describe/Execute) for prepared statements and parameterized queries.

---

## Tutorial

This step-by-step tutorial walks through the core features of ViperSQL.

### Step 1: Create a Table

Create a table with typed columns and constraints:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT UNIQUE,
    age INTEGER DEFAULT 0
);
```

The `id` column auto-increments on each insert. `name` cannot be NULL. `email` must be unique across all rows. `age` defaults to `0` if not specified.

### Step 2: Insert Data

Insert rows one at a time:

```sql
INSERT INTO users (name, email, age) VALUES ('Alice', 'alice@example.com', 30);
INSERT INTO users (name, email, age) VALUES ('Bob', 'bob@example.com', 25);
INSERT INTO users (name, email, age) VALUES ('Charlie', 'charlie@example.com', 35);
```

Insert multiple rows in one statement:

```sql
INSERT INTO users (name, email, age) VALUES
    ('Diana', 'diana@example.com', 28),
    ('Eve', 'eve@example.com', 32);
```

Omit the column list to insert values in definition order (including `id`):

```sql
INSERT INTO users VALUES (100, 'Frank', 'frank@example.com', 40);
```

### Step 3: Query Data

Select all columns:

```sql
SELECT * FROM users;
```

Select specific columns with a filter:

```sql
SELECT name, age FROM users WHERE age > 25;
```

Sort and limit results:

```sql
SELECT name, age FROM users ORDER BY age DESC LIMIT 3;
```

Use expressions in the select list:

```sql
SELECT name, age, age * 365 AS age_in_days FROM users;
```

### Step 4: Update and Delete

Update rows matching a condition:

```sql
UPDATE users SET age = 31 WHERE name = 'Alice';
```

Update multiple columns:

```sql
UPDATE users SET name = 'Robert', age = 26 WHERE name = 'Bob';
```

Delete specific rows:

```sql
DELETE FROM users WHERE age < 25;
```

Delete all rows from a table:

```sql
DELETE FROM users;
```

### Step 5: Aggregations

Count, sum, average, min, and max:

```sql
SELECT COUNT(*) FROM users;
SELECT AVG(age) FROM users;
SELECT MIN(age), MAX(age) FROM users;
```

Group results and filter groups:

```sql
SELECT department, COUNT(*) AS headcount, AVG(salary) AS avg_salary
FROM employees
GROUP BY department
HAVING COUNT(*) > 5
ORDER BY avg_salary DESC;
```

### Step 6: Joins

Combine data from multiple tables:

```sql
-- Create related tables
CREATE TABLE departments (id INTEGER PRIMARY KEY, name TEXT);
INSERT INTO departments VALUES (1, 'Engineering');
INSERT INTO departments VALUES (2, 'Marketing');

CREATE TABLE employees (id INTEGER PRIMARY KEY, name TEXT, dept_id INTEGER);
INSERT INTO employees VALUES (1, 'Alice', 1);
INSERT INTO employees VALUES (2, 'Bob', 2);
INSERT INTO employees VALUES (3, 'Charlie', 1);

-- INNER JOIN: only matching rows
SELECT e.name, d.name
FROM employees e
INNER JOIN departments d ON e.dept_id = d.id;

-- LEFT JOIN: all employees, even those without a department
SELECT e.name, d.name
FROM employees e
LEFT JOIN departments d ON e.dept_id = d.id;
```

### Step 7: Save Your Work

Save the entire database to a SQL dump file:

```sql
SAVE 'mydata.sql';
```

Restore it later:

```sql
OPEN 'mydata.sql';
```

Export a single table to CSV:

```sql
EXPORT users TO 'users.csv';
```

---

## SQL Reference

### Data Types

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

### DDL Statements

#### CREATE TABLE

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

#### DROP TABLE

```sql
DROP TABLE table_name;
```

#### ALTER TABLE

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

#### Table Partitioning

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

#### Table Inheritance (INHERITS)

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

See [Table Inheritance](#table-inheritance) for full documentation.

#### Generated Columns (GENERATED ALWAYS AS)

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

See [Generated Columns](#generated-columns) for full documentation.

#### CREATE VIEW / DROP VIEW

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

### DML Statements

#### INSERT

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

#### SELECT

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

#### UPDATE

```sql
UPDATE products SET price = 19.99 WHERE name = 'Widget';
UPDATE products SET price = price * 1.1, category = 'premium' WHERE price > 50;
```

#### DELETE

```sql
DELETE FROM orders WHERE status = 'cancelled';
DELETE FROM logs;  -- deletes all rows
```

#### RETURNING Clause

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

#### INSERT ON CONFLICT (UPSERT)

Handle duplicate key violations gracefully:

```sql
-- Skip duplicate inserts
INSERT INTO users VALUES (1, 'Alice') ON CONFLICT (id) DO NOTHING;

-- Update on conflict (upsert)
INSERT INTO users VALUES (1, 'Alice', 95)
  ON CONFLICT (id) DO UPDATE SET name = 'Alice', score = 95;
```

#### TRUNCATE TABLE

Fast table truncation (removes all rows):

```sql
TRUNCATE TABLE users;
TRUNCATE users;  -- TABLE keyword is optional
```

#### GENERATE_SERIES

Generate a series of integers as a virtual table:

```sql
SELECT * FROM generate_series(1, 10);            -- 1 to 10
SELECT * FROM generate_series(0, 100, 10);       -- 0, 10, 20, ..., 100
SELECT * FROM generate_series(5, 1, -1);         -- 5, 4, 3, 2, 1
SELECT * FROM generate_series(1, 1000) LIMIT 5;  -- first 5 values
```

#### Transactions

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

**MVCC snapshot isolation**: Each `BEGIN` assigns a unique transaction ID and takes a snapshot. Rows are versioned with xmin/xmax — readers see a consistent snapshot without blocking writers. See [MVCC (Snapshot Isolation)](#mvcc-snapshot-isolation) for details.

**Transaction rules**:
- Nested `BEGIN` is not allowed (returns an error)
- `COMMIT`/`ROLLBACK` without an active transaction returns an error
- DELETE compaction is deferred during transactions and applied on COMMIT
- WAL-based crash recovery with full redo/undo (ARIES-style) for persistent databases

#### Savepoints

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

#### Prepared Statements

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

#### EXPLAIN ANALYZE

Run a query and report actual execution statistics:

```sql
EXPLAIN ANALYZE SELECT * FROM users WHERE age > 25;
-- Returns: plan info, Execution Time, Rows Returned
```

#### Cursors

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

#### COPY TO/FROM

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

#### CALL Statement

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

#### Array Type & Functions

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

#### Materialized Views

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

#### CREATE TABLE AS SELECT (CTAS)

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

### Query Clauses

#### WHERE

Supports all comparison, logical, and special operators:

```sql
SELECT * FROM users WHERE age >= 18 AND age <= 65;
SELECT * FROM users WHERE name = 'Alice' OR name = 'Bob';
SELECT * FROM users WHERE NOT (age < 18);
```

#### ORDER BY

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

#### GROUP BY

Group rows by one or more columns for aggregate calculations:

```sql
SELECT department, COUNT(*), AVG(salary) FROM employees GROUP BY department;
SELECT city, state, COUNT(*) FROM customers GROUP BY city, state;
```

#### HAVING

Filter groups after aggregation (like WHERE but for groups):

```sql
SELECT department, AVG(salary) AS avg_sal
FROM employees
GROUP BY department
HAVING AVG(salary) > 60000;
```

#### LIMIT and OFFSET

Limit the number of returned rows and skip rows:

```sql
SELECT * FROM products ORDER BY price LIMIT 10;           -- first 10
SELECT * FROM products ORDER BY price LIMIT 10 OFFSET 20; -- rows 21-30
```

#### DISTINCT

Remove duplicate rows from results:

```sql
SELECT DISTINCT category FROM products;
SELECT DISTINCT city, state FROM customers;
```

### Expressions and Operators

#### Arithmetic

| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition | `price + tax` |
| `-` | Subtraction | `total - discount` |
| `*` | Multiplication | `quantity * price` |
| `/` | Division | `total / count` |
| `-` (unary) | Negation | `-amount` |

Integer arithmetic stays integer. Mixed integer/real operations produce real results.

#### Comparison

| Operator | Description |
|----------|-------------|
| `=` | Equal |
| `!=` or `<>` | Not equal |
| `<` | Less than |
| `<=` | Less than or equal |
| `>` | Greater than |
| `>=` | Greater than or equal |

#### Logical

| Operator | Description |
|----------|-------------|
| `AND` | Both conditions must be true |
| `OR` | Either condition must be true |
| `NOT` | Inverts a condition |

#### Special Operators

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

#### CASE Expressions

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

#### CAST

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

### Built-in Functions

#### String Functions

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

#### Math Functions

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

#### Extended String Functions

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

#### Conditional Functions

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `GREATEST(a, b, ...)` | Largest value | `GREATEST(1, 5, 3)` | `5` |
| `LEAST(a, b, ...)` | Smallest value | `LEAST(1, 5, 3)` | `1` |

#### NULL Handling Functions

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `COALESCE(a, b, ...)` | First non-NULL argument | `COALESCE(NULL, NULL, 'found')` | `found` |
| `IFNULL(expr, default)` | Default if NULL | `IFNULL(NULL, 'default')` | `default` |
| `NULLIF(a, b)` | NULL if a equals b | `NULLIF(5, 5)` | `NULL` |
| `IIF(cond, true_val, false_val)` | Inline conditional | `IIF(age > 18, 'adult', 'minor')` | depends |
| `TYPEOF(expr)` | Returns type name as string | `TYPEOF(42)` | `integer` |

### Date/Time Functions

ViperSQL provides date/time functions powered by the Viper runtime's `DateTime` library. Dates are stored as TEXT in ISO format (`YYYY-MM-DDTHH:MM:SS`) or as INTEGER Unix timestamps.

#### Current Date/Time

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `NOW()` | Current local datetime | `SELECT NOW()` | `2025-06-15T14:30:00` |
| `CURRENT_DATE` | Current date (no parens) | `SELECT CURRENT_DATE` | `2025-06-15` |
| `CURRENT_TIME` | Current time (no parens) | `SELECT CURRENT_TIME` | `14:30:00` |
| `CURRENT_TIMESTAMP` | Current datetime (no parens) | `SELECT CURRENT_TIMESTAMP` | `2025-06-15T14:30:00` |

#### Construction

| Function | Description | Example |
|----------|-------------|---------|
| `DATETIME(y, m, d [, h, min, s])` | Build datetime string | `DATETIME(2025, 6, 15, 10, 30, 0)` |
| `FROM_EPOCH(seconds)` | Unix epoch to ISO string | `FROM_EPOCH(1750000000)` |

#### Extraction

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

#### Arithmetic

| Function | Description | Example |
|----------|-------------|---------|
| `DATE_ADD(dt, days)` | Add days to datetime | `DATE_ADD('2025-01-01T00:00:00', 10)` |
| `DATE_SUB(dt, days)` | Subtract days from datetime | `DATE_SUB('2025-01-15T00:00:00', 5)` |
| `DATEDIFF(dt1, dt2)` | Difference in days | `DATEDIFF('2025-06-15T00:00:00', '2025-01-15T00:00:00')` -> `151` |
| `TIMEDIFF(dt1, dt2)` | Difference in seconds | `TIMEDIFF('2025-01-01T01:00:00', '2025-01-01T00:00:00')` -> `3600` |

#### Formatting

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

### Aggregate Functions

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

All aggregate functions support the `FILTER (WHERE ...)` clause — see [FILTER Clause](#filter-clause).

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

### Extended Date/Time Functions

Beyond the core date/time functions above, ViperSQL provides PostgreSQL-compatible extended date/time functions:

#### Date Component Functions

| Function | Description | Example |
|----------|-------------|---------|
| `DATE_PART(field, source)` | Extract component (year, month, day, hour, minute, second, dow, epoch) | `DATE_PART('year', NOW())` |
| `DATE_TRUNC(field, source)` | Truncate to precision (year, month, day, hour, minute) | `DATE_TRUNC('month', NOW())` |
| `AGE(ts1, ts2)` | Human-readable interval between timestamps | `AGE(NOW(), '2020-01-01T00:00:00')` |

#### Date/Time Construction

| Function | Description | Example |
|----------|-------------|---------|
| `MAKE_DATE(y, m, d)` | Construct a date | `MAKE_DATE(2025, 6, 15)` |
| `MAKE_TIMESTAMP(y, mo, d, h, mi, s)` | Construct a timestamp | `MAKE_TIMESTAMP(2025, 6, 15, 10, 30, 0)` |
| `MAKE_INTERVAL(years, months, days)` | Construct an interval string | `MAKE_INTERVAL(1, 2, 3)` |
| `TO_TIMESTAMP(epoch)` | Epoch seconds to timestamp | `TO_TIMESTAMP(1750000000)` |
| `TO_DATE(str, format)` | Parse date string (YYYY-MM-DD) | `TO_DATE('2025-06-15', 'YYYY-MM-DD')` |

#### Timing and Formatting

| Function | Description | Example |
|----------|-------------|---------|
| `TO_CHAR(ts, format)` | Format timestamp as string | `TO_CHAR(NOW(), 'YYYY-MM-DD HH24:MI:SS')` |
| `ISFINITE(ts)` | Check if timestamp is finite | `ISFINITE(NOW())` → `1` |
| `CLOCK_TIMESTAMP()` | Current wall-clock time | `SELECT CLOCK_TIMESTAMP()` |
| `STATEMENT_TIMESTAMP()` | Time of current statement | `SELECT STATEMENT_TIMESTAMP()` |
| `TRANSACTION_TIMESTAMP()` | Time of current transaction | `SELECT TRANSACTION_TIMESTAMP()` |
| `TIMEOFDAY()` | Current time as text string | `SELECT TIMEOFDAY()` |

### Regular Expression Functions

ViperSQL provides PostgreSQL-compatible regular expression operators and functions powered by the Viper runtime's `Pattern` library.

#### Regex Operators

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

#### Regex Functions

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

### JSON Functions

ViperSQL supports JSON and JSONB column types with a comprehensive set of JSON functions for inspection, extraction, and construction. Internally, JSON and JSONB are stored identically as validated JSON text.

#### JSON Column Type

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

#### JSON Inspection

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `JSON_VALID(s)` | Check if string is valid JSON | `JSON_VALID('{"a":1}')` | `1` |
| `JSON_TYPE(s)` | JSON value type | `JSON_TYPE('{"a":1}')` | `object` |
| `JSON_TYPEOF(s)` | Alias for JSON_TYPE | `JSON_TYPEOF('[1,2]')` | `array` |

#### JSON Extraction

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

#### JSON Construction

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `JSON_BUILD_OBJECT(k1, v1, ...)` | Build JSON object from key-value pairs | `JSON_BUILD_OBJECT('a', 1, 'b', 'hi')` | `{"a":1,"b":"hi"}` |
| `JSON_BUILD_ARRAY(v1, v2, ...)` | Build JSON array from values | `JSON_BUILD_ARRAY(1, 'two', 3)` | `[1,"two",3]` |
| `JSON_QUOTE(s)` | Wrap value in JSON quotes | `JSON_QUOTE('hello')` | `"hello"` |
| `JSON(s)` | Validate and return JSON | `JSON('{"a":1}')` | `{"a":1}` |

#### CAST to JSON

```sql
SELECT CAST('{"key": "value"}' AS JSON);
SELECT CAST('[1, 2, 3]' AS JSONB);
```

### Window Functions

Window functions compute values across a set of rows related to the current row, without collapsing the result set like aggregate functions do.

#### Ranking Functions

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

#### Aggregate Window Functions

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

#### Navigation & Distribution Functions

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

### Common Table Expressions (CTEs)

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

### Recursive CTEs

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

### Joins

#### INNER JOIN

Returns rows that have matching values in both tables:

```sql
SELECT e.name, d.name AS department
FROM employees e
INNER JOIN departments d ON e.dept_id = d.id;
```

#### LEFT JOIN

Returns all rows from the left table, with NULLs for unmatched right table columns:

```sql
SELECT e.name, d.name AS department
FROM employees e
LEFT JOIN departments d ON e.dept_id = d.id;
```

#### RIGHT JOIN

Returns all rows from the right table, with NULLs for unmatched left table columns:

```sql
SELECT e.name, d.name AS department
FROM employees e
RIGHT JOIN departments d ON e.dept_id = d.id;
```

#### FULL OUTER JOIN

Returns all rows from both tables, with NULLs where there is no match:

```sql
SELECT e.name, d.name AS department
FROM employees e
FULL JOIN departments d ON e.dept_id = d.id;
```

#### CROSS JOIN

Cartesian product of two tables. Two equivalent syntaxes:

```sql
-- Explicit CROSS JOIN
SELECT c.color, s.size FROM colors c CROSS JOIN sizes s;

-- Implicit (comma) syntax
SELECT c.color, s.size FROM colors c, sizes s;
```

#### Multi-Table Joins

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

Also supports `NATURAL JOIN` (auto-detect common columns), `JOIN ... USING (col1, col2)`, and `LATERAL` subqueries — see [LATERAL Joins](#lateral-joins) and [NATURAL JOIN and USING](#natural-join-and-using).

### Multi-Table UPDATE/DELETE

#### UPDATE...FROM

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

#### DELETE...USING

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

### Subqueries

#### Scalar Subqueries

A subquery that returns a single value, used in WHERE clauses:

```sql
SELECT name, score
FROM students
WHERE score > (SELECT AVG(score) FROM students);
```

#### IN Subqueries

Check if a value exists in a subquery result:

```sql
SELECT name
FROM employees
WHERE dept_id IN (SELECT id FROM departments WHERE name = 'Engineering');

SELECT name
FROM employees
WHERE dept_id NOT IN (SELECT id FROM departments WHERE location = 'Remote');
```

#### EXISTS / NOT EXISTS

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

### Set Operations

Combine results from multiple SELECT statements:

#### UNION (removes duplicates)

```sql
SELECT name FROM team_a
UNION
SELECT name FROM team_b;
```

#### UNION ALL (keeps duplicates)

```sql
SELECT name FROM team_a
UNION ALL
SELECT name FROM team_b;
```

#### EXCEPT (rows in first but not second)

```sql
SELECT name FROM all_students
EXCEPT
SELECT name FROM graduated_students;
```

#### INTERSECT (rows in both)

```sql
SELECT name FROM team_a
INTERSECT
SELECT name FROM team_b;
```

#### EXCEPT ALL / INTERSECT ALL (preserves duplicates)

```sql
-- EXCEPT ALL: if row appears N times in first and M in second, result has max(N-M, 0) copies
SELECT x FROM t1 EXCEPT ALL SELECT x FROM t2;

-- INTERSECT ALL: result has min(N, M) copies of each row
SELECT x FROM t1 INTERSECT ALL SELECT x FROM t2;
```

#### Chained Set Operations

Multiple set operations can be chained in a single query:

```sql
-- 3-way UNION ALL
SELECT x FROM t1 UNION ALL SELECT x FROM t2 UNION ALL SELECT x FROM t3;

-- Mixed operations (left-to-right evaluation)
SELECT x FROM t1 UNION ALL SELECT x FROM t2 EXCEPT SELECT x FROM t3;
```

### Indexes

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

#### Composite Indexes

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

#### Index Persistence

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

### Constraints

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

#### Foreign Key Actions

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

### Triggers

ViperSQL supports SQL triggers that fire automatically on INSERT, UPDATE, or DELETE operations.

#### CREATE TRIGGER

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

#### DROP TRIGGER / SHOW TRIGGERS

```sql
DROP TRIGGER log_order;
SHOW TRIGGERS;
```

Triggers are automatically cleaned up when the associated table is dropped.

### Sequences

ViperSQL supports PostgreSQL-style sequences for generating unique auto-incrementing values.

#### CREATE SEQUENCE

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

#### Using Sequences

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

#### ALTER / DROP / SHOW

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

### Stored Functions

ViperSQL supports user-defined SQL functions with parameter substitution.

#### CREATE FUNCTION

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

#### Using Functions

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

#### DROP FUNCTION / SHOW FUNCTIONS

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

### Multi-Database

ViperSQL supports multiple isolated databases within a single server instance:

```sql
-- Create a new database
CREATE DATABASE analytics;

-- Switch to it
USE analytics;

-- Tables are isolated per database
CREATE TABLE events (id INTEGER, event TEXT, ts INTEGER);
INSERT INTO events VALUES (1, 'click', 1000);

-- Switch back
USE main;

-- 'events' table is not visible here
SELECT * FROM events;  -- Error: table not found

-- List all databases (current marked with *)
SHOW DATABASES;

-- Create a database with persistent file storage
CREATE DATABASE mydb FILE 'mydb.vdb';

-- Drop a database (cannot drop current or 'main')
USE main;
DROP DATABASE analytics;
```

### Persistence and Import/Export

#### SQL Dump (SAVE / OPEN)

Save the entire current database as a SQL script:

```sql
SAVE 'backup.sql';
```

Restore from a SQL dump:

```sql
OPEN 'backup.sql';
```

The dump file contains `CREATE TABLE` and `INSERT` statements that recreate the database.

#### Binary Storage (.vdb)

Open a persistent binary database file (creates if it doesn't exist):

```sql
OPEN 'mydata.vdb';
```

Binary `.vdb` files use a page-based storage engine with:
- 4KB pages with slotted row storage
- Schema pages for table and index metadata persistence
- B-tree indexes on disk (automatically created, maintained, and restored on OPEN)
- Write-ahead log (WAL) for crash recovery

Close a persistent database:

```sql
CLOSE;
```

#### Persistent Database via CREATE DATABASE

Create a database backed by a file from the start:

```sql
CREATE DATABASE inventory FILE 'inventory.vdb';
USE inventory;
-- All operations are automatically persisted
```

#### CSV Import/Export

Export a table to CSV:

```sql
EXPORT products TO 'products.csv';
```

Import CSV data into an existing table:

```sql
IMPORT INTO products FROM 'products.csv';
```

The CSV handler supports quoted fields, escaped quotes, and automatic type detection based on the target table's column types.

### User Management

ViperSQL supports user management with password authentication. A default superuser `admin` (password: `admin`) is created on server startup.

#### CREATE USER

```sql
CREATE USER alice PASSWORD 'secret123';
```

#### DROP USER

```sql
DROP USER alice;
```

The `admin` user cannot be dropped.

#### ALTER USER (Change Password)

```sql
ALTER USER alice PASSWORD 'newpassword';

-- Alternative syntax
ALTER USER alice SET PASSWORD 'newpassword';
```

#### SHOW USERS

```sql
SHOW USERS;
```

Returns a list of all users. User information is also available via the `sys.users` system view:

```sql
SELECT * FROM sys.users;
```

Returns `username` and `is_superuser` columns.

### Privileges (GRANT/REVOKE)

ViperSQL implements a table-level privilege system with ownership. The user who creates a table is the **owner** and has full access. Other users need explicit GRANT to access the table. The `admin` superuser bypasses all privilege checks.

#### GRANT

```sql
-- Grant specific privileges
GRANT SELECT ON employees TO alice;
GRANT INSERT, UPDATE ON employees TO bob;
GRANT ALL ON employees TO charlie;
GRANT ALL PRIVILEGES ON employees TO dave;

-- Grant to all users
GRANT SELECT ON public_data TO PUBLIC;
```

#### REVOKE

```sql
REVOKE SELECT ON employees FROM alice;
REVOKE ALL ON employees FROM bob;
REVOKE SELECT ON public_data FROM PUBLIC;
```

#### Privilege Types

| Privilege | Allows |
|-----------|--------|
| SELECT | Read rows from the table |
| INSERT | Insert new rows |
| UPDATE | Modify existing rows |
| DELETE | Remove rows |
| ALL | All of the above |

#### Ownership Rules

- Table creator is the owner; owner has implicit ALL privileges
- Only the owner or superuser (admin) can GRANT/REVOKE, DROP TABLE, ALTER TABLE, CREATE/DROP INDEX
- `DROP TABLE` removes all associated privileges
- `DROP USER` removes all privileges granted to that user

#### SHOW GRANTS

```sql
-- Show grants for current user
SHOW GRANTS;

-- Show grants for a specific user
SHOW GRANTS FOR alice;
```

Privilege information is also available via the `INFORMATION_SCHEMA.TABLE_PRIVILEGES` system view:

```sql
SELECT * FROM INFORMATION_SCHEMA.TABLE_PRIVILEGES;
```

Returns `grantor`, `grantee`, `table_name`, and `privilege_type` columns.

### Table Inheritance

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

### Generated Columns

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

### FILTER Clause

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

### LATERAL Joins

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

### NATURAL JOIN and USING

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

### FETCH FIRST / OFFSET-FETCH

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

### GROUPING SETS / ROLLUP / CUBE

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

### DISTINCT ON

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

### Window Frame Specifications

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

#### EXCLUDE Clause

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

### NULLS FIRST / NULLS LAST

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

### IS DISTINCT FROM

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

### IF EXISTS / IF NOT EXISTS / CREATE OR REPLACE

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

### SHOW CREATE TABLE

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

### SHOW INDEXES / SHOW COLUMNS

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

### CREATE TABLE LIKE

Clone a table's structure (columns, types, constraints, defaults) without copying data.

```sql
-- Clone table structure
CREATE TABLE users_backup (LIKE users);

-- Clone preserves column types, PK, NOT NULL, UNIQUE, defaults
-- Does NOT copy data, indexes, or triggers
INSERT INTO users_backup SELECT * FROM users;
```

### TABLE Expression

Shorthand syntax for `SELECT * FROM tablename`:

```sql
TABLE users;  -- equivalent to SELECT * FROM users
```

### Composite PRIMARY KEY

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

### LIMIT PERCENT

Return a percentage of total rows instead of a fixed count:

```sql
-- Top 10% of scores
SELECT * FROM scores ORDER BY score DESC LIMIT 10 PERCENT;

-- 50% sample
SELECT * FROM large_table LIMIT 50 PERCENT;
```

### SELECT INTO

Create a new table from query results (alternative to CREATE TABLE AS SELECT):

```sql
SELECT name, salary INTO high_earners FROM employees WHERE salary > 100000;

-- Multi-table TRUNCATE
TRUNCATE TABLE t1, t2, t3;
```

### Temporary Tables

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

### Row-Level Locking

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

### MVCC (Snapshot Isolation)

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

### System Views

ViperSQL provides virtual system views that expose live server state. These are read-only and generated on demand. Three schema families are supported: `INFORMATION_SCHEMA`, `sys.*`, and `pg_catalog`.

#### INFORMATION_SCHEMA Views

| View | Columns | Description |
|------|---------|-------------|
| `information_schema.tables` | table_catalog, table_schema, table_name, table_type | All tables in the current database |
| `information_schema.columns` | table_name, column_name, ordinal_position, data_type, is_nullable | All columns across all tables |
| `information_schema.schemata` | catalog_name, schema_name | All databases as schemas |

```sql
-- List all tables
SELECT table_name, table_type FROM information_schema.tables;

-- List columns for a specific table
SELECT column_name, data_type FROM information_schema.columns
WHERE table_name = 'users';

-- List all databases
SELECT schema_name FROM information_schema.schemata;
```

#### sys.* Views

| View | Columns | Description |
|------|---------|-------------|
| `sys.databases` | name, table_count, is_persistent, file_path | All databases and their storage info |
| `sys.tables` | table_name, column_count, row_count | Tables in the current database with stats |
| `sys.columns` | table_name, column_name, column_type, ordinal_position | Column metadata for all tables |
| `sys.stats` | stat_name, stat_value | Server statistics (database_count, table_count, total_rows, etc.) |
| `sys.users` | username, is_superuser | All database users |
| `sys.vacuum_stats` | table_name, vacuum_count, dead_rows_removed, last_vacuum, analyze_count, live_rows, dead_rows, last_analyze | VACUUM/ANALYZE statistics per table |

```sql
-- Server statistics
SELECT * FROM sys.stats;

-- Database overview
SELECT * FROM sys.databases;

-- Table sizes
SELECT table_name, row_count FROM sys.tables ORDER BY row_count DESC;

-- All users
SELECT * FROM sys.users;

-- Vacuum/analyze history per table
SELECT table_name, vacuum_count, dead_rows_removed FROM sys.vacuum_stats;
```

#### pg_catalog Views

PostgreSQL-compatible system catalog views for tool and driver interoperability. Accessible with or without the `pg_catalog.` prefix.

| View | Key Columns | Description |
|------|-------------|-------------|
| `pg_class` | oid, relname, relnamespace, relkind, reltuples, relhasindex | Tables (`r`), views (`v`), and indexes (`i`) |
| `pg_attribute` | attrelid, attname, atttypid, attnum, attnotnull, atthasdef | Columns for all tables |
| `pg_type` | oid, typname, typlen, typtype | Data types (int4, float8, text, bool, date, timestamp, bytea, varchar) |
| `pg_namespace` | oid, nspname | Schemas (pg_catalog, public, information_schema) |
| `pg_index` | indexrelid, indrelid, indnatts, indisunique, indisprimary, indkey | Index metadata |
| `pg_database` | oid, datname, datdba, encoding, datcollate | All databases |
| `pg_proc` | oid, proname | Functions (stub — session-scoped functions) |
| `pg_settings` | name, setting, description | Server configuration parameters |
| `pg_stat_activity` | datid, datname, pid, usename, state, query, query_count | Current session activity |
| `pg_stat_user_tables` | relname, seq_scan, seq_tup_read, idx_scan, idx_tup_fetch, n_tup_ins/upd/del, n_live_tup | Per-table operation statistics |
| `pg_stat_user_indexes` | relname, indexrelname, idx_scan, idx_tup_read, idx_tup_fetch | Per-index scan statistics |

```sql
-- List all tables and indexes
SELECT relname, relkind FROM pg_class;

-- Show columns for a specific table
SELECT attname, atttypid, attnotnull FROM pg_attribute WHERE attrelid = 16384;

-- Check available types
SELECT typname, oid FROM pg_type;

-- Server settings
SELECT name, setting FROM pg_settings WHERE name = 'server_version';

-- Current session activity
SELECT datname, usename, state, query FROM pg_stat_activity;

-- Table-level statistics (sequential scans, DML counts)
SELECT relname, seq_scan, n_tup_ins, n_tup_upd, n_tup_del FROM pg_stat_user_tables;

-- Index usage statistics
SELECT indexrelname, idx_scan, idx_tup_fetch FROM pg_stat_user_indexes;
```

OIDs are stable across queries within a session and consistent between views (e.g., `pg_class.oid` matches `pg_attribute.attrelid`).

### Utility Commands

| Command | Description |
|---------|-------------|
| `SHOW TABLES` | List all tables in the current database |
| `SHOW DATABASES` | List all databases (current marked with `*`) |
| `SHOW USERS` | List all database users |
| `DESCRIBE table` | Show column names, types, and constraints |
| `VACUUM` | Remove dead rows (MVCC + soft-deleted) from all tables |
| `VACUUM tablename` | Remove dead rows from a specific table |
| `VACUUM FULL` | Vacuum + rebuild indexes + flush storage |
| `VACUUM ANALYZE` | Vacuum + update optimizer statistics |
| `VACUUM FULL ANALYZE` | Full vacuum + analyze combined |
| `TRUNCATE TABLE t` | Remove all rows from a table (faster than DELETE) |
| `ANALYZE` | Update optimizer statistics for all tables |
| `ANALYZE tablename` | Update statistics for a specific table |
| `HELP` | Show available SQL commands |

Interactive shell meta-commands:

| Command | Description |
|---------|-------------|
| `.help` | Show meta-command help |
| `.tables` | List all tables |
| `.schema` | Show all table schemas |
| `.schema tablename` | Show schema for a specific table |
| `.quit` / `.exit` | Exit the shell |

### VACUUM and ANALYZE

Remove dead rows and update optimizer statistics:

```sql
-- VACUUM: remove dead rows (MVCC + soft-deleted) from all tables
VACUUM;

-- Vacuum a specific table
VACUUM users;

-- VACUUM FULL: vacuum + rebuild indexes + flush storage
VACUUM FULL;

-- ANALYZE: update optimizer statistics (row counts, distinct values)
ANALYZE;

-- Analyze a specific table
ANALYZE orders;

-- Combined operations
VACUUM ANALYZE;
VACUUM FULL ANALYZE;
```

Statistics are tracked in the `sys.vacuum_stats` system view:

```sql
SELECT * FROM sys.vacuum_stats;
-- Columns: table_name, vacuum_count, dead_rows_removed, last_vacuum,
--          analyze_count, live_rows, dead_rows, last_analyze
```

### pg_stat Views

PostgreSQL-compatible statistics views for monitoring database activity:

```sql
-- Active sessions and queries
SELECT pid, usename, state, query FROM pg_stat_activity;

-- Table-level statistics (scans, tuple operations)
SELECT relname, seq_scan, idx_scan, n_tup_ins, n_tup_upd, n_tup_del, n_live_tup
FROM pg_stat_user_tables;

-- Index usage statistics
SELECT relname, indexrelname, idx_scan, idx_tup_read, idx_tup_fetch
FROM pg_stat_user_indexes;
```

All views support the `pg_catalog.*` prefix: `SELECT * FROM pg_catalog.pg_stat_activity`.

### DO Blocks

Execute multiple SQL statements as a block:

```sql
-- Execute multiple statements in a single DO block
DO 'CREATE TABLE temp1 (id INTEGER); INSERT INTO temp1 VALUES (1); INSERT INTO temp1 VALUES (2)';

-- Useful for setup scripts and multi-statement operations
DO 'DROP TABLE IF EXISTS old_data; CREATE TABLE new_data (id INTEGER, val TEXT)';
```

### Session Variables

Set and inspect session-level configuration variables:

```sql
-- Set a session variable
SET search_path = 'public';
SET application_name = 'my_app';
SET statement_timeout = '5000';

-- Show a variable
SHOW search_path;
SHOW ALL;  -- show all session variables

-- Reset to default
RESET search_path;
RESET ALL;

-- Add comments to database objects
COMMENT ON TABLE users IS 'Main user accounts table';
COMMENT ON COLUMN users.email IS 'Primary email address';
```

### System Functions

Built-in system information and utility functions:

```sql
-- User and session information
SELECT CURRENT_USER;           -- current authenticated user
SELECT SESSION_USER;           -- session user name
SELECT CURRENT_DATABASE();     -- current database name
SELECT VERSION();              -- ViperSQL version string

-- Transaction and backend info
SELECT TXID_CURRENT();         -- current transaction ID
SELECT PG_BACKEND_PID();       -- backend process ID

-- Configuration
SELECT CURRENT_SETTING('search_path');          -- read a setting
SELECT SET_CONFIG('app_name', 'myapp', false);  -- set a config value
```

### BETWEEN SYMMETRIC

Standard `BETWEEN` requires `low AND high` in order, but `BETWEEN SYMMETRIC` accepts operands in any order:

```sql
-- Standard BETWEEN: low must be <= high
SELECT * FROM t WHERE x BETWEEN 1 AND 10;

-- BETWEEN SYMMETRIC: automatically swaps if needed
SELECT * FROM t WHERE x BETWEEN SYMMETRIC 10 AND 1;   -- same as BETWEEN 1 AND 10
SELECT * FROM t WHERE x BETWEEN SYMMETRIC 1 AND 10;    -- works either way
SELECT * FROM t WHERE x NOT BETWEEN SYMMETRIC 10 AND 1; -- negated
```

### TABLESAMPLE

Random sampling of table rows without reading the entire table:

```sql
-- SYSTEM sampling: fast, block-level randomness (percentage of rows)
SELECT * FROM large_table TABLESAMPLE SYSTEM (10);   -- ~10% of rows

-- BERNOULLI sampling: row-level, more uniform distribution
SELECT * FROM large_table TABLESAMPLE BERNOULLI (25); -- ~25% of rows

-- Use with WHERE and other clauses
SELECT * FROM orders TABLESAMPLE BERNOULLI (50) WHERE status = 'active';
```

### ANY / ALL / SOME

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

### MERGE INTO

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

### Partial Indexes

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

### Named WINDOW Clause

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

### Row Value Constructors

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

---

## Architecture

### Query Pipeline

```
SQL text --> Lexer --> Parser --> AST (Stmt/Expr) --> Executor --> QueryResult
```

1. **Lexer** (`lexer.zia`) tokenizes SQL text into a stream of `Token` values
2. **Parser** (`parser.zia`) consumes tokens and builds an AST of `Stmt` and `Expr` nodes using recursive descent
3. **Executor** (`executor.zia`) walks the AST and evaluates it against the in-memory data model
4. Results are returned as `QueryResult` entities containing rows and column names

### Executor Composition

The executor delegates to specialized helper entities for complex operations:

```
Executor
├── JoinEngine          -- cross joins, multi-table joins, join GROUP BY, sorting
├── PersistenceManager  -- SAVE, OPEN, CLOSE commands (index restore on OPEN)
├── CsvHandler          -- EXPORT, IMPORT commands
├── IndexManager        -- bucket-accelerated hash index lookups (composite + single-column)
├── BTree[]             -- disk-based B-tree indexes for persistent databases
├── SqlFunctions        -- 80+ built-in SQL functions (string, math, date/time, regex, etc.)
├── SqlWindow           -- window function evaluation
├── SystemViews         -- INFORMATION_SCHEMA, sys.*, and pg_catalog views
├── TriggerManager      -- BEFORE/AFTER triggers for INSERT/UPDATE/DELETE
├── SequenceManager     -- PostgreSQL-style sequences (NEXTVAL/CURRVAL/SETVAL)
├── FunctionManager     -- user-defined SQL functions (CREATE FUNCTION)
└── JsonParser          -- pure-Zia JSON parser (validation, extraction, construction)
```

Each helper holds an `Executor` reference (via Zia's circular bind support) for shared state access.

### Transaction Management

The executor provides ACID transaction support:

- **Atomicity**: Changes within a transaction are all-or-nothing. Statement-level atomicity ensures individual statements are atomic even outside explicit transactions.
- **Journal-based rollback**: INSERT, UPDATE, and DELETE operations record journal entries (with before-images for updates). ROLLBACK replays the journal in reverse.
- **FK cascade handling**: ON DELETE/UPDATE CASCADE, SET NULL, and RESTRICT actions are enforced during DELETE and UPDATE operations, with cascaded changes also tracked in the journal.
- **Deferred compaction**: During transactions, DELETE operations use soft-delete; compaction is deferred to COMMIT.
- **MVCC snapshot isolation**: Each BEGIN assigns a unique transaction ID and snapshot. Row versioning (xmin/xmax) provides snapshot isolation — readers never block writers.
- **WAL undo phase**: ARIES-style crash recovery with Compensation Log Records (CLR), before-image capture for UPDATE/DELETE, full redo (INSERT+UPDATE+DELETE replay), and undo of uncommitted transactions.

### Storage Engine

Persistent storage uses a page-based architecture inspired by traditional RDBMS designs:

```
StorageEngine
├── Pager            -- 4KB page I/O (read/write/allocate)
├── BufferPool       -- in-memory page cache with dirty tracking
├── SchemaPage       -- table and index metadata serialization (IndexMeta persistence)
├── DataPage         -- slotted row storage with delete/compact
├── BTree            -- disk-based B-tree indexes (integrated with CREATE INDEX / OPEN)
├── WALManager       -- write-ahead log for crash recovery
└── TxnManager       -- transaction lifecycle (BEGIN/COMMIT/ROLLBACK)
```

### Concurrency and Thread Safety

The storage layer is designed for multi-user access using Monitor-based locking (`Viper.Threads.Monitor`). Each shared component has its own lock with a consistent internal/external method pattern to prevent re-entry deadlocks:

```
Lock Hierarchy (no circular dependencies):
  DatabaseServer.lock        -- protects database list mutations and user management
  TableLockManager.monitor   -- protects table-level S/X lock state
  StorageEngine.lock         -- protects table metadata and row operations
  BufferPool.lock            -- protects page cache (LRU eviction, fetch, flush)
  WALManager.lock            -- protects log writes and file rotation
```

Public methods acquire the lock and delegate to `*Internal` variants. Internal call chains (e.g., `openDatabase` calling `enableWal` and `performRecovery`) use internal variants to avoid deadlocking on the same lock. The Pager does not need its own lock since BufferPool is its sole caller.

**Table-Level Locking**: The `TableLockManager` (in `storage/txn.zia`) implements shared/exclusive table-level locks for concurrent query isolation. Lock semantics:
- `SELECT` acquires shared (S) locks on all FROM/JOIN tables
- `INSERT/UPDATE/DELETE` acquires exclusive (X) locks on the target table
- `CREATE/DROP/ALTER TABLE` acquires exclusive (X) locks
- S+S = compatible (concurrent readers), S+X or X+X = conflict (blocks with 5s timeout)
- Non-transactional statements: locks released after each statement
- Transactional statements: locks held until `COMMIT` or `ROLLBACK`

**Row-Level Locking**: The `RowLockManager` (in `storage/txn.zia`) implements fine-grained row-level locks for `SELECT ... FOR UPDATE` and `SELECT ... FOR SHARE`. Lock granularity is per-row (table+rowIndex). Supports `NOWAIT` (immediate error on conflict) and `SKIP LOCKED` (skip locked rows). Row locks are released on COMMIT/ROLLBACK.

### Session Architecture

ViperSQL supports per-connection isolation through the Session entity:

```
Session (per connection)
├── sessionId, clientHost, username   -- connection metadata
├── authenticated                     -- authentication state
├── preparedStmts[]                   -- named/unnamed prepared statements
├── portals[]                         -- bound portals (extended query protocol)
└── Executor (per session)
    ├── currentDbName, currentDbIndex -- per-session database context
    ├── inTransaction, journal        -- per-session transaction state
    ├── outerRow, subqueryDepth       -- per-session subquery context
    └── server (shared)               -- thread-safe DatabaseServer
```

Each connection gets its own `Session` with its own `Executor` instance. Multiple executors share the same `DatabaseServer` (created via `Executor.initWithServer()`). The current database is tracked per-executor, allowing independent `USE DATABASE` across connections. The session ID is used as the lock owner for table-level concurrency control.

### Multi-User TCP Server

The `server/` module implements a multi-user TCP server using a thread-per-connection model (like PostgreSQL):

- **Thread-per-connection**: Each accepted TCP connection spawns a dedicated thread via `Thread.StartSafe` for error isolation
- **PostgreSQL wire protocol (v3)**: Binary protocol on port 5432 for standard client compatibility (psql, ODBC, pgAdmin). Supports both Simple Query (Q) and Extended Query (Parse/Bind/Describe/Execute) protocols with prepared statements, parameterized queries, and portal suspension.
- **Simple text protocol**: SQL terminated by newline, response terminated by double-newline on port 5433
- **Authentication**: Cleartext password authentication via the PG wire protocol handshake (AuthenticationCleartextPassword / PasswordMessage)
- **Connection handler**: Per-connection Session with recv/execute/send loop, idle timeout (5 min), graceful disconnect
- **Entry point**: `viper run demos/zia/sqldb/server/tcp_server.zia`

#### PostgreSQL Wire Protocol Messages

| Message | Direction | Purpose |
|---------|-----------|---------|
| StartupMessage | Client -> Server | Version (3.0), user, database |
| AuthenticationCleartextPassword | Server -> Client | Request password |
| PasswordMessage | Client -> Server | Send password |
| AuthenticationOk | Server -> Client | Auth success |
| ParameterStatus | Server -> Client | Server parameters (version, encoding, etc.) |
| BackendKeyData | Server -> Client | Process ID and secret key |
| ReadyForQuery | Server -> Client | Ready status (I=idle, T=in-txn, E=error) |
| Query (Q) | Client -> Server | Simple query text |
| Parse (P) | Client -> Server | Prepare a named/unnamed statement |
| Bind (B) | Client -> Server | Bind parameters to create a portal |
| Describe (D) | Client -> Server | Describe a statement or portal |
| Execute (E) | Client -> Server | Execute a portal (with optional row limit) |
| Sync (S) | Client -> Server | End extended query pipeline, sync state |
| Close (C) | Client -> Server | Close a statement or portal |
| Flush (H) | Client -> Server | Flush output buffer |
| RowDescription (T) | Server -> Client | Column names and types |
| DataRow (D) | Server -> Client | Row values |
| CommandComplete (C) | Server -> Client | Completion tag (e.g., "SELECT 3") |
| ParseComplete (1) | Server -> Client | Parse succeeded |
| BindComplete (2) | Server -> Client | Bind succeeded |
| CloseComplete (3) | Server -> Client | Close succeeded |
| ParameterDescription (t) | Server -> Client | Parameter type OIDs |
| NoData (n) | Server -> Client | Statement produces no rows |
| PortalSuspended (s) | Server -> Client | Execute hit row limit |
| ErrorResponse (E) | Server -> Client | Error with severity, code, message |
| Terminate (X) | Client -> Server | Close connection |

### Query Optimizer

The optimizer (`optimizer/optimizer.zia`) provides cost-based query optimization:

- Table statistics (row counts, distinct value estimates)
- Access path selection (table scan vs. index scan vs. index seek)
- Cost estimation with configurable selectivity constants
- Automatic index selection for equality predicates

### Performance Optimizations

ViperSQL includes several algorithmic optimizations for efficient query processing:

- **Quicksort** — ORDER BY uses iterative quicksort with median-of-three pivot selection (O(n log n)) for result rows, row indices, and join results
- **Hash-based GROUP BY** — Group key deduplication uses 128-bucket hash tables for O(n) amortized grouping instead of O(n*g) linear scan
- **Hash-based DISTINCT** — Duplicate elimination uses hash buckets for O(n) amortized deduplication
- **Bucket-based index lookup** — Hash indexes use 64-bucket acceleration structures, reducing lookup from O(n) to O(n/b) per query
- **Disk-based B-tree indexes** — Persistent databases use order-50 B-trees (~99 keys per 4KB page) for O(log n) lookups, integrated with the BufferPool page cache
- **Short-circuit AND/OR** — Boolean expressions short-circuit: `FALSE AND ...` returns immediately without evaluating the right side, and `TRUE OR ...` returns immediately

---

## Testing

All tests use a shared harness (`tests/test_common.zia`) providing `assert()`, `check()`, `assertTrue()`, `assertFalse()`, and `printResults()`.

### Running Tests

```bash
# Core SQL functionality
viper run demos/zia/sqldb/tests/test_basic_crud.zia
viper run demos/zia/sqldb/tests/test_advanced.zia

# Specific feature areas
viper run demos/zia/sqldb/tests/test_constraints.zia
viper run demos/zia/sqldb/tests/test_functions.zia
viper run demos/zia/sqldb/tests/test_subquery.zia
viper run demos/zia/sqldb/tests/test_index.zia
viper run demos/zia/sqldb/tests/test_sql_features.zia

# Multi-database and persistence
viper run demos/zia/sqldb/tests/test_multidb.zia
viper run demos/zia/sqldb/tests/test_persistence.zia

# Storage engine internals
viper run demos/zia/sqldb/tests/test_storage.zia
viper run demos/zia/sqldb/tests/test_btree.zia
viper run demos/zia/sqldb/tests/test_engine.zia
viper run demos/zia/sqldb/tests/test_wal.zia
viper run demos/zia/sqldb/tests/test_txn.zia

# Stress tests and native codegen validation
viper run demos/zia/sqldb/tests/test_storage_persistence.zia
viper run demos/zia/sqldb/tests/test_storage_stress.zia
viper run demos/zia/sqldb/tests/test_native_stress.zia
viper run demos/zia/sqldb/tests/test_native_edge.zia
viper run demos/zia/sqldb/tests/test_native_torture.zia
viper run demos/zia/sqldb/tests/test_native_extreme.zia

# Phase feature tests
viper run demos/zia/sqldb/tests/test_phase1.zia
viper run demos/zia/sqldb/tests/test_phase2.zia
viper run demos/zia/sqldb/tests/test_phase3.zia
viper run demos/zia/sqldb/tests/test_phase3_txn.zia
viper run demos/zia/sqldb/tests/test_phase4_functions.zia
viper run demos/zia/sqldb/tests/test_phase4_cte.zia
viper run demos/zia/sqldb/tests/test_phase4_multitable.zia
viper run demos/zia/sqldb/tests/test_phase4_window.zia
viper run demos/zia/sqldb/tests/test_phase4_datetime.zia

# Multi-user and server tests
viper run demos/zia/sqldb/tests/test_multiuser.zia
viper run demos/zia/sqldb/tests/test_system_views.zia
viper run demos/zia/sqldb/tests/test_tempdb.zia
viper run demos/zia/sqldb/tests/test_auth.zia
viper run demos/zia/sqldb/tests/test_pg_wire.zia
viper run demos/zia/sqldb/tests/test_extended_query.zia

# Triggers, sequences, stored functions, composite indexes, pg_catalog, JSON
viper run demos/zia/sqldb/tests/test_phase17_triggers.zia
viper run demos/zia/sqldb/tests/test_phase18_sequences.zia
viper run demos/zia/sqldb/tests/test_phase19_procedures.zia
viper run demos/zia/sqldb/tests/test_phase20_composite_indexes.zia
viper run demos/zia/sqldb/tests/test_phase21_pg_catalog.zia
viper run demos/zia/sqldb/tests/test_phase25_json.zia
viper run demos/zia/sqldb/tests/test_phase26_vacuum.zia
viper run demos/zia/sqldb/tests/test_phase27_pg_stat.zia
viper run demos/zia/sqldb/tests/test_phase28_partition.zia
viper run demos/zia/sqldb/tests/test_phase29_extensions.zia
viper run demos/zia/sqldb/tests/test_phase30_prepared.zia
viper run demos/zia/sqldb/tests/test_phase32_cursors.zia
viper run demos/zia/sqldb/tests/test_phase33_savepoints.zia
viper run demos/zia/sqldb/tests/test_phase34_copy.zia
viper run demos/zia/sqldb/tests/test_phase35_call.zia
viper run demos/zia/sqldb/tests/test_phase36_arrays.zia
viper run demos/zia/sqldb/tests/test_phase37_matviews.zia
viper run demos/zia/sqldb/tests/test_phase38_expressions.zia
viper run demos/zia/sqldb/tests/test_phase39_ctas.zia
viper run demos/zia/sqldb/tests/test_phase40_aggregates.zia
viper run demos/zia/sqldb/tests/test_phase41_do_blocks.zia
viper run demos/zia/sqldb/tests/test_phase42_insert.zia
viper run demos/zia/sqldb/tests/test_phase43_session.zia
viper run demos/zia/sqldb/tests/test_phase44_sysfuncs.zia
viper run demos/zia/sqldb/tests/test_phase45_alter_col.zia
viper run demos/zia/sqldb/tests/test_phase46_sqlfuncs.zia
viper run demos/zia/sqldb/tests/test_phase47_recursive_cte.zia
viper run demos/zia/sqldb/tests/test_phase48_math.zia
viper run demos/zia/sqldb/tests/test_phase49_strings.zia
viper run demos/zia/sqldb/tests/test_phase50_datetime.zia
viper run demos/zia/sqldb/tests/test_phase51_regex.zia
viper run demos/zia/sqldb/tests/test_phase52_inherit.zia
viper run demos/zia/sqldb/tests/test_phase53_generated.zia
viper run demos/zia/sqldb/tests/test_phase54_filter.zia
viper run demos/zia/sqldb/tests/test_phase55_lateral.zia
viper run demos/zia/sqldb/tests/test_phase56_natural_using.zia
viper run demos/zia/sqldb/tests/test_phase57_setops_all.zia
viper run demos/zia/sqldb/tests/test_phase58_fetch_first.zia
viper run demos/zia/sqldb/tests/test_phase59_grouping_sets.zia
viper run demos/zia/sqldb/tests/test_phase60_distinct_on.zia
viper run demos/zia/sqldb/tests/test_phase61_window_frames.zia
viper run demos/zia/sqldb/tests/test_phase62_nulls_first_last.zia
viper run demos/zia/sqldb/tests/test_phase63_window_funcs.zia
viper run demos/zia/sqldb/tests/test_phase64_is_distinct.zia
viper run demos/zia/sqldb/tests/test_phase65_if_exists.zia
viper run demos/zia/sqldb/tests/test_phase66_show_create.zia
viper run demos/zia/sqldb/tests/test_phase67_introspection.zia
viper run demos/zia/sqldb/tests/test_phase68_table_pk_constraints.zia
viper run demos/zia/sqldb/tests/test_phase69_query_extras.zia
viper run demos/zia/sqldb/tests/test_phase70_query_features.zia
viper run demos/zia/sqldb/tests/test_phase71_any_all_some.zia
viper run demos/zia/sqldb/tests/test_phase72_window_exclude.zia

# Documentation examples (verifies all README examples)
viper run demos/zia/sqldb/tests/test_readme_examples.zia
```

### Compiling and Running Tests Natively

All tests also pass when compiled to native ARM64 machine code:

```bash
viper build demos/zia/sqldb/tests/test_basic_crud.zia -o /tmp/test_sql && /tmp/test_sql
viper build demos/zia/sqldb/tests/test_native_torture.zia -o /tmp/test_torture && /tmp/test_torture
```

### Test Suites

| Test File | Focus Area | Assertions |
|-----------|------------|------------|
| `test_basic_crud.zia` | Core CRUD operations | Basic SQL |
| `test_advanced.zia` | UNION, EXCEPT, INTERSECT, CASE | Set operations |
| `test_constraints.zia` | NOT NULL, UNIQUE, PK, FK, DEFAULT | Data integrity |
| `test_functions.zia` | String, math, null-handling functions | Built-in functions |
| `test_subquery.zia` | Scalar and IN subqueries | Nested queries |
| `test_index.zia` | Index creation, lookup, unique indexes | Index operations |
| `test_sql_features.zia` | ORDER BY, LIMIT, arithmetic, misc features | Query features |
| `test_multidb.zia` | CREATE/USE/DROP DATABASE | Multi-database |
| `test_optimizer.zia` | Cost estimation, access path selection | Query optimizer |
| `test_persistence.zia` | SAVE/OPEN round-trips, .vdb files | Persistence |
| `test_storage.zia` | Binary buffers, serialization | Storage primitives |
| `test_btree.zia` | B-tree insert, search, split | B-tree index |
| `test_engine.zia` | StorageEngine integration | Storage engine |
| `test_wal.zia` | Write-ahead log operations | WAL |
| `test_txn.zia` | Transaction manager | Transactions |
| `test_server.zia` | Wire protocol server | Network server |
| `test_storage_persistence.zia` | Large datasets, persistence stress | Stress testing |
| `test_storage_stress.zia` | Multi-database persistence | Stress testing |
| `test_stress4_native.zia` | Native codegen correctness | Native stress |
| `test_native_stress.zia` | 15 native stress tests (73 assertions) | Native codegen |
| `test_native_edge.zia` | 10 edge case tests (68 assertions) | Edge cases |
| `test_native_torture.zia` | 16 torture tests (75 assertions) | Complex queries |
| `test_native_extreme.zia` | 12 extreme tests (54 assertions) | Extreme scenarios |
| `test_phase1.zia` | INSERT...SELECT, EXISTS, CAST, Views, CHECK, Derived tables | Phase 1 features |
| `test_phase2.zia` | Quicksort, hash GROUP BY, hash DISTINCT, index buckets | Performance |
| `test_phase3.zia` | Join engine, hash join | Join algorithms |
| `test_phase3_txn.zia` | BEGIN/COMMIT/ROLLBACK, atomicity, ON DELETE/UPDATE CASCADE | Transactions |
| `test_phase4_functions.zia` | POWER, SQRT, LOG, LEFT, RIGHT, REVERSE, HEX, GREATEST, LEAST | Extended functions |
| `test_phase4_cte.zia` | WITH clause, multiple CTEs, CTE chaining, CTE with INSERT/JOIN | CTEs |
| `test_phase4_multitable.zia` | UPDATE...FROM, DELETE...USING, multi-table with CTE | Multi-table ops |
| `test_phase4_window.zia` | ROW_NUMBER, RANK, DENSE_RANK, SUM/COUNT OVER, PARTITION BY | Window functions |
| `test_phase4_datetime.zia` | NOW, YEAR, MONTH, DATEDIFF, DATE_ADD, STRFTIME, EPOCH | Date/time |
| `test_phase5_hashjoin.zia` | Hash join algorithm, equi-join optimization | Hash joins |
| `test_phase5_joinorder.zia` | Join order optimization | Join planning |
| `test_phase5_optimizer.zia` | Extended optimizer tests | Optimizer |
| `test_phase5_range.zia` | Range scan, index range queries | Range queries |
| `test_multiuser.zia` | Concurrent sessions, table locking, lock conflicts | Multi-user |
| `test_system_views.zia` | INFORMATION_SCHEMA, sys.* virtual views | System views |
| `test_tempdb.zia` | CREATE TEMP TABLE, session isolation, table shadowing | Temporary tables |
| `test_auth.zia` | CREATE/DROP/ALTER USER, SHOW USERS, password verification | Authentication |
| `test_pg_wire.zia` | PG wire protocol binary messages, auth handshake | Wire protocol |
| `test_extended_query.zia` | Extended query protocol (Parse/Bind/Describe/Execute) | Extended protocol |
| `test_pg_net_minimal.zia` | End-to-end PG network connectivity | Network integration |
| `test_phase12_types.zia` | BOOLEAN, DATE, TIMESTAMP types, IS TRUE/FALSE, CAST | Type system |
| `test_phase13_privileges.zia` | GRANT/REVOKE, ownership, superuser bypass, PUBLIC | Privileges |
| `test_phase14_row_locks.zia` | FOR UPDATE/FOR SHARE, NOWAIT, SKIP LOCKED, lock conflicts | Row locking |
| `test_phase15_wal_undo.zia` | CLR records, before-images, recovery redo/undo, WAL scanning | WAL undo |
| `test_phase16_mvcc.zia` | xmin/xmax, isVisible, snapshot isolation, MVCC with JOINs | MVCC |
| `test_phase17_triggers.zia` | BEFORE/AFTER triggers, OLD/NEW refs, statement triggers, DROP | Triggers |
| `test_phase18_sequences.zia` | CREATE/ALTER/DROP SEQUENCE, NEXTVAL/CURRVAL/SETVAL, CYCLE | Sequences |
| `test_phase19_procedures.zia` | CREATE/DROP FUNCTION, parameter substitution, overloading | Stored functions |
| `test_phase20_composite_indexes.zia` | Composite indexes, prefix lookup, unique composite | Composite indexes |
| `test_phase21_pg_catalog.zia` | pg_class, pg_attribute, pg_type, pg_namespace, pg_index, pg_database | pg_catalog views |
| `test_phase25_json.zia` | JSON/JSONB type, JSON_VALID, JSON_EXTRACT, JSON_BUILD_OBJECT, JSONPath | JSON support |
| `test_phase26_vacuum.zia` | VACUUM, VACUUM FULL, ANALYZE, VACUUM ANALYZE, sys.vacuum_stats, MVCC dead row cleanup | Maintenance |
| `test_phase27_pg_stat.zia` | pg_stat_activity, pg_stat_user_tables, pg_stat_user_indexes, DML tracking | Statistics views |
| `test_phase28_partition.zia` | PARTITION BY RANGE/LIST/HASH, partition routing, partition-aware SELECT, pruning | Table partitioning |
| `test_phase29_extensions.zia` | TRUNCATE, RETURNING, ON CONFLICT/UPSERT, GENERATE_SERIES | SQL extensions |
| `test_phase30_prepared.zia` | PREPARE/EXECUTE/DEALLOCATE, EXPLAIN ANALYZE | Prepared statements |
| `test_phase32_cursors.zia` | DECLARE/FETCH/CLOSE/MOVE cursors, bidirectional navigation | SQL cursors |
| `test_phase33_savepoints.zia` | SAVEPOINT/RELEASE/ROLLBACK TO, nested savepoints | Subtransactions |
| `test_phase34_copy.zia` | COPY TO/FROM files, COPY (query), CSV round-trip | Bulk data I/O |
| `test_phase35_call.zia` | CALL stored functions, parameter substitution | Stored procedures |
| `test_phase36_arrays.zia` | ARRAY constructor, 9 array functions, TYPEOF support | Array type |
| `test_phase37_matviews.zia` | CREATE/REFRESH/DROP MATERIALIZED VIEW, snapshot semantics | Materialized views |
| `test_phase38_expressions.zia` | NULLIF, ILIKE, INITCAP, REPEAT, SPLIT_PART, CHR, ASCII, CONCAT_WS | SQL expressions |
| `test_phase39_ctas.zia` | CREATE TABLE AS SELECT, CTAS with aggregation, temp CTAS | CTAS |
| `test_phase40_aggregates.zia` | STRING_AGG, GROUP_CONCAT, ARRAY_AGG, BOOL_AND, BOOL_OR | Aggregates |
| `test_phase41_do_blocks.zia` | DO anonymous blocks, multi-statement execution | DO blocks |
| `test_phase42_insert.zia` | INSERT DEFAULT VALUES, standalone VALUES queries | INSERT improvements |
| `test_phase43_session.zia` | SET/SHOW/RESET variables, COMMENT ON | Session variables |
| `test_phase44_sysfuncs.zia` | CURRENT_USER, VERSION(), CURRENT_SETTING, SET_CONFIG | System functions |
| `test_phase45_alter_col.zia` | ALTER COLUMN SET/DROP DEFAULT, SET/DROP NOT NULL, TYPE | Column modifications |
| `test_phase46_sqlfuncs.zia` | EXTRACT, POSITION, SUBSTRING FROM, TRIM LEADING/TRAILING | SQL-standard syntax |
| `test_phase47_recursive_cte.zia` | WITH RECURSIVE, fixpoint evaluation, hierarchy traversal | Recursive CTEs |
| `test_phase48_math.zia` | SIN, COS, TAN, ASIN, CBRT, GCD, LCM, DEGREES, RADIANS | Math functions |
| `test_phase49_strings.zia` | CHAR_LENGTH, TRANSLATE, OVERLAY, MD5, SHA256, TO_HEX | String functions |
| `test_phase50_datetime.zia` | DATE_PART, DATE_TRUNC, AGE, TO_CHAR, MAKE_DATE, TO_TIMESTAMP | Date/time functions |
| `test_phase51_regex.zia` | ~ operator, SIMILAR TO, REGEXP_MATCH/REPLACE/COUNT/SPLIT | Regular expressions |
| `test_phase52_inherit.zia` | INHERITS, ONLY, polymorphic queries, column inheritance | Table inheritance |
| `test_phase53_generated.zia` | GENERATED ALWAYS AS, computed columns, positional INSERT | Generated columns |
| `test_phase54_filter.zia` | Aggregate FILTER (WHERE ...), GROUP BY with FILTER | FILTER clause |
| `test_phase55_lateral.zia` | LATERAL subqueries, LEFT/CROSS JOIN LATERAL, aggregates | LATERAL joins |
| `test_phase56_natural_using.zia` | NATURAL JOIN, NATURAL LEFT/RIGHT/FULL, JOIN USING | NATURAL/USING |
| `test_phase57_setops_all.zia` | EXCEPT ALL, INTERSECT ALL, chained set operations | Set ops ALL |
| `test_phase58_fetch_first.zia` | FETCH FIRST/NEXT, OFFSET ROWS, combined paging | FETCH FIRST |
| `test_phase59_grouping_sets.zia` | ROLLUP, CUBE, GROUPING SETS, multi-level aggregation | Grouping sets |
| `test_phase60_distinct_on.zia` | DISTINCT ON single/multi column, with ORDER BY/LIMIT | DISTINCT ON |
| `test_phase61_window_frames.zia` | ROWS BETWEEN, sliding window SUM/COUNT/AVG/MIN/MAX | Window frames |
| `test_phase62_nulls_first_last.zia` | NULLS FIRST/LAST, multi-column, GROUP BY, WHERE+LIMIT | NULLS ordering |
| `test_phase63_window_funcs.zia` | LAG, LEAD, FIRST_VALUE, LAST_VALUE, NTH_VALUE, NTILE, PERCENT_RANK, CUME_DIST | Window functions |
| `test_phase64_is_distinct.zia` | IS DISTINCT FROM, IS NOT DISTINCT FROM, NULL-safe comparisons | NULL-safe ops |
| `test_phase65_if_exists.zia` | IF EXISTS, IF NOT EXISTS, CREATE OR REPLACE VIEW/FUNCTION | Defensive DDL |
| `test_phase66_show_create.zia` | SHOW CREATE TABLE, DDL round-trip, indexes, constraints | DDL introspection |
| `test_phase67_introspection.zia` | SHOW INDEXES, SHOW COLUMNS, CREATE TABLE LIKE | Schema introspection |
| `test_phase68_table_pk_constraints.zia` | TABLE expression, composite PK, named constraints | Table/PK/constraints |
| `test_phase69_query_extras.zia` | LIMIT PERCENT, SELECT INTO, multi-table TRUNCATE | Query extras |
| `test_phase70_query_features.zia` | BETWEEN SYMMETRIC, TABLESAMPLE, column aliases in ORDER BY | Query features |
| `test_phase71_any_all_some.zia` | ANY/ALL/SOME quantified subquery comparisons | Quantified comparisons |
| `test_phase72_window_exclude.zia` | EXCLUDE CURRENT ROW/NO OTHERS for window frames | Window exclusion |
| `test_phase73_merge.zia` | MERGE INTO with WHEN MATCHED UPDATE/DELETE, WHEN NOT MATCHED INSERT | MERGE upsert |
| `test_phase74_partial_indexes.zia` | Partial indexes with WHERE predicate, partial UNIQUE | Partial indexes |
| `test_phase75_named_windows.zia` | Named WINDOW clause, OVER w references, multiple windows | Named windows |
| `test_phase76_row_values.zia` | Row value constructors, tuple comparisons, lexicographic ordering, IN tuples | Row values |
| `test_readme_examples.zia` | All README documentation examples (129 assertions) | Documentation |

---

## Directory Structure

```
sqldb/
├── main.zia              Entry point -- interactive SQL shell (REPL)
├── executor.zia          Query executor -- dispatches parsed SQL to handlers
├── parser.zia            Recursive-descent SQL parser
├── lexer.zia             SQL tokenizer
├── token.zia             Token type definitions (140+ token types)
├── stmt.zia              Statement AST node types
├── expr.zia              Expression AST node types
├── types.zia             Core SQL value types (Integer, Real, Text, Null)
├── schema.zia            Column and Row definitions
├── table.zia             Table entity (row storage, column metadata)
├── database.zia          Database entity (table registry)
├── ddl.zia               DDL handler (CREATE/DROP/ALTER TABLE, INDEX, VIEW, DATABASE)
├── dml.zia               DML handler (INSERT, UPDATE, DELETE with constraints)
├── query.zia             Query handler (SELECT, GROUP BY, sorting, subqueries)
├── index.zia             Index manager (hash-based lookups, 64 buckets)
├── result.zia            QueryResult entity (returned from all queries)
├── join.zia              JoinEngine -- cross join, hash join, join GROUP BY, sorting
├── setops.zia            Set operations (UNION, EXCEPT, INTERSECT)
├── csv.zia               CSV import/export handler
├── persistence.zia       Save/Open/Close -- SQL dump and .vdb persistence
├── server.zia            DatabaseServer -- multi-database management, user management
├── session.zia           Per-connection session (wraps Executor for multi-user)
├── system_views.zia      System views (INFORMATION_SCHEMA, sys.*, pg_catalog)
├── sql_functions.zia     80+ built-in SQL functions (string, math, date/time, regex, etc.)
├── sql_window.zia        Window function evaluation engine
├── triggers.zia          Trigger manager (BEFORE/AFTER INSERT/UPDATE/DELETE)
├── sequence.zia          Sequence manager (NEXTVAL, CURRVAL, SETVAL, CYCLE)
├── procedures.zia        Stored function manager (CREATE FUNCTION, parameter substitution)
├── json.zia              JSON parser and SQL functions (JSON_EXTRACT, JSON_BUILD_OBJECT, etc.)
│
├── optimizer/
│   └── optimizer.zia     Query optimizer (cost-based, index selection)
│
├── server/
│   ├── tcp_server.zia    TCP server entry point (accept loop, dual port)
│   ├── connection.zia    Per-connection handler (recv/exec/send loop, auth)
│   ├── pg_wire.zia       PostgreSQL wire protocol v3 (binary message encoding/decoding)
│   ├── threadpool.zia    Thread-per-connection manager
│   ├── sql_server.zia    Query processing utilities
│   └── sql_client.zia    Wire protocol client (for testing)
│
├── storage/
│   ├── engine.zia        StorageEngine -- top-level persistent storage API
│   ├── pager.zia         Page-based file I/O (4KB pages)
│   ├── buffer.zia        BufferPool -- LRU page cache (100 pages)
│   ├── serializer.zia    Row/value binary serialization
│   ├── page.zia          Page type definitions and constants
│   ├── data_page.zia     Slotted data page (row storage)
│   ├── schema_page.zia   Schema page (table + index metadata persistence)
│   ├── btree.zia         B-tree index implementation
│   ├── btree_node.zia    B-tree node operations
│   ├── wal.zia           Write-ahead log manager (ARIES-style redo/undo, CLR)
│   └── txn.zia           TransactionManager, TableLockManager, RowLockManager
│
└── tests/                85 test files with 4,049+ assertions
    ├── test_common.zia   Shared test harness (assert, check, assertTrue, etc.)
    ├── test.zia          Core SQL tests
    ├── test_advanced.zia Set operations and CASE
    ├── test_auth.zia     User authentication tests
    ├── test_multiuser.zia Multi-user concurrency tests
    ├── test_system_views.zia System views tests
    ├── test_tempdb.zia   Temporary table tests
    ├── test_pg_wire.zia  PG wire protocol unit tests
    ├── test_pg_net_minimal.zia PG network integration test
    ├── ...               (see Testing section for full list)
    └── test_readme_examples.zia  Verifies all README examples
```
