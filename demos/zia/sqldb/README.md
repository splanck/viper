# ViperSQL

ViperSQL is a complete, PostgreSQL-compatible SQL database engine written entirely in [Zia](../../../docs/zia-getting-started.md) — the statically-typed, object-oriented language frontend for the Viper compiler toolchain. It is both a showcase project (demonstrating that Zia can handle serious systems-level work) and a usable database engine in its own right.

The engine implements a substantial subset of PostgreSQL SQL, including the full query pipeline from parser through optimizer to executor, a WAL-based storage layer with MVCC snapshot isolation, disk-based B-tree indexes, and a multi-user TCP server that speaks the PostgreSQL wire protocol. Standard clients — **psql**, **pgAdmin**, **ODBC drivers** — connect to it directly without modification.

It runs interpreted (via the Viper VM) or compiled to native **ARM64 / x86-64** machine code.

**60,100+ lines** of Zia · **4,985+ test assertions** · **109** source and test files

---

## Quick Start

```bash
# Build the toolchain first
./scripts/build_viper.sh

# Interactive SQL shell
viper run demos/zia/sqldb/main.zia

# Compile to a native binary
viper build demos/zia/sqldb/main.zia -o vipersql && ./vipersql

# Multi-user server (PostgreSQL wire protocol, port 5432)
viper run demos/zia/sqldb/server/tcp_server.zia

# Connect with psql
psql -h localhost -p 5432 -U admin -d main
```

Once the shell is running, you can start querying immediately:

```sql
CREATE TABLE products (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    name    TEXT NOT NULL,
    price   REAL,
    in_stock BOOLEAN DEFAULT TRUE
);

INSERT INTO products (name, price) VALUES
    ('Widget', 9.99), ('Gadget', 24.95), ('Doohickey', 4.50);

SELECT name, price FROM products WHERE price < 20 ORDER BY price DESC;
-- Doohickey | 4.5
-- Widget    | 9.99

SELECT in_stock, COUNT(*), AVG(price) FROM products GROUP BY in_stock;

SAVE 'mydb.vdb';   -- persist to disk
OPEN 'mydb.vdb';   -- reload on next run
```

---

## Features

### Core SQL
- **Data types:** INTEGER, REAL, TEXT, BOOLEAN, DATE, TIMESTAMP, JSON/JSONB, arrays, NULL
- **DDL:** CREATE/DROP/ALTER TABLE, CREATE/DROP VIEW, CREATE TABLE AS SELECT, CREATE TABLE LIKE, table partitioning (RANGE, LIST, HASH)
- **DML:** INSERT (multi-row, INSERT...SELECT, ON CONFLICT/UPSERT, RETURNING), UPDATE, DELETE, TRUNCATE, MERGE INTO
- **Queries:** SELECT with WHERE, GROUP BY, HAVING, ORDER BY, LIMIT/OFFSET, DISTINCT, DISTINCT ON, FETCH FIRST/OFFSET-FETCH
- **Joins:** INNER, LEFT, RIGHT, FULL OUTER, CROSS, multi-table, NATURAL JOIN, JOIN ... USING, LATERAL
- **Set operations:** UNION, UNION ALL, EXCEPT, EXCEPT ALL, INTERSECT, INTERSECT ALL (chained)
- **Subqueries:** scalar, IN, EXISTS/NOT EXISTS, derived tables, correlated, ANY/ALL/SOME
- **Transactions:** BEGIN/COMMIT/ROLLBACK, savepoints (SAVEPOINT/ROLLBACK TO/RELEASE), statement-level atomicity
- **Constraints:** PRIMARY KEY, AUTOINCREMENT, NOT NULL, UNIQUE, DEFAULT, REFERENCES (FK with ON DELETE/UPDATE CASCADE), CHECK, named constraints, composite PKs
- **Indexes:** single-column, composite, unique, partial (WHERE predicate), persistence across restarts

### Advanced SQL
- **Window functions:** ROW_NUMBER, RANK, DENSE_RANK, LAG, LEAD, FIRST_VALUE, LAST_VALUE, NTH_VALUE, NTILE, PERCENT_RANK, CUME_DIST; ROWS BETWEEN frame specs with EXCLUDE clause; named WINDOW clause
- **CTEs:** WITH (multiple), WITH RECURSIVE (fixpoint, hierarchy traversal, UNION ALL/UNION variants)
- **Aggregates:** COUNT, SUM, AVG, MIN, MAX, STRING_AGG, ARRAY_AGG, BOOL_AND/OR, COUNT(DISTINCT), FILTER clause
- **GROUPING SETS, ROLLUP, CUBE** for multi-level aggregation in a single query
- **Table inheritance** (INHERITS, ONLY, polymorphic queries), **generated columns** (GENERATED ALWAYS AS ... STORED)
- **Triggers:** BEFORE/AFTER INSERT/UPDATE/DELETE, FOR EACH ROW/STATEMENT
- **Sequences:** NEXTVAL/CURRVAL/SETVAL, CYCLE, ascending/descending with bounds
- **Stored functions:** CREATE FUNCTION with parameter substitution, overloading by arity; CALL statement
- **Cursors:** DECLARE/FETCH (absolute, relative, bidirectional)/CLOSE
- **Prepared statements:** PREPARE/EXECUTE/DEALLOCATE
- **Arrays:** ARRAY[] literals, ARRAY_AGG, 9 array manipulation functions
- **Materialized views:** CREATE/REFRESH/DROP MATERIALIZED VIEW
- **JSON/JSONB:** JSON_EXTRACT (JSONPath), JSON_BUILD_OBJECT/ARRAY, JSON_VALID, CAST to/from JSON
- **Regular expressions:** `~` operator, SIMILAR TO, REGEXP_MATCH/REPLACE/COUNT/SPLIT/SUBSTR
- **60+ date/time functions:** DATE_PART, DATE_TRUNC, AGE, TO_CHAR, MAKE_DATE, interval arithmetic
- **Extended string functions:** TRANSLATE, OVERLAY, MD5, SHA256, LPAD/RPAD and more

### Storage & Transactions
- **MVCC snapshot isolation:** xmin/xmax row versioning — readers never block writers
- **WAL-based crash recovery:** ARIES-style redo/undo with CLRs; survives unclean shutdown
- **Disk-based B-tree indexes:** order-50 (~99 keys/page), BufferPool with LRU page cache
- **Row-level locking:** FOR UPDATE / FOR SHARE with NOWAIT and SKIP LOCKED
- **Table-level S/X locking** with deadlock-safe acquisition order
- **Persistence:** binary `.vdb` format, SQL dump (SAVE/OPEN), CSV import/export (COPY TO/FROM)
- **VACUUM / ANALYZE** for dead-row reclamation and statistics updates

### Server & Protocol
- **Multi-user TCP server:** thread-per-connection, PostgreSQL wire protocol v3 (port 5432) and simple text protocol (port 5433)
- **Extended Query protocol:** Parse/Bind/Describe/Execute for prepared statements and parameterized queries
- **Multi-database:** CREATE DATABASE, USE, DROP DATABASE; each database has its own storage file
- **Session isolation:** per-connection session state, temporary tables, transaction isolation
- **User management:** CREATE/DROP/ALTER USER (password hashing), table-level GRANT/REVOKE
- **System views:** INFORMATION_SCHEMA, sys.* (databases, tables, stats, vacuum_stats), pg_catalog (pg_class, pg_attribute, pg_type, pg_stat_activity, pg_stat_user_tables, pg_stat_user_indexes)
- **EXPLAIN ANALYZE**, DO blocks, session variables (SET/SHOW/RESET), TABLESAMPLE
- **Compatible clients:** psql, pgAdmin, ODBC (psqlODBC/unixODBC), and any PostgreSQL v3 driver

---

## Architecture Overview

The query pipeline goes: **SQL text → Lexer → Parser → AST → Semantic Analysis → Optimizer → Executor → Storage**. Each SELECT query walks through expression evaluation, join ordering, and predicate pushdown before hitting the storage layer. The executor is composed of modular operator nodes (scan, filter, join, aggregate, sort, limit) that form a tree — each node pulls rows from its children.

The storage engine is independent of the query layer: it manages pages, buffer pools, B-tree index traversal, and the WAL. MVCC is implemented at the row level with xmin/xmax transaction IDs; snapshot visibility is computed on read without taking locks. The TCP server runs one thread per connection, each with its own session and transaction context. See [Architecture](docs/architecture.md) for the full breakdown.

---

## Documentation

| Document | Contents |
|----------|----------|
| [Getting Started](docs/getting-started.md) | Build, run the REPL, run from a script, compile native; full psql and ODBC connection setup; 7-step hands-on tutorial |
| [SQL Reference](docs/sql-reference.md) | Complete reference for data types, DDL, DML, query clauses, operators, built-in functions, date/time functions, aggregates, regex, JSON, all join types, subqueries, set operations, indexes, and constraints |
| [Advanced SQL](docs/advanced-sql.md) | Window functions and frame specs, CTEs and recursive CTEs, triggers, sequences, stored functions, table inheritance, generated columns, LATERAL joins, GROUPING SETS/ROLLUP/CUBE, DISTINCT ON, MVCC details, row-level locking, MERGE INTO, partial indexes, and more |
| [Server](docs/server.md) | Multi-database, persistence and binary storage, CSV import/export, user management, GRANT/REVOKE privileges, all system views, VACUUM/ANALYZE, DO blocks, session variables, system functions |
| [Architecture](docs/architecture.md) | Query pipeline stages, executor operator model, transaction management, storage engine internals, concurrency model, session architecture, TCP server design, PostgreSQL wire protocol, query optimizer |
| [Testing](docs/testing.md) | Running the test suite, compiling and testing native builds, full reference of all 104 test files with descriptions, directory structure |
