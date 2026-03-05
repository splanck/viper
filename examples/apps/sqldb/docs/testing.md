# Testing

## Table of Contents

- [Running Tests](#running-tests)
- [Compiling and Running Tests Natively](#compiling-and-running-tests-natively)
- [Test Suites](#test-suites)
- [Directory Structure](#directory-structure)

---

## Running Tests

All tests use a shared harness (`tests/test_common.zia`) providing `assert()`, `check()`, `assertTrue()`, `assertFalse()`, and `printResults()`.

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

---

## Compiling and Running Tests Natively

All tests also pass when compiled to native ARM64 machine code:

```bash
viper build demos/zia/sqldb/tests/test_basic_crud.zia -o /tmp/test_sql && /tmp/test_sql
viper build demos/zia/sqldb/tests/test_native_torture.zia -o /tmp/test_torture && /tmp/test_torture
```

---

## Test Suites

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
    ├── ...               (see Running Tests section for full list)
    └── test_readme_examples.zia  Verifies all README examples
```
