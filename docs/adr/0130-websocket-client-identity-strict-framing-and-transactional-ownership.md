---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0130: Give WebSocket Clients Stable Identity and Strict Transactional Framing

## Status

Accepted

## Context

The WebSocket client allocated its managed connection with class identifier
zero and public entry points cast opaque receivers directly to the private
payload. A forged, stale, undersized, or unrelated managed value could
therefore be interpreted as a native socket, TLS session, URL pointer, frame
buffer, and close-state record. String and Bytes arguments were likewise read
before their managed identities were established.

The private descriptor and TCP constructor used C `int` even though Windows
`SOCKET` is pointer-width. Nonblocking connection setup did not require mode
changes or the `SO_ERROR` query itself to succeed, and each resolved address
could restart the full timeout. Timed receive temporarily changed native
timeouts and reset them to an infinite wait afterward, losing the connection's
configured policy. The TLS send loop also treated every TLS failure as a
transient socket condition and could retry a fatal protocol error forever.

Opening-handshake validation accepted ambiguous input: a status prefix such as
`1010`, obsolete folded headers, duplicate singleton fields, and trailing
syntax not permitted by RFC 6455 could reach the upgraded state. Frame parsing
did not consistently reject non-canonical or high-bit 64-bit lengths, and
protocol errors could leave the transport open with a partially consumed byte
stream. A valid peer Close also deferred descriptor release until a later
explicit Close or managed finalization.

Outgoing binary messages were copied through per-element accessors. Handshake
key/accept generation staged managed Bytes and mutated them one element at a
time. Receive publication held a native frame allocation across a managed
String or Bytes allocation without a recovery transaction. Constructor traps
could similarly strand a partial object, socket, TLS session, or native URL
component.

Stable identity changes the runtime C ABI, while descriptor width, TLS
ownership, frame failure policy, and managed publication cross runtime layers.
The decision is therefore recorded explicitly. Existing `ws://` and `wss://`
support, text and binary methods, ping/pong, fragmentation, subprotocols,
timeouts, close methods, and test key helper remain available.

## Decision

- Reserve `RT_WEBSOCKET_CLASS_ID` as `-0x720214`. Every non-null public
  receiver validates that identifier and the complete private payload before
  reading transport or heap state. Invalid receivers emit one trap and return
  the method's documented sentinel even when an embedder trap hook returns.
- Store native descriptors as the approved pointer-width `socket_t`. Transfer
  WSS ownership to TLS through `intptr_t`; do not narrow a Windows `SOCKET` at
  any connect, send, receive, close, or finalization boundary.
- Treat construction as one publication transaction. Validate managed URL and
  subprotocol identities, embedded NULs, token syntax, and timeout range first;
  stage native URL components; then install a partial-safe finalizer before
  adopting a socket. A trap releases native staging, the partial object, and
  its uniquely owned TCP/TLS transport before re-raising the saved category.
- Use one monotonic deadline across DNS resolution and every resolved-address
  connection attempt. Require nonblocking enablement, readiness, a successful
  shared-adapter pending-error query returning zero, and restoration to
  blocking mode before adoption. Handshake and subsequent I/O retain the
  configured per-operation timeout.
- Apply timed receive changes transactionally to both native sockets and TLS
  sessions. Consume frame bytes retained by the opening-handshake read, or
  decrypted bytes retained by TLS, before waiting on raw-socket readiness.
  Restore the connection's configured policy after success, timeout, protocol
  close, or trap. TLS record failures are final TLS outcomes, not socket errors
  eligible for blind retry.
- Parse the opening response as bounded HTTP/1.1 syntax. Require an exact 101
  status boundary, valid field-name tokens and field-value bytes, no obsolete
  folding, at most 100 fields, and no duplicate
  `Sec-WebSocket-Accept` or `Sec-WebSocket-Protocol` singleton. Interpret
  repeated `Upgrade` and `Connection` fields with combined token semantics and
  compare the accept value exactly after optional whitespace trimming.
- Enforce canonical frame lengths, reject the forbidden high bit in 64-bit
  lengths, reserved bits/opcodes, masked server frames, fragmented or oversized
  control frames, invalid continuation ordering, invalid close payloads, and
  invalid UTF-8. Limit a frame and reassembled message to 64 MiB.
- On a protocol, UTF-8, size, or internal frame-allocation failure, send the
  appropriate best-effort Close status when possible and deterministically
  retire the transport. A peer Close is answered and closes locally in the same
  receive operation. Ordinary transport failures record abnormal closure and
  cannot leave the partially parsed stream reusable.
- Generate handshake nonces and accept values in bounded native buffers and
  construct only the final managed String. Send Bytes directly from their
  validated contiguous span. Publish a completed native receive buffer through
  one exact String or Bytes copy under local trap recovery, freeing native
  ownership before re-raising any allocation failure.
- Keep explicit Close bounded. It sends the caller's valid status and UTF-8
  reason, waits at most about one second for a peer Close while processing
  control traffic, then closes TCP/TLS regardless of peer behavior. The
  finalizer remains idempotent and performs deterministic non-publishing
  cleanup.

## Consequences

- Wrong-class and undersized handles cannot expose or corrupt socket, TLS,
  frame-buffer, URL, or close-reason state.
- Windows descriptors survive all WebSocket/TLS ownership transfers without
  truncation; POSIX descriptor behavior is unchanged.
- Multiple unreachable addresses share the requested connection budget, and a
  failed status query or blocking-mode restoration cannot become a false
  successful connection.
- Opening responses and frames have one unambiguous interpretation. Peers that
  emit obsolete folding, duplicate singleton handshake fields, non-canonical
  lengths, or other RFC violations are rejected and disconnected.
- Timed receive no longer silently changes the policy of later operations. A
  fatal TLS error cannot spin in a socket-style retry loop.
- Binary sends avoid a native allocation and O(n) managed accessor calls.
  Handshake crypto avoids temporary managed byte arrays. Receive publication
  uses one managed allocation and exact copy, with no native leak on OOM.
- Peer-initiated and protocol-error closes release descriptors promptly instead
  of waiting for a later method or garbage collection.
- Existing registry names and language signatures remain unchanged. Native
  embedders must recognize the new stable class identifier when inspecting
  managed objects.

## Verification

- `test_rt_websocket` verifies stable identity, timeout behavior, exact Host and
  subprotocol handshakes, embedded-NUL payload preservation, fragmented-message
  reassembly, UTF-8 close status, bounded graceful close, prompt transport EOF,
  forged String/Bytes rejection, and constructor/receive allocation failures
  with exact managed-object baselines.
- `test_rt_network_runtime` verifies strict opening-response status, optional
  whitespace, singleton duplication, and obsolete-fold rejection in addition
  to frame length, opcode, close-code, masking, UTF-8, and URL helpers.
- `test_rt_trap_return_network` verifies forged WebSocket receivers and payloads
  emit exactly one trap and stop before payload access with a returning trap
  hook.
- `test_rt_posix_socket_platform` verifies the shared pending-error adapter,
  finite EINTR deadlines, invalid arguments, and orderly EOF readiness.
- `docs/zannalib/network.md` and the public WebSocket/socket headers document
  stable identity, deadlines, strict parsing, exact I/O, close behavior, and
  thread-safety limits.

## Alternatives Considered

- Validate only payload size while retaining class identifier zero: rejected
  because unrelated zero-tagged objects can satisfy the same size threshold.
- Keep `int` descriptors because POSIX file descriptors fit: rejected because
  Windows `SOCKET` is part of the supported runtime ABI and is pointer-width.
- Preserve permissive HTTP parsing for compatibility: rejected because an
  upgrade establishes a new protocol over the same byte stream and ambiguity
  at that boundary is unsafe.
- Return protocol errors while leaving the socket open: rejected because frame
  parsing may already have consumed bytes and the stream cannot be resynchronized
  reliably.
- Restore every timed receive to an infinite timeout: rejected because it
  changes caller policy and makes later operations unexpectedly unbounded.
- Continue per-byte managed staging: rejected because String and Bytes already
  provide validated contiguous storage and exact-copy constructors.
- Remove WSS, subprotocol, timeout, fragmentation, or close-handshake features:
  rejected because the hardening work must preserve the complete feature set.
