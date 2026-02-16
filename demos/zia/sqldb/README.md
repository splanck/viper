# ViperSQL

A complete SQL database engine written entirely in [Zia](../../../docs/zia-guide.md), the high-level language frontend for the Viper compiler toolchain. ViperSQL implements a substantial subset of SQL including DDL, DML, joins, subqueries, aggregations, views, CHECK constraints, CAST expressions, EXISTS/NOT EXISTS, derived tables, INSERT...SELECT, transactions (BEGIN/COMMIT/ROLLBACK), ON DELETE/UPDATE CASCADE, window functions, Common Table Expressions (CTEs), multi-table UPDATE/DELETE, date/time functions, indexes, persistent storage, CSV import/export, multi-database support, a thread-safe storage layer, per-connection sessions, table-level concurrency control (S/X locking), a multi-user TCP server with PostgreSQL wire protocol (v3), system views (INFORMATION_SCHEMA and sys.*), temporary tables, and user authentication.

**37,000+ lines of Zia** across 89 source and test files, with **2,100+ automated test assertions** across 49 test files.

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
  - [Window Functions](#window-functions)
  - [Common Table Expressions (CTEs)](#common-table-expressions-ctes)
  - [Joins](#joins)
  - [Multi-Table UPDATE/DELETE](#multi-table-updatedelete)
  - [Subqueries](#subqueries)
  - [Set Operations](#set-operations)
  - [Indexes](#indexes)
  - [Constraints](#constraints)
  - [Multi-Database](#multi-database)
  - [Persistence and Import/Export](#persistence-and-importexport)
  - [User Management](#user-management)
  - [Temporary Tables](#temporary-tables)
  - [System Views](#system-views)
  - [Utility Commands](#utility-commands)
- [Architecture](#architecture)
- [Testing](#testing)
- [Directory Structure](#directory-structure)

---

## Getting Started

### Prerequisites

Build the Viper toolchain from the repository root:

```bash
cmake -S . -B build
cmake --build build -j
```

The `viper` CLI will be at `build/src/tools/viper/viper`.

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

`UseServerSidePrepare = 0` is required because ViperSQL implements the Simple Query protocol (not the Extended Query protocol with Parse/Bind/Execute).

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

Any PostgreSQL-compatible client that supports the Simple Query protocol should work, including:
- **psql** (PostgreSQL interactive terminal)
- **psqlODBC** (via unixODBC)
- **pgAdmin** (GUI tool)
- Programming language drivers with simple query mode (e.g., Python `psycopg2`, Node.js `pg`)

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
| `NULL` | Absence of a value | `NULL` |

String literals use single quotes. To include a literal single quote, double it: `'O''Brien'`.

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

Insert from a SELECT query (INSERT...SELECT):

```sql
-- Copy all rows from one table to another
INSERT INTO archive SELECT * FROM users;

-- Copy specific columns with a filter
INSERT INTO vip_users (name, age) SELECT name, age FROM users WHERE age > 30;

-- Insert aggregated data
INSERT INTO summary SELECT COUNT(*), AVG(age) FROM users;
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

**Transaction rules**:
- Nested `BEGIN` is not allowed (returns an error)
- `COMMIT`/`ROLLBACK` without an active transaction returns an error
- DELETE compaction is deferred during transactions and applied on COMMIT

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

**BETWEEN** — Range check (inclusive):

```sql
SELECT * FROM products WHERE price BETWEEN 10 AND 50;
SELECT * FROM products WHERE price NOT BETWEEN 10 AND 50;
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

Supported target types: `INTEGER`, `INT`, `REAL`, `FLOAT`, `TEXT`, `VARCHAR`.

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

#### Extended String Functions

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `LEFT(s, n)` | First n characters | `LEFT('hello', 3)` | `hel` |
| `RIGHT(s, n)` | Last n characters | `RIGHT('hello', 3)` | `llo` |
| `LPAD(s, len, pad)` | Left-pad to length | `LPAD('42', 5, '0')` | `00042` |
| `RPAD(s, len, pad)` | Right-pad to length | `RPAD('hi', 5, '.')` | `hi...` |
| `REVERSE(s)` | Reverse a string | `REVERSE('hello')` | `olleh` |
| `HEX(n)` | Integer to hex | `HEX(255)` | `FF` |

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

Supported window functions: `ROW_NUMBER()`, `RANK()`, `DENSE_RANK()`, `SUM()`, `COUNT()`, `AVG()`, `MIN()`, `MAX()`.

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

### Indexes

Create indexes to speed up queries on frequently searched columns:

```sql
-- Standard index
CREATE INDEX idx_users_email ON users (email);

-- Unique index (also enforces uniqueness)
CREATE UNIQUE INDEX idx_users_email ON users (email);

-- Multi-column index
CREATE INDEX idx_orders_date_status ON orders (order_date, status);

-- Drop an index
DROP INDEX idx_users_email;
```

Indexes are automatically used by the query optimizer when filtering with `=` on indexed columns. Unique indexes enforce uniqueness at INSERT time. Indexes are automatically maintained during INSERT, UPDATE, and DELETE operations.

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
- Schema pages for table metadata
- B-tree indexes on disk
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

### System Views

ViperSQL provides virtual system views that expose live server state. These are read-only and generated on demand.

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

```sql
-- Server statistics
SELECT * FROM sys.stats;

-- Database overview
SELECT * FROM sys.databases;

-- Table sizes
SELECT table_name, row_count FROM sys.tables ORDER BY row_count DESC;

-- All users
SELECT * FROM sys.users;
```

### Utility Commands

| Command | Description |
|---------|-------------|
| `SHOW TABLES` | List all tables in the current database |
| `SHOW DATABASES` | List all databases (current marked with `*`) |
| `SHOW USERS` | List all database users |
| `DESCRIBE table` | Show column names, types, and constraints |
| `VACUUM` | Remove deleted rows and reclaim space |
| `HELP` | Show available SQL commands |

Interactive shell meta-commands:

| Command | Description |
|---------|-------------|
| `.help` | Show meta-command help |
| `.tables` | List all tables |
| `.schema` | Show all table schemas |
| `.schema tablename` | Show schema for a specific table |
| `.quit` / `.exit` | Exit the shell |

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
├── PersistenceManager  -- SAVE, OPEN, CLOSE commands
├── CsvHandler          -- EXPORT, IMPORT commands
├── IndexManager        -- bucket-accelerated hash index lookups
├── SqlFunctions        -- 56+ built-in SQL functions
├── SqlWindow           -- window function evaluation
└── SystemViews         -- INFORMATION_SCHEMA and sys.* virtual views
```

Each helper holds an `Executor` reference (via Zia's circular bind support) for shared state access.

### Transaction Management

The executor provides ACID transaction support:

- **Atomicity**: Changes within a transaction are all-or-nothing. Statement-level atomicity ensures individual statements are atomic even outside explicit transactions.
- **Journal-based rollback**: INSERT, UPDATE, and DELETE operations record journal entries (with before-images for updates). ROLLBACK replays the journal in reverse.
- **FK cascade handling**: ON DELETE/UPDATE CASCADE, SET NULL, and RESTRICT actions are enforced during DELETE and UPDATE operations, with cascaded changes also tracked in the journal.
- **Deferred compaction**: During transactions, DELETE operations use soft-delete; compaction is deferred to COMMIT.

### Storage Engine

Persistent storage uses a page-based architecture inspired by traditional RDBMS designs:

```
StorageEngine
├── Pager            -- 4KB page I/O (read/write/allocate)
├── BufferPool       -- in-memory page cache with dirty tracking
├── SchemaPage       -- table metadata serialization
├── DataPage         -- slotted row storage with delete/compact
├── BTree            -- B-tree indexes on disk
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

### Session Architecture

ViperSQL supports per-connection isolation through the Session entity:

```
Session (per connection)
├── sessionId, clientHost, username   -- connection metadata
├── authenticated                     -- authentication state
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
- **PostgreSQL wire protocol (v3)**: Binary protocol on port 5432 for standard client compatibility (psql, ODBC, pgAdmin)
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
| RowDescription (T) | Server -> Client | Column names and types |
| DataRow (D) | Server -> Client | Row values |
| CommandComplete (C) | Server -> Client | Completion tag (e.g., "SELECT 3") |
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
| `test_pg_net_minimal.zia` | End-to-end PG network connectivity | Network integration |
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
├── index.zia             Index manager (hash-based lookups, 64 buckets)
├── result.zia            QueryResult entity (returned from all queries)
├── join.zia              JoinEngine -- cross join, hash join, join GROUP BY, sorting
├── setops.zia            Set operations (UNION, EXCEPT, INTERSECT)
├── csv.zia               CSV import/export handler
├── persistence.zia       Save/Open/Close -- SQL dump and .vdb persistence
├── server.zia            DatabaseServer -- multi-database management, user management
├── session.zia           Per-connection session (wraps Executor for multi-user)
├── system_views.zia      System views (INFORMATION_SCHEMA and sys.*)
├── sql_functions.zia     56+ built-in SQL functions (string, math, date/time, etc.)
├── sql_window.zia        Window function evaluation engine
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
│   ├── schema_page.zia   Schema page (table metadata persistence)
│   ├── btree.zia         B-tree index implementation
│   ├── btree_node.zia    B-tree node operations
│   ├── wal.zia           Write-ahead log manager (ARIES-style)
│   └── txn.zia           TransactionManager, TableLockManager (S/X locking)
│
└── tests/                49 test files with 2,100+ assertions
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
