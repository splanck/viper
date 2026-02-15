# ViperSQL

A SQL database engine written entirely in [Zia](../../../docs/zia-guide.md), the high-level language frontend for the Viper compiler toolchain. ViperSQL implements a substantial subset of SQL including DDL, DML, joins, subqueries, aggregations, indexes, transactions, persistent storage, and a wire-protocol server.

**~30,000 lines of Zia** across 57 source files, with 648+ automated tests.

## Running

```bash
# Interactive SQL shell
viper run demos/zia/sqldb/main.zia

# Run test suite
viper run demos/zia/sqldb/tests/test.zia
viper run demos/zia/sqldb/tests/test_advanced.zia
```

## Directory Structure

```
sqldb/
├── main.zia              Entry point — interactive SQL shell (REPL)
├── executor.zia          Query executor — dispatches parsed SQL to handlers
├── parser.zia            Recursive-descent SQL parser
├── lexer.zia             SQL tokenizer
├── token.zia             Token type definitions
├── stmt.zia              Statement AST node types
├── expr.zia              Expression AST node types
├── types.zia             Core SQL value types (Integer, Real, Text, Null)
├── schema.zia            Column and Row definitions
├── table.zia             Table entity (row storage, column metadata)
├── database.zia          Database entity (table registry)
├── index.zia             Index manager (B-tree backed lookups)
├── result.zia            QueryResult entity (returned from all queries)
├── join.zia              JoinEngine — cross join, join GROUP BY, sorting
├── setops.zia            Set operations (UNION, EXCEPT, INTERSECT)
├── csv.zia               CSV import/export handler
├── persistence.zia       Save/Open/Close — SQL dump and .vdb persistence
├── server.zia            Server command handler (START/STOP SERVER)
│
├── optimizer/
│   └── optimizer.zia     Query optimizer (index selection, predicate pushdown)
│
├── server/
│   ├── sql_server.zia    PostgreSQL wire protocol server
│   └── sql_client.zia    Wire protocol client
│
├── storage/
│   ├── engine.zia        StorageEngine — top-level persistent storage API
│   ├── pager.zia         Page-based file I/O (4KB pages)
│   ├── buffer.zia        BinaryBuffer — byte-level serialization
│   ├── serializer.zia    Row/value binary serialization
│   ├── page.zia          Page type definitions and constants
│   ├── data_page.zia     Slotted data page (row storage)
│   ├── schema_page.zia   Schema page (table metadata persistence)
│   ├── btree.zia         B-tree index implementation
│   ├── btree_node.zia    B-tree node operations
│   ├── wal.zia           Write-ahead log manager
│   └── txn.zia           Transaction manager (BEGIN/COMMIT/ROLLBACK)
│
└── tests/
    ├── test_common.zia   Shared test harness (assert, check, printResults)
    ├── test.zia           Core SQL tests (CREATE, INSERT, SELECT, UPDATE, DELETE)
    ├── test_advanced.zia  UNION, EXCEPT, INTERSECT, CASE expressions
    ├── test_btree.zia     B-tree unit tests
    ├── test_constraints.zia  NOT NULL, PRIMARY KEY, UNIQUE, FOREIGN KEY
    ├── test_engine.zia    StorageEngine integration tests
    ├── test_features.zia  INDEX, functions, arithmetic, ORDER BY, LIMIT
    ├── test_functions.zia SQL functions (string, math, conditional)
    ├── test_index.zia     Index creation and lookup
    ├── test_multidb.zia   Multi-database (CREATE/USE/DROP DATABASE)
    ├── test_optimizer.zia Query optimizer tests
    ├── test_persistence.zia  Persistent .vdb storage round-trips
    ├── test_server.zia    Wire protocol server unit tests
    ├── test_storage.zia   Binary storage primitives
    ├── test_stress2.zia   Stress tests (large datasets, persistence)
    ├── test_stress3.zia   Multi-database persistence stress tests
    ├── test_subquery.zia  Scalar and IN subqueries
    ├── test_txn.zia       Transaction manager tests
    └── test_wal.zia       Write-ahead log tests
```

## Architecture

### Query Pipeline

```
SQL text → Lexer → Parser → AST (Stmt/Expr) → Executor → QueryResult
```

1. **Lexer** (`lexer.zia`) tokenizes input into `Token` values
2. **Parser** (`parser.zia`) builds an AST of `Stmt` and `Expr` nodes
3. **Executor** (`executor.zia`) walks the AST and executes against the in-memory model
4. Results are returned as `QueryResult` entities with rows and column names

### Executor Composition

The executor delegates to helper entities for complex operations:

```
Executor
├── JoinEngine        — cross joins, join GROUP BY, join sorting
├── PersistenceManager — SAVE, OPEN, CLOSE commands
├── CsvHandler        — EXPORT, IMPORT commands
└── IndexManager      — index-accelerated lookups
```

Each helper holds an `Executor` reference (circular bind) for shared state access.

### Storage Engine

Persistent storage uses a page-based architecture:

```
StorageEngine
├── Pager           — 4KB page I/O (read/write/allocate)
├── BufferPool      — in-memory page cache with dirty tracking
├── SchemaPage      — table metadata serialization
├── DataPage        — slotted row storage with delete/compact
├── BTree           — B-tree indexes on disk
├── WALManager      — write-ahead log for crash recovery
└── TxnManager      — transaction lifecycle (BEGIN/COMMIT/ROLLBACK)
```

### SQL Features

| Category | Features |
|----------|----------|
| DDL | CREATE/DROP TABLE, CREATE/DROP INDEX, ALTER TABLE |
| DML | INSERT, SELECT, UPDATE, DELETE (single and multi-row) |
| Queries | WHERE, ORDER BY, GROUP BY, HAVING, LIMIT/OFFSET, DISTINCT |
| Joins | CROSS JOIN, INNER JOIN, LEFT/RIGHT/FULL OUTER JOIN |
| Set Ops | UNION, UNION ALL, EXCEPT, INTERSECT |
| Subqueries | Scalar subqueries, IN subqueries |
| Expressions | CASE/WHEN, BETWEEN, LIKE, IN, IS NULL |
| Aggregates | COUNT, SUM, AVG, MIN, MAX |
| Functions | UPPER, LOWER, LENGTH, SUBSTR, TRIM, ABS, COALESCE, IFNULL, IIF |
| Constraints | PRIMARY KEY, AUTOINCREMENT, NOT NULL, UNIQUE, FOREIGN KEY, DEFAULT |
| Persistence | SQL dump (SAVE/OPEN), binary .vdb files, WAL |
| Multi-DB | CREATE/USE/DROP DATABASE, database isolation |
| Server | PostgreSQL wire protocol (START/STOP SERVER) |

## Testing

All tests use a shared harness (`tests/test_common.zia`) providing `assert()`, `check()`, `assertTrue()`, `assertFalse()`, and `printResults()`.

```bash
# Run individual test suites
viper run demos/zia/sqldb/tests/test.zia
viper run demos/zia/sqldb/tests/test_advanced.zia
viper run demos/zia/sqldb/tests/test_persistence.zia

# Run stress tests
viper run demos/zia/sqldb/tests/test_stress2.zia
viper run demos/zia/sqldb/tests/test_stress3.zia
```
