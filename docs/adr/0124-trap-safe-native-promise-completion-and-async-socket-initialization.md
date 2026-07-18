---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0124: Make Native Promise Completion and AsyncSocket Initialization Trap-Safe

## Status

Accepted

## Context

AsyncSocket workers installed a local `setjmp` recovery frame around blocking
TCP and HTTP calls. After catching a trap, they copied the thread-local
diagnostic into a managed String and called the ordinary Promise error setter
while the same recovery frame was still installed. If that diagnostic
allocation trapped, control jumped back into the same handler, which could
repeat indefinitely and leave the Future pending. Ordinary transferred-value
completion also trapped when another producer had already settled the Promise,
making worker cleanup depend on exceptional control flow.

The process-wide AsyncSocket Pool had different initialization protocols on
Windows and POSIX. `pthread_once` permanently remembered a failed initializer,
while the Windows path published its READY state even when construction
returned NULL. `SetPoolSize` read the singleton without synchronization and
could race the first async operation. A failed first allocation could therefore
disable the API for the rest of the process, and concurrent configuration was a
data race.

Public async wrappers also allocated Promise/request state before all bounds
were checked, used trapping retains at the handoff boundary, and called Pool
submission without recovery. A submission invariant trap could skip native
request cleanup. Workers used the legacy `void *` callback ABI even though the
Pool implementation already had a typed C entry point, requiring a function-
pointer/object-pointer conversion that portable C does not guarantee.

Adding Promise completion functions and formally exposing the typed native Pool
submission functions changes the runtime C ABI surface and therefore requires
this ADR. These functions are internal native-runtime interfaces; they are not
added to the language runtime registry.

## Decision

- Add `rt_promise_try_set_transferred`. It accepts a producer-owned live Promise
  and one runtime-managed value reference. Successful completion transfers that
  reference to the Promise. Invalid or duplicate completion returns false and
  consumes the value reference without raising an invalid/duplicate Promise
  diagnostic.
- Add `rt_promise_try_set_error_cstr`. It copies a native diagnostic when memory
  is available, catches allocation failure locally, and still publishes an
  error state with no stored message as its allocation-free fallback. Invalid
  and duplicate completion return false without trapping.
- Keep ordinary public Promise semantics unchanged. `Set`, `SetOwned`,
  transferred completion through the existing strict helper, and `SetError`
  still diagnose duplicate completion. The new try-set functions are for native
  producer boundaries that must guarantee settlement and cleanup progress.
- Validate try-set Promise handles with a registry-backed, non-trapping class and
  payload-size check. The caller must own a Promise reference for the entire
  call; neither helper consumes that Promise reference.
- Detach completion listeners under the Promise mutex, wake all waiters, and run
  listeners after unlocking. Existing listener isolation continues to catch
  callback and cancellation-cleanup traps so try-set completion does not inherit
  user callback control flow.
- Replace platform-specific AsyncSocket singleton initialization with one
  portable atomic state machine: UNINITIALIZED, CONFIGURING, INITIALIZING, and
  READY. Configuration and first use claim the same state gate. Contenders yield,
  successful Pool publication uses release/acquire ordering, and any failed or
  trapped construction restores UNINITIALIZED so a later caller can retry.
- Preserve the existing Pool feature and sizing policy. `SetPoolSize(1..1024)`
  may replace an earlier setting until initialization starts. Without a setting,
  the Pool uses twice the logical CPU count, with a floor of eight and a cap of
  1,024. Configuration after initialization starts or succeeds still traps.
- Create Promise/Future pairs under a local recovery frame that owns and releases
  partial objects before re-raising construction failure. Once a pair exists,
  native request OOM, Pool initialization failure, backpressure, shutdown, and
  submission traps settle its Future as an error instead of leaking the Promise.
- Validate all synchronous arguments before first use. Hosts and URLs must be
  live non-empty Strings without embedded NUL bytes; ports are 1..65535;
  timeouts and receive limits are 0..INT_MAX; send requires stable TCP and Bytes
  identities. A returning trap hook observes exactly one validation trap and a
  failed Future rather than continued private-payload access.
- Snapshot host, URL, and HTTP body bytes before queueing. Retain TCP and Bytes
  arguments with non-trapping live-retain operations and revalidate their stable
  identities after retention, closing the validation-to-share and address-reuse
  windows. The POST body remains length-delimited and preserves embedded NULs.
- Stage every worker-produced managed value in a volatile owner across longjmp.
  Clear operation recovery before rejecting a trap, transfer successful results
  through the non-trapping Promise helper, and release all staged inputs,
  outputs, request storage, and the producer Promise reference exactly once.
- Declare `rt_threadpool_submit_fn` and `rt_threadpool_submit_owned_fn` in the
  Pool header as internal typed C adapters. Native runtime code uses them to
  preserve the real `void (*)(void *)` callback type. The legacy `void *`
  callback functions remain available unchanged for generated/IL ABI callers.

## Consequences

- A secondary OOM while formatting a worker failure cannot recursively re-enter
  the same trap handler or strand a Future. `Future.IsError` is true even when
  the optional error String is empty; `Future.Get` uses its generic error text.
- A duplicate or invalid native producer completion cannot leak a freshly
  produced result and does not interrupt request cleanup with another trap.
- Failed first-use Pool construction is transient rather than process-wide.
  Concurrent retry callers observe either the published Pool or their own
  settled setup failure; none observe READY with a NULL Pool.
- `SetPoolSize` is data-race-free with first use. It remains a pre-initialization
  configuration API, so no existing feature or sizing option is removed.
- Async argument snapshots and retained handles remain valid after the caller's
  frame returns. Pool refusal and native allocation failure release all partial
  ownership before returning the failed Future.
- Future getters remain non-consuming: the Promise/Future pair owns the
  transferred result until finalization, and each successful getter returns a
  separately retained caller reference.
- The typed Pool submission declarations expand the internal C header surface
  but do not change registered language signatures or legacy entry points.

## Verification

- `test_rt_future` injects OOM into diagnostic copying and verifies message-less
  error settlement, duplicate rejection, and transferred-value consumption on
  successful, duplicate, and invalid-Promise paths.
- `test_rt_async_socket_init` injects OOM into the first Pool object allocation,
  verifies configuration remains possible, races eight retry callers, waits for
  every Future, and confirms configuration traps after READY publication.
- `test_rt_trap_return_network` verifies invalid AsyncSocket host, port, TCP,
  Bytes, receive-size, URL, and body boundaries raise one synchronous trap and
  return a settled error Future when the embedder hook returns.
- Existing network lifetime, boxed-send, receive, failure, and hardened transport
  CTests continue to cover successful result ownership and worker error paths.

## Alternatives Considered

- Keep `pthread_once` and cache initialization failure: rejected because a
  transient OOM would permanently disable every later AsyncSocket call.
- Publish READY after a failed construction and let submission reject NULL:
  rejected because it makes retry impossible and gives READY no useful invariant.
- Allocate one immortal fallback error String: rejected because its own lazy
  construction can fail and introduces global initialization ordering. A
  message-less error state already has defined Future behavior.
- Reuse strict Promise setters inside a broader worker recovery frame: rejected
  because allocation or duplicate-completion traps can re-enter cleanup and
  obscure the original operation failure.
- Remove asynchronous wrappers or make them synchronous on failure: rejected
  because the user requires existing features to remain available and callers
  rely on one Future-shaped result contract.
- Continue converting native function pointers through `void *`: rejected for
  portable runtime code because ISO C does not guarantee that representation;
  the legacy ABI remains only where compatibility requires it.
