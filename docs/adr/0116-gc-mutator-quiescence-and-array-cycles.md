---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0116: Coordinate Cycle Collection with Managed-Graph Mutators

## Status

Accepted

## Context

The cycle collector protects its tracking table with a native mutex, then drops
that mutex before invoking per-type traversal callbacks. Dropping the table
mutex is necessary because a callback may query the GC, but it does not make
the traversed payload stable. A different runtime thread can concurrently
resize a sequence, replace a map entry, remove the final reference to a child,
or free the payload whose fields are being enumerated. Atomic reference counts
keep an individual counter coherent; they do not make the counter and its
owning container slot one atomic graph update.

The collector also rejected every `RT_HEAP_ARRAY` payload. Object arrays can
hold strong managed references, including a reference to themselves or a cycle
through another tracked container, so excluding them made those cycles
permanent leaks.

The live-allocation registry originally exposed raw `rt_heap_hdr_t *` values to
borrowed-handle validators after releasing its lock. A concurrent final release
or realloc could invalidate that header before the validator read its fields.
Likewise, `rt_heap_realloc` moved a payload without proving that the caller was
its sole owner, leaving every alias pointed at reclaimed storage.

This is a cross-component runtime dependency: heap ownership, trap unwinding,
collections, object arrays, and the collector must share one quiescence
contract. It does not add an IL opcode or a language-visible runtime method.

## Decision

- The runtime owns one process-wide managed-graph reader/writer barrier.
  Refcount changes, tracked-object registration, and structural changes to a
  GC-traversed payload execute in a shared mutator scope. A collection pass
  holds the exclusive scope while it snapshots and traverses the graph.
- Mutator scopes nest through thread-local depth counters. Nested retain,
  release, allocation-cleanup, and collection operations therefore acquire the
  native shared lock only once.
- A collection requested from inside an active mutator scope is deferred until
  the outer scope exits. The runtime never attempts an unsupported shared-to-
  exclusive lock upgrade.
- Immediately before trap dispatch transfers control with `longjmp`, it abandons
  any shared mutator scope held by that thread. This prevents a recoverable trap
  from permanently blocking future GC. A returning embedder trap hook keeps the
  lexical scope intact so its caller can execute ordinary cleanup. An
  exclusive collector scope is retained so the collector's own recovery path
  can restore graph state before resuming other mutators.
- Traversal callbacks run while the collector owns the exclusive graph scope
  but without the GC table mutex. Callbacks may query GC bookkeeping without
  deadlocking, while payload structure and reference counts remain stable.
- Object- and boxed-reference arrays register a generic traversal callback from
  `rt_heap_alloc` before their payload escapes. Tracking-table growth failure
  rolls the new allocation back transactionally. Final release untracks all
  heap payload kinds centrally, and cycle reclaim invokes the registered
  traversal for objects and both reference-array kinds.
- Borrowed or untrusted handle validators use `rt_heap_get_info`, which copies
  scalar header metadata while the allocation-registry lock is held. The raw
  `rt_heap_try_get_header` API remains internal for callers that already own a
  strong reference or the collector's exclusive graph scope.
- `rt_heap_contains_range` performs both exact-payload and interior-range checks
  entirely under the registry lock. It never carries a header pointer across an
  unlocked interval.
- `rt_heap_realloc` requires an exact reference count of one while holding the
  registry lock. Shared and immortal allocations trap without changing the
  original payload, its contents, or its registry entry.
- Confirmed garbage members are indexed in an open-addressed pointer set during
  reclaim. Snapshot entries link directly to their garbage state, eliminating
  the prior repeated linear membership and recovery scans.
- Allocation thresholds accumulate a coalescing collection request; allocation
  itself never runs a collector or finalizer. The VM services debt after a
  completed runtime call through `rt_gc_safepoint`, and native embedders may use
  the same internal boundary. Explicit collection remains synchronous.
- Shutdown finalization walks tracking entries with per-sweep epochs and one
  temporary retain at a time. It requires no heap snapshot allocation, detaches
  callbacks before invocation, and releases all runtime-global locks while user
  finalizers run.
- The barrier functions are internal C coordination hooks. They are not added
  to `runtime.def`, the generated runtime registry, or the supported language
  ABI surface.

## Consequences

- Collection no longer traverses a concurrently reallocated container or makes
  a reachability decision from a graph/refcount pair that changed mid-pass.
- Threads modifying unrelated managed containers may proceed concurrently with
  one another, but they pause during the synchronous graph snapshot and trial-
  deletion phases.
- Refcount-only hot paths pay a thread-local nesting check and a shared-lock
  acquire/release at their outer boundary. Compound collection mutations hold
  one shared scope so nested retains do not multiply lock traffic.
- Recoverable traps have one additional runtime cleanup hook. Direct calls to a
  user-overridden `vm_trap` remain supported, including hooks that return.
- Object and boxed-reference arrays containing cycles become reclaimable.
  Numeric and string arrays remain untracked because their elements cannot form
  strong object cycles.
- Heap metadata validation no longer dereferences an unpinned header. Callers
  that need to mutate header fields still require explicit ownership and a
  mutator scope.
- Moving reallocation is an ownership-transfer operation, not a copy-on-write
  operation. Callers must first establish unique ownership or allocate and copy
  into a new managed value.
- Automatic collection latency is bounded by the next explicit safepoint, while
  allocator reentrancy and observation of partially initialized results are
  removed. Multiple threshold crossings before that boundary intentionally
  coalesce into one pass.
- Native resource finalizers are no longer skipped when shutdown cannot allocate
  a table-sized snapshot.

## Alternatives Considered

- Treat atomic reference counts as sufficient: rejected because traversal can
  race a `realloc` or observe a container slot before its matching refcount
  update.
- Hold the GC table mutex during callbacks: rejected because callbacks may
  query GC state and because the mutex still would not serialize container
  mutations that do not touch the tracking table.
- Add one mutex to every container: rejected as the sole GC mechanism because
  refcount-only root changes would remain unsynchronized and lock ordering
  across nested containers would become type-dependent.
- Disable automatic collection whenever more than one runtime thread exists:
  rejected because explicit collection and shutdown finalization would retain
  the same race, while long-running multithreaded programs would leak cycles.
