---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0128: Give HTTP Servers Stable Identity and Serialized Lifecycles

## Status

Accepted

## Context

The native `HttpServer` and `HttpsServer` surfaces allocated public receivers
with class identifier zero and treated per-request `ServerReq` and `ServerRes`
values as stack payloads. Public methods could therefore reinterpret an
unrelated managed object as mutex, listener, router, request, or response
state. A managed callback that retained either stack address outlived the
worker frame and could later access reused memory.

Server construction eagerly created a logical-CPU-sized worker pool even when
the server never started. Route registration, handler rebinding, Start, Stop,
port reads, finalization, and synchronous request dispatch used incomplete or
different synchronization domains. Concurrent Start calls could publish more
than one listener; concurrent Stop calls could close or join the same state;
replacement cleanup ran while configuration locks were held and could
deadlock when it called back into the server. A handler could race route or
binding mutation while the router and native binding arrays were being read.

Request parsing, routing, callback execution, response serialization, TLS
handshake, and HTTP/2 conversion crossed native and managed ownership without
one cleanup transaction. A trap could leak partial Strings, Maps, socket
buffers, accepted handles, TLS sessions, HTTP/2 headers, or active-connection
registrations. `ServerRes.Json` updated the content type before the body copy,
so allocation failure exposed a mixed response. HTTP/2 response conversion
read Map values without validating String identity, emitted handler spelling
instead of mandatory lowercase names, and sent bodies for statuses that forbid
them.

Stable class identifiers extend the runtime C ABI. Managed callback lifetime,
serialized lifecycle publication, lazy worker creation, and HTTP/2 response
normalization are cross-layer runtime contracts, so the decision is recorded
explicitly. Existing routes, methods, TLS support, HTTP/1.1 keep-alive,
HTTP/2, and callback features must remain available.

## Decision

- Reserve `RT_HTTP_SERVER_CLASS_ID` as `-0x72020F` and
  `RT_HTTPS_SERVER_CLASS_ID` as `-0x720210`. Every non-null public server
  receiver validates the class identifier, complete payload size, and
  initialized state/lifecycle mutexes before private access.
- Reserve `RT_SERVER_REQ_CLASS_ID` as `-0x720211` and
  `RT_SERVER_RES_CLASS_ID` as `-0x720212`, shared by HTTP and HTTPS callbacks.
  Allocate request and response snapshots as managed objects with partial-safe
  finalizers. The server releases its producer reference after serialization;
  callbacks may retain either handle independently.
- Initialize both server mutexes before fallible subordinate state and install
  a partial-safe finalizer. Constructor recovery releases the router, copied
  credential paths, TLS context, mutexes, and managed server at every
  allocation boundary. Certificate/key paths must be valid managed Strings,
  non-empty, exactly length-bounded, and free of embedded NUL bytes before
  native filesystem access.
- Do not create a listener or worker pool in either constructor. Create the
  pool lazily inside the first successful Start transaction and reuse it after
  Stop/Start. A stopped, never-started server consumes no worker threads or
  socket.
- Serialize Start, Stop, route registration, handler binding, and finalization
  with a lifecycle mutex. Protect running state, listener publication, active
  connections, port, and synchronous-dispatch count with the state mutex. When
  both are needed, acquire lifecycle before state. Reject route/binding changes
  while running, stopping, finalizing, or synchronously dispatching.
- Make Start a rollback transaction. Publish running/listener/thread state only
  after each preceding step succeeds, and release a newly created pool if
  startup fails. Make Stop idempotently detach one listener/thread state,
  interrupt active connections, join the accept loop once, and drain workers.
  A worker calling Stop must not wait for itself.
- Detach a replaced binding's prior cleanup/context under the lifecycle lock,
  publish the replacement tuple, unlock, and only then invoke cleanup. Reentrant
  cleanup may safely call server APIs.
- Represent live HTTP, HTTPS/1.1, HTTPS/2, and synchronous dispatch as
  heap-resident ownership transactions that remain defined across `longjmp`.
  Recovery detaches and releases native buffers, Bytes, request/response
  snapshots, accepted TCP objects, TLS sessions, HTTP/2 state, and active
  registrations exactly once before preserving the original trap.
- Validate response header key/value String identity and exact lengths before
  payload access. Reject embedded NUL, CR, LF, invalid token bytes, and
  server-managed framing fields. Snapshot Map keys once per serialization.
  HTTP/2 conversion lowercases accepted field names and publishes a complete
  native list only after conversion succeeds.
- Apply the same body-eligibility calculation to HTTP/1.1 and HTTP/2. Statuses
  such as 204 never send a handler-provided body, and HTTP/2 advertises the
  exact resulting `content-length`.
- Make `ServerRes.Send` stage the replacement body before publication. Make
  `ServerRes.Json` stage an exact body copy and a cloned header Map containing
  the canonical JSON content type, then publish both together. Allocation
  failure preserves the complete prior response.
- When the runtime constructs an internal JSON routing error, publish its error
  status and sent marker before fallible formatting so recovered allocation
  failure cannot turn the error into a stale success response.

## Consequences

- Forged, stale, wrong-class, and undersized managed handles cannot reach
  server mutexes, routers, listeners, TLS state, or request/response payloads.
- Callbacks can deliberately retain complete request or response snapshots;
  no stack address escapes dispatch. The extra managed allocation per snapshot
  buys explicit lifetime and safe finalization.
- Concurrent Start and Stop calls observe one complete lifecycle transition.
  Stopped servers remain restartable and reuse their existing worker pool.
- Route tables and handler bindings cannot race synchronous or live dispatch,
  and reentrant binding cleanup no longer deadlocks on the lifecycle mutex.
- Parser, router, callback, socket, TLS, serializer, and HTTP/2 traps have one
  bounded cleanup path. Partial native header lists and managed key snapshots
  do not leak.
- JSON header/body publication and HTTP/2 header/body framing cannot expose
  internally inconsistent responses after allocation failure.
- Existing registry names, method signatures, HTTP/1.1/TLS/HTTP/2 features,
  route patterns, callback binding, and restart behavior are preserved. The
  four class identifiers extend the native runtime contract.

## Verification

- `test_rt_http_server` verifies stable identity, forged receiver rejection,
  constructor and synchronous-dispatch allocation sweeps, managed and retained
  callback snapshots, all request accessors, wire responses, transactional JSON
  replacement at every managed allocation edge, reentrant binding cleanup,
  concurrent Start/Stop, restart, and exact managed-object baselines.
- `test_rt_https_server` verifies stable HTTPS identity, real-certificate
  constructor rollback at every managed allocation edge, forged and
  embedded-NUL credential-path rejection, retained callback snapshots over
  TLS, concurrent Start/Stop, restart, reusable workers, and exact object
  baselines.
- `test_rt_network_highlevel` verifies HTTP/1.1 TLS keep-alive, SNI, HTTP/2 ALPN
  reuse, lowercase custom HTTP/2 response fields, 204 body suppression with
  exact zero content length, and RSA certificate verification.
- `test_rt_trap_return_network` verifies forged server/request/response
  receivers raise exactly one trap and stop local payload access when an
  embedder trap hook returns.
- `docs/zannalib/network.md` and both public server headers document lazy worker
  creation, serialized lifecycle/configuration, managed snapshot retention,
  transactional response publication, and protocol-correct HTTP/2 framing.

## Alternatives Considered

- Keep zero class identifiers and validate only payload size: rejected because
  unrelated zero-tagged objects can satisfy the same size threshold.
- Keep callback request/response payloads on worker stacks: rejected because
  managed callbacks can retain object values and no safe lifetime contract can
  make an escaped stack address valid.
- Spawn workers in constructors: rejected because stopped configuration-only
  servers should not reserve a CPU-sized thread pool.
- Use only the state mutex for lifecycle and callbacks: rejected because Stop
  can wait for workers, and a worker attempting configuration would deadlock
  behind the same lock ordering.
- Invoke replaced-context cleanup while locked: rejected because cleanup is
  user/native code and may re-enter the server.
- Mutate JSON headers and bodies in place: rejected because a recoverable
  allocation failure must preserve one complete response version.
- Drop custom HTTP/2 headers or handler-provided forbidden bodies silently at a
  different layer: rejected because normalization and status-body suppression
  belong in the shared server serialization contract.
- Remove HTTP/2, TLS, route mutation while stopped, callback retention, or
  restart support: rejected because hardening must preserve the complete server
  feature surface.
