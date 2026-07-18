---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0136: Serialize Runtime Context State and Reserve Child Bindings Before Native Start

## Status

Accepted

## Context

`RtContext` isolates mutable runtime state for one VM, but the original binding
contract assumed one host thread at a time. Native `Thread.Start` copied the
parent's context pointer into its wrapper and incremented the binding count only
after the new OS thread reached its trampoline. The parent could unbind and
clean up caller-owned context storage in the interval between native creation
and the child entry. Concurrent bindings also shared RNG, module-variable,
argument, file-channel, and type-registry arrays without one synchronization
policy.

First- and last-binding compatibility migrations move state between a VM
context and the process legacy context. Publishing thread-local state before
that move, or migrating while an unbound legacy caller was reading the same
arrays, exposed partially transferred ownership. Cleanup had a related
publication window: it could observe `bind_count == 0` and start destroying
native locks immediately before another thread published a new binding.

The fix changes the caller-allocated `RtContext` layout and the internal
cross-component interface used by native thread adapters and the type registry.
It therefore requires an explicit runtime C ABI and dependency decision.

## Decision

- `RtContext` contains independently allocated recursive locks for RNG,
  module-variable metadata, file channels, arguments, and lifecycle handoff.
  The type registry retains its reader-writer lock. Unrelated mutable
  subsystems do not share one hot mutex.
- Context initialization is transactional. All fields begin unpublished,
  native state locks and the type-registry lock must initialize successfully,
  and only then does the context publish `RT_CONTEXT_LIFECYCLE_READY`. Failure
  releases every lock already constructed.
- `RtContext::lifecycle_state` has three atomic states: `UNINITIALIZED`,
  `READY`, and `CLEANING`. Binding and child-reservation paths accept only
  `READY`.
- Cleanup and binding use the same process handoff lock for the decisive state
  and binding-count transition. Cleanup changes `READY` to `CLEANING` only
  after proving the count is zero, then releases owned state and publishes
  `UNINITIALIZED`. A racing bind therefore wins a counted reservation or is
  rejected before TLS publication; it cannot observe destroyed locks.
- `Thread.Start` reserves the inherited context before calling
  `pthread_create` or `_beginthreadex`. Native creation failure cancels that
  reservation. The child trampoline adopts the existing reservation without a
  second increment and releases it through ordinary unbinding when its entry
  returns.
- Binding counter transitions use checked compare/exchange loops. Overflow and
  underflow trap without changing the prior TLS binding. Context cleanup is
  rejected while any other binding or pre-start reservation exists.
- First-binding adoption and last-binding return of legacy-compatible state are
  serialized with legacy initialization and shutdown. File, argument, and type
  registry storage is locked on both contexts while ownership moves. TLS is
  published only after the complete counter and migration transaction.
- An unbound native caller acquires the selected legacy subsystem lock while
  legacy lifecycle state is stable. Thus unbound threads share synchronized
  legacy state instead of an unsynchronized fallback.
- Context lock acquisitions are recorded in a thread-local LIFO. The central
  trap dispatcher releases that stack before a non-local recovery transfer, so
  a runtime trap cannot permanently strand a recursive subsystem lock.
- POSIX recursive-lock setup uses `pthread_mutexattr_init`,
  `pthread_mutexattr_settype`, and `pthread_mutexattr_destroy`. These symbols
  are explicit known dynamic imports in the in-tree native linker, alongside
  the existing pthread mutex functions, so native applications that include
  the base runtime resolve the same context implementation as CMake-built hosts.
- Reservation/adoption/cancellation and direct state-lock helpers remain
  runtime-internal C interfaces. They are not registered as Zanna language
  methods.

## Consequences

- A parent may unbind immediately after a successful native thread start; the
  child's pre-start reservation keeps context cleanup from succeeding until
  the child exits.
- Multiple inherited threads can safely use the same supported mutable context
  subsystems. Operations within one subsystem serialize; independent
  subsystems can proceed concurrently.
- Legacy compatibility migration has one well-defined ownership point, and a
  runtime call cannot see a context before migration completes.
- A cleaned context rejects future bindings until the embedder explicitly
  calls `rt_context_init` again. The runtime still does not allocate or free the
  outer caller-owned `RtContext` storage.
- The public C structure is larger and must be recompiled with the matching
  runtime. No language-visible class or function signature changes.
- Context initialization now allocates native lock storage and can trap. Hosts
  must treat a non-ready lifecycle as failed initialization and must not bind
  it.
- Native-link import audits now cover the complete POSIX recursive-mutex
  initialization sequence; omitting a required pthread symbol fails a focused
  planner test and the runtime-archive import audit.

## Alternatives Considered

- Increment the count inside the child trampoline: rejected because it leaves
  the native-create-to-entry lifetime gap open.
- Retain only the outer managed Thread object: rejected because `RtContext`
  storage is caller-owned and is not a managed heap object.
- Require every embedder to serialize all context calls externally: rejected
  because runtime-created threads inherit the context automatically and the
  legacy fallback is shared by design.
- Use one mutex for every context field: rejected because RNG, file I/O,
  argument mutation, and registry work are independent and would create an
  avoidable global bottleneck.
- Infer readiness from a non-null lock slot: rejected because cleanup needs all
  locks to remain addressable while the lifecycle is already closed to new
  bindings. An explicit atomic state makes that transition auditable.
