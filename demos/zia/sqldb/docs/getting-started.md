# Getting Started with ViperSQL

## Table of Contents

- [Prerequisites](#prerequisites)
- [Running the Interactive Shell](#running-the-interactive-shell)
- [Running from a Script](#running-from-a-script)
- [Compiling to Native Code](#compiling-to-native-code)
- [Connecting to the Server](#connecting-to-the-server)
  - [Starting the Server](#starting-the-server)
  - [Connecting via psql](#connecting-via-psql)
  - [Connecting via ODBC](#connecting-via-odbc)
  - [Compatible Clients](#compatible-clients)
- [Tutorial](#tutorial)
  - [Step 1: Create a Table](#step-1-create-a-table)
  - [Step 2: Insert Data](#step-2-insert-data)
  - [Step 3: Query Data](#step-3-query-data)
  - [Step 4: Update and Delete](#step-4-update-and-delete)
  - [Step 5: Aggregations](#step-5-aggregations)
  - [Step 6: Joins](#step-6-joins)
  - [Step 7: Save Your Work](#step-7-save-your-work)

---

## Prerequisites

Build the Viper toolchain from the repository root:

```bash
# macOS / Linux
./scripts/build_viper.sh

# Windows
scripts\build_viper.cmd
```

This builds the toolchain, runs all tests, and installs the `viper` CLI to `/usr/local/bin`.

## Running the Interactive Shell

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

## Running from a Script

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

## Compiling to Native Code

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

### Compatible Clients

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
