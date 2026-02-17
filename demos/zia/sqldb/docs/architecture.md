# Architecture

## Table of Contents

- [Query Pipeline](#query-pipeline)
- [Executor Composition](#executor-composition)
- [Transaction Management](#transaction-management)
- [Storage Engine](#storage-engine)
- [Concurrency and Thread Safety](#concurrency-and-thread-safety)
- [Session Architecture](#session-architecture)
- [Multi-User TCP Server](#multi-user-tcp-server)
- [Query Optimizer](#query-optimizer)
- [Performance Optimizations](#performance-optimizations)

---

## Query Pipeline

```
SQL text --> Lexer --> Parser --> AST (Stmt/Expr) --> Executor --> QueryResult
```

1. **Lexer** (`lexer.zia`) tokenizes SQL text into a stream of `Token` values
2. **Parser** (`parser.zia`) consumes tokens and builds an AST of `Stmt` and `Expr` nodes using recursive descent
3. **Executor** (`executor.zia`) walks the AST and evaluates it against the in-memory data model
4. Results are returned as `QueryResult` entities containing rows and column names

---

## Executor Composition

The executor delegates to specialized helper entities for complex operations:

```
Executor
├── JoinEngine          -- cross joins, multi-table joins, join GROUP BY, sorting
├── PersistenceManager  -- SAVE, OPEN, CLOSE commands (index restore on OPEN)
├── CsvHandler          -- EXPORT, IMPORT commands
├── IndexManager        -- bucket-accelerated hash index lookups (composite + single-column)
├── BTree[]             -- disk-based B-tree indexes for persistent databases
├── SqlFunctions        -- 80+ built-in SQL functions (string, math, date/time, regex, etc.)
├── SqlWindow           -- window function evaluation
├── SystemViews         -- INFORMATION_SCHEMA, sys.*, and pg_catalog views
├── TriggerManager      -- BEFORE/AFTER triggers for INSERT/UPDATE/DELETE
├── SequenceManager     -- PostgreSQL-style sequences (NEXTVAL/CURRVAL/SETVAL)
├── FunctionManager     -- user-defined SQL functions (CREATE FUNCTION)
└── JsonParser          -- pure-Zia JSON parser (validation, extraction, construction)
```

Each helper holds an `Executor` reference (via Zia's circular bind support) for shared state access.

---

## Transaction Management

The executor provides ACID transaction support:

- **Atomicity**: Changes within a transaction are all-or-nothing. Statement-level atomicity ensures individual statements are atomic even outside explicit transactions.
- **Journal-based rollback**: INSERT, UPDATE, and DELETE operations record journal entries (with before-images for updates). ROLLBACK replays the journal in reverse.
- **FK cascade handling**: ON DELETE/UPDATE CASCADE, SET NULL, and RESTRICT actions are enforced during DELETE and UPDATE operations, with cascaded changes also tracked in the journal.
- **Deferred compaction**: During transactions, DELETE operations use soft-delete; compaction is deferred to COMMIT.
- **MVCC snapshot isolation**: Each BEGIN assigns a unique transaction ID and snapshot. Row versioning (xmin/xmax) provides snapshot isolation — readers never block writers.
- **WAL undo phase**: ARIES-style crash recovery with Compensation Log Records (CLR), before-image capture for UPDATE/DELETE, full redo (INSERT+UPDATE+DELETE replay), and undo of uncommitted transactions.

---

## Storage Engine

Persistent storage uses a page-based architecture inspired by traditional RDBMS designs:

```
StorageEngine
├── Pager            -- 4KB page I/O (read/write/allocate)
├── BufferPool       -- in-memory page cache with dirty tracking
├── SchemaPage       -- table and index metadata serialization (IndexMeta persistence)
├── DataPage         -- slotted row storage with delete/compact
├── BTree            -- disk-based B-tree indexes (integrated with CREATE INDEX / OPEN)
├── WALManager       -- write-ahead log for crash recovery
└── TxnManager       -- transaction lifecycle (BEGIN/COMMIT/ROLLBACK)
```

---

## Concurrency and Thread Safety

The storage layer is designed for multi-user access using Monitor-based locking (`Viper.Threads.Monitor`). Each shared component has its own lock with a consistent internal/external method pattern to prevent re-entry deadlocks:

```
Lock Hierarchy (no circular dependencies):
  DatabaseServer.lock        -- protects database list mutations and user management
  TableLockManager.monitor   -- protects table-level S/X lock state
  StorageEngine.lock         -- protects table metadata and row operations
  BufferPool.lock            -- protects page cache (LRU eviction, fetch, flush)
  WALManager.lock            -- protects log writes and file rotation
```

Public methods acquire the lock and delegate to `*Internal` variants. Internal call chains (e.g., `openDatabase` calling `enableWal` and `performRecovery`) use internal variants to avoid deadlocking on the same lock. The Pager does not need its own lock since BufferPool is its sole caller.

**Table-Level Locking**: The `TableLockManager` (in `storage/txn.zia`) implements shared/exclusive table-level locks for concurrent query isolation. Lock semantics:
- `SELECT` acquires shared (S) locks on all FROM/JOIN tables
- `INSERT/UPDATE/DELETE` acquires exclusive (X) locks on the target table
- `CREATE/DROP/ALTER TABLE` acquires exclusive (X) locks
- S+S = compatible (concurrent readers), S+X or X+X = conflict (blocks with 5s timeout)
- Non-transactional statements: locks released after each statement
- Transactional statements: locks held until `COMMIT` or `ROLLBACK`

**Row-Level Locking**: The `RowLockManager` (in `storage/txn.zia`) implements fine-grained row-level locks for `SELECT ... FOR UPDATE` and `SELECT ... FOR SHARE`. Lock granularity is per-row (table+rowIndex). Supports `NOWAIT` (immediate error on conflict) and `SKIP LOCKED` (skip locked rows). Row locks are released on COMMIT/ROLLBACK.

---

## Session Architecture

ViperSQL supports per-connection isolation through the Session entity:

```
Session (per connection)
├── sessionId, clientHost, username   -- connection metadata
├── authenticated                     -- authentication state
├── preparedStmts[]                   -- named/unnamed prepared statements
├── portals[]                         -- bound portals (extended query protocol)
└── Executor (per session)
    ├── currentDbName, currentDbIndex -- per-session database context
    ├── inTransaction, journal        -- per-session transaction state
    ├── outerRow, subqueryDepth       -- per-session subquery context
    └── server (shared)               -- thread-safe DatabaseServer
```

Each connection gets its own `Session` with its own `Executor` instance. Multiple executors share the same `DatabaseServer` (created via `Executor.initWithServer()`). The current database is tracked per-executor, allowing independent `USE DATABASE` across connections. The session ID is used as the lock owner for table-level concurrency control.

---

## Multi-User TCP Server

The `server/` module implements a multi-user TCP server using a thread-per-connection model (like PostgreSQL):

- **Thread-per-connection**: Each accepted TCP connection spawns a dedicated thread via `Thread.StartSafe` for error isolation
- **PostgreSQL wire protocol (v3)**: Binary protocol on port 5432 for standard client compatibility (psql, ODBC, pgAdmin). Supports both Simple Query (Q) and Extended Query (Parse/Bind/Describe/Execute) protocols with prepared statements, parameterized queries, and portal suspension.
- **Simple text protocol**: SQL terminated by newline, response terminated by double-newline on port 5433
- **Authentication**: Cleartext password authentication via the PG wire protocol handshake (AuthenticationCleartextPassword / PasswordMessage)
- **Connection handler**: Per-connection Session with recv/execute/send loop, idle timeout (5 min), graceful disconnect
- **Entry point**: `viper run demos/zia/sqldb/server/tcp_server.zia`

### PostgreSQL Wire Protocol Messages

| Message | Direction | Purpose |
|---------|-----------|---------|
| StartupMessage | Client -> Server | Version (3.0), user, database |
| AuthenticationCleartextPassword | Server -> Client | Request password |
| PasswordMessage | Client -> Server | Send password |
| AuthenticationOk | Server -> Client | Auth success |
| ParameterStatus | Server -> Client | Server parameters (version, encoding, etc.) |
| BackendKeyData | Server -> Client | Process ID and secret key |
| ReadyForQuery | Server -> Client | Ready status (I=idle, T=in-txn, E=error) |
| Query (Q) | Client -> Server | Simple query text |
| Parse (P) | Client -> Server | Prepare a named/unnamed statement |
| Bind (B) | Client -> Server | Bind parameters to create a portal |
| Describe (D) | Client -> Server | Describe a statement or portal |
| Execute (E) | Client -> Server | Execute a portal (with optional row limit) |
| Sync (S) | Client -> Server | End extended query pipeline, sync state |
| Close (C) | Client -> Server | Close a statement or portal |
| Flush (H) | Client -> Server | Flush output buffer |
| RowDescription (T) | Server -> Client | Column names and types |
| DataRow (D) | Server -> Client | Row values |
| CommandComplete (C) | Server -> Client | Completion tag (e.g., "SELECT 3") |
| ParseComplete (1) | Server -> Client | Parse succeeded |
| BindComplete (2) | Server -> Client | Bind succeeded |
| CloseComplete (3) | Server -> Client | Close succeeded |
| ParameterDescription (t) | Server -> Client | Parameter type OIDs |
| NoData (n) | Server -> Client | Statement produces no rows |
| PortalSuspended (s) | Server -> Client | Execute hit row limit |
| ErrorResponse (E) | Server -> Client | Error with severity, code, message |
| Terminate (X) | Client -> Server | Close connection |

---

## Query Optimizer

The optimizer (`optimizer/optimizer.zia`) provides cost-based query optimization:

- Table statistics (row counts, distinct value estimates)
- Access path selection (table scan vs. index scan vs. index seek)
- Cost estimation with configurable selectivity constants
- Automatic index selection for equality predicates

---

## Performance Optimizations

ViperSQL includes several algorithmic optimizations for efficient query processing:

- **Quicksort** — ORDER BY uses iterative quicksort with median-of-three pivot selection (O(n log n)) for result rows, row indices, and join results
- **Hash-based GROUP BY** — Group key deduplication uses 128-bucket hash tables for O(n) amortized grouping instead of O(n*g) linear scan
- **Hash-based DISTINCT** — Duplicate elimination uses hash buckets for O(n) amortized deduplication
- **Bucket-based index lookup** — Hash indexes use 64-bucket acceleration structures, reducing lookup from O(n) to O(n/b) per query
- **Disk-based B-tree indexes** — Persistent databases use order-50 B-trees (~99 keys per 4KB page) for O(log n) lookups, integrated with the BufferPool page cache
- **Short-circuit AND/OR** — Boolean expressions short-circuit: `FALSE AND ...` returns immediately without evaluating the right side, and `TRUE OR ...` returns immediately
