# Server Features

## Table of Contents

- [Multi-Database](#multi-database)
- [Persistence and Import/Export](#persistence-and-importexport)
- [User Management](#user-management)
- [Privileges (GRANT/REVOKE)](#privileges-grantrevoke)
- [System Views](#system-views)
- [Utility Commands](#utility-commands)
- [VACUUM and ANALYZE](#vacuum-and-analyze)
- [pg_stat Views](#pg_stat-views)
- [DO Blocks](#do-blocks)
- [Session Variables](#session-variables)
- [System Functions](#system-functions)
- [Connecting to the Server](#connecting-to-the-server)

---

## Multi-Database

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

---

## Persistence and Import/Export

### SQL Dump (SAVE / OPEN)

Save the entire current database as a SQL script:

```sql
SAVE 'backup.sql';
```

Restore from a SQL dump:

```sql
OPEN 'backup.sql';
```

The dump file contains `CREATE TABLE` and `INSERT` statements that recreate the database.

### Binary Storage (.vdb)

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

### Persistent Database via CREATE DATABASE

Create a database backed by a file from the start:

```sql
CREATE DATABASE inventory FILE 'inventory.vdb';
USE inventory;
-- All operations are automatically persisted
```

### CSV Import/Export

Export a table to CSV:

```sql
EXPORT products TO 'products.csv';
```

Import CSV data into an existing table:

```sql
IMPORT INTO products FROM 'products.csv';
```

The CSV handler supports quoted fields, escaped quotes, and automatic type detection based on the target table's column types.

---

## User Management

ViperSQL supports user management with password authentication. A default superuser `admin` (password: `admin`) is created on server startup.

### CREATE USER

```sql
CREATE USER alice PASSWORD 'secret123';
```

### DROP USER

```sql
DROP USER alice;
```

The `admin` user cannot be dropped.

### ALTER USER (Change Password)

```sql
ALTER USER alice PASSWORD 'newpassword';

-- Alternative syntax
ALTER USER alice SET PASSWORD 'newpassword';
```

### SHOW USERS

```sql
SHOW USERS;
```

Returns a list of all users. User information is also available via the `sys.users` system view:

```sql
SELECT * FROM sys.users;
```

Returns `username` and `is_superuser` columns.

---

## Privileges (GRANT/REVOKE)

ViperSQL implements a table-level privilege system with ownership. The user who creates a table is the **owner** and has full access. Other users need explicit GRANT to access the table. The `admin` superuser bypasses all privilege checks.

### GRANT

```sql
-- Grant specific privileges
GRANT SELECT ON employees TO alice;
GRANT INSERT, UPDATE ON employees TO bob;
GRANT ALL ON employees TO charlie;
GRANT ALL PRIVILEGES ON employees TO dave;

-- Grant to all users
GRANT SELECT ON public_data TO PUBLIC;
```

### REVOKE

```sql
REVOKE SELECT ON employees FROM alice;
REVOKE ALL ON employees FROM bob;
REVOKE SELECT ON public_data FROM PUBLIC;
```

### Privilege Types

| Privilege | Allows |
|-----------|--------|
| SELECT | Read rows from the table |
| INSERT | Insert new rows |
| UPDATE | Modify existing rows |
| DELETE | Remove rows |
| ALL | All of the above |

### Ownership Rules

- Table creator is the owner; owner has implicit ALL privileges
- Only the owner or superuser (admin) can GRANT/REVOKE, DROP TABLE, ALTER TABLE, CREATE/DROP INDEX
- `DROP TABLE` removes all associated privileges
- `DROP USER` removes all privileges granted to that user

### SHOW GRANTS

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

---

## System Views

ViperSQL provides virtual system views that expose live server state. These are read-only and generated on demand. Three schema families are supported: `INFORMATION_SCHEMA`, `sys.*`, and `pg_catalog`.

### INFORMATION_SCHEMA Views

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

### sys.* Views

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

### pg_catalog Views

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

---

## Utility Commands

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

---

## VACUUM and ANALYZE

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

---

## pg_stat Views

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

---

## DO Blocks

Execute multiple SQL statements as a block:

```sql
-- Execute multiple statements in a single DO block
DO 'CREATE TABLE temp1 (id INTEGER); INSERT INTO temp1 VALUES (1); INSERT INTO temp1 VALUES (2)';

-- Useful for setup scripts and multi-statement operations
DO 'DROP TABLE IF EXISTS old_data; CREATE TABLE new_data (id INTEGER, val TEXT)';
```

---

## Session Variables

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

---

## System Functions

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

### Connecting via ODBC

ViperSQL works with the **psqlODBC** driver via unixODBC. See [getting-started.md](getting-started.md#connecting-via-odbc) for full ODBC setup instructions.

### Compatible Clients

Any PostgreSQL-compatible client should work, including:
- **psql** (PostgreSQL interactive terminal)
- **psqlODBC** (via unixODBC)
- **pgAdmin** (GUI tool)
- Programming language drivers (Python `psycopg2`/`psycopg3`, Node.js `pg`, Go `pgx`, Java JDBC)

ViperSQL supports both the **Simple Query** protocol (Q messages) and the **Extended Query** protocol (Parse/Bind/Describe/Execute) for prepared statements and parameterized queries.
