# ViperSQL Development Roadmap

**Status:** Active Development
**Current Stats:** ~40,000 lines of Zia | 94 source files | 52 test files | 2,290+ test assertions

---

## Completed Phases

### Phase 1: Core SQL Engine
**Status:** DONE

- SQL lexer and parser (tokenizer, recursive-descent parser)
- DDL: CREATE TABLE, DROP TABLE, ALTER TABLE (ADD/DROP/RENAME COLUMN)
- DML: INSERT, SELECT, UPDATE, DELETE
- WHERE clause with comparison operators
- ORDER BY, LIMIT, OFFSET
- Column constraints: PRIMARY KEY, NOT NULL, UNIQUE, DEFAULT, AUTOINCREMENT
- Data types: INTEGER, TEXT, REAL
- Expression evaluation (arithmetic, string concatenation, LIKE)
- REPL interactive shell
- SQL dump persistence (SAVE/OPEN/CLOSE)

**Test files:** test_phase1.zia (105 assertions), test_basic_crud.zia

---

### Phase 2: Aggregations, Subqueries, Views
**Status:** DONE

- Aggregate functions: COUNT, SUM, AVG, MIN, MAX
- GROUP BY and HAVING clauses
- Subqueries (scalar, IN, EXISTS/NOT EXISTS)
- Correlated subqueries
- Views (CREATE VIEW, DROP VIEW, SELECT from views)
- DISTINCT
- INSERT...SELECT
- Derived tables (subqueries in FROM)
- CASE expressions
- CAST expressions
- BETWEEN, IN (value list)

**Test files:** test_phase2.zia (169 assertions), test_subquery.zia, test_sql_features.zia

---

### Phase 3: Transactions
**Status:** DONE

- BEGIN / COMMIT / ROLLBACK
- Row-level undo via transaction log
- Auto-rollback on errors within transactions
- Nested BEGIN detection (error)

**Test files:** test_phase3_txn.zia (110 assertions), test_phase3.zia, test_txn.zia

---

### Phase 4: Functions, CTEs, Window Functions, Multi-table Ops
**Status:** DONE

- Built-in functions: UPPER, LOWER, LENGTH, SUBSTR, REPLACE, TRIM, COALESCE, NULLIF, ABS, ROUND, TYPEOF, IFNULL, INSTR, HEX, QUOTE, RANDOM, PRINTF
- Date/time functions: DATE, TIME, DATETIME, JULIANDAY, STRFTIME with modifiers
- Window functions: ROW_NUMBER, RANK, DENSE_RANK, SUM/AVG/COUNT/MIN/MAX OVER, LAG, LEAD, FIRST_VALUE, LAST_VALUE, NTILE, PERCENT_RANK, CUME_DIST
- Common Table Expressions (WITH ... AS)
- Multi-table UPDATE and DELETE
- CHECK constraints

**Test files:** test_phase4_functions.zia (58), test_phase4_cte.zia (49), test_phase4_multitable.zia (62), test_phase4_window.zia, test_phase4_datetime.zia, test_functions.zia, test_constraints.zia

---

### Phase 5: Joins, Optimizer, Indexes
**Status:** DONE

- Join types: INNER JOIN, LEFT/RIGHT/FULL OUTER JOIN, CROSS JOIN, NATURAL JOIN
- Hash join implementation (probe/build phases)
- Join order optimization (cost-based)
- In-memory hash indexes (64-bucket, CREATE INDEX / DROP INDEX)
- Index-accelerated equality and range lookups
- Query optimizer with selectivity estimation
- Set operations: UNION, UNION ALL, INTERSECT, EXCEPT

**Test files:** test_phase5_hashjoin.zia (129), test_phase5_optimizer.zia (105), test_phase5_range.zia (183), test_phase5_joinorder.zia, test_join_basic.zia, test_index.zia, test_optimizer.zia

---

### Phase 6: Persistent Storage Engine
**Status:** DONE

- Page-based binary storage (.vdb files)
- 4KB page format with header, schema pages, data pages
- BufferPool with LRU eviction (configurable capacity)
- Pager for raw page I/O
- Binary serializer for rows (Integer/Real/Text/Null/Blob)
- Schema page management (table metadata persistence)
- Write-Ahead Log (WAL) for crash recovery
- WAL checkpointing
- StorageEngine facade with thread-safe locking

**Key files:** storage/engine.zia (1,072 lines), storage/btree.zia (547), storage/wal.zia (732), storage/buffer.zia (340), storage/pager.zia (425), storage/schema_page.zia (522), storage/data_page.zia (313), storage/serializer.zia (400), storage/page.zia (119), storage/btree_node.zia (379), storage/txn.zia (696)

**Test files:** test_storage.zia, test_storage_persistence.zia, test_storage_stress.zia, test_engine.zia, test_pager_io.zia, test_wal.zia, test_btree.zia

---

### Phase 7: Multi-User Server & PostgreSQL Wire Protocol
**Status:** DONE

- TCP server with thread pool
- PostgreSQL wire protocol v3 (authentication, simple query, error response)
- Per-connection sessions with isolated temp tables
- Multi-database support (CREATE DATABASE, USE, DROP DATABASE)
- System views: INFORMATION_SCHEMA.TABLES/COLUMNS, sys.databases, sys.sessions
- User authentication (CREATE USER, ALTER USER, DROP USER)
- Table-level concurrency control (S/X locking)
- CSV import/export

**Key files:** server.zia (624), server/pg_wire.zia (662), server/connection.zia (545), server/sql_server.zia (363), server/tcp_server.zia (110), server/sql_client.zia (185), session.zia (185), system_views.zia (423)

**Test files:** test_server.zia, test_pg_wire.zia, test_pg_net_minimal.zia, test_multiuser.zia, test_multidb.zia, test_tempdb.zia, test_system_views.zia, test_auth.zia

---

### Phase 8: Executor Modularization
**Status:** DONE

- Split monolithic executor.zia into focused modules:
  - `ddl.zia` — DDL operations (CREATE/DROP/ALTER TABLE, CREATE/DROP INDEX/VIEW)
  - `dml.zia` — DML operations (INSERT, UPDATE, DELETE, index maintenance)
  - `query.zia` — SELECT execution, WHERE evaluation, aggregation, subqueries
- Executor remains as coordinator with shared state

**Key files:** executor.zia (2,177), ddl.zia (996), dml.zia (1,205), query.zia (1,510)

---

### Phase 9: B-tree Implementation
**Status:** DONE

- Order-50 B-tree (~99 keys per 4KB page)
- BTreeKey with (value, dataPageId, dataSlotId) for disk row references
- Insert with node splitting
- Delete with rebalancing (merge/redistribute)
- Point search and range search
- Integration with BufferPool for page I/O
- BTreeNodeSerializer for binary page format

**Key files:** storage/btree.zia (547), storage/btree_node.zia (379)

**Test files:** test_btree.zia (94 assertions)

---

### Phase 10: Disk-Based Index Integration
**Status:** DONE

- IndexMeta entity persisted in schema pages alongside TableMeta
- CREATE INDEX creates both hash index (in-memory) and B-tree (on-disk) for persistent databases
- DROP INDEX removes from both in-memory and persistent storage
- INSERT maintains B-tree indexes alongside hash indexes
- DELETE/rebuild maintains B-tree indexes on compaction
- OPEN .vdb restores indexes: loads IndexMeta, rebuilds hash indexes from table data, opens B-trees from persisted root pages
- B-tree tracking in Executor (btrees + btreeIndexNames parallel lists)

**Files modified:** storage/schema_page.zia, storage/engine.zia, ddl.zia, dml.zia, executor.zia, persistence.zia, README.md

---

### Phase 11: Extended Query Protocol
**Status:** DONE

- Parse message handler (named/unnamed prepared statements with $N parameter counting)
- Bind message handler (parameter substitution with NULL, numeric, and string escaping)
- Describe message handler (ParameterDescription for statements, RowDescription for portals)
- Execute message handler (portal execution with row limits and portal suspension)
- Sync/Close/Flush message handling
- PreparedStatement and Portal entity lifecycle management per-session
- Error recovery (discard messages until Sync after ErrorResponse)
- Type OID inference from query results (INTEGER→INT4, REAL→FLOAT8, TEXT→TEXT, BLOB→BYTEA)
- Message parsing helpers for all extended query frontend messages
- Response builders: ParseComplete, BindComplete, CloseComplete, NoData, PortalSuspended, ParameterDescription, RowDescriptionTyped

**Key files:** server/pg_wire.zia (~950 lines), server/connection.zia (~1,040 lines), session.zia (185 lines)

**Test files:** test_extended_query.zia (39 assertions across 12 test functions)

---

### Phase 12: BOOLEAN & DATE/TIMESTAMP Native Types
**Status:** DONE

- BOOLEAN type with TRUE/FALSE literals, SQL_BOOLEAN kind (intValue 0/1)
- IS TRUE / IS FALSE / IS NOT TRUE / IS NOT FALSE predicates
- DATE type with native storage (epoch seconds at noon local), DATE() constructor
- TIMESTAMP type with epoch seconds storage, TIMESTAMP() constructor
- NOW() and CURRENT_TIMESTAMP return native SQL_TIMESTAMP; CURRENT_DATE returns SQL_DATE
- DATE arithmetic: DATE + INTEGER (add days), DATE - DATE (day difference)
- CAST support: CAST_BOOLEAN, CAST_DATE, CAST_TIMESTAMP
- TYPEOF returns "boolean", "date", "timestamp" for new types
- BOOLEAN/DATE/TIMESTAMP column types in CREATE TABLE with DEFAULT TRUE/FALSE
- Auto-coercion on INSERT: INTEGER→BOOLEAN, TEXT→DATE, TEXT→TIMESTAMP
- Binary serializer: VAL_TYPE_BOOL (1 byte), VAL_TYPE_DATE (8 bytes), VAL_TYPE_TIMESTAMP (8 bytes)
- PG wire protocol: PG_OID_BOOL, PG_OID_DATE, PG_OID_TIMESTAMP in column type inference
- Schema display: DESCRIBE shows BOOLEAN, DATE, TIMESTAMP type names
- DATE_ADD/DATE_SUB return native TIMESTAMP values

**Key files:** types.zia, token.zia, lexer.zia, parser.zia, executor.zia, sql_functions.zia, schema.zia, dml.zia, storage/page.zia, storage/serializer.zia, server/pg_wire.zia, server/connection.zia

**Test files:** test_phase12_types.zia (87 assertions)

---

### Phase 13: GRANT/REVOKE & Privilege System
**Status:** DONE

- GRANT/REVOKE for SELECT, INSERT, UPDATE, DELETE, ALL on tables
- Table ownership model (creator = owner, owner has implicit ALL)
- Superuser (admin) bypasses all privilege checks
- PUBLIC pseudo-user for grants to all users
- Privilege checking at handler entry points (query, dml, ddl)
- Owner/superuser required for DDL operations (DROP TABLE, ALTER TABLE, CREATE/DROP INDEX)
- SHOW GRANTS [FOR username] command
- INFORMATION_SCHEMA.TABLE_PRIVILEGES system view
- Privilege cleanup on DROP TABLE and DROP USER
- Privilege bitmask with bitwise emulation (Zia has no bitwise operators)

**Key files:** token.zia, lexer.zia, executor.zia, server.zia, ddl.zia, dml.zia, query.zia, system_views.zia, server/connection.zia

**Test files:** test_phase13_privileges.zia (67 assertions)

---

## Upcoming Phases

### Part 2: Protocol & Type System (Phases 14-15)

---

#### Phase 14: Row-Level Locking
**Priority:** High | **Estimate:** 500-700 lines

Current table-level S/X locks are too coarse for concurrent workloads. Row-level locking enables concurrent writers on different rows.

- SELECT ... FOR UPDATE (exclusive row locks)
- SELECT ... FOR SHARE (shared row locks)
- Row lock tracking per-transaction (table_name + row_id → lock_mode)
- Lock conflict detection (shared/shared OK, shared/exclusive blocks, exclusive/exclusive blocks)
- Lock release on COMMIT/ROLLBACK
- Deadlock detection (wait-for graph cycle detection)
- Lock timeout configuration

**Key files to modify:** storage/txn.zia, executor.zia, query.zia, parser.zia, stmt.zia

---

#### Phase 15: WAL Undo Phase
**Priority:** Medium-High | **Estimate:** 300-500 lines

Current WAL only supports redo (replay). Undo support enables proper crash recovery where uncommitted transactions are rolled back.

- UNDO log records (before-images for UPDATE/DELETE)
- Recovery phases: Analysis → Redo → Undo
- Active transaction tracking in WAL checkpoint
- Compensation Log Records (CLR) to prevent undo loops
- Integration with transaction manager

**Key files to modify:** storage/wal.zia, storage/engine.zia, storage/txn.zia

---

### Part 3: Concurrency & Programmability (Phases 16-20)

#### Phase 16: MVCC (Multi-Version Concurrency Control)
**Priority:** Medium | **Estimate:** 1,000-1,500 lines

MVCC eliminates most read locks by giving each transaction a consistent snapshot. Readers never block writers, writers never block readers.

- Transaction IDs (monotonically increasing, stored per-row)
- Row versioning: xmin (creating txn), xmax (deleting txn)
- Visibility rules: row visible if xmin committed AND (xmax not set OR xmax not committed)
- Snapshot isolation: each transaction sees data as of its start time
- Version chain traversal for finding visible row version
- Vacuum/cleanup of dead row versions

**Key files to modify:** types.zia, table.zia, storage/txn.zia, query.zia, dml.zia, executor.zia

---

#### Phase 17: Triggers
**Priority:** Medium | **Estimate:** ~500 lines

- CREATE TRIGGER ... BEFORE/AFTER INSERT/UPDATE/DELETE
- Row-level triggers (FOR EACH ROW)
- Statement-level triggers (FOR EACH STATEMENT)
- OLD and NEW row references in trigger body
- Trigger execution ordering (alphabetical by name, per PG convention)
- DROP TRIGGER
- Recursive trigger prevention (max depth)

**Key files to modify:** parser.zia, stmt.zia, ddl.zia, dml.zia, executor.zia

**New files:** triggers.zia

---

#### Phase 18: Sequences
**Priority:** Medium | **Estimate:** ~300 lines

- CREATE SEQUENCE name [START n] [INCREMENT n] [MINVALUE n] [MAXVALUE n] [CYCLE]
- NEXTVAL('seq_name'), CURRVAL('seq_name'), SETVAL('seq_name', n)
- Sequence persistence in schema pages
- DEFAULT NEXTVAL('seq') for column defaults
- ALTER SEQUENCE, DROP SEQUENCE

**Key files to modify:** parser.zia, stmt.zia, ddl.zia, sql_functions.zia, storage/schema_page.zia

**New files:** sequence.zia

---

#### Phase 19: Stored Procedures / Functions
**Priority:** Medium | **Estimate:** ~800 lines

- CREATE FUNCTION name(params) RETURNS type AS $$ ... $$ LANGUAGE sql
- Simple SQL-body functions (single SELECT/INSERT/UPDATE/DELETE)
- Parameter passing and return value handling
- DROP FUNCTION
- Function call syntax in expressions: func_name(args)
- INFORMATION_SCHEMA.ROUTINES view

**Key files to modify:** parser.zia, stmt.zia, ddl.zia, expr.zia, executor.zia, system_views.zia

**New files:** procedures.zia

---

#### Phase 20: Composite & Expression Indexes
**Priority:** Medium | **Estimate:** ~400 lines

- Multi-column index support in B-tree (composite key comparison)
- Expression indexes: CREATE INDEX idx ON t (LOWER(col))
- Partial indexes: CREATE INDEX idx ON t (col) WHERE condition
- Index-only scans when all selected columns are in the index
- EXPLAIN output showing index usage

**Key files to modify:** index.zia, storage/btree.zia, storage/btree_node.zia, ddl.zia, query.zia, optimizer/optimizer.zia

---

### Part 4: Compatibility & Observability (Phases 21-25)

#### Phase 21: pg_catalog Compatibility
**Priority:** Medium | **Estimate:** ~600 lines

Many PostgreSQL tools query pg_catalog tables to discover schema. Implementing the most common ones enables broader tool compatibility.

- pg_catalog.pg_class (tables, indexes, sequences)
- pg_catalog.pg_attribute (columns)
- pg_catalog.pg_type (data types)
- pg_catalog.pg_namespace (schemas)
- pg_catalog.pg_index (index metadata)
- pg_catalog.pg_database (databases)
- OID generation for objects

**Key files to modify:** system_views.zia, executor.zia, server/pg_wire.zia

---

#### Phase 22: SSL/TLS Support
**Priority:** Medium | **Estimate:** TBD (depends on Zia TLS runtime support)

- SSLRequest message handling in PG wire protocol
- TLS handshake wrapping existing TCP connections
- Certificate-based authentication option
- sslmode parameter support (disable, allow, prefer, require)

**Key files to modify:** server/tcp_server.zia, server/connection.zia, server/pg_wire.zia

**Dependencies:** Zia runtime TLS bindings

---

#### Phase 23: Connection Pooling
**Priority:** Medium | **Estimate:** ~400 lines

- Idle connection timeout and cleanup
- Maximum connections per user/database
- Connection reuse (session reset between uses)
- Connection queue when pool is full
- Pool statistics in sys.pool_stats view

**Key files to modify:** server/sql_server.zia, server/connection.zia, session.zia, system_views.zia

---

#### Phase 24: DECIMAL/NUMERIC Type
**Priority:** Medium | **Estimate:** ~500 lines

- Fixed-point decimal type with configurable precision/scale
- NUMERIC(precision, scale) syntax
- Decimal arithmetic (add, subtract, multiply, divide with proper rounding)
- Decimal comparison
- CAST between DECIMAL and INTEGER/REAL/TEXT
- Storage serializer for decimal values
- Wire protocol decimal formatting

**Key files to modify:** types.zia, parser.zia, expr.zia, storage/serializer.zia, server/pg_wire.zia

---

#### Phase 25: JSON/JSONB Type & Functions
**Priority:** Medium | **Estimate:** ~800 lines

- JSON and JSONB data types
- JSON parsing and validation on INSERT
- Operators: ->, ->>, #>, #>>, @>, <@, ?
- Functions: json_extract_path, json_array_length, json_each, json_object_keys
- JSONB containment and existence indexing (GIN-compatible in future)
- JSON construction: json_build_object, json_build_array, json_agg

**Key files to modify:** types.zia, parser.zia, expr.zia, storage/serializer.zia

**New files:** json.zia

---

### Part 5: Production Features (Phases 26-30)

#### Phase 26: Autovacuum Background Worker
**Priority:** Low-Medium | **Estimate:** ~400 lines

- Background thread for dead row cleanup (works with MVCC Phase 16)
- Configurable thresholds (dead row ratio, table size)
- Table-level vacuum statistics tracking
- VACUUM and ANALYZE manual commands
- Vacuum progress reporting in sys.vacuum_stats

**Key files to modify:** executor.zia, storage/engine.zia, system_views.zia

**New files:** vacuum.zia

**Dependencies:** Phase 16 (MVCC)

---

#### Phase 27: pg_stat Views
**Priority:** Low-Medium | **Estimate:** ~300 lines

- pg_stat_activity (current sessions, queries, state)
- pg_stat_user_tables (sequential/index scans, rows fetched, inserts, updates, deletes)
- pg_stat_user_indexes (index scans, tuples fetched)
- Statement-level timing and counting
- Buffer hit/miss ratios

**Key files to modify:** system_views.zia, executor.zia, storage/buffer.zia, query.zia

---

#### Phase 28: Table Partitioning
**Priority:** Low | **Estimate:** ~700 lines

- PARTITION BY RANGE (column)
- PARTITION BY LIST (column)
- PARTITION BY HASH (column)
- CREATE TABLE ... PARTITION OF parent
- Partition pruning in query optimizer
- Partition-wise joins

**Key files to modify:** parser.zia, stmt.zia, ddl.zia, query.zia, optimizer/optimizer.zia, table.zia

---

#### Phase 29: Parallel Query Execution
**Priority:** Low | **Estimate:** ~600 lines

- Parallel sequential scan (partition table across workers)
- Parallel hash join (build phase parallelism)
- Parallel aggregation (partial agg → combine)
- Worker thread pool with task queue
- Gather node to merge parallel results
- SET max_parallel_workers_per_gather

**Key files to modify:** query.zia, join.zia, executor.zia, server/threadpool.zia

---

#### Phase 30: Logical Replication
**Priority:** Low | **Estimate:** ~1,000 lines

- Replication slots with WAL position tracking
- Logical decoding (WAL → row change events)
- Publication (CREATE PUBLICATION ... FOR TABLE ...)
- Subscription (CREATE SUBSCRIPTION ... CONNECTION ... PUBLICATION ...)
- Initial table sync on subscription creation
- Streaming replication protocol over PG wire

**Key files to modify:** storage/wal.zia, server/pg_wire.zia, executor.zia

**New files:** replication/publisher.zia, replication/subscriber.zia, replication/decoder.zia

**Dependencies:** Phase 15 (WAL Undo)

---

## Architecture Overview

```
                    ┌─────────────┐
                    │   Clients   │  psql, ODBC, custom
                    └──────┬──────┘
                           │ TCP :5433
                    ┌──────┴──────┐
                    │  PG Wire    │  server/pg_wire.zia
                    │  Protocol   │  server/connection.zia
                    └──────┬──────┘
                           │
                    ┌──────┴──────┐
                    │   Server    │  server.zia, session.zia
                    │  Sessions   │  server/sql_server.zia
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
        ┌─────┴─────┐ ┌───┴───┐ ┌─────┴─────┐
        │  Parser   │ │Lexer  │ │  Token    │
        │ 1,662 loc │ │344 loc│ │  276 loc  │
        └─────┬─────┘ └───────┘ └───────────┘
              │
        ┌─────┴─────┐
        │ Executor  │  executor.zia (2,177 loc)
        │ ┌─────────┤
        │ │  DDL    │  ddl.zia (996 loc)
        │ │  DML    │  dml.zia (1,205 loc)
        │ │  Query  │  query.zia (1,510 loc)
        │ └─────────┤
        └─────┬─────┘
              │
    ┌─────────┼──────────┬───────────┐
    │         │          │           │
┌───┴───┐ ┌──┴──┐ ┌─────┴────┐ ┌───┴────┐
│ Table │ │Index│ │ Join     │ │Optimizer│
│155 loc│ │380  │ │1,269 loc │ │417 loc  │
└───────┘ └─────┘ └──────────┘ └─────────┘
              │
        ┌─────┴──────┐
        │  Storage   │
        │  Engine    │  storage/engine.zia (1,072 loc)
        │ ┌──────────┤
        │ │ B-tree   │  storage/btree.zia (547 loc)
        │ │ WAL      │  storage/wal.zia (732 loc)
        │ │ Buffer   │  storage/buffer.zia (340 loc)
        │ │ Pager    │  storage/pager.zia (425 loc)
        │ │ Schema   │  storage/schema_page.zia (522 loc)
        │ │ Data     │  storage/data_page.zia (313 loc)
        │ └──────────┤
        └────────────┘
```

---

## Gap Analysis: Path to Production PostgreSQL Compatibility

### Critical Gaps (must fix for real client compatibility)
1. ~~**Extended Query Protocol** (Phase 11)~~ — **DONE** ✓
2. ~~**BOOLEAN type** (Phase 12)~~ — **DONE** ✓
3. **pg_catalog tables** (Phase 21) — psql, pgAdmin, ORMs all query these on connect

### High-Priority Gaps (needed for multi-user production use)
4. ~~**GRANT/REVOKE** (Phase 13)~~ — **DONE** ✓
5. **Row-level locking** (Phase 14) — table-level locks cause unnecessary contention
6. **MVCC** (Phase 16) — readers should not block writers

### Medium-Priority Gaps (feature completeness)
7. **Triggers** (Phase 17) — common application requirement
8. **Sequences** (Phase 18) — PG-standard auto-increment
9. **JSON support** (Phase 25) — increasingly common data type

---

## Zia Language Fixes Needed

Issues discovered during ViperSQL development that require compiler-level fixes:

1. **Bytes type inference** — runtime functions returning `obj` (Ptr) cause Zia to type variables as Ptr, not Bytes. Property access silently returns 0/null. Workaround: explicit `var x: Bytes = ...` annotations.

2. **Chained method calls** — `bytes.Slice(x,y).ToStr()` causes IL lowering errors. Workaround: break into separate statements.

3. **List[Boolean] boxing** — `List[Boolean].get(i)` in boolean expressions causes type mismatch. Workaround: use `List[Integer]` with 0/1.

4. **FOR loop support** — Zia has no for loop; all iteration is `while` with manual index management. Every loop requires 3 extra lines (init, condition rewrite, increment).

5. **String interpolation** — No f-string or interpolation syntax. All string building is manual concatenation.

6. **Match/switch** — No pattern matching. Complex dispatch requires if/else chains.

These are tracked in `PLATFORM_BUGS.md`.
