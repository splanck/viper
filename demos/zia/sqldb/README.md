# ViperSQL

A complete SQL database engine written entirely in [Zia](../../../docs/zia-guide.md), the high-level language frontend for the Viper compiler toolchain. ViperSQL implements a substantial subset of SQL including DDL, DML, joins, subqueries, aggregations, indexes, persistent storage, CSV import/export, multi-database support, and a wire-protocol server.

**24,500+ lines of Zia** across 62 source files, with **1,000+ automated test assertions** across 30 test files.

Runs both interpreted (via the Viper VM) and compiled to native ARM64/x86-64 machine code.

---

## Table of Contents

- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Running the Interactive Shell](#running-the-interactive-shell)
  - [Running from a Script](#running-from-a-script)
  - [Compiling to Native Code](#compiling-to-native-code)
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
  - [Aggregate Functions](#aggregate-functions)
  - [Joins](#joins)
  - [Subqueries](#subqueries)
  - [Set Operations](#set-operations)
  - [Indexes](#indexes)
  - [Constraints](#constraints)
  - [Multi-Database](#multi-database)
  - [Persistence and Import/Export](#persistence-and-importexport)
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

#### NULL Handling Functions

| Function | Description | Example | Result |
|----------|-------------|---------|--------|
| `COALESCE(a, b, ...)` | First non-NULL argument | `COALESCE(NULL, NULL, 'found')` | `found` |
| `IFNULL(expr, default)` | Default if NULL | `IFNULL(NULL, 'default')` | `default` |
| `NULLIF(a, b)` | NULL if a equals b | `NULLIF(5, 5)` | `NULL` |
| `IIF(cond, true_val, false_val)` | Inline conditional | `IIF(age > 18, 'adult', 'minor')` | depends |
| `TYPEOF(expr)` | Returns type name as string | `TYPEOF(42)` | `integer` |

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

Indexes are automatically used by the query optimizer when filtering with `=` on indexed columns.

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

Multiple constraints can be combined:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    email TEXT NOT NULL UNIQUE,
    role TEXT DEFAULT 'user',
    manager_id INTEGER REFERENCES users(id)
);
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

### Utility Commands

| Command | Description |
|---------|-------------|
| `SHOW TABLES` | List all tables in the current database |
| `SHOW DATABASES` | List all databases (current marked with `*`) |
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
└── IndexManager        -- hash-based index lookups
```

Each helper holds an `Executor` reference (via Zia's circular bind support) for shared state access.

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

### Query Optimizer

The optimizer (`optimizer/optimizer.zia`) provides cost-based query optimization:

- Table statistics (row counts, distinct value estimates)
- Access path selection (table scan vs. index scan vs. index seek)
- Cost estimation with configurable selectivity constants
- Automatic index selection for equality predicates

### Wire Protocol Server

The `server/` module implements a PostgreSQL-compatible wire protocol:

- Message types: Query, Terminate, Ready, Error, RowDescription, DataRow, CommandComplete, Authentication
- Default port: 5432
- Allows ViperSQL to accept connections from standard PostgreSQL clients

---

## Testing

All tests use a shared harness (`tests/test_common.zia`) providing `assert()`, `check()`, `assertTrue()`, `assertFalse()`, and `printResults()`.

### Running Tests

```bash
# Core SQL functionality
viper run demos/zia/sqldb/tests/test.zia
viper run demos/zia/sqldb/tests/test_advanced.zia

# Specific feature areas
viper run demos/zia/sqldb/tests/test_constraints.zia
viper run demos/zia/sqldb/tests/test_functions.zia
viper run demos/zia/sqldb/tests/test_subquery.zia
viper run demos/zia/sqldb/tests/test_index.zia
viper run demos/zia/sqldb/tests/test_features.zia

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
viper run demos/zia/sqldb/tests/test_stress2.zia
viper run demos/zia/sqldb/tests/test_stress3.zia
viper run demos/zia/sqldb/tests/test_native_stress.zia
viper run demos/zia/sqldb/tests/test_native_edge.zia
viper run demos/zia/sqldb/tests/test_native_torture.zia
viper run demos/zia/sqldb/tests/test_native_extreme.zia

# Documentation examples (verifies all README examples)
viper run demos/zia/sqldb/tests/test_readme_examples.zia
```

### Compiling and Running Tests Natively

All tests also pass when compiled to native ARM64 machine code:

```bash
viper build demos/zia/sqldb/tests/test.zia -o /tmp/test_sql && /tmp/test_sql
viper build demos/zia/sqldb/tests/test_native_torture.zia -o /tmp/test_torture && /tmp/test_torture
```

### Test Suites

| Test File | Focus Area | Assertions |
|-----------|------------|------------|
| `test.zia` | Core CRUD operations | Basic SQL |
| `test_advanced.zia` | UNION, EXCEPT, INTERSECT, CASE | Set operations |
| `test_constraints.zia` | NOT NULL, UNIQUE, PK, FK, DEFAULT | Data integrity |
| `test_functions.zia` | String, math, null-handling functions | Built-in functions |
| `test_subquery.zia` | Scalar and IN subqueries | Nested queries |
| `test_index.zia` | Index creation, lookup, unique indexes | Index operations |
| `test_features.zia` | ORDER BY, LIMIT, arithmetic, misc features | Query features |
| `test_multidb.zia` | CREATE/USE/DROP DATABASE | Multi-database |
| `test_optimizer.zia` | Cost estimation, access path selection | Query optimizer |
| `test_persistence.zia` | SAVE/OPEN round-trips, .vdb files | Persistence |
| `test_storage.zia` | Binary buffers, serialization | Storage primitives |
| `test_btree.zia` | B-tree insert, search, split | B-tree index |
| `test_engine.zia` | StorageEngine integration | Storage engine |
| `test_wal.zia` | Write-ahead log operations | WAL |
| `test_txn.zia` | Transaction manager | Transactions |
| `test_server.zia` | Wire protocol server | Network server |
| `test_stress2.zia` | Large datasets, persistence stress | Stress testing |
| `test_stress3.zia` | Multi-database persistence | Stress testing |
| `test_stress4_native.zia` | Native codegen correctness | Native stress |
| `test_native_stress.zia` | 15 native stress tests (73 assertions) | Native codegen |
| `test_native_edge.zia` | 10 edge case tests (68 assertions) | Edge cases |
| `test_native_torture.zia` | 16 torture tests (75 assertions) | Complex queries |
| `test_native_extreme.zia` | 12 extreme tests (54 assertions) | Extreme scenarios |
| `test_readme_examples.zia` | All README documentation examples (129 assertions) | Documentation |

---

## Directory Structure

```
sqldb/
├── main.zia              Entry point -- interactive SQL shell (REPL)
├── executor.zia          Query executor -- dispatches parsed SQL to handlers
├── parser.zia            Recursive-descent SQL parser
├── lexer.zia             SQL tokenizer
├── token.zia             Token type definitions
├── stmt.zia              Statement AST node types
├── expr.zia              Expression AST node types
├── types.zia             Core SQL value types (Integer, Real, Text, Null)
├── schema.zia            Column and Row definitions
├── table.zia             Table entity (row storage, column metadata)
├── database.zia          Database entity (table registry)
├── index.zia             Index manager (hash-based lookups)
├── result.zia            QueryResult entity (returned from all queries)
├── join.zia              JoinEngine -- cross join, join GROUP BY, sorting
├── setops.zia            Set operations (UNION, EXCEPT, INTERSECT)
├── csv.zia               CSV import/export handler
├── persistence.zia       Save/Open/Close -- SQL dump and .vdb persistence
├── server.zia            Multi-database server (manages database instances)
│
├── optimizer/
│   └── optimizer.zia     Query optimizer (cost-based, index selection)
│
├── server/
│   ├── sql_server.zia    PostgreSQL wire protocol server
│   └── sql_client.zia    Wire protocol client
│
├── storage/
│   ├── engine.zia        StorageEngine -- top-level persistent storage API
│   ├── pager.zia         Page-based file I/O (4KB pages)
│   ├── buffer.zia        BinaryBuffer -- byte-level serialization
│   ├── serializer.zia    Row/value binary serialization
│   ├── page.zia          Page type definitions and constants
│   ├── data_page.zia     Slotted data page (row storage)
│   ├── schema_page.zia   Schema page (table metadata persistence)
│   ├── btree.zia         B-tree index implementation
│   ├── btree_node.zia    B-tree node operations
│   ├── wal.zia           Write-ahead log manager
│   └── txn.zia           Transaction manager (BEGIN/COMMIT/ROLLBACK)
│
└── tests/                30 test files with 1,000+ assertions
    ├── test_common.zia   Shared test harness
    ├── test.zia          Core SQL tests
    ├── test_advanced.zia Set operations and CASE
    ├── ...               (see Testing section for full list)
    └── test_readme_examples.zia  Verifies all README examples
```
