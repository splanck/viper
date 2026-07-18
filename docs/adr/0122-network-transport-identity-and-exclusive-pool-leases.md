---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0122: Validate Network Transports and Lease Pooled TCP Handles Exclusively

## Status

Accepted

## Context

`Tcp`, `TcpServer`, and `ConnectionPool` were managed objects created with class
id zero. Their native entry points accepted opaque `void *` receivers and cast
them directly to private structures. A wrong-class, undersized, or merely
uninitialized same-class allocation could therefore be interpreted as a socket,
listener, or native mutex. A returning trap hook made the boundary especially
dangerous because control could continue after a diagnostic.

ConnectionPool stored a separately allocated `host:port` string for every
tracked connection even though the Tcp object already owns an immutable host
and port. That duplicated data, required the non-portable `strdup`, imposed an
arbitrary 299-byte formatted-key limit, and introduced allocation failure into
the locked release path. Its readiness probe also treated a native wait error
as healthy.

The pool intentionally supports adopting a healthy untracked Tcp. Without an
ownership marker, however, releasing a connection checked out from pool A into
pool B made both pools retain the same socket. Each could later hand it to a
different borrower, causing interleaved protocols, double close, or use of a
descriptor after reuse. Removing adoption would delete an existing feature, so
the implementation needs an exclusive ownership protocol instead.

Stable managed class tags and validation change the runtime C ABI contract and
therefore require this ADR.

## Decision

- Assign stable `RT_TCP_CLASS_ID`, `RT_TCP_SERVER_CLASS_ID`, and
  `RT_CONNPOOL_CLASS_ID` tags. Each implementation also publishes an
  initialization magic value only after its native resources are ready.
- Validate heap kind, class id, complete payload size, and initialization magic
  at every non-null Tcp, TcpServer, and ConnectionPool receiver boundary.
  Returning trap hooks receive a safe sentinel and no private payload access.
- Validate Tcp payload arguments before socket calls: `Send`/`SendAll` require a
  real Bytes object, string sends require a real runtime string, and raw send or
  receive sizes reject negative values. Timeout properties change only after
  `setsockopt` succeeds.
- Install local recovery boundaries around post-receive result construction.
  A failed short-read right-sizing allocation releases the oversized Bytes;
  failed `RecvStr` conversion releases its temporary Bytes; and failed
  `RecvLine` string construction releases its native line buffer before the
  original allocation diagnostic propagates.
- Capture `errno`/WinSock last-error immediately after every failed receive and
  classify that saved value after managed/native cleanup. Cleanup is not
  permitted to overwrite timeout or network-error classification.
- Preserve one native monotonic deadline across interrupted readiness waits.
  POSIX `poll` and WinSock `select` rebuild their mutable inputs and recompute
  only the remaining duration after interruption. Invalid arguments publish a
  deterministic native error; POSIX error-only events resolve `SO_ERROR`, and
  orderly read-side hangup remains readable so `recv` can report EOF.
- Make listener sockets non-blocking and register every accept operation in an
  atomic in-flight counter. Close publishes the stopped state, waits for
  bounded readiness slices to drain, then closes the descriptor. A native,
  allocation-free monotonic clock keeps `AcceptFor` on one overall deadline.
  Accepted transports are restored to blocking mode before publication.
- Retain TcpServer receivers for every public property, accept, and close
  operation. Listener, outbound-Tcp, and accepted-Tcp publication use local
  recovery boundaries that close native sockets and free staging strings when
  managed object allocation traps.
- Give each ConnectionPool a nonzero process-local identity. Every Tcp stores an
  atomic pool-owner token. Tracking claims zero to that identity with
  compare-and-exchange; a same-owner claim is idempotent and a foreign token is
  never overwritten. Close paths hold the token until after the transport is
  closed, preventing another pool from adopting between the ownership check and
  close.
- Preserve untracked-Tcp adoption. A healthy unleased connection can still be
  returned into a pool with capacity. A connection leased to another pool traps
  without being adopted or closed.
- Remove per-entry key allocation. Cache an FNV-1a endpoint hash and confirm
  collisions by byte-exact, case-sensitive comparison against the immutable
  host and port already stored in Tcp. Host validation no longer has an
  internal formatted-key length limit.
- Retain a ConnectionPool for the duration of every public operation before
  locking it. Use non-trapping live-retain operations for Tcp publication so a
  refcount boundary cannot strand an entry as checked out or leak a native
  mutex through non-local trap recovery.
- Treat native mutex initialization as mandatory and native readiness errors as
  unhealthy. The tracked-capacity semantics remain unchanged: overflow
  acquisitions may return an untracked connection, which is closed on release
  if no slot has become available.
- Keep raw Tcp I/O externally serialized. This decision makes pool bookkeeping
  concurrent and handle boundaries safe; it does not add per-socket read/write
  serialization or permit I/O after `ConnectionPool.Release`.

## Consequences

- A forged or wrong managed value cannot be used as a socket descriptor,
  listener, or mutex, including when a trap hook returns.
- Two pools cannot concurrently retain or publish the same TCP transport.
  Cross-pool misuse is diagnosed without damaging the original borrower.
- Reuse and release perform no endpoint allocation, avoid `strdup`, accept any
  host length supported by the runtime string and resolver, and compare hashes
  before exact host bytes.
- Pool finalization cannot destroy its mutex while a public operation is active.
  Clear/finalize continue to close idle entries and detach checked-out entries,
  preserving the established borrower-lifetime guarantee.
- A recoverable result-allocation trap no longer leaks receive staging storage.
  Bytes already consumed from the socket remain consumed and the transport
  remains open, preserving normal I/O side-effect semantics.
- Signal storms cannot extend a finite socket wait by repeatedly granting it a
  fresh timeout, and cleanup cannot cause callers to classify a stale errno
  after error-only readiness. Orderly peer closure follows the normal EOF path
  instead of being promoted to a readiness error.
- Concurrent Close cannot race a readiness check into `accept()` on a reused
  descriptor number, destroy a listener payload still in use, or strand an
  indefinite accept. Accept calls that were already registered return no
  client when Close wins; calls begun after closure retain the established
  `Err_ConnectionClosed` diagnostic.
- Recoverable allocation failure after bind, connect, or accept no longer
  leaks the acquired native transport. The listener stays reusable after a
  failed accepted-Tcp publication.
- `Size` remains the tracked entry count rather than a hard process connection
  limit. Applications that need admission control must layer it separately.
- Tcp and TcpServer payload layouts gain private magic/token fields. Those
  structures are opaque; only the stable class identifiers become part of the
  native runtime contract.

## Alternatives Considered

- Remove adoption of untracked Tcp handles: rejected because the user required
  existing features to remain available and adoption is documented behavior.
- Store a raw pool pointer in Tcp: rejected because address reuse creates an ABA
  ambiguity after pool finalization. A monotonic process token decouples lease
  identity from allocation addresses.
- Keep allocated string keys and add an owner field only to pool entries:
  rejected because a second pool cannot observe the first pool's private entry,
  and key allocation remains a locked portability/failure cost.
- Serialize every Tcp send and receive internally: deferred because it changes
  protocol scheduling and close semantics beyond the pool/handle boundary. Raw
  Tcp retains its documented external-serialization contract.
