# Zia SQL Database - Bug Tracker

## Summary
- **Total Bugs Found**: 3
- **Viper Bugs Fixed**: 2 (BUG-002, BUG-003)
- **Viper Bugs Open**: 0
- **SQLzia Bugs Open**: 1 (BUG-001 - application-level, not a Viper bug)
- **Tests Passing**: 602
- **Tests Failing**: 1

---

## Bug Log

### BUG-003: Optional type narrowing does not work after null check (VIPER BUG)
- **Status**: FIXED
- **Severity**: High
- **Component**: Viper/Zia Compiler
- **Description**: After a null check on an optional type (`T?`), the type is not narrowed to the non-optional type (`T`) for subsequent operations. This means even after checking `if x != null`, the variable `x` is still treated as `T?` when assigning to a field or passing to functions.
- **Steps to Reproduce**:
  ```zia
  func test(maybeBuf: BinaryBuffer?) {
      if maybeBuf == null {
          return;
      }
      // At this point, maybeBuf should be narrowed to BinaryBuffer
      var buf = maybeBuf;  // buf is still BinaryBuffer?
      someField = buf;  // Error: expected BinaryBuffer, got BinaryBuffer?
  }
  ```
- **Expected**: After `if maybeBuf == null { return; }`, the variable should be narrowed to the non-optional type
- **Actual**: Variable remains optional type, causing type mismatch errors
- **Impact**: High - requires workarounds for all null-checked optional values
- **Workaround**: Use boolean return patterns or pass destination buffers as arguments
- **Fix**: Added flow-sensitive type narrowing to Sema.cpp and Sema_Stmt.cpp. The analyzeIfStmt() method now detects null check patterns (x != null, x == null, null != x, null == x) and narrows the variable's type to the non-optional inner type in the appropriate branch.
- **Test**: test_zia_optional_narrowing.cpp

---

### BUG-002: List.remove() confusion with removeAt() (VIPER BUG)
- **Status**: FIXED
- **Severity**: High
- **Component**: Viper/Zia Semantic Analysis
- **Description**: Users expected `List.remove(1)` to remove the element at index 1, but `remove(value)` actually removes by value comparison, not index. When passing an integer to a non-integer list's remove(), the value is never found (pointer equality fails), so nothing is removed.
- **Root Cause**: The semantic analyzer didn't recognize `removeAt(index)` method, and didn't provide helpful error messages when `remove(value)` was called with a mismatched type.
- **Steps to Reproduce**:
  ```zia
  var list: List[String] = [];
  list.add("a");
  list.add("b");
  list.add("c");
  list.remove(1);  // User expected to remove at index 1, but actually searches for value "1"
  ```
- **Expected**: Use `list.removeAt(1)` to remove by index
- **Actual**: `list.remove(1)` on a String list silently fails because Integer 1 != String elements
- **Impact**: High - affects any code using List.remove() including DROP TABLE, DROP DATABASE, etc.
- **Fix**:
  1. Added `removeAt` to the recognized List methods in Sema_Expr.cpp
  2. Added type checking for `remove()` that detects when an Integer argument is passed to a non-Integer list, providing a helpful error suggesting `removeAt()` instead
- **Test**: test_zia_list_remove_at.cpp (3 tests: removeAt works, remove type mismatch errors, remove matching type works)

---

### BUG-001: SELECT with non-existent column returns NULL instead of error
- **Status**: Open
- **Severity**: Low
- **Description**: When selecting a column that doesn't exist in a table, the query succeeds and returns NULL values instead of an error.
- **Steps to Reproduce**:
  1. Create table: `CREATE TABLE t (id INTEGER)`
  2. Run: `SELECT nonexistent_col FROM t`
  3. Query succeeds with NULL instead of "column not found" error
- **Expected**: Error "column 'nonexistent_col' not found"
- **Actual**: Query succeeds with NULL value
- **Impact**: Minor - matches some SQL implementations that are lenient

---

## Development Progress

### Phase 10: Storage & Persistence (Steps 49-53) - COMPLETED
- [x] Step 49: File format design (SQL dump format)
- [x] Step 50: SAVE command (`SAVE 'filename'`)
- [x] Step 51: OPEN command (`OPEN 'filename'`)
- [x] Step 52: EXPORT to CSV (`EXPORT table TO 'file.csv'`)
- [x] Step 53: IMPORT from CSV (`IMPORT INTO table FROM 'file.csv'`)

### Phase 11: Advanced Features (Steps 54-60) - COMPLETED
- [x] Step 54: ALTER TABLE (ADD/DROP/RENAME COLUMN, RENAME TABLE)
- [x] Step 55: DROP TABLE/INDEX (already implemented)
- [x] Step 56: VACUUM
- [x] Step 57: UNION/UNION ALL
- [x] Step 58: CASE expressions
- [ ] Step 59: Views (CREATE VIEW) - Deferred (complex, low priority)
- [ ] Step 60: Triggers (basic) - Deferred (complex, low priority)

### Phase 13: REPL & CLI (Steps 66-68) - COMPLETED
- [x] Step 66: Interactive REPL (`repl.zia`)
- [x] Step 67: Multi-line input (supported via semicolon detection)
- [x] Step 68: Help & metadata (HELP, DESCRIBE, .help, .tables, etc.)

### Phase 1 (Roadmap): Multi-Database Support - COMPLETED
- [x] CREATE DATABASE, DROP DATABASE commands
- [x] USE database command
- [x] SHOW DATABASES command
- [x] Per-database tables and indexes
- [x] DatabaseServer entity managing multiple databases

### Phase 2 (Roadmap): Binary Storage Engine - CORE COMPLETED
- [x] Page constants and structures (page.zia)
- [x] Binary serialization (serializer.zia)
- [x] Low-level page I/O (pager.zia)
- [x] Buffer pool with LRU eviction (buffer.zia)
- [x] Data page management (data_page.zia)
- [x] Schema page management (schema_page.zia)
- [ ] Overflow pages for large values
- [ ] Full executor integration

### Phase 3 (Roadmap): B-Tree Indexes - CORE COMPLETED
- [x] B-Tree node structure (btree_node.zia)
- [x] B-Tree key entity with row pointers
- [x] Node serialization/deserialization
- [x] B-Tree implementation (btree.zia)
- [x] Insert with node splitting
- [x] Search (exact and range)
- [x] Delete operations
- [ ] Full integration with IndexManager

### Phase 4 (Roadmap): Write-Ahead Logging - COMPLETED
- [x] Log Sequence Number (LSN) entity
- [x] Log record types (BEGIN, COMMIT, ABORT, INSERT, UPDATE, DELETE, CHECKPOINT)
- [x] Log record serialization/deserialization
- [x] WAL Manager with buffer and file management
- [x] Logging operations (logBegin, logCommit, logInsert, etc.)
- [x] Checkpoint support
- [x] Recovery framework (simplified)

### Phase 5 (Roadmap): Transaction Manager - COMPLETED
- [x] Transaction entity with state tracking
- [x] Lock entity (table, page, row level)
- [x] Lock manager with conflict detection
- [x] Lock compatibility (S+S, S+X, X+X)
- [x] Transaction manager (begin, commit, abort)
- [x] Automatic lock release on commit/abort
- [x] WAL integration for logging operations
- [x] Auto-commit mode support

### Phase 6 (Roadmap): Network Server - COMPLETED
- [x] Server constants and protocol message types
- [x] ClientSession entity with authentication tracking
- [x] ServerQueryResult for network-safe result format
- [x] SqlServer entity with executor integration
- [x] Query processing with result formatting
- [x] Session management
- [x] Statistics tracking (connections, queries)
- [x] SqlClient entity for network connections
- [x] Interactive client mode

### Phase 7 (Roadmap): Query Optimizer - COMPLETED
- [x] TableStats entity for table statistics
- [x] AccessPath entity (TableScan, IndexSeek, IndexScan)
- [x] QueryPlan entity with cost estimation
- [x] StatsManager for row count tracking
- [x] QueryOptimizer with cost-based optimization
- [x] Join planning (nested loop vs hash join)
- [x] Selectivity estimation
- [x] EXPLAIN plan output

### Phase 8 (Roadmap): Advanced SQL Features - COMPLETED
- [x] EXCEPT set operation
- [x] INTERSECT set operation
- [x] Duplicate removal in set operations

---

## Test Summary

| Test File | Passed | Failed |
|-----------|--------|--------|
| test.zia | 22 | 0 |
| test_features.zia | 22 | 0 |
| test_functions.zia | 25 | 0 |
| test_index.zia | 16 | 0 |
| test_constraints.zia | 16 | 0 |
| test_subquery.zia | 16 | 0 |
| test_persistence.zia | 31 | 1 |
| test_advanced.zia | 24 | 0 |
| test_multidb.zia | 29 | 0 |
| test_storage.zia | 50 | 0 |
| test_btree.zia | 94 | 0 |
| test_wal.zia | 69 | 0 |
| test_txn.zia | 84 | 0 |
| test_server.zia | 42 | 0 |
| test_optimizer.zia | 62 | 0 |
| **Total** | **602** | **1** |

---

## Features Implemented

### Core SQL
- DDL: CREATE TABLE, DROP TABLE, CREATE INDEX, DROP INDEX
- DML: INSERT, SELECT, UPDATE, DELETE
- Clauses: WHERE, ORDER BY, LIMIT, OFFSET, DISTINCT, GROUP BY, HAVING
- Joins: CROSS, INNER, LEFT, RIGHT, FULL OUTER
- Aggregates: COUNT, SUM, AVG, MIN, MAX
- Subqueries: Scalar, IN

### Extended Features (Phase 11)
- UNION / UNION ALL
- EXCEPT / INTERSECT
- CASE expressions
- ALTER TABLE (ADD/DROP/RENAME COLUMN, RENAME TO)
- VACUUM

### Persistence (Phase 10)
- SAVE / OPEN (SQL dump format)
- EXPORT / IMPORT (CSV format)

### Functions Library
- String: UPPER, LOWER, LENGTH, SUBSTR, TRIM, LTRIM, RTRIM, REPLACE, CONCAT, INSTR
- Math: ABS, MOD, MIN, MAX
- Null-handling: COALESCE, IFNULL, NULLIF, IIF
- Type: TYPEOF

### REPL & CLI (Phase 13)
- Interactive shell with prompt
- Multi-line input support
- Meta-commands: .help, .quit, .tables, .describe, .save, .open, .schema

### Multi-Database Support (Phase 1 Roadmap)
- CREATE DATABASE / DROP DATABASE
- USE database_name
- SHOW DATABASES
- Independent tables/indexes per database

### Binary Storage Engine (Phase 2 Roadmap)
- 4KB page-based storage
- Binary value serialization (NULL, INTEGER, REAL, TEXT)
- Buffer pool with LRU page eviction
- Pager for disk I/O
- Data page management with slot directory
- Schema page for table metadata

### B-Tree Indexes (Phase 3 Roadmap)
- B-tree node structure (min degree 50, ~99 keys/node)
- Node serialization to pages
- Insert with automatic node splitting
- Search (exact match and range queries)
- Delete operations

### Write-Ahead Logging (Phase 4 Roadmap)
- Log Sequence Numbers (LSN) for ordering
- Log record types: BEGIN, COMMIT, ABORT, INSERT, UPDATE, DELETE, CHECKPOINT
- Binary log file format
- WAL buffer with disk persistence
- Checkpoint mechanism for recovery

### Transaction Manager (Phase 5 Roadmap)
- Transaction lifecycle (begin, commit, abort)
- Lock manager with table, page, row-level locking
- Lock compatibility rules (S/X modes)
- Automatic lock release
- WAL integration for durability

### Network Server (Phase 6 Roadmap)
- SqlServer with PostgreSQL-like port (5432)
- ClientSession for connection tracking
- ServerQueryResult for network-safe results
- Query processing and result formatting
- SqlClient for network connections
- Interactive client mode

### Query Optimizer (Phase 7 Roadmap)
- Cost-based query optimizer
- Table statistics (row counts, distinct values)
- Access path types: TableScan, IndexSeek, IndexScan
- Cost estimation per row (scaled integers for precision)
- Join planning with optimal table ordering
- Join type selection (NestedLoop vs Hash based on size)
- Selectivity estimation for predicates
- EXPLAIN plan generation

### Advanced SQL Features (Phase 8 Roadmap)
- EXCEPT set operation (difference)
- INTERSECT set operation (intersection)
- Automatic duplicate removal in set operations
