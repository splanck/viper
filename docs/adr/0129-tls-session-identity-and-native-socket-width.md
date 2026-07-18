---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0129: Give TLS Sessions Stable Identity and Native-Width Socket Ownership

## Status

Accepted

## Context

The low-level TLS engine allocated `rt_tls_session_t` as a managed object with
class identifier zero, then accepted its opaque pointer at protocol entry
points without stable identity validation. An unrelated, undersized, stale, or
forged managed value could therefore be interpreted as socket, record-buffer,
certificate, key, and sequence-counter state. Language-visible
`Zanna.Crypto.Tls` wrappers checked only a class identifier before reading their
native session and host pointers, while String and Bytes arguments could be
inspected before their own identities were established.

The low-level constructor, server accept helper, and socket accessor used C
`int` for native descriptors. POSIX file descriptors fit that type, but Windows
`SOCKET` is pointer-width; narrowing it could select or close the wrong native
resource. Session objects also lacked a finalizer, so releasing an abandoned
managed session without calling the explicit close function leaked its socket
and retained protocol secrets. Configuration validation happened after the
session took ownership, making malformed hostname, ALPN, or CA-path input
publish an error-state object instead of failing without consuming the socket.

The nonblocking connect loop ignored failures from mode transitions and from
`getsockopt(SO_ERROR)`, allowing a failed status query to be mistaken for a
successful connect. The wrapper copied every outgoing Bytes element through
individual accessors, allocated a native staging buffer for every receive, and
copied results back one byte at a time. Result and line-String construction did
not consistently clean owned sessions, native buffers, or temporary managed
values when allocation trapped.

The stable low-level identifier and pointer-width signature change extend the
runtime C ABI. Socket ownership, managed finalization, wrapper publication, and
connect validation cross the runtime/network boundary, so the decision is
recorded explicitly. The existing TLS 1.3 protocol, cipher suites,
certificate-verification controls, ALPN, wrapper methods, and compatibility
constructors remain available.

## Decision

- Reserve `RT_TLS_SESSION_CLASS_ID` as `-0x720213`. Every public low-level
  session entry point validates this identity and the complete private payload
  before reading protocol state. The low-level C API returns its documented
  invalid-argument or empty sentinel for invalid handles rather than trapping.
- Keep `RT_TLS_CLASS_ID` as the language-visible wrapper identity, but validate
  both its stable identifier and complete payload size. A forged wrapper emits
  one trap and stops local control flow even when an embedder trap hook returns.
- Represent native socket arguments and accessors as `intptr_t` at the public
  TLS C boundary and convert immediately to the approved `socket_t` platform
  adapter. HTTPS, HTTP, SMTP, SSE, WebSocket, and WSS call sites must not narrow
  descriptors while transferring or observing TLS ownership.
- Validate the native descriptor, hostname, ALPN list, and CA-path length
  before allocating a session or taking socket ownership. Reject empty or
  malformed verification hostnames and empty ALPN list elements. On constructor
  failure, the caller retains and must close the descriptor.
- Install a partial-safe session finalizer before publishing descriptor
  ownership. Explicit close performs a bounded `close_notify` exchange;
  finalization performs no network I/O. Both paths release dynamic handshake
  state, securely wipe fixed and dynamic secrets, close the native descriptor,
  and leave an idempotent closed payload. Explicit close consumes one managed
  producer reference.
- Treat zero-length send and receive as state-independent no-ops. Reject a NULL
  byte pointer for any positive length before testing connection state, and
  reject lengths that cannot be represented by the API's signed return type.
- Expose `rt_tls_set_io_timeout` for serialized transport adapters that need a
  temporary bounded receive. Apply native receive/send options before
  publishing the new session timeout and roll back the first option
  best-effort if the second fails.
- Require every nonblocking-mode transition to succeed. After write readiness,
  accept a connection only when the shared pointer-width socket adapter's
  `SO_ERROR` query itself succeeds and returns zero; require restoration to
  blocking mode before adopting the socket. One monotonic deadline continues
  to cover resolution, all address attempts, and the TLS handshake.
- Validate managed String and Bytes identities before their lengths or data
  are read. Send Bytes directly from their stable contiguous span. Receive into
  one bounded record-sized stack buffer and construct an exact managed Bytes or
  String value in one copy, eliminating per-byte access and native heap
  staging.
- Stage the wrapper host copy and use local trap recovery around wrapper,
  Result, and completed-line String publication. Every allocation-failure path
  releases temporary Strings/Results, native buffers, wrappers, and owned
  sessions before re-raising the saved diagnostic.

## Consequences

- Forged, stale, wrong-class, and undersized session or wrapper handles cannot
  reach descriptors, key material, record buffers, or native host pointers.
- Windows socket values survive every TLS ownership transfer without 32-bit
  truncation; POSIX behavior is unchanged.
- Releasing the last managed low-level session reference is sufficient to
  close and scrub an abandoned connection. Callers should still use explicit
  close when they want the bounded protocol shutdown exchange.
- Invalid configuration has a clear transactional contract: NULL is returned,
  a stable diagnostic is recorded, and socket ownership remains with the
  caller. Strict ALPN validation catches accidental leading, repeated, or
  trailing separators before a ClientHello or ServerHello is emitted.
- Connect readiness can no longer convert a failed status query or mode update
  into a false success.
- Wrapper Bytes sends avoid an allocation and an O(n) accessor loop. Wrapper
  receives perform one managed allocation and one exact copy rather than a
  native allocation plus per-byte mutation.
- Existing registry names, language signatures, TLS 1.3 features, result and
  compatibility constructors, ALPN accessors, verification policy, and close
  semantics remain available. Native embedders must recompile for the
  pointer-width C signatures and observe that `rt_tls_close` consumes the
  session reference.

## Verification

- `test_rt_tls_wrapper` verifies stable low-level identity, forged-handle
  sentinels, complete native descriptor round trips, state-independent empty
  I/O, NULL-buffer rejection, explicit close, nonblocking finalization, exact
  managed-object baselines, pre-ownership config rejection, strict ALPN lists,
  forged String validation, Result allocation sweeps, and the overall connect
  deadline.
- `test_rt_https_server` performs a real TLS 1.3 wrapper connection, rejects
  forged Bytes and String payloads, sends a contiguous Bytes request, receives
  line and binary response data, validates host/port/ALPN accessors, closes the
  wrapper, and restores the exact TLS/client/server object baseline.
- `test_rt_trap_return_network` verifies forged wrapper receivers emit exactly
  one trap and return their documented sentinel when `vm_trap` returns.
- `test_rt_network_highlevel` and `test_rt_https_server` exercise the updated
  client/server native-width session transfers through HTTPS/1.1, HTTP/2, SNI,
  ALPN, and certificate-verification paths.
- `docs/zannalib/crypto.md`, `docs/zannalib/network.md`, and the public TLS
  headers document stable identity, ownership transfer, strict configuration,
  finalization, exact-buffer I/O, and timeout behavior.

## Alternatives Considered

- Keep class identifier zero and validate only payload size: rejected because
  unrelated zero-tagged managed objects can satisfy the same size threshold.
- Continue using `int` because POSIX descriptors fit: rejected because the
  supported Windows ABI defines `SOCKET` at pointer width.
- Close the descriptor when configuration is invalid: rejected because the
  constructor has not succeeded and therefore has not established ownership.
- Let finalization perform `close_notify`: rejected because managed release and
  collector paths must not block on a peer. Explicit close remains the graceful
  path.
- Preserve permissive comma skipping in ALPN configuration: rejected because
  leading, repeated, and trailing separators do not name protocols and hide
  caller mistakes.
- Keep native and per-byte wrapper staging: rejected because Bytes already
  provides a validated contiguous span and TLS records have a fixed bounded
  receive size.
- Remove compatibility constructors, raw TLS access, ALPN, verification bypass
  for local tests, or graceful close: rejected because hardening must preserve
  the complete TLS feature surface.
