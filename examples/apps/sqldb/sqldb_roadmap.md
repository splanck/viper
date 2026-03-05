# ViperSQL Development Roadmap

**Status:** Active Development
**Current Stats:** ~60,100 lines of Zia | 109 source files | 104 test files | 4,985+ test assertions

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

#### ~~Phase 14: Row-Level Locking~~ — DONE ✓
**Status:** Complete | **Lines added:** ~400

Implemented row-level locking for fine-grained concurrent access control.

- ✅ SELECT ... FOR UPDATE (exclusive row locks)
- ✅ SELECT ... FOR SHARE (shared row locks)
- ✅ FOR UPDATE/SHARE NOWAIT (non-blocking, immediate error on conflict)
- ✅ FOR UPDATE/SHARE SKIP LOCKED (skip locked rows instead of blocking)
- ✅ RowLockManager entity with thread-safe monitor-based concurrency
- ✅ Lock conflict detection (S+S OK, S+X blocks, X+X blocks)
- ✅ Lock release on COMMIT/ROLLBACK and after non-transactional statements
- ✅ Lock timeout (5s default with 10ms retry interval)
- ✅ Fixed BUG-TOKEN-001: duplicate token constants (TK_CLOSE/TK_EXCEPT collision)

**Files modified:** token.zia, lexer.zia, stmt.zia, parser.zia, storage/txn.zia, server.zia, executor.zia, query.zia
**Test files:** test_phase14_row_locks.zia (68 assertions)

---

#### ~~Phase 15: WAL Undo Phase~~ — DONE ✓
**Status:** Complete | **Lines added:** ~250

Implemented full ARIES-style undo support for crash recovery and transaction abort.

- ✅ Compensation Log Records (CLR, LOG_CLR = 9) to prevent undo loops
- ✅ Before-image capture for UPDATE and DELETE operations in StorageEngine
- ✅ Full redo: INSERT, UPDATE, and DELETE replay for committed transactions
- ✅ Undo phase in recovery: rolls back uncommitted transactions using before-images
- ✅ TransactionManager.initWithStorage() for abort-time undo with BufferPool/DataPageManager
- ✅ WAL.readAllRecords() helper for scanning all log files
- ✅ TransactionManager.abort() performs undo by scanning WAL backwards

**Files modified:** storage/wal.zia, storage/engine.zia, storage/txn.zia
**Test files:** test_phase15_wal_undo.zia (126 assertions)

---

### Part 3: Concurrency & Programmability (Phases 16-20)

#### ~~Phase 16: MVCC (Multi-Version Concurrency Control)~~ — DONE ✓
**Status:** Complete | **Lines added:** ~200

Implemented snapshot isolation with row-level versioning. Readers never block writers.

- ✅ Row versioning: xmin (creating txn ID), xmax (deleting/updating txn ID)
- ✅ `isVisible(snapshotId, currentTxnId)` method on Row entity
- ✅ Snapshot isolation: each transaction gets a snapshot at BEGIN time
- ✅ INSERT stamps row.xmin with current transaction ID
- ✅ DELETE stamps row.xmax with current transaction ID
- ✅ UPDATE stamps row.xmin after modification
- ✅ Visibility checks in all query paths: SELECT, JOIN, INDEX, DDL, subqueries
- ✅ Non-MVCC fallback: snapshotId=0 uses legacy deleted flag
- ✅ MVCC state cleared on COMMIT/ROLLBACK
- ✅ Transaction counter monotonically increases across transactions

**Files modified:** schema.zia, executor.zia, dml.zia, query.zia, join.zia, ddl.zia
**Test files:** test_phase16_mvcc.zia (104 assertions)

---

#### ~~Phase 17: Triggers~~ — DONE ✓
**Status:** Complete | **Lines added:** ~400

Implemented SQL triggers with BEFORE/AFTER firing for INSERT/UPDATE/DELETE.

- ✅ CREATE TRIGGER name BEFORE|AFTER INSERT|UPDATE|DELETE ON table FOR EACH ROW EXECUTE 'sql'
- ✅ FOR EACH ROW triggers (fire per affected row)
- ✅ FOR EACH STATEMENT triggers
- ✅ OLD and NEW row references via temp tables (old/new with source table schema)
- ✅ Trigger execution ordering (alphabetical by name, per PG convention)
- ✅ DROP TRIGGER, SHOW TRIGGERS
- ✅ Recursive trigger prevention (MAX_TRIGGER_DEPTH = 16)
- ✅ Trigger cleanup on DROP TABLE
- ✅ TriggerManager entity with sorted retrieval

**Files modified:** token.zia, lexer.zia, stmt.zia, executor.zia, ddl.zia, dml.zia
**New files:** triggers.zia
**Test files:** test_phase17_triggers.zia (69 assertions)

---

#### ~~Phase 18: Sequences~~ — DONE ✓
**Status:** Complete | **Lines added:** ~350

Implemented PostgreSQL-style sequences for auto-incrementing ID generation.

- ✅ CREATE SEQUENCE name [START [WITH] n] [INCREMENT [BY] n] [MINVALUE n] [MAXVALUE n] [CYCLE | NO CYCLE]
- ✅ NEXTVAL('seq_name'), CURRVAL('seq_name'), SETVAL('seq_name', n [, is_called])
- ✅ DROP SEQUENCE, ALTER SEQUENCE (RESTART [WITH n], INCREMENT BY, MINVALUE, MAXVALUE, CYCLE)
- ✅ SHOW SEQUENCES
- ✅ Ascending and descending sequences
- ✅ CYCLE wraps around (ascending → minValue, descending → maxValue)
- ✅ NO CYCLE returns NULL on exhaustion
- ✅ Sequences survive ROLLBACK (per PostgreSQL semantics)
- ✅ NEXTVAL usable in INSERT VALUES, WHERE clauses, and expressions

**New files:** sequence.zia
**Files modified:** token.zia, lexer.zia, executor.zia, ddl.zia
**Test files:** test_phase18_sequences.zia (105 assertions)

---

#### ~~Phase 19: Stored Procedures / Functions~~ — DONE ✓
**Status:** Complete | **Lines added:** ~500

Implemented PostgreSQL-style user-defined SQL functions.

- ✅ CREATE FUNCTION name(params) RETURNS type [LANGUAGE SQL] AS 'sql_body'
- ✅ Parameter types: INTEGER, REAL, TEXT, BOOLEAN, DATE, TIMESTAMP
- ✅ Return types: INTEGER, REAL, TEXT, BOOLEAN, VOID
- ✅ Positional parameter substitution ($1, $2, ...) in SQL body
- ✅ Function call syntax in expressions: func_name(args)
- ✅ Function overloading (same name, different parameter counts)
- ✅ DROP FUNCTION, SHOW FUNCTIONS
- ✅ User-defined functions in SELECT, WHERE, INSERT, UPDATE expressions
- ✅ Functions that query tables (SELECT FROM within function body)
- ✅ FunctionManager with duplicate detection
- ✅ LANGUAGE SQL clause (optional, for PostgreSQL compatibility)

**New files:** procedures.zia
**Files modified:** token.zia, lexer.zia, executor.zia, ddl.zia
**Test files:** test_phase19_procedures.zia (77 assertions)

---

#### ~~Phase 20: Composite & Expression Indexes~~ — DONE ✓
**Status:** Complete | **Lines added:** ~300

Implemented multi-column composite index support with full equality and prefix lookups.

- ✅ CREATE INDEX on multiple columns: CREATE INDEX idx ON t (col1, col2, col3)
- ✅ CREATE UNIQUE INDEX with composite key uniqueness enforcement
- ✅ Full composite equality lookup (col1 = v1 AND col2 = v2)
- ✅ Prefix lookup: leading column(s) of composite index (col1 = v1 on (col1, col2) index)
- ✅ Three-column composite indexes with full and partial key matching
- ✅ Composite index maintenance: INSERT checks uniqueness, DELETE/UPDATE rebuild
- ✅ Mixed single-column and composite indexes (single-column preferred when available)
- ✅ EXPLAIN output shows composite index usage with column list
- ✅ Key separator (`|`) ensures no cross-value collisions (e.g., "x"|"y" vs "xy"|"")
- ✅ IndexManager.findIndexForColumns() for ordered prefix matching

**Files modified:** index.zia, executor.zia, ddl.zia
**Test files:** test_phase20_composite_indexes.zia (56 assertions)

---

### Part 4: Compatibility & Observability (Phases 21-25)

#### ~~Phase 21: pg_catalog Compatibility~~ — DONE ✓
**Status:** Complete | **Lines added:** ~350

Implemented PostgreSQL-compatible system catalog views for tool interoperability.

- ✅ pg_catalog.pg_class (tables as relkind='r', views as 'v', indexes as 'i')
- ✅ pg_catalog.pg_attribute (columns with attrelid, atttypid, attnum, attnotnull)
- ✅ pg_catalog.pg_type (int4, float8, text, bool, date, timestamp, bytea, varchar)
- ✅ pg_catalog.pg_namespace (pg_catalog, public, information_schema)
- ✅ pg_catalog.pg_index (indexrelid, indrelid, indnatts, indisunique, indkey)
- ✅ pg_catalog.pg_database (oid, datname, datdba, encoding, datcollate)
- ✅ pg_catalog.pg_proc (functions stub — session-scoped)
- ✅ pg_catalog.pg_settings (server_version, encoding, max_connections, etc.)
- ✅ Stable OID generation via hash (avoids PG system OID range)
- ✅ Accessible with or without pg_catalog prefix
- ✅ Cross-view OID consistency (pg_class oids match pg_attribute attrelid, pg_index indexrelid)
- ✅ SQL type → PG type OID mapping (int4=23, text=25, bool=16, etc.)

**Files modified:** system_views.zia
**Test files:** test_phase21_pg_catalog.zia (57 assertions)

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

#### ~~Phase 25: JSON/JSONB Type & Functions~~ — DONE ✓
**Status:** Complete | **Lines added:** ~500

Implemented JSON/JSONB data types with pure-Zia JSON parser and 11 SQL functions.

- ✅ JSON and JSONB column types (both stored as SQL_JSON internally)
- ✅ Pure-Zia recursive descent JSON parser with validation
- ✅ JSON_VALID, JSON_TYPE, JSON_TYPEOF for introspection
- ✅ JSON_EXTRACT with JSONPath syntax ($.key, $[0], nested paths)
- ✅ JSON_EXTRACT_TEXT (unwraps JSON strings)
- ✅ JSON_ARRAY_LENGTH, JSON_OBJECT_KEYS
- ✅ JSON_BUILD_OBJECT, JSON_BUILD_ARRAY for construction
- ✅ JSON_QUOTE, JSON() for text→JSON conversion
- ✅ CAST(expr AS JSON), CAST(expr AS JSONB)
- ✅ TYPEOF returns 'json' for JSON values
- ✅ JSON storage serialization (length-prefixed string)
- ✅ PG wire protocol: PG_OID_JSON (114), PG_OID_JSONB (3802)
- ✅ JSON in pg_type system view
- ✅ Auto-coercion TEXT→JSON on INSERT into JSON columns
- ✅ Fixed cloneValue bug for SQL_JSON textValue propagation

**New files:** json.zia (~380 lines)
**Files modified:** types.zia, token.zia, lexer.zia, parser.zia, schema.zia, dml.zia, executor.zia, sql_functions.zia, system_views.zia, storage/page.zia, storage/serializer.zia, server/pg_wire.zia, server/connection.zia
**Test files:** test_phase25_json.zia (119 assertions)

---

### Part 5: Production Features (Phases 26-30)

#### Phase 26: VACUUM, ANALYZE & Dead Row Cleanup — DONE
**Priority:** Low-Medium | **Actual:** ~350 lines | **Tests:** 51 assertions

- VACUUM with MVCC dead row detection (xmax-based + legacy soft-delete)
- VACUUM [tablename] for per-table vacuum
- VACUUM FULL (vacuum + index rebuild + storage flush)
- VACUUM ANALYZE combined operation
- ANALYZE command with optimizer statistics update (row counts, distinct values)
- ANALYZE [tablename] for per-table analysis
- sys.vacuum_stats system view (8 columns: table_name, vacuum_count, dead_rows_removed, last_vacuum, analyze_count, live_rows, dead_rows, last_analyze)
- Vacuum/analyze statistics tracking across multiple passes
- Error handling for nonexistent tables

**Files modified:** token.zia, lexer.zia, ddl.zia (~200 lines), executor.zia (~120 lines), query.zia, system_views.zia
**Test file:** tests/test_phase26_vacuum.zia (14 test functions, 51 assertions)
**Dependencies:** Phase 16 (MVCC)

---

#### Phase 27: pg_stat Views — DONE
**Priority:** Low-Medium | **Actual:** ~300 lines | **Tests:** 49 assertions

- pg_stat_activity: datid, datname, pid, usename, application_name, client_addr, backend_start, state, query, query_count
- pg_stat_user_tables: relid, schemaname, relname, seq_scan, seq_tup_read, idx_scan, idx_tup_fetch, n_tup_ins, n_tup_upd, n_tup_del, n_live_tup, n_dead_tup
- pg_stat_user_indexes: relid, indexrelid, schemaname, relname, indexrelname, idx_scan, idx_tup_read, idx_tup_fetch
- Per-table DML tracking (INSERT/UPDATE/DELETE counts)
- Sequential scan and index scan counting
- Statement-level query counting and last-query tracking
- pg_catalog.* prefix support for all three views

**Files modified:** executor.zia (~130 lines), query.zia (~150 lines), dml.zia (6 tracking calls), system_views.zia
**Test file:** tests/test_phase27_pg_stat.zia (11 test functions, 49 assertions)

---

#### Phase 28: Table Partitioning
**Status:** DONE

- PARTITION BY RANGE (column) with MINVALUE/MAXVALUE
- PARTITION BY LIST (column) with IN (value-list)
- PARTITION BY HASH (column) with MODULUS/REMAINDER
- CREATE TABLE child PARTITION OF parent FOR VALUES ...
- Partition-aware INSERT routing (auto-route to correct child)
- Partition-aware SELECT from parent (merges child data)
- Partition pruning on equality predicates
- Aggregate support (COUNT, SUM, MIN, MAX, AVG) on partitioned tables
- Column inheritance from parent to child partitions
- LIMIT/OFFSET support on partitioned queries
- Error handling: no matching partition, no partitions, non-partitioned parent

**Test file:** test_phase28_partition.zia (34 assertions)

**Key files modified:** parser.zia, stmt.zia, ddl.zia, dml.zia, query.zia, table.zia, types.zia

---

#### Phase 29: Common SQL Extensions
**Status:** DONE

- TRUNCATE TABLE (with and without TABLE keyword)
- TRUNCATE on partitioned tables (clears all children)
- INSERT ... RETURNING * / column list
- UPDATE ... RETURNING * / column list
- DELETE ... RETURNING * / column list
- INSERT ... ON CONFLICT (columns) DO NOTHING
- INSERT ... ON CONFLICT (columns) DO UPDATE SET col = val
- Multi-row INSERT with ON CONFLICT handling
- GENERATE_SERIES(start, stop [, step]) table function
- Ascending and descending series with custom step
- LIMIT/OFFSET support on GENERATE_SERIES

**Test file:** test_phase29_extensions.zia (66 assertions)

**Key files modified:** token.zia, lexer.zia, stmt.zia, parser.zia, executor.zia, dml.zia, query.zia, table.zia

---

#### Phase 30: Prepared Statements & EXPLAIN ANALYZE
**Status:** DONE

- PREPARE name AS sql_template (with $1, $2 parameter placeholders)
- EXECUTE name (param1, param2, ...) — parameter substitution
- DEALLOCATE name — remove a prepared statement
- DEALLOCATE ALL — remove all prepared statements
- Prepared statement reuse (execute same plan with different params)
- EXPLAIN ANALYZE — executes query and reports actual execution time + rows returned
- Duplicate prepared statement name detection

**Test file:** test_phase30_prepared.zia (24 assertions)

**Key files modified:** token.zia, lexer.zia, executor.zia

---

#### Phase 31: Logical Replication
**Priority:** Low | **Estimate:** ~1,000 lines

- Replication slots with WAL position tracking
- Logical decoding (WAL → row change events)
- Publication (CREATE PUBLICATION ... FOR TABLE ...)
- Subscription (CREATE SUBSCRIPTION ... CONNECTION ... PUBLICATION ...)
- Initial table sync on subscription creation
- Streaming replication protocol over PG wire

**Key files to modify:** storage/wal.zia, server/pg_wire.zia, executor.zia

**New files:** replication/publisher.zia, replication/subscriber.zia, replication/decoder.zia

---

### Part 6: SQL Completeness (Phases 32-35)

#### Phase 32: Cursors (DECLARE/FETCH/CLOSE) — DONE
**Status:** Complete | **Lines added:** ~400 | **Tests:** 31 assertions

Implemented SQL cursor support for iterating over result sets.

- ✅ DECLARE cursor_name CURSOR FOR select_query
- ✅ FETCH NEXT/PRIOR/FIRST/LAST FROM cursor_name
- ✅ FETCH FORWARD n / FORWARD ALL FROM cursor_name
- ✅ FETCH ABSOLUTE n / RELATIVE n FROM cursor_name
- ✅ FETCH ALL FROM cursor_name (fetch all remaining rows)
- ✅ CLOSE cursor_name — release cursor resources
- ✅ CLOSE ALL — release all cursors
- ✅ MOVE direction IN cursor_name — reposition without returning rows
- ✅ Duplicate cursor name detection
- ✅ Materialized result set with bidirectional navigation

**Test file:** test_phase32_cursors.zia (31 assertions)
**Key files modified:** token.zia, lexer.zia, executor.zia

---

#### Phase 33: Savepoints — DONE
**Status:** Complete | **Lines added:** ~150 | **Tests:** 23 assertions

Implemented subtransaction support with savepoints.

- ✅ SAVEPOINT name — create a savepoint within a transaction
- ✅ ROLLBACK TO [SAVEPOINT] name — undo changes back to savepoint
- ✅ RELEASE [SAVEPOINT] name — discard a savepoint
- ✅ Nested savepoints (SAVEPOINT sp1 → SAVEPOINT sp2 → ROLLBACK TO sp1)
- ✅ Savepoint overwrite (re-creating same-named savepoint updates position)
- ✅ Journal-based undo for rollback to savepoint
- ✅ Index rebuild after partial rollback
- ✅ Savepoint cleanup on COMMIT/ROLLBACK
- ✅ Error handling: outside transaction, nonexistent savepoint

**Test file:** test_phase33_savepoints.zia (23 assertions)
**Key files modified:** executor.zia

---

#### Phase 34: COPY TO/FROM — DONE
**Status:** Complete | **Lines added:** ~300 | **Tests:** 24 assertions

Implemented PostgreSQL-style bulk data import/export.

- ✅ COPY table_name TO 'filename' — export table to CSV file
- ✅ COPY table_name FROM 'filename' — import CSV data into table
- ✅ COPY (query) TO 'filename' — export query results
- ✅ COPY table TO STDOUT — return CSV data as result rows
- ✅ CSV format with proper quoting (commas in text, escaped quotes)
- ✅ Header row handling (include/skip on import)
- ✅ WITH (FORMAT CSV, HEADER, DELIMITER 'x') options
- ✅ Type-aware import (INTEGER, REAL, TEXT columns)
- ✅ CSV round-trip fidelity (export then import preserves data)

**Test file:** test_phase34_copy.zia (24 assertions)
**Key files modified:** executor.zia

---

#### Phase 35: CALL Statement — DONE
**Status:** Complete | **Lines added:** ~100 | **Tests:** 15 assertions

Implemented CALL statement for executing stored functions.

- ✅ CALL function_name(arg1, arg2, ...) — execute stored function
- ✅ CALL with VOID return type (side-effects only, returns CALL message)
- ✅ CALL with result return (returns function's query result)
- ✅ Parameter substitution ($1, $2 placeholders in function body)
- ✅ CALL with no arguments
- ✅ Error handling: nonexistent function, wrong argument count

**Test file:** test_phase35_call.zia (15 assertions)
**Key files modified:** token.zia, lexer.zia, executor.zia

**Dependencies:** Phase 15 (WAL Undo)

---

#### Phase 36: Array Type & Functions — DONE
**Status:** Complete | **Lines added:** ~250 | **Tests:** 41 assertions

Implemented PostgreSQL-compatible array type with constructor syntax and 9 array functions.

- ✅ SQL_ARRAY type (kind=9, PG-format textValue `{1,2,3}`)
- ✅ ARRAY[...] constructor syntax with expression evaluation
- ✅ ARRAY_LENGTH(arr, dim) — element count
- ✅ ARRAY_UPPER(arr, dim) / ARRAY_LOWER(arr, dim) — bounds
- ✅ ARRAY_TO_STRING(arr, delimiter) — join elements
- ✅ ARRAY_APPEND(arr, elem) / ARRAY_PREPEND(elem, arr) — add elements
- ✅ ARRAY_CAT(arr1, arr2) — concatenate arrays
- ✅ ARRAY_REMOVE(arr, elem) — remove occurrences
- ✅ ARRAY_POSITION(arr, elem) — find element (1-based, NULL if not found)
- ✅ TYPEOF(array) returns "array"
- ✅ Array with arithmetic expressions as elements

**Test file:** test_phase36_arrays.zia (41 assertions)
**Key files modified:** token.zia, lexer.zia, types.zia, parser.zia, sql_functions.zia, executor.zia

---

#### Phase 37: Materialized Views — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 41 assertions

Implemented PostgreSQL-style materialized views with snapshot semantics.

- ✅ CREATE MATERIALIZED VIEW name AS SELECT ... — executes query and stores result
- ✅ REFRESH MATERIALIZED VIEW name — re-executes query and replaces stored data
- ✅ DROP MATERIALIZED VIEW name — removes view and backing table
- ✅ SELECT from materialized view (reads stored data, no re-execution)
- ✅ Snapshot semantics: changes to source tables don't affect matview until REFRESH
- ✅ Materialized views with aggregation (GROUP BY, SUM, etc.)
- ✅ Name conflict detection (tables, views, duplicate matviews)
- ✅ Error handling: duplicate name, nonexistent matview

**Test file:** test_phase37_matviews.zia (41 assertions)
**Key files modified:** token.zia, lexer.zia, database.zia, executor.zia

---

#### Phase 38: SQL Expression Enhancements — DONE
**Status:** Complete | **Lines added:** ~250 | **Tests:** 51 assertions

Added missing SQL functions and operators for PostgreSQL compatibility.

- ✅ NULLIF(a, b) — returns NULL when args are equal, first arg otherwise
- ✅ ILIKE — case-insensitive LIKE pattern matching
- ✅ NOT ILIKE — negated case-insensitive LIKE
- ✅ INITCAP(s) — capitalize first letter of each word
- ✅ REPEAT(s, n) — repeat string n times
- ✅ SPLIT_PART(s, delimiter, n) — split and return nth part
- ✅ CHR(n) — ASCII code to character
- ✅ ASCII(s) — first character to ASCII code
- ✅ CONCAT_WS(sep, ...) — concatenate with separator, skipping NULLs
- ✅ FORMAT(s) — basic format function

**Test file:** test_phase38_expressions.zia (51 assertions)
**Key files modified:** token.zia, lexer.zia, parser.zia, executor.zia, sql_functions.zia

---

#### Phase 39: CREATE TABLE AS SELECT (CTAS) — DONE
**Status:** Complete | **Lines added:** ~150 | **Tests:** 24 assertions

Implemented CREATE TABLE ... AS SELECT for creating tables from query results.

- ✅ CREATE TABLE name AS SELECT ... — creates table with columns and rows from query
- ✅ CREATE TEMPORARY TABLE name AS SELECT ... — temporary CTAS
- ✅ CTAS with WHERE, GROUP BY, ORDER BY, aggregation
- ✅ CTAS with SELECT * (copy all columns)
- ✅ Result message shows row count (SELECT N)
- ✅ Independent snapshot — changes to source don't affect CTAS table
- ✅ Error handling: existing table name, invalid query
- ✅ Normal CREATE TABLE with column definitions still works

**Test file:** test_phase39_ctas.zia (24 assertions)
**Key files modified:** executor.zia

---

#### Phase 40: Aggregate Function Enhancements — DONE
**Status:** Complete | **Lines added:** ~250 | **Tests:** 34 assertions

Added new aggregate functions for string concatenation, array collection, and boolean logic.

- ✅ STRING_AGG(column, separator) — concatenate values with separator per group
- ✅ GROUP_CONCAT(column, separator) — MySQL-compatible alias for STRING_AGG
- ✅ ARRAY_AGG(column) — collect column values into a PG-format array
- ✅ BOOL_AND(column) — logical AND of all non-NULL values per group
- ✅ BOOL_OR(column) — logical OR of all non-NULL values per group
- ✅ All aggregates work with GROUP BY and without (all rows)
- ✅ NULL handling: NULLs skipped in STRING_AGG/GROUP_CONCAT, preserved in ARRAY_AGG
- ✅ Partition aggregate support for all new functions

**Test file:** test_phase40_aggregates.zia (34 assertions)
**Key files modified:** executor.zia, query.zia

---

#### Phase 41: DO Blocks — DONE
**Status:** Complete | **Lines added:** ~80 | **Tests:** 13 assertions

Implemented PostgreSQL-style anonymous code blocks for multi-statement execution.

- ✅ DO 'stmt1; stmt2; stmt3' — execute multiple SQL statements in one call
- ✅ Semicolon-separated statement splitting (respects string literals)
- ✅ Error stops execution (first failing statement returns error)
- ✅ DO message shows statement count
- ✅ DDL + DML combinations in single DO block
- ✅ Empty DO block error handling

**Test file:** test_phase41_do_blocks.zia (13 assertions)
**Key files modified:** executor.zia

---

#### Phase 42: INSERT Improvements — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 46 assertions

Enhanced INSERT statement with DEFAULT VALUES and standalone VALUES queries.

- ✅ INSERT INTO table DEFAULT VALUES — inserts row with all defaults/autoincrement/NULL
- ✅ INSERT INTO table DEFAULT VALUES with RETURNING
- ✅ DEFAULT VALUES with autoincrement columns
- ✅ DEFAULT VALUES with DEFAULT column values
- ✅ DEFAULT VALUES with NOT NULL constraint validation
- ✅ Standalone VALUES (...) queries — evaluate expressions without table
- ✅ Multi-row standalone VALUES with multiple columns
- ✅ VALUES with arithmetic expressions and function calls

**Test file:** test_phase42_insert.zia (46 assertions)
**Key files modified:** stmt.zia, parser.zia, dml.zia, executor.zia

---

#### Phase 43: Session Variables & COMMENT ON — DONE
**Status:** Complete | **Lines added:** ~300 | **Tests:** 35 assertions

Implemented PostgreSQL-compatible session variables and object comments.

- ✅ SET variable = value (with = and TO syntax)
- ✅ SET LOCAL/SESSION prefix support
- ✅ SET with dotted variable names (my_app.debug)
- ✅ SHOW variable_name (with dotted names)
- ✅ SHOW ALL — display all session variables
- ✅ RESET variable / RESET ALL
- ✅ 12 default session variables (server_version, timezone, work_mem, etc.)
- ✅ COMMENT ON TABLE name IS 'description'
- ✅ COMMENT ON COLUMN table.col IS 'description'
- ✅ COMMENT ON ... IS NULL (remove comment)
- ✅ Comment validation (table/column must exist)

**Test file:** test_phase43_session.zia (35 assertions)
**Key files modified:** token.zia, lexer.zia, executor.zia

---

#### Phase 44: System Functions — DONE
**Status:** Complete | **Lines added:** ~150 | **Tests:** 29 assertions

Added PostgreSQL-compatible system information functions.

- ✅ CURRENT_USER, USER, SESSION_USER — return current username
- ✅ CURRENT_DATABASE, CURRENT_CATALOG — return current database name
- ✅ CURRENT_SCHEMA — return current search path
- ✅ VERSION() — return server version string
- ✅ CURRENT_SETTING(name) — look up session variable
- ✅ SET_CONFIG(name, value, is_local) — set session variable
- ✅ PG_BACKEND_PID() — return session ID
- ✅ INET_SERVER_PORT() — return 5433
- ✅ PG_IS_IN_RECOVERY() — return false
- ✅ TXID_CURRENT() — return current transaction ID
- ✅ HAS_TABLE_PRIVILEGE, HAS_SCHEMA_PRIVILEGE stubs
- ✅ Parameterless keyword functions in parser (CURRENT_USER, SESSION_USER, etc.)

**Test file:** test_phase44_sysfuncs.zia (29 assertions)
**Key files modified:** parser.zia, executor.zia

---

#### Phase 45: ALTER TABLE Column Modifications — DONE
**Status:** Complete | **Lines added:** ~150 | **Tests:** 17 assertions

Implemented ALTER TABLE ... ALTER COLUMN for column-level modifications.

- ✅ ALTER TABLE t ALTER [COLUMN] col SET DEFAULT value
- ✅ ALTER TABLE t ALTER [COLUMN] col DROP DEFAULT
- ✅ ALTER TABLE t ALTER [COLUMN] col SET NOT NULL (validates no existing NULLs)
- ✅ ALTER TABLE t ALTER [COLUMN] col DROP NOT NULL
- ✅ ALTER TABLE t ALTER [COLUMN] col TYPE new_type
- ✅ COLUMN keyword is optional
- ✅ Error handling: nonexistent column, NULLs exist for SET NOT NULL
- ✅ Multiple ALTER operations on same table

**Test file:** test_phase45_alter_col.zia (17 assertions)
**Key files modified:** ddl.zia

---

#### Phase 46: SQL Standard Expressions — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 31 assertions

Added SQL-standard syntax for EXTRACT, POSITION, SUBSTRING, and TRIM.

- ✅ EXTRACT(YEAR/MONTH/DAY/HOUR/MINUTE/SECOND/DOW/EPOCH FROM source)
- ✅ POSITION(substr IN str) — returns 1-based position or 0
- ✅ SUBSTRING(str FROM pos [FOR len]) — SQL-standard syntax
- ✅ SUBSTRING(str, pos, len) — traditional comma syntax still works
- ✅ TRIM(LEADING char FROM str) — trim from start only
- ✅ TRIM(TRAILING char FROM str) — trim from end only
- ✅ TRIM(BOTH char FROM str) — trim from both sides
- ✅ TRIM(str) — simple whitespace trim still works
- ✅ EXTRACT with table column references
- ✅ POSITION with table column references

**Test file:** test_phase46_sqlfuncs.zia (31 assertions)
**Key files modified:** parser.zia, sql_functions.zia

#### Phase 47: Recursive CTEs (WITH RECURSIVE) — DONE
**Status:** Complete | **Lines added:** ~250 | **Tests:** 45 assertions

Implemented iterative fixpoint evaluation for recursive Common Table Expressions.

- ✅ WITH RECURSIVE name(col1, col2) AS (anchor UNION [ALL] recursive) SELECT ...
- ✅ Anchor member execution + iterative recursive member evaluation
- ✅ UNION ALL (accumulate all rows) and UNION (with dedup)
- ✅ Fixpoint termination (no new rows) with MAX_RECURSION = 1000
- ✅ Optional CTE column list (explicit or inferred from anchor)
- ✅ Tree traversal with JOINs (employee hierarchy example)
- ✅ Multi-column recursive CTEs (factorial, Fibonacci)
- ✅ String concatenation in recursive member
- ✅ Aggregate queries on recursive CTE results

**Test file:** test_phase47_recursive_cte.zia (45 assertions)
**Key files modified:** query.zia

---

#### Phase 48: Math Functions — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 54 assertions

Added trigonometric, logarithmic, and integer math functions.

- ✅ SIN, COS, TAN — trigonometric functions
- ✅ ASIN, ACOS, ATAN, ATAN2 — inverse trig
- ✅ DEGREES(rad), RADIANS(deg) — angle conversion
- ✅ TRUNC/TRUNCATE — truncate to integer
- ✅ CBRT — cube root
- ✅ GCD(a,b), LCM(a,b) — greatest common divisor / least common multiple
- ✅ DIV(a,b) — integer division (returns NULL on div by zero)
- ✅ GREATEST(a,b,...), LEAST(a,b,...) — variadic min/max
- ✅ ABS for REAL values (previously INTEGER only)
- ✅ NULL propagation for all math functions

**Test file:** test_phase48_math.zia (54 assertions)
**Key files modified:** sql_functions.zia

---

#### Phase 49: String Functions — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 42 assertions

Added PostgreSQL-compatible string functions for text processing and hashing.

- ✅ CHAR_LENGTH / CHARACTER_LENGTH — character count
- ✅ OCTET_LENGTH — byte count
- ✅ BIT_LENGTH — bit count (8 * bytes)
- ✅ TRANSLATE(str, from, to) — character substitution with deletion
- ✅ OVERLAY(str, new, start [, count]) — substring replacement
- ✅ STARTS_WITH(str, prefix), ENDS_WITH(str, suffix) — boolean tests
- ✅ QUOTE_LITERAL(str), QUOTE_IDENT(str) — SQL quoting
- ✅ MD5(str), SHA256(str) — cryptographic hash functions
- ✅ TO_HEX(int) — integer to hex string

**Test file:** test_phase49_strings.zia (42 assertions)
**Key files modified:** sql_functions.zia

---

#### Phase 50: Date/Time Functions — DONE
**Status:** Complete | **Lines added:** ~300 | **Tests:** 62 assertions

Added PostgreSQL-compatible date/time functions beyond the core extraction and arithmetic.

- ✅ DATE_PART(field, source) — extract component (year, month, day, hour, minute, second, dow, epoch)
- ✅ DATE_TRUNC(field, source) — truncate to specified precision (year, month, day, hour, minute)
- ✅ AGE(timestamp1, timestamp2) — interval between timestamps
- ✅ TO_CHAR(timestamp, format) — format timestamp as string
- ✅ MAKE_DATE(year, month, day) — construct date
- ✅ MAKE_TIMESTAMP(y, mo, d, h, mi, s) — construct timestamp
- ✅ MAKE_INTERVAL(years, months, days) — construct interval
- ✅ ISFINITE(timestamp) — check if timestamp is finite
- ✅ TO_TIMESTAMP(epoch) — convert epoch to timestamp
- ✅ TO_DATE(str, format) — parse date string
- ✅ CLOCK_TIMESTAMP(), STATEMENT_TIMESTAMP(), TRANSACTION_TIMESTAMP() — timing functions
- ✅ TIMEOFDAY() — current time as text string

**Test file:** test_phase50_datetime.zia (62 assertions)
**Key files modified:** sql_functions.zia

---

#### Phase 51: Regular Expressions — DONE
**Status:** Complete | **Lines added:** ~400 | **Tests:** 48 assertions

Implemented regex pattern matching with operators and functions.

- ✅ `~` operator (regex match), `~*` (case-insensitive regex match)
- ✅ SIMILAR TO pattern matching (SQL-standard wildcards → regex conversion)
- ✅ NOT SIMILAR TO
- ✅ REGEXP_MATCH(text, pattern) — test for match (returns 1/0)
- ✅ REGEXP_MATCHES(text, pattern) — return all matches as array
- ✅ REGEXP_REPLACE(text, pattern, replacement) — regex substitution
- ✅ REGEXP_COUNT(text, pattern) — count non-overlapping matches
- ✅ REGEXP_SPLIT_TO_ARRAY(text, pattern) — split into array by pattern
- ✅ REGEXP_SUBSTR(text, pattern) — extract first match
- ✅ REGEXP_INSTR(text, pattern) — position of first match (1-based)
- ✅ REGEXP_LIKE(text, pattern) — boolean match test

**Test file:** test_phase51_regex.zia (48 assertions)
**Key files modified:** token.zia, lexer.zia, expr.zia, parser.zia, executor.zia, sql_functions.zia

---

#### Phase 52: Table Inheritance — DONE
**Status:** Complete | **Lines added:** ~300 | **Tests:** 45 assertions

Implemented PostgreSQL-style table inheritance with polymorphic queries.

- ✅ CREATE TABLE child (extra_cols) INHERITS (parent) — column inheritance
- ✅ Polymorphic queries: SELECT from parent returns parent + all child rows
- ✅ SELECT ... FROM ONLY parent — exclude child rows
- ✅ WHERE filter on inherited queries
- ✅ SELECT * from parent (projects only parent columns from child rows)
- ✅ Multiple children per parent (fan-out inheritance)
- ✅ LIMIT/OFFSET on inherited queries
- ✅ ORDER BY on inherited queries
- ✅ Error handling for nonexistent parent tables

**Test file:** test_phase52_inherit.zia (45 assertions)
**Key files modified:** token.zia, lexer.zia, stmt.zia, parser.zia, table.zia, ddl.zia, query.zia

---

#### Phase 53: Generated Columns (GENERATED ALWAYS AS) — DONE
**Status:** Complete | **Lines added:** ~250 | **Tests:** 38 assertions

Implemented SQL-standard generated (computed) columns with STORED semantics.

- ✅ GENERATED ALWAYS AS (expr) STORED — define computed columns
- ✅ Expression stored as SQL text, parsed and evaluated at INSERT/UPDATE time
- ✅ Positional INSERT skips generated columns automatically
- ✅ UPDATE recomputes generated columns after SET
- ✅ Direct UPDATE of generated columns blocked with error
- ✅ String, arithmetic, and function expressions (UPPER, CASE, etc.)
- ✅ Multiple generated columns per table
- ✅ WHERE clause filtering on generated columns
- ✅ DESCRIBE shows GENERATED ALWAYS AS (...) STORED

**Test file:** test_phase53_generated.zia (38 assertions)
**Key files modified:** token.zia, lexer.zia, schema.zia, parser.zia, dml.zia

---

#### Phase 54: FILTER Clause for Aggregates — DONE
**Status:** Complete | **Lines added:** ~150 | **Tests:** 30 assertions

Implemented SQL-standard FILTER (WHERE ...) clause for aggregate functions.

- ✅ COUNT/SUM/AVG/MIN/MAX with FILTER (WHERE condition)
- ✅ Multiple aggregates with different FILTER clauses in same SELECT
- ✅ FILTER with GROUP BY
- ✅ Mixed filtered and unfiltered aggregates
- ✅ Complex FILTER conditions (AND, comparison operators)
- ✅ FILTER returning no matches (returns 0 for COUNT, NULL for others)
- ✅ Pre-filter approach: matchingRows filtered before aggregate evaluation

**Test file:** test_phase54_filter.zia (30 assertions)
**Key files modified:** token.zia, lexer.zia, expr.zia, parser.zia, query.zia

---

#### Phase 55: LATERAL Joins — DONE
**Status:** Complete | **Lines added:** ~400 | **Tests:** 38 assertions

Implemented LATERAL subquery joins — each row of the outer table drives a correlated subquery.

- ✅ Basic LATERAL with comma syntax (FROM t1, LATERAL (SELECT ...))
- ✅ LATERAL with LIMIT (top-N per group)
- ✅ LATERAL with aggregate functions (COUNT, SUM, MAX)
- ✅ LEFT JOIN LATERAL (preserves unmatched outer rows with NULL)
- ✅ CROSS JOIN LATERAL
- ✅ LATERAL with expressions and multiple output columns
- ✅ LATERAL with no matching rows (empty result set)
- ✅ SQL text substitution approach for outer column references

**Test file:** test_phase55_lateral.zia (38 assertions)
**Key files modified:** token.zia, lexer.zia, stmt.zia, parser.zia, join.zia

---

#### Phase 56: NATURAL JOIN and USING — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 67 assertions

Implemented NATURAL JOIN (auto-detect common columns) and JOIN ... USING (col1, col2) clause.

- ✅ NATURAL JOIN (inner) with single common column
- ✅ NATURAL LEFT JOIN, NATURAL RIGHT JOIN, NATURAL FULL JOIN
- ✅ NATURAL INNER JOIN (explicit INNER keyword)
- ✅ NATURAL JOIN with multiple common columns
- ✅ NATURAL JOIN with no common columns (degrades to CROSS JOIN)
- ✅ JOIN ... USING (single column)
- ✅ JOIN ... USING (multiple columns)
- ✅ LEFT JOIN ... USING
- ✅ JOIN USING with WHERE clause
- ✅ Condition synthesis: converts NATURAL/USING to equi-join expressions at execution time

**Test file:** test_phase56_natural_using.zia (67 assertions)
**Key files modified:** token.zia, lexer.zia, stmt.zia, parser.zia, join.zia

---

#### Phase 57: EXCEPT ALL, INTERSECT ALL & Chained Set Operations — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 56 assertions

Implemented ALL variants for EXCEPT/INTERSECT and chained (multi-operand) set operations.

- ✅ EXCEPT ALL (preserves duplicates: N-M copies)
- ✅ INTERSECT ALL (preserves duplicates: min(N,M) copies)
- ✅ Chained UNION/UNION ALL (3+ operands)
- ✅ Chained 4-way UNION ALL
- ✅ Mixed chained operations (UNION ALL + EXCEPT, UNION ALL + INTERSECT)
- ✅ Edge cases: no overlap, total overlap, empty sets, text values
- ✅ Loop-based execution: left-to-right evaluation of chained set operations
- ✅ Fixed pre-existing `stringContains` missing function in sql_functions.zia

**Test file:** test_phase57_setops_all.zia (56 assertions)
**Key files modified:** setops.zia, executor.zia, sql_functions.zia

---

#### Phase 58: FETCH FIRST / OFFSET-FETCH Syntax — DONE
**Status:** Complete | **Lines added:** ~30 | **Tests:** 63 assertions

Implemented SQL-standard FETCH FIRST/OFFSET-FETCH paging syntax as an alternative to PostgreSQL LIMIT/OFFSET.

- ✅ FETCH FIRST N ROWS ONLY (basic paging)
- ✅ FETCH NEXT N ROWS ONLY (synonym for FIRST)
- ✅ FETCH FIRST 1 ROW ONLY (singular ROW keyword)
- ✅ OFFSET N ROWS (SQL-standard skip syntax)
- ✅ OFFSET N ROWS FETCH FIRST M ROWS ONLY (combined)
- ✅ Works with ORDER BY, WHERE, GROUP BY
- ✅ LIMIT/OFFSET compatibility preserved (no conflict)
- ✅ Edge cases: offset beyond data, fetch larger than result, offset zero

**Test file:** test_phase58_fetch_first.zia (63 assertions)
**Key files modified:** parser.zia

---

#### Phase 59: GROUPING SETS / ROLLUP / CUBE — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 41 assertions

Implemented advanced GROUP BY features for multi-level aggregation.

- ✅ GROUP BY ROLLUP (col1, col2, ...) — hierarchical subtotals + grand total
- ✅ GROUP BY CUBE (col1, col2, ...) — all possible subtotal combinations (2^n subsets)
- ✅ GROUP BY GROUPING SETS ((...), (...), ()) — explicit grouping set specification
- ✅ NULL output for columns not in active grouping set (standard SQL behavior)
- ✅ Works with SUM, COUNT, AVG, MIN, MAX aggregates
- ✅ HAVING clause support with ROLLUP/CUBE/GROUPING SETS
- ✅ 1, 2, and 3-column ROLLUP and CUBE
- ✅ Grand total only via GROUPING SETS (())

**Test file:** test_phase59_grouping_sets.zia (41 assertions)
**Key files modified:** token.zia, lexer.zia, stmt.zia, parser.zia, query.zia

---

#### Phase 60: SELECT DISTINCT ON — DONE
**Status:** Complete | **Lines added:** ~120 | **Tests:** 55 assertions

Implemented PostgreSQL's DISTINCT ON for returning first row per group.

- ✅ SELECT DISTINCT ON (expr) — single column deduplication
- ✅ SELECT DISTINCT ON (expr1, expr2) — multi-column deduplication
- ✅ Works with ORDER BY to control which row is "first" (highest, lowest, etc.)
- ✅ Works with WHERE, LIMIT, SELECT *
- ✅ Hash-based deduplication with column name matching
- ✅ Join query support
- ✅ Without ORDER BY, returns first inserted row per group

**Test file:** test_phase60_distinct_on.zia (55 assertions)
**Key files modified:** stmt.zia, parser.zia, query.zia, join.zia

---

#### Phase 61: Window Frame Specifications — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 83 assertions

Implemented ROWS BETWEEN frame specifications for window functions.

- ✅ ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW (running aggregate)
- ✅ ROWS BETWEEN N PRECEDING AND M FOLLOWING (sliding window)
- ✅ ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING (reverse running)
- ✅ ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING (full partition)
- ✅ ROWS N PRECEDING (shorthand for BETWEEN N PRECEDING AND CURRENT ROW)
- ✅ Frame-bounded SUM, COUNT, AVG, MIN, MAX
- ✅ Frame specs with PARTITION BY
- ✅ Backward-compatible with existing window functions (no frame = default behavior)

**Test file:** test_phase61_window_frames.zia (83 assertions)
**Key files modified:** expr.zia, parser.zia, sql_window.zia

---

#### Phase 62: NULLS FIRST / NULLS LAST — DONE
**Status:** Complete | **Lines added:** ~100 | **Tests:** 102 assertions

ORDER BY columns can specify explicit NULL ordering with `NULLS FIRST` or `NULLS LAST` modifiers. Default is NULLs last for ASC, NULLs first for DESC.

**Features:**
- ✅ `ORDER BY col ASC NULLS FIRST` — NULLs sorted to top regardless of direction
- ✅ `ORDER BY col DESC NULLS LAST` — NULLs sorted to bottom regardless of direction
- ✅ Multi-column ORDER BY with per-column NULLS mode
- ✅ Works with WHERE, LIMIT, GROUP BY, JOINs
- ✅ Fixed pre-existing bug: multi-column ORDER BY with both-NULL values now correctly continues to subsequent sort keys

**Test file:** test_phase62_nulls_first_last.zia (102 assertions)
**Key files modified:** stmt.zia, parser.zia, query.zia, join.zia

---

#### Phase 63: Additional Window Functions — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 107 assertions

Eight additional window functions completing SQL standard window function support.

**Features:**
- ✅ `LAG(expr [, offset [, default]])` — value from previous row(s) in partition
- ✅ `LEAD(expr [, offset [, default]])` — value from next row(s) in partition
- ✅ `FIRST_VALUE(expr)` — first value in partition
- ✅ `LAST_VALUE(expr)` — last value in partition
- ✅ `NTH_VALUE(expr, n)` — nth value (1-based) in partition
- ✅ `NTILE(n)` — divide partition into n approximately equal buckets
- ✅ `PERCENT_RANK()` — relative rank as decimal (0 to 1)
- ✅ `CUME_DIST()` — cumulative distribution as decimal (0 to 1)
- ✅ All functions work with PARTITION BY and ORDER BY
- ✅ LAG/LEAD support custom offset and default values

**Test file:** test_phase63_window_funcs.zia (107 assertions)
**Key files modified:** sql_window.zia

---

#### Phase 64: IS DISTINCT FROM / IS NOT DISTINCT FROM — DONE
**Status:** Complete | **Lines added:** ~50 | **Tests:** 59 assertions

NULL-safe comparison operators that treat NULL values as equal rather than unknown.

**Features:**
- ✅ `expr IS DISTINCT FROM expr` — NULL-safe inequality (NULL = NULL → false, i.e., not distinct)
- ✅ `expr IS NOT DISTINCT FROM expr` — NULL-safe equality (NULL = NULL → true)
- ✅ Works in WHERE, SELECT, UPDATE conditions
- ✅ Column vs column, column vs literal, and NULL comparisons
- ✅ TEXT and INTEGER type support
- ✅ Backward-compatible with IS NULL / IS NOT NULL

**Test file:** test_phase64_is_distinct.zia (59 assertions)
**Key files modified:** expr.zia, parser.zia, executor.zia

#### Phase 65: IF EXISTS / IF NOT EXISTS / CREATE OR REPLACE — DONE
**Status:** Complete | **Lines added:** ~150 | **Tests:** 55 assertions

Defensive DDL operations that prevent errors when objects already exist or don't exist.

**Features:**
- ✅ `DROP TABLE IF EXISTS` — no error if table doesn't exist
- ✅ `DROP INDEX IF EXISTS` — no error if index doesn't exist
- ✅ `DROP VIEW IF EXISTS` — no error if view doesn't exist
- ✅ `DROP DATABASE IF EXISTS` — no error if database doesn't exist
- ✅ `DROP TRIGGER IF EXISTS` — no error if trigger doesn't exist
- ✅ `DROP SEQUENCE IF EXISTS` — no error if sequence doesn't exist
- ✅ `DROP FUNCTION IF EXISTS` — no error if function doesn't exist
- ✅ `DROP USER IF EXISTS` — no error if user doesn't exist
- ✅ `CREATE TABLE IF NOT EXISTS` — skip if table already exists
- ✅ `CREATE INDEX IF NOT EXISTS` — skip if index already exists
- ✅ `CREATE OR REPLACE VIEW` — replace existing view or create new
- ✅ `CREATE OR REPLACE FUNCTION` — replace existing function or create new
- ✅ Idempotent DDL script pattern support

**Test file:** test_phase65_if_exists.zia (55 assertions)
**Key files modified:** stmt.zia, parser.zia, ddl.zia, executor.zia

---

#### Phase 66: SHOW CREATE TABLE — DONE
**Status:** Complete | **Lines added:** ~120 | **Tests:** 36 assertions

DDL introspection: generate CREATE TABLE statements from table metadata.

**Features:**
- ✅ `SHOW CREATE TABLE tablename` — returns DDL for table
- ✅ Column types: INTEGER, TEXT, REAL, BOOLEAN, BLOB
- ✅ Column constraints: PRIMARY KEY, NOT NULL, UNIQUE, AUTOINCREMENT
- ✅ DEFAULT values in column definitions
- ✅ Foreign key references with ON DELETE/UPDATE actions
- ✅ CHECK constraints
- ✅ GENERATED ALWAYS AS ... STORED columns
- ✅ PARTITION BY and INHERITS metadata
- ✅ Index DDL as additional rows (CREATE INDEX, CREATE UNIQUE INDEX)
- ✅ Round-trip: DDL output is valid SQL that recreates the table
- ✅ Error handling for non-existent tables

**Test file:** test_phase66_show_create.zia (36 assertions)
**Key files modified:** ddl.zia, index.zia, executor.zia

---

#### Phase 67: SHOW INDEXES, SHOW COLUMNS, CREATE TABLE LIKE — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 56 assertions

Schema introspection commands and table structure cloning.

**Features:**
- ✅ `SHOW INDEXES` — list all indexes across all tables
- ✅ `SHOW INDEXES FROM tablename` — list indexes for a specific table
- ✅ Index metadata: name, table, columns (including composite), uniqueness
- ✅ `SHOW COLUMNS FROM tablename` — tabular column metadata
- ✅ Column metadata: name, type, nullable, default, primary key
- ✅ `CREATE TABLE new (LIKE source)` — clone table structure without data
- ✅ Cloned tables preserve: types, constraints (PK, NOT NULL, UNIQUE), defaults, CHECK, generated columns
- ✅ Error handling for non-existent tables, duplicate names

**Test file:** test_phase67_introspection.zia (56 assertions)
**Key files modified:** ddl.zia, executor.zia

---

#### Phase 68: TABLE Expression, Composite PRIMARY KEY, Named Constraints — DONE
**Status:** Complete | **Lines added:** ~250 | **Tests:** 33 assertions

SQL convenience features: TABLE expression shorthand, composite primary keys, and named constraints.

**Features:**
- ✅ `TABLE tablename` — shorthand for `SELECT * FROM tablename`
- ✅ `PRIMARY KEY (col1, col2)` — table-level composite primary key constraint
- ✅ Composite PK uniqueness: rejects duplicates only when ALL PK columns match
- ✅ `CONSTRAINT name CHECK (...)` — named constraints on columns
- ✅ `CONSTRAINT name PRIMARY KEY (...)` — named table-level PK constraints
- ✅ Table-level `CHECK (...)` without CONSTRAINT keyword
- ✅ Table-level `UNIQUE (...)` and `FOREIGN KEY (...)` parsing
- ✅ SHOW CREATE TABLE reflects composite PK columns
- ✅ SHOW COLUMNS shows PK status for composite PK columns

**Test file:** test_phase68_table_pk_constraints.zia (33 assertions)
**Key files modified:** executor.zia, parser.zia, stmt.zia, schema.zia, ddl.zia, dml.zia

---

#### Phase 69: LIMIT PERCENT, SELECT INTO, Multi-table TRUNCATE — DONE
**Status:** Complete | **Lines added:** ~180 | **Tests:** 23 assertions

Query and DDL convenience features: percentage-based limits, table creation from queries, and multi-table truncation.

**Features:**
- ✅ `LIMIT n PERCENT` — returns n% of total rows (with ORDER BY for top-N%)
- ✅ Minimum 1 row for PERCENT when table is non-empty
- ✅ `SELECT ... INTO tablename FROM ...` — create new table from query results
- ✅ Error handling for SELECT INTO with existing table
- ✅ `TRUNCATE TABLE t1, t2, t3` — truncate multiple tables in one statement
- ✅ Single-table TRUNCATE regression preserved

**Test file:** test_phase69_query_extras.zia (23 assertions)
**Key files modified:** executor.zia, parser.zia, stmt.zia, query.zia

---

#### Phase 70: BETWEEN SYMMETRIC, TABLESAMPLE, Column Aliases in ORDER BY — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 31 assertions

Advanced query features: symmetric range checks, random table sampling, and alias-based sorting.

**Features:**
- ✅ `BETWEEN SYMMETRIC low AND high` — matches regardless of argument order
- ✅ `NOT BETWEEN SYMMETRIC` — symmetric exclusion
- ✅ `FROM table TABLESAMPLE SYSTEM (n)` — random n% row sampling
- ✅ `FROM table TABLESAMPLE BERNOULLI (n)` — alias for SYSTEM sampling
- ✅ TABLESAMPLE 0% returns no rows, 100% returns all rows
- ✅ Column aliases in ORDER BY (`SELECT price AS cost ... ORDER BY cost`)
- ✅ Alias resolution works for both ASC and DESC ordering
- ✅ Column alias displayed in result headers

**Test file:** test_phase70_query_features.zia (31 assertions)
**Key files modified:** parser.zia, stmt.zia, query.zia

---

#### Phase 71: ANY/ALL/SOME Quantified Comparisons — DONE
**Status:** Complete | **Lines added:** ~200 | **Tests:** 23 assertions

SQL-standard quantified subquery comparisons for flexible row filtering.

**Features:**
- ✅ `= ANY (SELECT ...)` — equivalent to IN subquery
- ✅ `> ANY (SELECT ...)` — greater than at least one subquery value
- ✅ `< ANY (SELECT ...)` — less than at least one
- ✅ `>= ANY`, `<= ANY`, `<> ANY` — all comparison operators
- ✅ `> ALL (SELECT ...)` — greater than every subquery value
- ✅ `< ALL (SELECT ...)` — less than every value
- ✅ `<> ALL (SELECT ...)` — not equal to any (like NOT IN)
- ✅ `= SOME (SELECT ...)` — synonym for ANY
- ✅ ALL with empty subquery returns true (vacuous truth)
- ✅ ANY with empty subquery returns false
- ✅ NULL handling (NULL left operand returns false)

**Test file:** test_phase71_any_all_some.zia (23 assertions)
**Key files modified:** expr.zia, parser.zia, executor.zia

---

#### Phase 72: EXCLUDE CURRENT ROW Window Frame Clause — DONE
**Status:** Complete | **Lines added:** ~180 | **Tests:** 26 assertions

Window frame exclusion for precision analytics: exclude the current row from aggregate calculations.

**Features:**
- ✅ `EXCLUDE CURRENT ROW` — skip current row from frame aggregation
- ✅ Works with SUM, COUNT, AVG, MIN, MAX aggregates
- ✅ Works with sliding windows (`ROWS BETWEEN N PRECEDING AND N FOLLOWING`)
- ✅ Works with full partition frames (`UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING`)
- ✅ `EXCLUDE NO OTHERS` — explicit default (include all rows)
- ✅ `EXCLUDE GROUP` and `EXCLUDE TIES` syntax recognized (parser support)

**Test file:** test_phase72_window_exclude.zia (26 assertions)
**Key files modified:** expr.zia, parser.zia, sql_window.zia

---

#### Phase 73: MERGE INTO — DONE
**Status:** Complete | **Lines added:** ~210 | **Tests:** 41 assertions

Standard SQL MERGE statement for upsert and conditional DML operations.

**Features:**
- ✅ `MERGE INTO target USING source ON condition` — two-table join-based DML
- ✅ `WHEN MATCHED THEN UPDATE SET` — update matching target rows from source
- ✅ `WHEN MATCHED THEN DELETE` — delete matching target rows
- ✅ `WHEN NOT MATCHED THEN INSERT` — insert source rows with no target match
- ✅ Combined WHEN MATCHED + WHEN NOT MATCHED in single statement (upsert)
- ✅ Table aliases (`AS t`, `AS s`) and table-qualified column names
- ✅ Multi-table expression evaluation (source columns in SET/VALUES)
- ✅ Default value and autoincrement handling for inserts
- ✅ MVCC visibility and transaction journal integration

**Test file:** test_phase73_merge.zia (41 assertions)
**Key files modified:** token.zia, lexer.zia, stmt.zia, parser.zia, executor.zia, dml.zia

---

#### Phase 74: Partial Indexes — DONE
**Status:** Complete | **Lines added:** ~130 | **Tests:** 32 assertions

Partial indexes with WHERE predicates for selective indexing — index only rows matching a condition.

**Features:**
- ✅ `CREATE INDEX idx ON t (col) WHERE condition` — partial index with predicate
- ✅ `CREATE UNIQUE INDEX ... WHERE condition` — partial unique constraint
- ✅ WHERE predicates: comparisons (`=`, `>`, `<`, `>=`, `<=`, `!=`), `IS NOT NULL`, `AND`/`OR`
- ✅ Index rebuild respects WHERE predicate (only matching rows indexed)
- ✅ INSERT maintenance: new rows checked against predicate before index entry
- ✅ `SHOW INDEXES` displays partial flag column
- ✅ Partial unique indexes allow duplicates in non-matching rows

**Test file:** test_phase74_partial_indexes.zia (32 assertions)
**Key files modified:** index.zia, stmt.zia, parser.zia, ddl.zia, dml.zia

---

#### Phase 75: Named WINDOW Clause — DONE
**Status:** Complete | **Lines added:** ~170 | **Tests:** 37 assertions

Named window definitions for reusable window specifications across multiple window functions.

**Features:**
- ✅ `WINDOW w AS (PARTITION BY col ORDER BY col)` — named window definition
- ✅ `SUM(col) OVER w` — reference named window by name
- ✅ Multiple named windows: `WINDOW w1 AS (...), w2 AS (...)`
- ✅ Multiple functions sharing same named window
- ✅ Full window spec in definitions: PARTITION BY, ORDER BY, frame specs
- ✅ Works with all window functions: SUM, COUNT, AVG, ROW_NUMBER, etc.

**Test file:** test_phase75_named_windows.zia (37 assertions)
**Key files modified:** expr.zia, stmt.zia, parser.zia, sql_window.zia

---

#### Phase 76: Row Value Constructors — DONE
**Status:** Complete | **Lines added:** ~150 | **Tests:** 33 assertions

Row value constructor expressions for multi-column comparisons and tuple matching.

**Features:**
- ✅ `(a, b) = (1, 2)` — row value equality comparison
- ✅ `(a, b) <> (1, 2)` — row value inequality
- ✅ `(a, b) < (x, y)` — lexicographic less-than ordering
- ✅ `(a, b) <= (x, y)`, `(a, b) > (x, y)`, `(a, b) >= (x, y)` — full ordering
- ✅ `(a, b) IN ((1, 2), (3, 4))` — row value IN tuple list
- ✅ Three+ element row values: `(a, b, c) = (1, 2, 3)`
- ✅ Expressions in row values: `(a + 1, b) = (4, 4)`

**Test file:** test_phase76_row_values.zia (33 assertions)
**Key files modified:** expr.zia, parser.zia, executor.zia

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
5. ~~**Row-level locking** (Phase 14)~~ — **DONE** ✓
6. **MVCC** (Phase 16) — readers should not block writers

### Medium-Priority Gaps (feature completeness)
7. **Triggers** (Phase 17) — common application requirement
8. **Sequences** (Phase 18) — PG-standard auto-increment
9. ~~**JSON support** (Phase 25)~~ — **DONE** ✓

---

## Zia Language Fixes Needed

Issues discovered during ViperSQL development that require compiler-level fixes:

1. ~~**Bytes type inference**~~ — **FIXED** (BUG-FE-010). Cross-class Ptr method/property fallback now searches all runtime classes when primary class lookup fails. No more silent 0/null returns.

2. ~~**Chained method calls**~~ — **FIXED** (BUG-FE-008). `defineExternFunction()` was returning the full Function type instead of extracting the return type. Chained calls like `bytes.Slice(x,y).ToStr()` now work correctly.

3. ~~**List[Boolean] boxing**~~ — **FIXED** (BUG-FE-009). `emitUnbox` for I1 now uses I64 return type matching the `rt_unbox_i1` runtime signature `"i64(obj)"`. `List[Boolean].get(i)` works correctly in boolean expressions.

4. ~~**FOR loop support**~~ — **NOT A BUG.** Zia fully supports `for (init; cond; update)` and `for x in collection` loops with ranges (`0..10`, `0..=10`), tuple destructuring, break/continue. Used throughout demo projects.

5. ~~**String interpolation**~~ — **NOT A BUG.** Zia supports `"Hello ${name}"` interpolation with `${}` syntax, multi-expression support, and `\$` escaping.

6. ~~**Match/switch**~~ — **NOT A BUG.** Zia has `match` expressions with wildcard, literal, binding, constructor, tuple, and guard patterns. Dedicated 502-line lowering module.

Fixes 1-3 are verified with regression tests in `test_zia_bugfixes.cpp`. Items 4-6 were incorrectly listed — these features exist in Zia but weren't used in the ViperSQL codebase. All compiler bugs tracked in `PLATFORM_BUGS.md`.

---

## Viper Runtime Gaps (Classes & Methods Missing from Viper.*)

Audit of the Viper runtime (194+ classes) against ViperSQL needs. These are things that **don't exist** in the platform and require workarounds.

### Tier 1: Missing Runtime Classes (highest impact)

| # | Missing Class/Method | Type | Workaround LOC | Impact |
|---|---------------------|------|----------------|--------|
| 1 | **IntMap** (integer-keyed dictionary) | New class | ~300 | `Viper.Collections.Map` is string-keyed only. All integer-keyed lookups (session IDs, row indices, page IDs) use parallel lists with O(n) scan. |
| 2 | **BinaryBuffer** (positioned binary I/O) | New class | ~250 | No cursor-based binary read/write. Serializer and pg_wire both implement custom byte buffers with manual int16/32/64 encoding via division/modulo. |
| 3 | **Core.Parse.Int/Float** | New methods | ~70 | No string→number parsing. Manual digit-by-digit parsing in `types.zia`. `Core.Convert.ToInt` may exist but is unused/undiscoverable. |
| 4 | **List.SortBy(comparator)** | New method | ~200 | `Seq.SortBy` exists but `List` has no comparator sort. Manual quicksort in query.zia (100 lines) and join.zia (80 lines). |
| 5 | **Hash.Fast(str)** | New method | ~20 | No non-cryptographic string hash. `Crypto.Hash.SHA256` is too slow. Manual hash in index.zia uses only 32K range with poor distribution. |

### Tier 2: Missing Methods for Future Phases

| # | Missing Class/Method | Type | Needed For |
|---|---------------------|------|-----------|
| 6 | **DateTime.Parse(str)** | New method | DATE/TIMESTAMP string parsing (currently manual substring extraction) |
| 7 | **String.Like(text, pattern)** | New method | SQL LIKE operator (% and _ wildcards); no simple wildcard matching exists |
| 8 | **Bytes.ReadInt32LE/BE(offset)** | New methods | Multi-byte binary read/write at offset; only per-byte Get/Set exists |
| 9 | **Map.SetInt/GetInt** | New methods | Typed map accessors to avoid manual Box.I64/ToI64 wrapping |

### Tier 3: Missing Language Features

| # | Feature | Impact |
|---|---------|--------|
| 10 | **Enum types** | token.zia has 225+ `final Integer` constants with no type safety. Any integer accepted where LockMode/SqlType expected. |
| 11 | **Operator overloading** | SqlValue comparison requires `.compare()` method calls. Every WHERE/ORDER BY/GROUP BY uses manual comparison at hundreds of call sites. |
| 12 | **Default parameters** | AST has `defaultValue` field but not implemented in sema/lowerer. Would reduce overloaded function variants. |
| 13 | **Destructuring bind** | Tuples exist but can't do `var (x, y) = getResult()`. Must extract fields one at a time. |

### Tier 4: Future Scalability

| # | Feature | Needed For |
|---|---------|-----------|
| 14 | **Async TCP / Event Loop** | Non-blocking I/O for connection pooling at scale (Phase 23). Currently one-thread-per-connection with blocking Recv. |
| 15 | **Memory-mapped files (mmap)** | Large database performance (Phase 29). Currently all page I/O through File.ReadAllBytes. |

### Existing Runtime Classes That Should Be Adopted

These classes **already exist** in Viper.* but aren't used by ViperSQL yet:

| Class | Current Workaround | Potential Use |
|-------|-------------------|---------------|
| `Viper.Collections.Map` | Parallel `List[String]` pairs | Replace all string-keyed parallel lists in server.zia, database.zia |
| `Viper.Text.StringBuilder` | O(n²) string concatenation in loops | serializer.zia readString(), result.zia formatting |
| `Viper.Collections.Set` | Linear membership scans | GROUP BY column sets, DISTINCT tracking |
| `Viper.Threads.ConcurrentMap` | Monitor-wrapped parallel lists | Thread-safe shared state in server.zia |
| `Viper.Threads.RwLock` | Exclusive monitors everywhere | Phase 16 MVCC (readers shouldn't block writers) |
| `Viper.Collections.TreeMap` | No in-memory ordered index | Range scan optimization (WHERE BETWEEN) |
| `Viper.Collections.Heap` | Full sort + take N | ORDER BY ... LIMIT N optimization (top-k) |
| `Viper.Text.Json` / `JsonPath` | Pure-Zia JSON parser | Phase 25 ✓ (implemented with custom parser for safety) |
| `Viper.Math.BigInt` | Not implemented | Phase 24 DECIMAL/NUMERIC type (60% effort reduction) |
| `Viper.Threads.Pool` | One thread per connection | Phase 23 Connection Pooling (50% effort reduction) |
| `Viper.Threads.SafeI64` | Not implemented | Phase 18 Sequences (atomic counter IS a sequence) |
| `Viper.Collections.CountMap` | Not implemented | Phase 27 pg_stat views (stat collection built-in) |
| `Viper.Time.Stopwatch` | No query timing | EXPLAIN ANALYZE support |
| `Viper.Collections.BloomFilter` | No pre-filtering | Hash join probe optimization |
| `Viper.Threads.CancelToken` | No query cancellation | Long-running query termination |
