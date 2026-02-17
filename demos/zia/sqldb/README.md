# ViperSQL

A complete SQL database engine written entirely in [Zia](../../../docs/zia-guide.md), the high-level language frontend for the Viper compiler toolchain. ViperSQL implements a broad subset of PostgreSQL-compatible SQL — from DDL and DML through window functions, CTEs, triggers, sequences, MVCC, and a full multi-user TCP server speaking the PostgreSQL wire protocol. It spans 60,100+ lines of Zia across 109 source and test files, with 4,985+ automated test assertions. Runs both interpreted (via the Viper VM) and compiled to native ARM64/x86-64 machine code.

---

## Features

### Core SQL
- Data types: INTEGER, REAL, TEXT, BOOLEAN, DATE, TIMESTAMP, JSON/JSONB, NULL
- DDL: CREATE/DROP/ALTER TABLE, CREATE/DROP VIEW, CREATE TABLE AS SELECT, CREATE TABLE LIKE, table partitioning (RANGE, LIST, HASH)
- DML: INSERT (multi-row, INSERT...SELECT, ON CONFLICT/UPSERT, RETURNING), UPDATE, DELETE, TRUNCATE, MERGE INTO
- Queries: SELECT with WHERE, GROUP BY, HAVING, ORDER BY, LIMIT/OFFSET, DISTINCT, DISTINCT ON
- Joins: INNER, LEFT, RIGHT, FULL OUTER, CROSS, multi-table, NATURAL JOIN, JOIN ... USING, LATERAL
- Set operations: UNION, UNION ALL, EXCEPT, EXCEPT ALL, INTERSECT, INTERSECT ALL (chained)
- Subqueries: scalar, IN, EXISTS/NOT EXISTS, derived tables, correlated
- Transactions: BEGIN/COMMIT/ROLLBACK, savepoints, statement-level atomicity
- Constraints: PRIMARY KEY, AUTOINCREMENT, NOT NULL, UNIQUE, DEFAULT, REFERENCES (FK), CHECK, named constraints, composite PKs
- Indexes: single-column, composite, unique, partial (WHERE predicate), persistence across restarts

### Advanced SQL
- Window functions: ROW_NUMBER, RANK, DENSE_RANK, LAG, LEAD, FIRST_VALUE, LAST_VALUE, NTH_VALUE, NTILE, PERCENT_RANK, CUME_DIST; ROWS BETWEEN frame specs; EXCLUDE clause; named WINDOW clause
- Common Table Expressions: WITH, multiple CTEs, WITH RECURSIVE (fixpoint, hierarchy traversal, UNION/UNION ALL)
- Aggregates: COUNT, SUM, AVG, MIN, MAX, STRING_AGG, ARRAY_AGG, BOOL_AND/OR, COUNT(DISTINCT), FILTER clause
- GROUPING SETS, ROLLUP, CUBE for multi-level aggregation
- Table inheritance (INHERITS, ONLY), generated columns (GENERATED ALWAYS AS ... STORED)
- Triggers (BEFORE/AFTER INSERT/UPDATE/DELETE, FOR EACH ROW/STATEMENT)
- Sequences (NEXTVAL/CURRVAL/SETVAL, CYCLE, ascending/descending with bounds)
- Stored functions (CREATE FUNCTION with parameter substitution, overloading by arity)
- Cursors (DECLARE/FETCH bidirectional/CLOSE), prepared statements (PREPARE/EXECUTE/DEALLOCATE)
- Arrays (ARRAY[], ARRAY_AGG, 9 array functions), materialized views (CREATE/REFRESH/DROP)
- JSON/JSONB (JSON_EXTRACT JSONPath, JSON_BUILD_OBJECT/ARRAY, JSON_VALID, CAST to JSON)
- Regular expressions (~ operator, SIMILAR TO, REGEXP_MATCH/REPLACE/COUNT/SPLIT/SUBSTR)
- Row value constructors, ANY/ALL/SOME quantified comparisons, BETWEEN SYMMETRIC, TABLESAMPLE

### Storage & Transactions
- MVCC snapshot isolation (xmin/xmax row versioning, readers never block writers)
- WAL-based crash recovery (ARIES-style redo/undo with CLR)
- Disk-based B-tree indexes (order-50, ~99 keys/page) with BufferPool LRU page cache
- Row-level locking: FOR UPDATE/FOR SHARE with NOWAIT and SKIP LOCKED
- Table-level S/X locking with deadlock-safe lock hierarchy
- Persistent binary storage (.vdb), SQL dump (SAVE/OPEN), CSV import/export (COPY TO/FROM)

### Server & Protocol
- Multi-user TCP server: thread-per-connection, PostgreSQL wire protocol v3 (port 5432) and simple text protocol (port 5433)
- Extended Query protocol (Parse/Bind/Describe/Execute) for prepared statements and parameterized queries
- Multi-database support (CREATE DATABASE, USE), session isolation, temporary tables
- User management (CREATE/DROP/ALTER USER) and table-level GRANT/REVOKE privileges
- System views: INFORMATION_SCHEMA, sys.* (databases, tables, stats, vacuum_stats), pg_catalog (pg_class, pg_attribute, pg_type, pg_stat_activity, pg_stat_user_tables/indexes)
- VACUUM, ANALYZE, EXPLAIN ANALYZE, DO blocks, session variables (SET/SHOW/RESET)
- Connect with psql, ODBC (psqlODBC/unixODBC), pgAdmin, or any PostgreSQL-compatible driver

---

## Quick Start

```bash
# Build the toolchain
./scripts/build_viper.sh

# Run the interactive REPL
viper run demos/zia/sqldb/main.zia

# Or compile to a native binary
viper build demos/zia/sqldb/main.zia -o vipersql && ./vipersql

# Run the multi-user server (PostgreSQL wire protocol on port 5432)
viper run demos/zia/sqldb/server/tcp_server.zia
# Connect: psql -h localhost -p 5432 -U admin -d main
```

---

## Documentation

- [Getting Started](docs/getting-started.md) — Prerequisites, running the REPL, connecting via psql/ODBC, full tutorial (Steps 1-7)
- [SQL Reference](docs/sql-reference.md) — Data types, DDL, DML, query clauses, operators, built-in functions, date/time, aggregates, regex, JSON, joins, subqueries, set operations, indexes, constraints
- [Advanced SQL](docs/advanced-sql.md) — Window functions, CTEs, recursive CTEs, triggers, sequences, stored functions, table inheritance, generated columns, LATERAL joins, GROUPING SETS, DISTINCT ON, MVCC, MERGE INTO, partial indexes, and more
- [Server](docs/server.md) — Multi-database, persistence, user management, GRANT/REVOKE, system views, VACUUM/ANALYZE, DO blocks, session variables, system functions
- [Architecture](docs/architecture.md) — Query pipeline, executor composition, transaction management, storage engine, concurrency model, session architecture, TCP server, query optimizer
- [Testing](docs/testing.md) — Running tests, native compilation testing, full test suite reference, directory structure

---

## Stats

- **60,100+ lines** of Zia source code
- **4,985+ test assertions** across 104 test files
- **109** source and test files
