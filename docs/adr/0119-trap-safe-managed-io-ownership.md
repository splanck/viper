---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0119: Make Managed I/O Construction and Result Ownership Trap-Safe

## Status

Accepted

## Context

Zanna runtime traps can transfer control non-locally through a per-thread
recovery point. That makes ordinary C cleanup-after-NULL patterns insufficient
whenever an operation acquires a native resource or temporary allocation before
constructing a managed result. `BinFile`, `Stream`, `LineReader`, and
`LineWriter` all crossed that boundary: a `FILE *`, backing object, or staging
buffer could exist when managed allocation trapped. A conventional check after
the allocation was unreachable on the longjmp path.

The managed heap also allocated object and array payloads directly through
`malloc`, while the documented runtime allocation shim and its deterministic
test hook covered only standalone allocations. Tests therefore could appear to
exercise constructor OOM while actually failing during argument construction,
and managed resize failures could not be selected reliably.

Finally, several opaque I/O APIs accepted any registered object with the right
class id and cast it to a larger private structure. A forged object containing
the class tag but too few payload bytes could pass validation and cause an
out-of-bounds field access. This is a shared contract among the managed heap,
trap subsystem, native stdio adapters, runtime object validation, and public I/O
classes. It does not change an IL opcode or the language-visible runtime ABI.

## Decision

- Non-pooled managed heap blocks, including replacement blocks used by managed
  resizing, are allocated through `rt_alloc` and released through `rt_free`.
  Pool slabs and heap-registry metadata remain internal allocator structures.
- A constructor that opens a native handle or creates a backing object before
  allocating its public wrapper installs a local recovery boundary. Ownership
  stays in volatile cleanup slots until the wrapper is fully constructed. Every
  returning, NULL, or non-local trap path releases those resources before
  propagating the original diagnostic.
- Operations that stage data before allocating a managed `Bytes` or String
  result apply the same rule. Ownership moves explicitly between helpers so a
  nested right-sizing failure frees exactly one buffer.
- Explicit close reports delayed host flush/close failures after detaching the
  handle. GC finalizers perform the same cleanup but suppress errors because a
  finalizer must not start a new user-visible trap during reclamation.
- Opaque BinFile, MemStream, Stream, LineReader, LineWriter, and Watcher receivers use
  `rt_obj_is_instance` with their private implementation size. Class identity
  alone is not sufficient.
- Fixed-width and byte-container entry points reject malformed negative lengths
  before pointer arithmetic or stdio conversion. Text writes additionally
  verify that signed runtime lengths are representable as host `size_t`.
- Fault-injection tests construct arguments before arming the allocator hook,
  select the exact managed allocation that should fail, and compare native
  process resource counts before and after constructor failures.
- Watcher construction installs its finalizer before retaining or deriving path
  components, and event-path construction owns native staging buffers across
  managed-string traps. Stop always closes residual backend handles and clears
  its event epoch, even after a terminal backend event marked it inactive.
- Every Watcher instance records its construction thread. All public instance
  methods and properties enforce that affinity before reading or mutating
  native handles, overlapped buffers, the event ring, or last-event state. The
  unreachable-object finalizer is exempt so collector-thread cleanup remains
  possible. Backend read/parse/terminal errors enqueue an unknown-count
  overflow marker before retiring or rearming the watch.
- `Stream.Read(count)` measures the current remainder and caps its initial
  allocation before reading. `MemStream.Clear()` preserves reusable capacity
  without zeroing unobservable bytes; later sparse writes zero every gap before
  extending logical length.
- `LineReader.ReadAll()` begins at the logical cursor, including a byte staged
  by `PeekChar`, and advances to EOF instead of rewinding the file.

## Consequences

- Managed allocation hooks now cover object/array payloads and heap replacement
  blocks, making OOM tests deterministic and matching the allocation shim's
  documented scope.
- Native files, backing streams, temporary byte buffers, and line buffers are
  not leaked when an embedder recovers from an allocation trap.
- Short file reads may advance the host cursor before a subsequent result
  right-sizing allocation fails, matching ordinary I/O side-effect semantics;
  the temporary managed buffer is nevertheless reclaimed.
- Finalization remains best-effort and non-throwing, while explicit Close keeps
  reporting errors that callers can act on.
- Watcher state has one auditable synchronization policy, and native backend
  failure is visible as a rescan signal rather than an indistinguishable empty
  poll.
- Large near-EOF reads cannot force allocation proportional to the requested
  count, clear/reuse avoids capacity-sized wipes, and mixed LineReader calls do
  not duplicate already consumed content.
- Size-aware opaque-handle checks add a constant-time registry/header lookup and
  prevent private-structure reads beyond a forged payload.

## Alternatives Considered

- Require every caller to close resources after a trapped constructor: rejected
  because no partially constructed handle is returned and the caller cannot
  reach the acquired native resource.
- Convert all runtime APIs to error-code returns: rejected because it would be a
  broad language/runtime ABI redesign and would not match existing trap
  semantics.
- Add test-only failure counters to each I/O class: rejected in favor of routing
  managed payload allocations through the existing central shim.
- Validate only class ids: rejected because class tags do not prove the payload
  is large enough for the implementation being cast.
